#include "core/config_paths.h"

#include <cstdlib>
#include <filesystem>

namespace ps1emu {

static bool exists(const std::string &path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::string default_config_path() {
  const char *env = std::getenv("PS1EMU_CONFIG");
  if (env && *env) {
    return std::string(env);
  }

  if (exists("ps1emu.conf")) {
    return "ps1emu.conf";
  }

  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) {
    std::string path = std::string(xdg) + "/ps1emu/ps1emu.conf";
    if (exists(path)) {
      return path;
    }
  }

  const char *home = std::getenv("HOME");
  if (home && *home) {
    std::string path = std::string(home) + "/.config/ps1emu/ps1emu.conf";
    if (exists(path)) {
      return path;
    }
  }

  if (exists("/app/share/ps1emu/ps1emu.conf")) {
    return "/app/share/ps1emu/ps1emu.conf";
  }

  return "ps1emu.conf";
}

} // namespace ps1emu
