#ifndef PS1EMU_CORE_H
#define PS1EMU_CORE_H

#include "core/bios.h"
#include "core/config.h"
#include "core/cpu.h"
#include "core/memory_map.h"
#include "core/scheduler.h"
#include "plugins/plugin_host.h"

#include <cstdint>

namespace ps1emu {

class EmulatorCore {
public:
  EmulatorCore();

  bool initialize(const std::string &config_path);
  void run_for_cycles(uint32_t cycles);
  void shutdown();

private:
  bool load_and_apply_config(const std::string &config_path);
  CpuCore::Mode resolve_cpu_mode() const;

  PluginHost plugin_host_;
  Config config_;
  BiosImage bios_;
  MemoryMap memory_;
  Scheduler scheduler_;
  CpuCore cpu_;
};

} // namespace ps1emu

#endif
