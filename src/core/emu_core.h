#ifndef PS1EMU_CORE_H
#define PS1EMU_CORE_H

#include "core/bios.h"
#include "core/config.h"
#include "core/cpu.h"
#include "core/memory_map.h"
#include "core/mmio.h"
#include "core/scheduler.h"
#include "plugins/plugin_host.h"

#include <cstdint>

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
  void process_dma();

  bool load_and_apply_config(const std::string &config_path);
  CpuCore::Mode resolve_cpu_mode() const;

  PluginHost plugin_host_;
  Config config_;
  BiosImage bios_;
  MemoryMap memory_;
  MmioBus mmio_;
  Scheduler scheduler_;
  CpuCore cpu_;
};

} // namespace ps1emu

#endif
