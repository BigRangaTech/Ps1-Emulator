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
  uint32_t step_interpreter();
  uint32_t step_dynarec();

  MemoryMap *memory_ = nullptr;
  Scheduler *scheduler_ = nullptr;
  Mode mode_ = Mode::Interpreter;
  CpuState state_;
  DynarecCache dynarec_cache_;
  std::unique_ptr<DynarecBackend> dynarec_backend_;
};

} // namespace ps1emu

#endif
