#ifndef PS1EMU_IPC_H
#define PS1EMU_IPC_H

#include "ps1emu/sandbox.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ps1emu {

class IpcChannel {
public:
  IpcChannel() = default;
  explicit IpcChannel(int read_fd, int write_fd);
  ~IpcChannel();

  IpcChannel(const IpcChannel &) = delete;
  IpcChannel &operator=(const IpcChannel &) = delete;
  IpcChannel(IpcChannel &&) noexcept;
  IpcChannel &operator=(IpcChannel &&) noexcept;

  bool valid() const;
  bool send_line(const std::string &line);
  bool recv_line(std::string &out_line);
  bool send_frame(uint16_t type, const std::vector<uint8_t> &payload);
  bool recv_frame(uint16_t &out_type, std::vector<uint8_t> &out_payload);

private:
  int read_fd_ = -1;
  int write_fd_ = -1;
  std::string read_buffer_;
};

struct SpawnResult {
  int pid = -1;
  IpcChannel channel;
};

SpawnResult spawn_plugin_process(const std::string &path,
                                 const std::vector<std::string> &args,
                                 const SandboxOptions &sandbox);

} // namespace ps1emu

#endif
