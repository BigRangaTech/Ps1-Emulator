#ifndef PS1EMU_CONFIG_H
#define PS1EMU_CONFIG_H

#include "ps1emu/sandbox.h"

#include <string>

namespace ps1emu {

enum class CpuMode {
  Auto,
  Interpreter,
  Dynarec
};

struct Config {
  std::string bios_path;
  std::string plugin_gpu;
  std::string plugin_spu;
  std::string plugin_input;
  std::string plugin_cdrom;
  std::string cdrom_image;
  CpuMode cpu_mode = CpuMode::Auto;
  SandboxOptions sandbox;
};

bool load_config_file(const std::string &path, Config &out, std::string &error);
bool update_config_value(const std::string &path,
                         const std::string &key,
                         const std::string &value,
                         std::string &error);

} // namespace ps1emu

#endif
