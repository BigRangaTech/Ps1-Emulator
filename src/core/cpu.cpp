#include "core/cpu.h"

namespace ps1emu {

CpuCore::CpuCore(MemoryMap &memory, Scheduler &scheduler)
    : memory_(&memory),
      scheduler_(&scheduler),
      dynarec_cache_(4096),
      dynarec_backend_(std::make_unique<NullDynarecBackend>()) {}

void CpuCore::reset() {
  // TODO: reset registers and pipeline state.
  state_ = {};
  state_.pc = 0xBFC00000;
  state_.next_pc = state_.pc + 4;
  dynarec_cache_.invalidate_all();
}

void CpuCore::set_mode(Mode mode) {
  mode_ = mode;
}

CpuCore::Mode CpuCore::mode() const {
  return mode_;
}

CpuState &CpuCore::state() {
  return state_;
}

uint32_t CpuCore::step() {
  if (mode_ == Mode::Dynarec) {
    return step_dynarec();
  }
  return step_interpreter();
}

bool CpuCore::dynarec_available() {
  return true;
}

void CpuCore::invalidate_code_range(uint32_t start, uint32_t size) {
  dynarec_cache_.invalidate_range(start, size);
}

uint32_t CpuCore::step_interpreter() {
  // Minimal fetch-only interpreter stub.
  uint32_t opcode = memory_->read32(state_.pc);
  (void)opcode;
  state_.pc = state_.next_pc;
  state_.next_pc = state_.pc + 4;
  state_.gpr[0] = 0;
  if (scheduler_) {
    scheduler_->advance(1);
  }
  return 1;
}

uint32_t CpuCore::step_dynarec() {
  if (!dynarec_backend_) {
    return step_interpreter();
  }

  JitBlock *block = dynarec_cache_.lookup(state_.pc);
  if (!block) {
    block = dynarec_cache_.compile(state_.pc, *dynarec_backend_, *memory_);
  }

  if (block && block->entry) {
    uint32_t cycles = block->entry(&state_, memory_);
    if (scheduler_) {
      scheduler_->advance(cycles);
    }
    return cycles;
  }

  // Fallback to interpreter until a backend is available.
  return step_interpreter();
}

} // namespace ps1emu
