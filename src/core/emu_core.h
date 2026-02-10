#ifndef PS1EMU_CORE_H
#define PS1EMU_CORE_H

#include "core/bios.h"
#include "core/config.h"
#include "core/cpu.h"
#include "core/gpu_packets.h"
#include "core/memory_map.h"
#include "core/mmio.h"
#include "core/scheduler.h"
#include "plugins/plugin_host.h"

#include <cstdint>
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

private:
  friend struct EmulatorCoreTestAccess;
  void flush_gpu_commands();
  void flush_gpu_control();
  void process_dma();
  bool send_gpu_packet(const GpuPacket &packet);
  bool request_vram_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  bool load_and_apply_config(const std::string &config_path);
  CpuCore::Mode resolve_cpu_mode() const;

  PluginHost plugin_host_;
  Config config_;
  BiosImage bios_;
  MemoryMap memory_;
  MmioBus mmio_;
  Scheduler scheduler_;
  CpuCore cpu_;
  std::vector<uint32_t> gpu_dma_remainder_;
};

} // namespace ps1emu

#endif
