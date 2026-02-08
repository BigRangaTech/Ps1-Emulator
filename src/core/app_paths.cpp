#include "core/app_paths.h"

#include <cstdlib>
#include <filesystem>

namespace ps1emu {

std::string app_data_dir() {
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg) {
    return std::string(xdg) + "/ps1emu";
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.local/share/ps1emu";
  }
  return "./data";
}

bool ensure_directory(const std::string &path, std::string &error) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    return true;
  }
  if (!std::filesystem::create_directories(path, ec)) {
    error = "Unable to create directory: " + path;
    return false;
  }
  return true;
}

} // namespace ps1emu
