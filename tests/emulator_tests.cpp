#include "core/cpu.h"
#include "core/emu_core.h"
#include "core/gpu_packets.h"
#include "core/memory_map.h"
#include "core/mmio.h"
#include "core/scheduler.h"
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

  mmio.write32(0x1F801070, (1u << 3));
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

  std::vector<uint8_t> data(2352 * 2, 0);
  for (size_t sector = 0; sector < 2; ++sector) {
    size_t base = sector * 2352 + 24;
    std::fill(data.begin() + static_cast<long>(base),
              data.begin() + static_cast<long>(base + 2048),
              static_cast<uint8_t>(0x30 + sector));
  }
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

  uint8_t b0 = mmio.read8(0x1F801802);
  CHECK(b0 == 0x31);
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

int main() {
  setenv("PS1EMU_HEADLESS", "1", 1);

  struct Test {
    const char *name;
    bool (*fn)();
  } tests[] = {
      {"load_delay", test_load_delay},
      {"branch_delay", test_branch_delay},
      {"mmio_gpu_fifo", test_mmio_gpu_fifo},
      {"gpu_status_bits", test_gpu_status_bits},
      {"gpu_read_fifo", test_gpu_read_fifo},
      {"dma_irq", test_dma_irq},
      {"timer_irq_on_target", test_timer_irq_on_target},
      {"gpu_packet_parsing", test_gpu_packet_parsing},
      {"gpu_packet_parsing_edges", test_gpu_packet_parsing_edges},
      {"memory_map_mmio", test_memory_map_mmio},
      {"cdrom_iso_read_mmio", test_cdrom_iso_read_mmio},
      {"cdrom_cue_read_mmio", test_cdrom_cue_read_mmio},
      {"stub_plugins_handshake", test_stub_plugins_handshake},
      {"gpu_pipeline_dma_integration", test_gpu_pipeline_dma_integration},
      {"dma_decrement_and_blocks", test_dma_decrement_and_blocks},
      {"dma_bcr_zero", test_dma_bcr_zero},
      {"cdrom_dma_transfer", test_cdrom_dma_transfer},
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
