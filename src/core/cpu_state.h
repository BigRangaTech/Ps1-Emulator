#ifndef PS1EMU_CPU_STATE_H
#define PS1EMU_CPU_STATE_H

#include <cstdint>

namespace ps1emu {

struct CpuState {
  uint32_t gpr[32] = {};
  uint32_t pc = 0;
  uint32_t next_pc = 0;
  uint32_t hi = 0;
  uint32_t lo = 0;
};

} // namespace ps1emu

#endif
