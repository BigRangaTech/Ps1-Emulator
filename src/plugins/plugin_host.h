#ifndef PS1EMU_PLUGIN_HOST_H
#define PS1EMU_PLUGIN_HOST_H

#include "plugins/ipc.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps1emu {

enum class PluginType {
  Gpu,
  Spu,
  Input,
  Cdrom
};

struct PluginProcess {
  int pid = -1;
  IpcChannel channel;
  bool frame_mode = false;
};

class PluginHost {
public:
  bool launch_plugin(PluginType type, const std::string &path, const SandboxOptions &sandbox);
  bool handshake(PluginType type);
  bool enter_frame_mode(PluginType type);
  bool send_frame(PluginType type, uint16_t message_type, const std::vector<uint8_t> &payload);
  bool recv_frame(PluginType type, uint16_t &out_type, std::vector<uint8_t> &out_payload);
  bool is_frame_mode(PluginType type) const;
  void shutdown_all();

private:
  std::unordered_map<PluginType, PluginProcess> plugins_;
  const char *type_to_string(PluginType type) const;
};

} // namespace ps1emu

#endif
