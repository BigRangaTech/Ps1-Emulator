#include "core/gpu_commands.h"

namespace ps1emu {

std::vector<uint32_t> build_demo_gpu_commands() {
  std::vector<uint32_t> commands;

  // These are placeholder words for the GPU stub. They do not represent real PS1 GPU commands yet.
  commands.push_back(0x10000001);
  commands.push_back(0x20000002);
  commands.push_back(0x30000003);
  commands.push_back(0x40000004);
  commands.push_back(0x50000005);

  return commands;
}

} // namespace ps1emu
