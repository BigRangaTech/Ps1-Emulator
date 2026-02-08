#ifndef PS1EMU_CPU_H
#define PS1EMU_CPU_H

#include "core/cpu_state.h"
#include "core/dynarec.h"
#include "core/memory_map.h"
#include "core/scheduler.h"

#include <cstdint>
#include <memory>

namespace ps1emu {

class CpuCore {
public:
  enum class Mode {
    Interpreter,
    Dynarec
  };

  CpuCore(MemoryMap &memory, Scheduler &scheduler);

  void reset();
  void set_mode(Mode mode);
  Mode mode() const;
  CpuState &state();
  std::vector<JitBlock> dynarec_blocks() const;

  uint32_t step();

  static bool dynarec_available();
  void invalidate_code_range(uint32_t start, uint32_t size);

private:
  struct PendingLoad {
    bool valid = false;
    uint32_t reg = 0;
    uint32_t value = 0;
  };

  uint32_t step_interpreter();
  uint32_t step_dynarec();

  uint32_t execute_instruction(uint32_t instr,
                               uint32_t instr_pc,
                               bool in_delay,
                               PendingLoad &out_load,
                               bool &out_branch,
                               bool &out_exception);
  uint32_t read_reg(uint32_t index) const;
  void write_reg(uint32_t index, uint32_t value);
  void set_branch_target(uint32_t target);
  void raise_exception(uint32_t excode, uint32_t badaddr, bool in_delay, uint32_t instr_pc, uint32_t epc_pc);
  bool check_interrupts();

  MemoryMap *memory_ = nullptr;
  Scheduler *scheduler_ = nullptr;
  Mode mode_ = Mode::Interpreter;
  CpuState state_;
  DynarecCache dynarec_cache_;
  std::unique_ptr<DynarecBackend> dynarec_backend_;
  PendingLoad load_delay_;
  bool load_delay_shadow_valid_ = false;
  uint32_t load_delay_shadow_reg_ = 0;
  uint32_t load_delay_shadow_value_ = 0;
  bool branch_pending_ = false;
};

} // namespace ps1emu

#endif
