#include "core/config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace ps1emu {

static std::string trim(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

static std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

static bool parse_bool(const std::string &value, bool &out) {
  std::string normalized = to_lower(trim(value));
  if (normalized == "true" || normalized == "1" || normalized == "yes") {
    out = true;
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no") {
    out = false;
    return true;
  }
  return false;
}

static bool parse_int(const std::string &value, int &out) {
  try {
    size_t idx = 0;
    int parsed = std::stoi(trim(value), &idx, 10);
    if (idx != trim(value).size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

static bool parse_cpu_mode(const std::string &value, CpuMode &out) {
  std::string normalized = to_lower(trim(value));
  if (normalized == "auto") {
    out = CpuMode::Auto;
    return true;
  }
  if (normalized == "interpreter") {
    out = CpuMode::Interpreter;
    return true;
  }
  if (normalized == "dynarec") {
    out = CpuMode::Dynarec;
    return true;
  }
  return false;
}

bool load_config_file(const std::string &path, Config &out, std::string &error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Unable to open config file: " + path;
    return false;
  }

  std::string line;
  int line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    size_t equals = trimmed.find('=');
    if (equals == std::string::npos) {
      std::ostringstream msg;
      msg << "Invalid config line " << line_number << " (missing '=')";
      error = msg.str();
      return false;
    }

    std::string key = trim(trimmed.substr(0, equals));
    std::string value = trim(trimmed.substr(equals + 1));

    if (key == "bios.path") {
      out.bios_path = value;
      continue;
    }
    if (key == "plugin.gpu") {
      out.plugin_gpu = value;
      continue;
    }
    if (key == "plugin.spu") {
      out.plugin_spu = value;
      continue;
    }
    if (key == "plugin.input") {
      out.plugin_input = value;
      continue;
    }
    if (key == "plugin.cdrom") {
      out.plugin_cdrom = value;
      continue;
    }
    if (key == "cpu.mode") {
      CpuMode mode = CpuMode::Auto;
      if (!parse_cpu_mode(value, mode)) {
        error = "Invalid cpu.mode value";
        return false;
      }
      out.cpu_mode = mode;
      continue;
    }
    if (key == "sandbox.enabled") {
      bool enabled = true;
      if (!parse_bool(value, enabled)) {
        error = "Invalid sandbox.enabled value";
        return false;
      }
      out.sandbox.enabled = enabled;
      continue;
    }
    if (key == "sandbox.seccomp_strict") {
      bool enabled = false;
      if (!parse_bool(value, enabled)) {
        error = "Invalid sandbox.seccomp_strict value";
        return false;
      }
      out.sandbox.seccomp_strict = enabled;
      continue;
    }
    if (key == "sandbox.rlimit_cpu_seconds") {
      int parsed = 0;
      if (!parse_int(value, parsed)) {
        error = "Invalid sandbox.rlimit_cpu_seconds value";
        return false;
      }
      out.sandbox.rlimit_cpu_seconds = parsed;
      continue;
    }
    if (key == "sandbox.rlimit_as_mb") {
      int parsed = 0;
      if (!parse_int(value, parsed)) {
        error = "Invalid sandbox.rlimit_as_mb value";
        return false;
      }
      out.sandbox.rlimit_as_mb = parsed;
      continue;
    }
    if (key == "sandbox.rlimit_nofile") {
      int parsed = 0;
      if (!parse_int(value, parsed)) {
        error = "Invalid sandbox.rlimit_nofile value";
        return false;
      }
      out.sandbox.rlimit_nofile = parsed;
      continue;
    }

    // Unknown keys are ignored to allow forward-compatible configs.
  }

  return true;
}

bool update_config_value(const std::string &path,
                         const std::string &key,
                         const std::string &value,
                         std::string &error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Unable to open config file: " + path;
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  bool updated = false;
  while (std::getline(file, line)) {
    std::string original = line;
    if (!original.empty() && original.back() == '\r') {
      original.pop_back();
    }
    std::string trimmed = trim(original);
    if (!trimmed.empty() && trimmed[0] != '#') {
      if (trimmed.rfind(key, 0) == 0 && trimmed.size() > key.size() && trimmed[key.size()] == '=') {
        original = key + "=" + value;
        updated = true;
      }
    }
    lines.push_back(original);
  }

  if (!updated) {
    lines.push_back(key + "=" + value);
  }

  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    error = "Unable to write config file: " + path;
    return false;
  }
  for (const auto &out_line : lines) {
    out << out_line << "\n";
  }

  return true;
}

} // namespace ps1emu
