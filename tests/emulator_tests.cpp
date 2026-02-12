#include "core/cpu.h"
#include "core/emu_core.h"
#include "core/gte.h"
#include "core/gpu_packets.h"
#include "core/memory_map.h"
#include "core/mmio.h"
#include "core/scheduler.h"
#include "core/xa_adpcm.h"
#include "plugins/ipc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <string>
#include <vector>

static uint32_t encode_i(uint8_t op, uint8_t rs, uint8_t rt, uint16_t imm) {
  return (static_cast<uint32_t>(op) << 26) |
         (static_cast<uint32_t>(rs) << 21) |
         (static_cast<uint32_t>(rt) << 16) |
         static_cast<uint32_t>(imm);
}

static uint32_t gte_cmd(uint32_t op, bool sf = false, bool lm = false) {
  return op | (sf ? (1u << 19) : 0u) | (lm ? (1u << 10) : 0u);
}

static uint32_t gte_cmd_mvmva(uint32_t mx,
                              uint32_t v,
                              uint32_t cv,
                              bool sf = false,
                              bool lm = false) {
  return 0x12u |
         ((mx & 0x3u) << 17) |
         ((v & 0x3u) << 15) |
         ((cv & 0x3u) << 13) |
         (sf ? (1u << 19) : 0u) |
         (lm ? (1u << 10) : 0u);
}

static int16_t gte_read_s16(ps1emu::Gte &gte, uint32_t reg) {
  return static_cast<int16_t>(gte.read_data(reg) & 0xFFFFu);
}

#define CHECK(cond)                                                                               \
  do {                                                                                            \
    if (!(cond)) {                                                                                \
      std::cerr << "FAIL: " << __func__ << ":" << __LINE__ << ": " << #cond << "\n";     \
      return false;                                                                               \
    }                                                                                             \
  } while (0)

namespace ps1emu {
struct EmulatorCoreTestAccess {
  static MemoryMap &memory(EmulatorCore &core) { return core.memory_; }
  static MmioBus &mmio(EmulatorCore &core) { return core.mmio_; }
  static void flush_gpu(EmulatorCore &core) { core.flush_gpu_commands(); }
  static void process_dma(EmulatorCore &core) { core.process_dma(); }
};
} // namespace ps1emu

struct ScopedConfigFile {
  std::string path;
  explicit ScopedConfigFile(std::string p) : path(std::move(p)) {}
  ~ScopedConfigFile() {
    if (!path.empty()) {
      std::remove(path.c_str());
    }
  }
};

struct ScopedTempFile {
  std::string path;
  explicit ScopedTempFile(std::string p) : path(std::move(p)) {}
  ~ScopedTempFile() {
    if (!path.empty()) {
      std::remove(path.c_str());
    }
  }
};

struct ScopedCore {
  ps1emu::EmulatorCore core;
  bool active = false;
  ~ScopedCore() {
    if (active) {
      core.shutdown();
    }
  }
};

static bool write_test_config(const std::string &path, const std::string &cdrom_image = std::string()) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }
  file << "plugin.gpu=./build/ps1emu_gpu_stub\n";
  file << "plugin.spu=./build/ps1emu_spu_stub\n";
  file << "plugin.input=./build/ps1emu_input_stub\n";
  file << "plugin.cdrom=./build/ps1emu_cdrom_stub\n";
  file << "bios.path=\n";
  if (!cdrom_image.empty()) {
    file << "cdrom.image=" << cdrom_image << "\n";
  } else {
    file << "cdrom.image=\n";
  }
  file << "cpu.mode=interpreter\n";
  file << "sandbox.enabled=false\n";
  file << "sandbox.seccomp_strict=false\n";
  file << "sandbox.rlimit_cpu_seconds=0\n";
  file << "sandbox.rlimit_as_mb=0\n";
  file << "sandbox.rlimit_nofile=0\n";
  return true;
}

static bool write_binary_file(const std::string &path, const std::vector<uint8_t> &data) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  if (!data.empty()) {
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  }
  return file.good();
}

static constexpr uint32_t kCdromReadPeriodCycles = 33868800 / 75;
static constexpr uint32_t kCdromSeekDelayCycles = 33868800 / 60;
static constexpr uint32_t kCdromGetIdDelayCycles = 33868800 / 120;
static constexpr uint32_t kCdromTocDelayCycles = 33868800 / 30;

static std::vector<uint8_t> read_cdrom_response(ps1emu::MmioBus &mmio, size_t count) {
  std::vector<uint8_t> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    out.push_back(mmio.read8(0x1F801801));
  }
  return out;
}

static uint8_t to_bcd(uint32_t value) {
  value %= 100;
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

static void lba_to_bcd(uint32_t lba, uint8_t &mm, uint8_t &ss, uint8_t &ff) {
  uint32_t lba_adj = lba + 150;
  uint32_t total_seconds = lba_adj / 75;
  uint32_t frames = lba_adj % 75;
  uint32_t minutes = total_seconds / 60;
  uint32_t seconds = total_seconds % 60;
  mm = to_bcd(minutes);
  ss = to_bcd(seconds);
  ff = to_bcd(frames);
}

static std::vector<uint8_t> make_raw_sector(uint32_t lba,
                                            uint8_t mode,
                                            uint8_t submode,
                                            uint8_t fill) {
  std::vector<uint8_t> raw(2352, 0x00);
  raw[0] = 0x00;
  for (int i = 1; i <= 10; ++i) {
    raw[static_cast<size_t>(i)] = 0xFF;
  }
  raw[11] = 0x00;

  uint8_t mm = 0, ss = 0, ff = 0;
  lba_to_bcd(lba, mm, ss, ff);
  raw[0x0C] = mm;
  raw[0x0D] = ss;
  raw[0x0E] = ff;
  raw[0x0F] = mode;

  size_t data_offset = (mode == 2) ? 0x18u : 0x10u;
  size_t data_size = 0x800u;
  if (mode == 2) {
    raw[0x10] = 0x11;
    raw[0x11] = 0x22;
    raw[0x12] = submode;
    raw[0x13] = 0x00;
    raw[0x14] = raw[0x10];
    raw[0x15] = raw[0x11];
    raw[0x16] = raw[0x12];
    raw[0x17] = raw[0x13];
    if (submode & 0x20u) {
      data_size = 0x914u;
    }
  }

  for (size_t i = 0; i < data_size && data_offset + i < raw.size(); ++i) {
    raw[data_offset + i] = fill;
  }
  return raw;
}

static bool run_stub_handshake(const std::string &path, const std::string &name) {
  ps1emu::SandboxOptions sandbox;
  sandbox.enabled = false;
  sandbox.seccomp_strict = false;
  sandbox.rlimit_cpu_seconds = 0;
  sandbox.rlimit_as_mb = 0;
  sandbox.rlimit_nofile = 0;

  auto result = ps1emu::spawn_plugin_process(path, {}, sandbox);
  CHECK(result.pid > 0);
  CHECK(result.channel.valid());

  std::string line;
  CHECK(result.channel.send_line("HELLO " + name + " 1"));
  CHECK(result.channel.recv_line(line));
  CHECK(line == ("READY " + name + " 1"));

  CHECK(result.channel.send_line("PING"));
  CHECK(result.channel.recv_line(line));
  CHECK(line == "PONG");

  CHECK(result.channel.send_line("SHUTDOWN"));

  int status = 0;
  waitpid(result.pid, &status, 0);
  return true;
}

static bool test_load_delay() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;
  st.gpr[1] = 0x11111111;
  st.gpr[2] = 0x00001000;

  mem.write32(0x00001000, 0xDEADBEEF);
  mem.write32(0x00000000, encode_i(0x23, 2, 1, 0)); // lw r1, 0(r2)
  mem.write32(0x00000004, encode_i(0x08, 1, 3, 5)); // addi r3, r1, 5
  mem.write32(0x00000008, 0x00000000);              // nop

  cpu.step();
  cpu.step();

  CHECK(st.gpr[1] == 0xDEADBEEF);
  CHECK(st.gpr[3] == 0x11111116);
  return true;
}

static bool test_cpu_reset_state() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  CHECK(st.pc == 0xBFC00000);
  CHECK(st.next_pc == 0xBFC00004);
  CHECK((st.cop0.sr & (1u << 22)) != 0);
  CHECK((st.cop0.sr & ((1u << 21) | (1u << 17) | (1u << 1) | (1u << 0))) == 0);
  return true;
}

static bool test_cache_isolated_store_ignored() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  mem.write32(0x00000000, encode_i(0x09, 0, 9, 0x0100)); // addiu r9, r0, 0x100
  mem.write32(0x00000004, encode_i(0x09, 0, 8, 0x55AA)); // addiu r8, r0, 0x55aa
  mem.write32(0x00000008, encode_i(0x2B, 9, 8, 0));      // sw r8, 0(r9)
  mem.write32(0x0000000C, 0x00000000);                   // nop

  auto &st = cpu.state();
  st.cop0.sr |= (1u << 16);
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;

  cpu.step();
  cpu.step();
  cpu.step();

  CHECK(mem.read32(0x00000100) == 0x00000000);

  st.cop0.sr &= ~(1u << 16);
  st.gpr[9] = 0x00000100;
  st.gpr[8] = 0x11223344;
  st.pc = 0x00000008;
  st.next_pc = st.pc + 4;
  cpu.step();

  CHECK(mem.read32(0x00000100) == 0x11223344);
  return true;
}

static bool test_branch_delay() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;
  st.gpr[1] = 1;
  st.gpr[2] = 0;

  mem.write32(0x00000000, encode_i(0x04, 1, 1, 1)); // beq r1, r1, +1
  mem.write32(0x00000004, encode_i(0x08, 2, 2, 1)); // addi r2, r2, 1 (delay slot)
  mem.write32(0x00000008, encode_i(0x08, 2, 2, 2)); // addi r2, r2, 2 (branch target)

  cpu.step();
  cpu.step();
  cpu.step();

  CHECK(st.gpr[2] == 3);
  return true;
}

static bool test_cpu_exception_trace() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;

  mem.write32(0x00000000, 0x0000000C); // syscall

  cpu.step();

  ps1emu::CpuExceptionInfo info;
  CHECK(cpu.consume_exception(info));
  CHECK(info.code == 8);
  CHECK(info.pc == 0x00000000);
  CHECK(!info.in_delay);
  CHECK((info.cause & (0x1Fu << 2)) == (8u << 2));
  CHECK(!cpu.consume_exception(info));
  return true;
}

static bool test_cpu_exception_sr_shift() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.cop0.sr = 0x00000003u;
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;

  mem.write32(0x00000000, 0x0000000C); // syscall
  cpu.step();

  CHECK((st.cop0.sr & 0x3Fu) == 0x0Eu);
  return true;
}

static bool test_branch_likely_not_taken() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;
  st.gpr[1] = 1;
  st.gpr[2] = 2;
  st.gpr[3] = 0;

  mem.write32(0x00000000, encode_i(0x14, 1, 2, 1)); // beql r1, r2, +1 (not taken)
  mem.write32(0x00000004, encode_i(0x08, 3, 3, 1)); // addi r3, r3, 1 (delay slot, skipped)
  mem.write32(0x00000008, encode_i(0x08, 3, 3, 2)); // addi r3, r3, 2 (fallthrough)

  cpu.step();
  cpu.step();
  cpu.step();

  CHECK(st.gpr[3] == 2);
  return true;
}

static bool test_branch_likely_taken() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;
  st.gpr[1] = 1;
  st.gpr[2] = 1;
  st.gpr[3] = 0;

  mem.write32(0x00000000, encode_i(0x14, 1, 2, 1)); // beql r1, r2, +1 (taken)
  mem.write32(0x00000004, encode_i(0x08, 3, 3, 1)); // addi r3, r3, 1 (delay slot)
  mem.write32(0x00000008, encode_i(0x08, 3, 3, 2)); // addi r3, r3, 2 (target)

  cpu.step();
  cpu.step();
  cpu.step();

  CHECK(st.gpr[3] == 3);
  return true;
}

static bool test_mmio_gpu_fifo() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write32(0x1F801810, 0x11112222);
  mmio.write32(0x1F801810, 0x33334444);

  CHECK(mmio.has_gpu_commands());
  auto cmds = mmio.take_gpu_commands();
  CHECK(cmds.size() == 2);
  CHECK(cmds[0] == 0x11112222);
  CHECK(cmds[1] == 0x33334444);
  CHECK(!mmio.has_gpu_commands());

  mmio.restore_gpu_commands(cmds);
  CHECK(mmio.has_gpu_commands());
  auto cmds2 = mmio.take_gpu_commands();
  CHECK(cmds2.size() == 2);
  CHECK(cmds2[0] == 0x11112222);
  CHECK(cmds2[1] == 0x33334444);
  return true;
}

static bool test_vblank_irq() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  constexpr uint32_t kCyclesPerFrame = 33868800 / 60;
  mmio.tick(kCyclesPerFrame);
  CHECK((mmio.irq_stat() & 0x1u) != 0);

  mmio.write16(0x1F801070, static_cast<uint16_t>(1u << 0));
  CHECK((mmio.irq_stat() & 0x1u) == 0);
  return true;
}

static bool test_gpu_status_bits() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  uint32_t stat = mmio.read32(0x1F801814);
  CHECK(stat == 0x14802000);

  uint32_t mode = 0;
  mode |= 5u;
  mode |= (1u << 4);
  mode |= (2u << 5);
  mode |= (1u << 7);
  mode |= (1u << 9);
  mode |= (1u << 10);
  mode |= (1u << 11);
  mmio.apply_gp0_state(0xE1000000u | mode);

  stat = mmio.read32(0x1F801814);
  CHECK((stat & 0xFu) == 5);
  CHECK((stat & (1u << 4)) != 0);
  CHECK(((stat >> 5) & 0x3u) == 2);
  CHECK(((stat >> 7) & 0x3u) == 1);
  CHECK((stat & (1u << 9)) != 0);
  CHECK((stat & (1u << 10)) != 0);
  CHECK((stat & (1u << 15)) != 0);

  mmio.write32(0x1F801814, 0x080000FFu);
  stat = mmio.read32(0x1F801814);
  CHECK((stat & (1u << 16)) != 0);
  CHECK(((stat >> 17) & 0x3u) == 3);
  CHECK((stat & (1u << 19)) != 0);
  CHECK((stat & (1u << 20)) != 0);
  CHECK((stat & (1u << 21)) != 0);
  CHECK((stat & (1u << 22)) != 0);
  CHECK((stat & (1u << 14)) != 0);
  return true;
}

static bool test_gpu_read_fifo() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  CHECK((mmio.read32(0x1F801814) & (1u << 27)) == 0);
  mmio.queue_gpu_read_data({0x11223344u, 0x55667788u});
  CHECK((mmio.read32(0x1F801814) & (1u << 27)) != 0);

  uint32_t first = mmio.read32(0x1F801810);
  uint32_t second = mmio.read32(0x1F801810);
  CHECK(first == 0x11223344u);
  CHECK(second == 0x55667788u);

  CHECK((mmio.read32(0x1F801814) & (1u << 27)) == 0);
  uint32_t latched = mmio.read32(0x1F801810);
  CHECK(latched == 0x55667788u);
  return true;
}

static bool test_gpu_dma_request_bits() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write32(0x1F801814, 0x04000003u); // DMA dir GPU->CPU
  uint32_t stat = mmio.read32(0x1F801814);
  CHECK((stat & (1u << 25)) == 0);

  mmio.queue_gpu_read_data({0x12345678u});
  stat = mmio.read32(0x1F801814);
  CHECK((stat & (1u << 25)) != 0);

  return true;
}

static bool test_gpu_read_delay() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.schedule_gpu_read_data({0x0A0B0C0Du}, 5);
  CHECK((mmio.read32(0x1F801814) & (1u << 27)) == 0);

  mmio.tick(4);
  CHECK((mmio.read32(0x1F801814) & (1u << 27)) == 0);

  mmio.tick(1);
  CHECK((mmio.read32(0x1F801814) & (1u << 27)) != 0);
  uint32_t word = mmio.read32(0x1F801810);
  CHECK(word == 0x0A0B0C0Du);
  return true;
}

static bool test_gpu_stat_busy_decay() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write32(0x1F801810, 0x02000000);
  uint32_t stat = mmio.read32(0x1F801814);
  CHECK((stat & (1u << 26)) == 0);

  mmio.tick(2);
  stat = mmio.read32(0x1F801814);
  CHECK((stat & (1u << 26)) != 0);
  return true;
}

static bool test_gte_flags_and_saturation() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(9, 0x4000);
  gte.write_data(10, 0x4000);
  gte.write_data(11, 0x4000);
  gte.execute(gte_cmd(0x28, false, false)); // SQR

  uint32_t flags = gte.read_ctrl(63);
  CHECK((flags & (1u << 24)) != 0);
  CHECK((flags & (1u << 23)) != 0);
  CHECK((flags & (1u << 22)) != 0);
  return true;
}

static bool test_gte_color_fifo_saturation() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(8, 0x1000);
  gte.write_data(9, 0x7FFF);
  gte.write_data(10, 0x7FFF);
  gte.write_data(11, 0x7FFF);
  gte.execute(gte_cmd(0x3D, false, false)); // GPF

  uint32_t flags = gte.read_ctrl(63);
  CHECK((flags & (1u << 21)) != 0);
  CHECK((flags & (1u << 20)) != 0);
  CHECK((flags & (1u << 19)) != 0);

  uint32_t rgb2 = gte.read_data(22);
  CHECK((rgb2 & 0x00FFFFFFu) == 0x00FFFFFFu);
  return true;
}

static bool test_gte_divide_overflow() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_ctrl(32, 1);  // RT m11
  gte.write_ctrl(34, 1);  // RT m22
  gte.write_ctrl(36, 1);  // RT m33
  gte.write_ctrl(58, 0x2000); // H
  gte.write_data(0, 0x00000000); // VXY0
  gte.write_data(1, 0x00000001); // VZ0

  gte.execute(gte_cmd(0x01, false, false)); // RTPS
  uint32_t flags = gte.read_ctrl(63);
  CHECK((flags & (1u << 17)) != 0);
  return true;
}

static bool test_gte_dpct_uses_rgb0() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(6, 0x00AABBCCu);  // RGBC
  gte.write_data(20, 0x00112233u); // RGB0
  gte.write_data(21, 0x00445566u); // RGB1
  gte.write_data(22, 0x00778899u); // RGB2
  gte.write_data(8, 0);            // IR0 = 0

  gte.execute(gte_cmd(0x2A, true, true)); // DPCT (sf=1)
  uint32_t rgb0 = gte.read_data(20);
  uint32_t rgb1 = gte.read_data(21);
  uint32_t rgb2 = gte.read_data(22);
  CHECK((rgb0 & 0x00FFFFFFu) == 0x00112233u);
  CHECK((rgb1 & 0x00FFFFFFu) == 0x00445566u);
  CHECK((rgb2 & 0x00FFFFFFu) == 0x00778899u);
  return true;
}

static bool test_gte_rtps_lm_ignored() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_ctrl(32, 1);
  gte.write_ctrl(34, 1);
  gte.write_ctrl(36, 1);
  gte.write_data(0, 0x0000FFFFu); // VXY0: vx=-1, vy=0
  gte.write_data(1, 0x00000001u); // VZ0

  gte.execute(gte_cmd(0x01, false, true)); // RTPS, lm bit set
  CHECK(gte_read_s16(gte, 9) == -1);
  return true;
}

static bool test_gte_gpl_overflow_flag() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(8, 0x1000); // IR0 = 1.0
  gte.write_data(9, 0x7FFF);
  gte.write_data(10, 0x7FFF);
  gte.write_data(11, 0x7FFF);
  gte.write_data(25, 0x7FFFFFFF);
  gte.write_data(26, 0x7FFFFFFF);
  gte.write_data(27, 0x7FFFFFFF);

  gte.execute(gte_cmd(0x3E, true, false)); // GPL with sf=1
  uint32_t flags = gte.read_ctrl(63);
  CHECK((flags & (1u << 30)) != 0);
  CHECK((flags & (1u << 29)) != 0);
  CHECK((flags & (1u << 28)) != 0);
  return true;
}

static bool test_gte_h_read_sign_extension() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_ctrl(58, 0x8001);
  uint32_t h = gte.read_ctrl(58);
  CHECK(h == 0xFFFF8001u);
  return true;
}

static bool test_gte_sxyp_write_fifo() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(12, 0x00010002);
  gte.write_data(13, 0x00030004);
  gte.write_data(14, 0x00050006);
  gte.write_data(15, 0x00070008);

  CHECK(gte.read_data(12) == 0x00030004u);
  CHECK(gte.read_data(13) == 0x00050006u);
  CHECK(gte.read_data(14) == 0x00070008u);
  return true;
}

static bool test_gte_mvmva_fc_bug() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_ctrl(33, 0x00000002); // RT13=2
  gte.write_ctrl(34, 0x00030003); // RT22=3, RT23=3
  gte.write_ctrl(36, 0x00000004); // RT33=4
  gte.write_data(0, 0x00050006);  // VXY0: vx=5, vy=6
  gte.write_data(1, 0x00000007);  // VZ0=7

  gte.execute(gte_cmd_mvmva(0, 0, 2, false, false)); // mx=RT, v=V0, cv=FC

  CHECK(gte_read_s16(gte, 9) == 14);
  CHECK(gte_read_s16(gte, 10) == 21);
  CHECK(gte_read_s16(gte, 11) == 28);
  return true;
}

static bool test_gte_mvmva_mx3_garbage_matrix() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(8, 0x0040); // IR0
  gte.write_ctrl(33, 0x00000002); // RT13=2
  gte.write_ctrl(34, 0x00000003); // RT22=3
  gte.write_data(0, 0x00020001);  // VXY0: vx=1, vy=2
  gte.write_data(1, 0x00000003);  // VZ0=3

  gte.execute(gte_cmd_mvmva(3, 0, 0, false, false));

  CHECK(gte_read_s16(gte, 9) == 288);
  CHECK(gte_read_s16(gte, 10) == 12);
  CHECK(gte_read_s16(gte, 11) == 18);
  return true;
}

static bool test_gte_dpcs_depth_cue_extremes() {
  ps1emu::Gte gte;
  gte.reset();
  gte.write_data(6, 0x00112233u);
  gte.write_ctrl(53, 0x00000000);
  gte.write_ctrl(54, 0x00000000);
  gte.write_ctrl(55, 0x00000000);
  gte.write_data(8, 0x1000); // IR0 = 1.0
  gte.execute(gte_cmd(0x10, false, false)); // DPCS

  uint32_t rgb2 = gte.read_data(22);
  CHECK((rgb2 & 0x00FFFFFFu) == 0x00000000u);
  return true;
}

static bool test_gte_command_cycles() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;

  mem.write32(0x00000000, (0x12u << 26) | (0x10u << 21) | 0x01u); // COP2 RTPS
  uint32_t cycles = cpu.step();
  CHECK(cycles == 15);
  return true;
}

static bool test_gte_lwc2_delay() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  ps1emu::Scheduler sched;
  mem.reset();
  mmio.reset();
  sched.reset();
  mem.attach_mmio(mmio);

  ps1emu::CpuCore cpu(mem, sched);
  cpu.reset();

  auto &st = cpu.state();
  st.pc = 0x00000000;
  st.next_pc = st.pc + 4;

  mem.write32(0x00001000, 0x12345678);
  mem.write32(0x00000000, encode_i(0x32, 0, 1, 0x1000)); // LWC2 reg1, 0x1000(r0)
  mem.write32(0x00000004, (0x12u << 26) | (0u << 21) | (2u << 16) | (1u << 11)); // MFC2 r2, reg1
  mem.write32(0x00000008, 0x00000000); // nop
  mem.write32(0x0000000C, (0x12u << 26) | (0u << 21) | (3u << 16) | (1u << 11)); // MFC2 r3, reg1
  mem.write32(0x00000010, 0x00000000); // nop

  cpu.step(); // LWC2
  cpu.step(); // MFC2 r2
  cpu.step(); // nop (commit r2)
  cpu.step(); // MFC2 r3 (should see new value)
  cpu.step(); // nop (commit r3)

  CHECK(st.gpr[2] == 0);
  CHECK(st.gpr[3] == 0x00005678u);
  return true;
}

static bool test_dma_irq() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  uint32_t dicr = (1u << 23) | (1u << (16 + 2));
  mmio.write32(0x1F8010F4, dicr);

  mmio.write32(0x1F8010A8, (1u << 24)); // CHCR for channel 2

  uint32_t chan = mmio.consume_dma_channel();
  CHECK(chan == 2);
  CHECK((mmio.irq_stat() & (1u << 3)) != 0);

  uint32_t dicr_after = mmio.read32(0x1F8010F4);
  CHECK((dicr_after & (1u << 31)) != 0);
  CHECK((dicr_after & (1u << (24 + 2))) != 0);

  mmio.write32(0x1F801070, static_cast<uint32_t>(1u << 3));
  CHECK((mmio.irq_stat() & (1u << 3)) == 0);
  return true;
}

static bool test_dma_dicr_clears_irq() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  uint32_t dicr = (1u << 23) | (1u << (16 + 2));
  mmio.write32(0x1F8010F4, dicr);
  mmio.write32(0x1F8010A8, (1u << 24));

  uint32_t chan = mmio.consume_dma_channel();
  CHECK(chan == 2);
  CHECK((mmio.irq_stat() & (1u << 3)) != 0);

  mmio.write32(0x1F8010F4, (1u << (24 + 2)));
  CHECK((mmio.irq_stat() & (1u << 3)) == 0);
  return true;
}

static bool test_timer_irq_on_target() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write16(0x1F801108, 5); // timer0 target
  mmio.write16(0x1F801104, (1u << 4) | (1u << 7)); // enable + irq on target

  mmio.tick(5);
  CHECK((mmio.irq_stat() & (1u << 4)) != 0);
  return true;
}

static bool test_joypad_stub_ready() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  uint16_t stat = mmio.read16(0x1F801044);
  CHECK((stat & 0x0001u) != 0); // TX ready
  CHECK((stat & 0x0004u) != 0); // TX empty
  return true;
}

static bool test_joypad_rx_ready_after_write() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write8(0x1F801040, 0x01);
  mmio.tick(5000);
  uint16_t stat = mmio.read16(0x1F801044);
  CHECK((stat & 0x0002u) != 0); // RX ready
  CHECK((stat & 0x0080u) != 0); // DSR/ACK
  uint8_t data = mmio.read8(0x1F801040);
  CHECK(data == 0xFF);
  stat = mmio.read16(0x1F801044);
  CHECK((stat & 0x0002u) == 0);
  CHECK((stat & 0x0080u) == 0);
  return true;
}

static bool test_sio1_stub_ready() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  uint16_t stat = mmio.read16(0x1F801054);
  CHECK((stat & 0x0001u) != 0); // TX ready
  CHECK((stat & 0x0004u) != 0); // TX idle
  CHECK((stat & 0x0180u) == 0x0180u); // DSR + CTS high
  return true;
}

static bool test_sio1_rx_ready_after_write() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write8(0x1F801050, 0x01);
  uint16_t stat = mmio.read16(0x1F801054);
  CHECK((stat & 0x0002u) != 0); // RX ready
  uint8_t data = mmio.read8(0x1F801050);
  CHECK(data == 0xFF);
  stat = mmio.read16(0x1F801054);
  CHECK((stat & 0x0002u) == 0);
  return true;
}

static bool test_spu_status_tracks_ctrl() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write16(0x1F801DAA, 0x0030); // transfer mode DMA read
  uint16_t stat = mmio.read16(0x1F801DAE);
  CHECK((stat & 0x003Fu) == 0x0030);
  CHECK((stat & 0x0200u) != 0);
  return true;
}

static bool test_gpu_packet_parsing() {
  std::vector<uint32_t> words = {0x02000000, 0x00000000, 0x00000000};
  std::vector<uint32_t> remainder;
  auto packets = ps1emu::parse_gp0_packets(words, remainder);

  CHECK(packets.size() == 1);
  CHECK(packets[0].command == 0x02);
  CHECK(packets[0].words.size() == 3);
  CHECK(remainder.empty());

  std::vector<uint32_t> poly = {0x48000000, 0x00010002, 0x00030004, 0x50005000};
  packets = ps1emu::parse_gp0_packets(poly, remainder);
  CHECK(packets.size() == 1);
  CHECK(packets[0].command == 0x48);
  CHECK(packets[0].words.size() == 4);
  CHECK(remainder.empty());

  std::vector<uint32_t> incomplete = {0xA0000000, 0x00000000};
  packets = ps1emu::parse_gp0_packets(incomplete, remainder);
  CHECK(packets.empty());
  CHECK(remainder.size() == 2);
  return true;
}

static bool test_gpu_packet_parsing_edges() {
  std::vector<uint32_t> load_image = {
      0xA0000000, 0x00000000, 0x00020004, 0x11111111, 0x22222222, 0x33333333, 0x44444444};
  std::vector<uint32_t> remainder;
  auto packets = ps1emu::parse_gp0_packets(load_image, remainder);
  CHECK(packets.size() == 1);
  CHECK(packets[0].command == 0xA0);
  CHECK(packets[0].words.size() == 7);
  CHECK(remainder.empty());

  std::vector<uint32_t> polyline_partial = {0x48000000, 0x00010002, 0x00030004};
  packets = ps1emu::parse_gp0_packets(polyline_partial, remainder);
  CHECK(packets.empty());
  CHECK(remainder.size() == polyline_partial.size());
  return true;
}

static bool test_memory_map_mmio() {
  ps1emu::MemoryMap mem;
  ps1emu::MmioBus mmio;
  mem.reset();
  mmio.reset();
  mem.attach_mmio(mmio);

  mem.write32(0x1F801074, 0x1234);
  uint32_t val = mem.read32(0x1F801074);
  CHECK((val & 0xFFFFu) == 0x1234u);
  return true;
}

static bool test_cdrom_iso_read_mmio() {
  ScopedTempFile iso("/tmp/ps1emu_test.iso");

  std::vector<uint8_t> data(2048 * 2, 0);
  std::fill(data.begin(), data.begin() + 2048, 0x11);
  std::fill(data.begin() + 2048, data.end(), 0x22);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);

  mmio.tick(kCdromReadPeriodCycles);

  uint8_t b0 = mmio.read8(0x1F801802);
  uint8_t b1 = mmio.read8(0x1F801802);
  uint8_t b2 = mmio.read8(0x1F801802);
  uint8_t b3 = mmio.read8(0x1F801802);
  CHECK(b0 == 0x11);
  CHECK(b1 == 0x11);
  CHECK(b2 == 0x11);
  CHECK(b3 == 0x11);
  return true;
}

static bool test_cdrom_cue_read_mmio() {
  ScopedTempFile bin("/tmp/ps1emu_test.bin");
  ScopedTempFile cue("/tmp/ps1emu_test.cue");

  std::vector<uint8_t> data;
  std::vector<uint8_t> sector0 = make_raw_sector(0, 2, 0x00, 0x30);
  std::vector<uint8_t> sector1 = make_raw_sector(1, 2, 0x00, 0x31);
  data.insert(data.end(), sector0.begin(), sector0.end());
  data.insert(data.end(), sector1.begin(), sector1.end());
  CHECK(write_binary_file(bin.path, data));

  std::ofstream cue_file(cue.path);
  CHECK(cue_file.is_open());
  cue_file << "FILE \"" << bin.path << "\" BINARY\n";
  cue_file << "  TRACK 01 MODE2/2352\n";
  cue_file << "    INDEX 01 00:02:00\n";
  cue_file.close();

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(cue.path, error));

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x01);
  mmio.write8(0x1F801801, 0x02); // Setloc to LBA 1
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06);
  (void)mmio.read8(0x1F801801);

  mmio.tick(kCdromReadPeriodCycles);

  uint8_t b0 = mmio.read8(0x1F801802);
  CHECK(b0 == 0x31);
  return true;
}

static bool test_cdrom_param_filter_roundtrip() {
  ps1emu::MmioBus mmio;
  mmio.reset();

  mmio.write8(0x1F801802, 0xA5);
  mmio.write8(0x1F801801, 0x0E); // Setmode
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801802, 0x12);
  mmio.write8(0x1F801802, 0x34);
  mmio.write8(0x1F801801, 0x0D); // Setfilter
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x0F); // Getparam
  std::vector<uint8_t> resp = read_cdrom_response(mmio, 5);
  CHECK(resp.size() == 5);
  CHECK(resp[1] == 0xA5);
  CHECK(resp[2] == 0x00);
  CHECK(resp[3] == 0x12);
  CHECK(resp[4] == 0x34);
  return true;
}

static bool test_cdrom_loc_and_tracks() {
  ScopedTempFile iso("/tmp/ps1emu_loc.iso");
  std::vector<uint8_t> data(2048 * 2, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);
  mmio.tick(kCdromReadPeriodCycles);

  mmio.write8(0x1F801801, 0x10); // GetlocL
  std::vector<uint8_t> resp = read_cdrom_response(mmio, 9);
  uint8_t mm = 0, ss = 0, ff = 0;
  lba_to_bcd(0, mm, ss, ff);
  CHECK(resp[1] == mm);
  CHECK(resp[2] == ss);
  CHECK(resp[3] == ff);
  CHECK(resp[4] == 0x01);

  mmio.write8(0x1F801801, 0x11); // GetlocP
  resp = read_cdrom_response(mmio, 9);
  lba_to_bcd(0, mm, ss, ff);
  CHECK(resp[1] == 0x01);
  CHECK(resp[2] == 0x01);
  CHECK(resp[3] == mm);
  CHECK(resp[4] == ss);
  CHECK(resp[5] == ff);
  CHECK(resp[6] == mm);
  CHECK(resp[7] == ss);
  CHECK(resp[8] == ff);

  mmio.write8(0x1F801801, 0x13); // GetTN
  resp = read_cdrom_response(mmio, 3);
  CHECK(resp[1] == 0x01);
  CHECK(resp[2] == 0x01);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x14); // GetTD track 0 (lead-out)
  resp = read_cdrom_response(mmio, 4);
  lba_to_bcd(1, mm, ss, ff);
  CHECK(resp[1] == mm);
  CHECK(resp[2] == ss);
  CHECK(resp[3] == ff);

  mmio.write8(0x1F801802, 0x01);
  mmio.write8(0x1F801801, 0x14); // GetTD track 1 (start)
  resp = read_cdrom_response(mmio, 4);
  lba_to_bcd(0, mm, ss, ff);
  CHECK(resp[1] == mm);
  CHECK(resp[2] == ss);
  CHECK(resp[3] == ff);
  return true;
}

static bool test_cdrom_seek_delay_irq() {
  ScopedTempFile iso("/tmp/ps1emu_seek.iso");
  std::vector<uint8_t> data(2048, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);
  mmio.write8(0x1F801803, 0x80 | 0x01);
  mmio.write8(0x1F801803, 0x1F);

  mmio.write8(0x1F801801, 0x15); // SeekL
  uint8_t status = mmio.read8(0x1F801801);
  CHECK((status & 0x08) != 0);

  uint8_t irq = mmio.read8(0x1F801803);
  CHECK((irq & 0x04) != 0);
  mmio.write8(0x1F801803, 0x80 | 0x04);
  CHECK((mmio.read8(0x1F801803) & 0x04) == 0);

  mmio.tick(kCdromSeekDelayCycles - 1);
  CHECK((mmio.read8(0x1F801803) & 0x01) == 0);
  mmio.tick(1);
  CHECK((mmio.read8(0x1F801803) & 0x01) != 0);

  uint8_t done = mmio.read8(0x1F801801);
  CHECK((done & 0x08) == 0);
  return true;
}

static bool test_cdrom_getid_delay_irq() {
  ScopedTempFile iso("/tmp/ps1emu_getid.iso");
  std::vector<uint8_t> data(2048, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801801, 0x1A); // GetID
  (void)mmio.read8(0x1F801801);
  CHECK((mmio.read8(0x1F801803) & 0x04) != 0);
  mmio.write8(0x1F801803, 0x80 | 0x04);

  mmio.tick(kCdromGetIdDelayCycles - 1);
  CHECK((mmio.read8(0x1F801803) & 0x01) == 0);
  mmio.tick(1);
  CHECK((mmio.read8(0x1F801803) & 0x01) != 0);

  std::vector<uint8_t> resp = read_cdrom_response(mmio, 8);
  CHECK(resp.size() == 8);
  CHECK(resp[1] == 0x00);
  CHECK(resp[2] == 0x20);
  CHECK(resp[3] == 0x00);
  CHECK(resp[4] == 'S');
  CHECK(resp[5] == 'C');
  CHECK(resp[6] == 'E');
  CHECK(resp[7] == 'I');
  return true;
}

static bool test_cdrom_toc_delay_irq() {
  ScopedTempFile iso("/tmp/ps1emu_toc.iso");
  std::vector<uint8_t> data(2048, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801801, 0x1E); // ReadTOC
  uint8_t status = mmio.read8(0x1F801801);
  CHECK((status & 0x08) != 0);
  CHECK((mmio.read8(0x1F801803) & 0x04) != 0);
  mmio.write8(0x1F801803, 0x80 | 0x04);

  mmio.tick(kCdromTocDelayCycles - 1);
  CHECK((mmio.read8(0x1F801803) & 0x01) == 0);
  mmio.tick(1);
  CHECK((mmio.read8(0x1F801803) & 0x01) != 0);

  std::vector<uint8_t> resp = read_cdrom_response(mmio, 6);
  uint8_t mm = 0, ss = 0, ff = 0;
  lba_to_bcd(1, mm, ss, ff);
  CHECK(resp[1] == 0x01);
  CHECK(resp[2] == 0x01);
  CHECK(resp[3] == mm);
  CHECK(resp[4] == ss);
  CHECK(resp[5] == ff);

  uint8_t done = mmio.read8(0x1F801800);
  CHECK((done & 0x08) == 0);
  return true;
}

static bool test_cdrom_irq_ack_overlapping() {
  ScopedTempFile iso("/tmp/ps1emu_irq.iso");
  std::vector<uint8_t> data(2048, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801803, 0x1F); // enable all
  mmio.write8(0x1F801803, 0x80 | 0x1F); // clear pending

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);
  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);

  uint8_t flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x04) != 0);
  CHECK((mmio.irq_stat() & (1u << 2)) != 0);

  mmio.write8(0x1F801803, 0x80 | 0x04);
  CHECK((mmio.read8(0x1F801803) & 0x04) == 0);

  mmio.tick(kCdromReadPeriodCycles);
  flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x02) != 0);

  mmio.write8(0x1F801801, 0x01); // Getstat
  (void)mmio.read8(0x1F801801);
  flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x03) == 0x03);

  mmio.write8(0x1F801803, 0x80 | 0x01);
  flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x02) != 0);
  CHECK((flags & 0x01) == 0);
  CHECK((mmio.irq_stat() & (1u << 2)) != 0);

  mmio.write8(0x1F801803, 0x80 | 0x02);
  flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x03) == 0);
  CHECK((mmio.irq_stat() & (1u << 2)) == 0);
  return true;
}

static bool test_cdrom_status_transitions() {
  ScopedTempFile iso("/tmp/ps1emu_status.iso");
  std::vector<uint8_t> data(2048, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801803, 0x1F); // enable IRQs
  mmio.write8(0x1F801803, 0x80 | 0x1F);

  mmio.write8(0x1F801801, 0x01); // Getstat
  uint8_t stat = mmio.read8(0x1F801801);
  CHECK((stat & 0x02) != 0);
  CHECK((stat & 0x10) == 0);
  CHECK((stat & 0x40) == 0);
  CHECK((stat & 0x08) == 0);

  mmio.write8(0x1F801801, 0x03); // Play
  (void)mmio.read8(0x1F801801);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x40) != 0);
  CHECK((stat & 0x10) == 0);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);
  mmio.write8(0x1F801801, 0x15); // SeekL
  (void)mmio.read8(0x1F801801);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x08) != 0);

  mmio.tick(kCdromSeekDelayCycles);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x08) == 0);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x10) != 0);
  CHECK((stat & 0x40) == 0);

  mmio.write8(0x1F801801, 0x09); // Pause
  (void)mmio.read8(0x1F801801);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x10) == 0);
  CHECK((stat & 0x40) == 0);

  mmio.write8(0x1F801801, 0x08); // Stop
  (void)mmio.read8(0x1F801801);
  stat = mmio.read8(0x1F801800);
  CHECK((stat & 0x10) == 0);
  CHECK((stat & 0x40) == 0);
  CHECK((stat & 0x08) == 0);
  return true;
}

static bool test_cdrom_read_irq_cadence() {
  ScopedTempFile iso("/tmp/ps1emu_read_irq.iso");
  std::vector<uint8_t> data(2048 * 2, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(iso.path, error));

  mmio.write8(0x1F801803, 0x1F); // enable IRQs
  mmio.write8(0x1F801803, 0x80 | 0x1F);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);
  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);

  uint8_t flags = mmio.read8(0x1F801803);
  CHECK((flags & 0x04) != 0);
  mmio.write8(0x1F801803, 0x80 | 0x04);

  mmio.tick(kCdromReadPeriodCycles - 1);
  CHECK((mmio.read8(0x1F801803) & 0x02) == 0);
  mmio.tick(1);
  CHECK((mmio.read8(0x1F801803) & 0x02) != 0);
  mmio.write8(0x1F801803, 0x80 | 0x02);
  std::vector<uint8_t> sector(2048);
  (void)mmio.read_cdrom_data(sector.data(), sector.size());

  mmio.tick(kCdromReadPeriodCycles - 1);
  CHECK((mmio.read8(0x1F801803) & 0x02) == 0);
  mmio.tick(1);
  CHECK((mmio.read8(0x1F801803) & 0x02) != 0);

  return true;
}

static bool test_cdrom_whole_sector_mode1() {
  ScopedTempFile bin("/tmp/ps1emu_raw_mode1.bin");
  std::vector<uint8_t> raw = make_raw_sector(0, 1, 0, 0x5A);
  CHECK(write_binary_file(bin.path, raw));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(bin.path, error));

  mmio.write8(0x1F801802, 0x20); // whole sector
  mmio.write8(0x1F801801, 0x0E); // Setmode
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);
  mmio.tick(kCdromReadPeriodCycles);

  uint8_t b0 = mmio.read8(0x1F801802);
  uint8_t b1 = mmio.read8(0x1F801802);
  uint8_t b2 = mmio.read8(0x1F801802);
  uint8_t b3 = mmio.read8(0x1F801802);
  CHECK(b0 == 0x00);
  CHECK(b1 == 0x02);
  CHECK(b2 == 0x00);
  CHECK(b3 == 0x01);

  uint8_t data0 = mmio.read8(0x1F801802);
  CHECK(data0 == 0x5A);
  return true;
}

static bool test_cdrom_mode2_form2_size() {
  ScopedTempFile bin("/tmp/ps1emu_raw_form2.bin");
  std::vector<uint8_t> raw = make_raw_sector(0, 2, 0x20, 0x6B);
  CHECK(write_binary_file(bin.path, raw));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(bin.path, error));

  mmio.write8(0x1F801802, 0x00); // data-only
  mmio.write8(0x1F801801, 0x0E); // Setmode
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);
  mmio.tick(kCdromReadPeriodCycles);

  uint8_t first = mmio.read8(0x1F801802);
  CHECK(first == 0x6B);

  for (size_t i = 1; i < 0x914u; ++i) {
    (void)mmio.read8(0x1F801802);
  }

  uint8_t extra = mmio.read8(0x1F801802);
  CHECK(extra == 0x00);
  return true;
}

static bool test_cdrom_xa_audio_queue() {
  ScopedTempFile bin("/tmp/ps1emu_xa_audio.bin");
  std::vector<uint8_t> raw = make_raw_sector(0, 2, 0x64, 0x7E);
  CHECK(write_binary_file(bin.path, raw));

  ps1emu::MmioBus mmio;
  mmio.reset();
  std::string error;
  CHECK(mmio.load_cdrom_image(bin.path, error));

  mmio.write8(0x1F801802, 0x40); // ADPCM enable
  mmio.write8(0x1F801801, 0x0E); // Setmode
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);

  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);
  mmio.tick(kCdromReadPeriodCycles);

  ps1emu::MmioBus::XaAudioSector sector;
  CHECK(mmio.pop_xa_audio(sector));
  CHECK(sector.data.size() == 0x914u);
  CHECK(sector.data[0] == 0x7E);
  CHECK(sector.lba == 0u);

  uint8_t b0 = mmio.read8(0x1F801802);
  CHECK(b0 == 0x00);
  return true;
}

static bool test_xa_adpcm_zero_decode() {
  std::vector<uint8_t> data(0x900, 0x00);
  for (size_t g = 0; g < 0x12; ++g) {
    size_t base = g * 128;
    data[base + 4] = 0x00;
    data[base + 5] = 0x00;
    data[base + 6] = 0x00;
    data[base + 7] = 0x00;
  }

  ps1emu::XaDecodeState state;
  ps1emu::XaDecodeInfo info;
  std::vector<int16_t> left;
  std::vector<int16_t> right;
  CHECK(ps1emu::decode_xa_adpcm(data.data(), data.size(), 0x00, state, info, left, right));
  CHECK(info.channels == 1);
  CHECK(info.sample_rate == 37800);
  CHECK(left.size() == 0x12u * 4u * 2u * 28u);
  for (size_t i = 0; i < left.size(); ++i) {
    CHECK(left[i] == 0);
  }
  return true;
}

static bool test_xa_adpcm_8bit_zero_decode() {
  std::vector<uint8_t> data(0x900, 0x00);
  for (size_t g = 0; g < 0x12; ++g) {
    size_t base = g * 128;
    data[base + 4] = 0x00;
    data[base + 5] = 0x00;
    data[base + 6] = 0x00;
    data[base + 7] = 0x00;
  }

  ps1emu::XaDecodeState state;
  ps1emu::XaDecodeInfo info;
  std::vector<int16_t> left;
  std::vector<int16_t> right;
  CHECK(ps1emu::decode_xa_adpcm(data.data(), data.size(), 0x10, state, info, left, right));
  CHECK(info.channels == 1);
  CHECK(info.sample_rate == 37800);
  CHECK(left.size() == 0x12u * 4u * 28u);
  for (size_t i = 0; i < left.size(); ++i) {
    CHECK(left[i] == 0);
  }
  return true;
}

static bool test_stub_plugins_handshake() {
  CHECK(run_stub_handshake("./build/ps1emu_spu_stub", "SPU"));
  CHECK(run_stub_handshake("./build/ps1emu_input_stub", "INPUT"));
  CHECK(run_stub_handshake("./build/ps1emu_cdrom_stub", "CDROM"));
  return true;
}

static bool test_gpu_pipeline_dma_integration() {
  ScopedConfigFile config("ps1emu_tests.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  mmio.write32(0x1F801810, 0x02000000);
  mmio.write32(0x1F801810, 0x00000000);
  mmio.write32(0x1F801810, 0x00000000);
  ps1emu::EmulatorCoreTestAccess::flush_gpu(scoped.core);
  CHECK(!mmio.has_gpu_commands());

  uint32_t base = 0x00010000;
  memory.write32(base + 0, 0x02000000);
  memory.write32(base + 4, 0x00000000);
  memory.write32(base + 8, 0x00000000);

  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 2)));
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x0, base);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x4, (1u << 16) | 3u);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x8, (1u << 24));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK(mmio.dma_madr(2) == base + 12);
  CHECK((mmio.irq_stat() & (1u << 3)) != 0);
  return true;
}

static bool test_gpu_dma_read_to_ram() {
  ScopedConfigFile config("ps1emu_tests_gpu_dma_read.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  mmio.queue_gpu_read_data({0x11223344u, 0x55667788u});
  mmio.write32(0x1F801814, 0x04000003u); // DMA direction GPU->CPU

  uint32_t base = 0x00018000;
  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 2)));
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x0, base);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x4, (1u << 16) | 2u);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x8, (1u << 24));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK(memory.read32(base + 0) == 0x11223344u);
  CHECK(memory.read32(base + 4) == 0x55667788u);
  return true;
}

static bool test_gpu_dma_linked_list() {
  ScopedConfigFile config("ps1emu_tests_gpu_dma_chain.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  uint32_t base1 = 0x00030000;
  uint32_t base2 = 0x00030020;
  memory.write32(base1 + 0, (3u << 24) | base2);
  memory.write32(base1 + 4, 0x02000000);
  memory.write32(base1 + 8, 0x00000000);
  memory.write32(base1 + 12, 0x00000000);

  memory.write32(base2 + 0, (1u << 24) | 0x800000u);
  memory.write32(base2 + 4, 0x00000000);

  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 2)));
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x0, base1);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x4, 0);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x8, (1u << 24) | (2u << 8));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK((mmio.irq_stat() & (1u << 3)) != 0);
  CHECK(mmio.dma_madr(2) != base1);
  return true;
}

static bool test_dma_decrement_and_blocks() {
  ScopedConfigFile config("ps1emu_tests_dma.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  uint32_t base = 0x00020000;
  memory.write32(base + 0, 0xAAAA0001);
  memory.write32(base + 4, 0xAAAA0002);
  memory.write32(base + 8, 0xAAAA0003);
  memory.write32(base + 12, 0xAAAA0004);

  uint32_t madr = base + 12;
  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 2)));
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x0, madr);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x4, (2u << 16) | 2u);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x8, (1u << 24) | (1u << 1));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK(mmio.dma_madr(2) == madr - 16);
  CHECK((mmio.irq_stat() & (1u << 3)) != 0);
  return true;
}

static bool test_dma_bcr_zero() {
  ScopedConfigFile config("ps1emu_tests_bcr.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  uint32_t madr = 0x00030000;
  memory.write32(madr, 0x12345678);

  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 2)));
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x0, madr);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x4, 0);
  mmio.write32(0x1F801080 + 0x10 * 2 + 0x8, (1u << 24));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK(mmio.dma_madr(2) == madr + 4);
  return true;
}

static bool test_cdrom_dma_transfer() {
  ScopedTempFile iso("/tmp/ps1emu_dma.iso");

  std::vector<uint8_t> data(2048 * 1, 0x5A);
  CHECK(write_binary_file(iso.path, data));

  ScopedConfigFile config("ps1emu_tests_cdrom.conf");
  CHECK(write_test_config(config.path, iso.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801802, 0x02);
  mmio.write8(0x1F801802, 0x00);
  mmio.write8(0x1F801801, 0x02); // Setloc
  (void)mmio.read8(0x1F801801);
  mmio.write8(0x1F801801, 0x06); // ReadN
  (void)mmio.read8(0x1F801801);

  mmio.tick(kCdromReadPeriodCycles);

  uint32_t madr = 0x00040000;
  mmio.write32(0x1F8010F4, (1u << 23) | (1u << (16 + 3)));
  mmio.write32(0x1F801080 + 0x10 * 3 + 0x0, madr);
  mmio.write32(0x1F801080 + 0x10 * 3 + 0x4, (1u << 16) | 512u);
  mmio.write32(0x1F801080 + 0x10 * 3 + 0x8, (1u << 24));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  uint32_t word0 = memory.read32(madr);
  CHECK((word0 & 0xFFu) == 0x5Au);
  return true;
}

static bool test_dma_otc_clear() {
  ScopedConfigFile config("ps1emu_tests_otc.conf");
  CHECK(write_test_config(config.path));

  ScopedCore scoped;
  CHECK(scoped.core.initialize(config.path));
  scoped.active = true;

  auto &mmio = ps1emu::EmulatorCoreTestAccess::mmio(scoped.core);
  auto &memory = ps1emu::EmulatorCoreTestAccess::memory(scoped.core);

  uint32_t dicr = (1u << 23) | (1u << (16 + 6));
  mmio.write32(0x1F8010F4, dicr);

  uint32_t base = 0x00001000;
  mmio.write32(0x1F801080 + 0x10 * 6 + 0x0, base);
  mmio.write32(0x1F801080 + 0x10 * 6 + 0x4, 4);
  mmio.write32(0x1F801080 + 0x10 * 6 + 0x8, (1u << 24));

  ps1emu::EmulatorCoreTestAccess::process_dma(scoped.core);

  CHECK(memory.read32(base) == 0x00000FFCu);
  CHECK(memory.read32(base - 4) == 0x00000FF8u);
  CHECK(memory.read32(base - 8) == 0x00000FF4u);
  CHECK(memory.read32(base - 12) == 0x00FFFFFFu);
  return true;
}

int main() {
  setenv("PS1EMU_HEADLESS", "1", 1);

  struct Test {
    const char *name;
    bool (*fn)();
  } tests[] = {
      {"load_delay", test_load_delay},
      {"cpu_reset_state", test_cpu_reset_state},
      {"cache_isolated_store_ignored", test_cache_isolated_store_ignored},
      {"branch_delay", test_branch_delay},
      {"cpu_exception_trace", test_cpu_exception_trace},
      {"cpu_exception_sr_shift", test_cpu_exception_sr_shift},
      {"branch_likely_not_taken", test_branch_likely_not_taken},
      {"branch_likely_taken", test_branch_likely_taken},
      {"mmio_gpu_fifo", test_mmio_gpu_fifo},
      {"vblank_irq", test_vblank_irq},
      {"gpu_status_bits", test_gpu_status_bits},
      {"gpu_read_fifo", test_gpu_read_fifo},
      {"gpu_dma_request_bits", test_gpu_dma_request_bits},
      {"gpu_read_delay", test_gpu_read_delay},
      {"gpu_stat_busy_decay", test_gpu_stat_busy_decay},
      {"gte_flags_and_saturation", test_gte_flags_and_saturation},
      {"gte_color_fifo_saturation", test_gte_color_fifo_saturation},
      {"gte_divide_overflow", test_gte_divide_overflow},
      {"gte_dpct_rgb0", test_gte_dpct_uses_rgb0},
      {"gte_rtps_lm_ignored", test_gte_rtps_lm_ignored},
      {"gte_gpl_overflow_flag", test_gte_gpl_overflow_flag},
      {"gte_h_read_signext", test_gte_h_read_sign_extension},
      {"gte_sxyp_fifo", test_gte_sxyp_write_fifo},
      {"gte_mvmva_fc_bug", test_gte_mvmva_fc_bug},
      {"gte_mvmva_mx3", test_gte_mvmva_mx3_garbage_matrix},
      {"gte_dpcs_depth_cue", test_gte_dpcs_depth_cue_extremes},
      {"gte_command_cycles", test_gte_command_cycles},
      {"gte_lwc2_delay", test_gte_lwc2_delay},
      {"dma_irq", test_dma_irq},
      {"dma_dicr_clears_irq", test_dma_dicr_clears_irq},
      {"timer_irq_on_target", test_timer_irq_on_target},
      {"joypad_stub_ready", test_joypad_stub_ready},
      {"joypad_rx_ready_after_write", test_joypad_rx_ready_after_write},
      {"sio1_stub_ready", test_sio1_stub_ready},
      {"sio1_rx_ready_after_write", test_sio1_rx_ready_after_write},
      {"spu_status_tracks_ctrl", test_spu_status_tracks_ctrl},
      {"gpu_packet_parsing", test_gpu_packet_parsing},
      {"gpu_packet_parsing_edges", test_gpu_packet_parsing_edges},
      {"memory_map_mmio", test_memory_map_mmio},
      {"cdrom_iso_read_mmio", test_cdrom_iso_read_mmio},
      {"cdrom_cue_read_mmio", test_cdrom_cue_read_mmio},
      {"cdrom_param_filter_roundtrip", test_cdrom_param_filter_roundtrip},
      {"cdrom_loc_and_tracks", test_cdrom_loc_and_tracks},
      {"cdrom_seek_delay_irq", test_cdrom_seek_delay_irq},
      {"cdrom_getid_delay_irq", test_cdrom_getid_delay_irq},
      {"cdrom_toc_delay_irq", test_cdrom_toc_delay_irq},
      {"cdrom_irq_ack_overlapping", test_cdrom_irq_ack_overlapping},
      {"cdrom_status_transitions", test_cdrom_status_transitions},
      {"cdrom_read_irq_cadence", test_cdrom_read_irq_cadence},
      {"cdrom_whole_sector_mode1", test_cdrom_whole_sector_mode1},
      {"cdrom_mode2_form2_size", test_cdrom_mode2_form2_size},
      {"cdrom_xa_audio_queue", test_cdrom_xa_audio_queue},
      {"xa_adpcm_zero_decode", test_xa_adpcm_zero_decode},
      {"xa_adpcm_8bit_zero_decode", test_xa_adpcm_8bit_zero_decode},
      {"stub_plugins_handshake", test_stub_plugins_handshake},
      {"gpu_pipeline_dma_integration", test_gpu_pipeline_dma_integration},
      {"gpu_dma_read_to_ram", test_gpu_dma_read_to_ram},
      {"gpu_dma_linked_list", test_gpu_dma_linked_list},
      {"dma_decrement_and_blocks", test_dma_decrement_and_blocks},
      {"dma_bcr_zero", test_dma_bcr_zero},
      {"cdrom_dma_transfer", test_cdrom_dma_transfer},
      {"dma_otc_clear", test_dma_otc_clear},
  };

  int passed = 0;
  int failed = 0;
  for (const auto &t : tests) {
    if (t.fn()) {
      std::cout << "PASS: " << t.name << "\n";
      passed++;
    } else {
      failed++;
    }
  }

  std::cout << "Summary: " << passed << " passed, " << failed << " failed.\n";
  return failed == 0 ? 0 : 1;
}
