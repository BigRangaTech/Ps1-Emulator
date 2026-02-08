#include "plugins/plugin_host.h"

#include <sys/wait.h>

#include <sstream>

namespace ps1emu {

const char *PluginHost::type_to_string(PluginType type) const {
  switch (type) {
    case PluginType::Gpu:
      return "GPU";
    case PluginType::Spu:
      return "SPU";
    case PluginType::Input:
      return "INPUT";
    case PluginType::Cdrom:
      return "CDROM";
  }
  return "UNKNOWN";
}

bool PluginHost::launch_plugin(PluginType type, const std::string &path, const SandboxOptions &sandbox) {
  std::vector<std::string> args;
  SpawnResult result = spawn_plugin_process(path, args, sandbox);
  if (result.pid <= 0 || !result.channel.valid()) {
    return false;
  }
  plugins_[type] = PluginProcess{result.pid, std::move(result.channel)};
  return true;
}

bool PluginHost::handshake(PluginType type) {
  auto it = plugins_.find(type);
  if (it == plugins_.end()) {
    return false;
  }

  std::ostringstream msg;
  msg << "HELLO " << type_to_string(type) << " 1";
  if (!it->second.channel.send_line(msg.str())) {
    return false;
  }

  std::string reply;
  if (!it->second.channel.recv_line(reply)) {
    return false;
  }

  std::ostringstream expected;
  expected << "READY " << type_to_string(type) << " 1";
  return reply == expected.str();
}

bool PluginHost::enter_frame_mode(PluginType type) {
  auto it = plugins_.find(type);
  if (it == plugins_.end()) {
    return false;
  }

  if (!it->second.channel.send_line("FRAME_MODE")) {
    return false;
  }

  std::string reply;
  if (!it->second.channel.recv_line(reply)) {
    return false;
  }
  if (reply != "FRAME_READY") {
    return false;
  }

  it->second.frame_mode = true;
  return true;
}

bool PluginHost::send_frame(PluginType type, uint16_t message_type, const std::vector<uint8_t> &payload) {
  auto it = plugins_.find(type);
  if (it == plugins_.end() || !it->second.frame_mode) {
    return false;
  }
  return it->second.channel.send_frame(message_type, payload);
}

bool PluginHost::recv_frame(PluginType type, uint16_t &out_type, std::vector<uint8_t> &out_payload) {
  auto it = plugins_.find(type);
  if (it == plugins_.end() || !it->second.frame_mode) {
    return false;
  }
  return it->second.channel.recv_frame(out_type, out_payload);
}

void PluginHost::shutdown_all() {
  std::vector<int> pids;
  pids.reserve(plugins_.size());
  for (const auto &entry : plugins_) {
    if (entry.second.pid > 0) {
      pids.push_back(entry.second.pid);
    }
  }

  plugins_.clear();

  for (int pid : pids) {
    int status = 0;
    waitpid(pid, &status, 0);
  }
}

} // namespace ps1emu
