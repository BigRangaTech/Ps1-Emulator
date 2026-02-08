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

  struct Cop0 {
    uint32_t sr = 0;
    uint32_t cause = 0;
    uint32_t epc = 0;
    uint32_t badvaddr = 0;
    uint32_t prid = 0x00000002;
  } cop0;
};

} // namespace ps1emu

#endif
