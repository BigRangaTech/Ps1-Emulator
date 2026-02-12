#ifndef PS1EMU_CORE_H
#define PS1EMU_CORE_H

#include "core/bios.h"
#include "core/config.h"
#include "core/cpu.h"
#include "core/gpu_packets.h"
#include "core/memory_map.h"
#include "core/mmio.h"
#include "core/scheduler.h"
#include "core/xa_adpcm.h"
#include "plugins/plugin_host.h"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace ps1emu {

class EmulatorCore {
public:
  EmulatorCore();

  bool initialize(const std::string &config_path);
  void run_for_cycles(uint32_t cycles);
  void dump_dynarec_profile() const;
  void shutdown();
  const Config &config() const;
  bool bios_is_hle() const;
  void set_trace_enabled(bool enabled);
  void set_trace_period_cycles(uint32_t cycles);
  void set_trace_pc(uint32_t pc);
  void set_trace_pc_period_cycles(uint32_t cycles);
  void set_watchdog_enabled(bool enabled);
  void set_watchdog_sample_cycles(uint32_t cycles);
  void set_watchdog_stall_cycles(uint32_t cycles);
  void dump_memory_words(uint32_t addr, uint32_t words) const;

private:
  friend struct EmulatorCoreTestAccess;
  void flush_gpu_commands();
  void flush_gpu_control();
  void flush_spu_controls();
  void flush_xa_audio();
  void process_dma();
  void flush_gpu_dma_pending();
  bool send_gpu_packet(const GpuPacket &packet);
  bool request_vram_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  bool load_and_apply_config(const std::string &config_path);
  CpuCore::Mode resolve_cpu_mode() const;
  void log_trace_state(const char *label);
  void log_exception_event(const CpuExceptionInfo &info);
  void log_trace_pc_state(uint32_t instr_pc);
  void watchdog_sample();

  PluginHost plugin_host_;
  Config config_;
  BiosImage bios_;
  MemoryMap memory_;
  MmioBus mmio_;
  Scheduler scheduler_;
  CpuCore cpu_;
  std::vector<uint32_t> gpu_dma_remainder_;
  std::deque<GpuPacket> gpu_dma_pending_packets_;
  std::unordered_map<uint16_t, XaDecodeState> xa_decode_states_;
  uint16_t spu_main_vol_l_ = 0x3FFF;
  uint16_t spu_main_vol_r_ = 0x3FFF;
  bool trace_enabled_ = false;
  uint32_t trace_period_cycles_ = 1000000;
  bool trace_pc_enabled_ = false;
  uint32_t trace_pc_ = 0;
  uint32_t trace_pc_period_cycles_ = 1000000;
  uint64_t next_trace_pc_cycle_ = 0;
  uint64_t total_cycles_ = 0;
  uint64_t next_trace_cycle_ = 0;
  bool watchdog_enabled_ = false;
  uint32_t watchdog_sample_cycles_ = 2048;
  uint32_t watchdog_stall_cycles_ = 1000000;
  uint64_t watchdog_cycle_accum_ = 0;
  uint32_t watchdog_last_pc_ = 0;
  uint32_t watchdog_prev_pc_ = 0;
  uint32_t watchdog_prev2_pc_ = 0;
  uint32_t watchdog_same_pc_samples_ = 0;
  uint32_t watchdog_alt_pc_samples_ = 0;
  bool watchdog_reported_ = false;
};

} // namespace ps1emu

#endif
