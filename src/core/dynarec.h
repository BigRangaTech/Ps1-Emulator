#ifndef PS1EMU_DYNAREC_H
#define PS1EMU_DYNAREC_H

#include "core/cpu_state.h"
#include "core/memory_map.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ps1emu {

using JitFunc = uint32_t (*)(CpuState *state, MemoryMap *memory);

struct JitBlock {
  uint32_t pc = 0;
  uint32_t size = 0;
  JitFunc entry = nullptr;
  uint64_t last_used = 0;
  std::vector<uint32_t> opcodes;
};

class DynarecBackend {
public:
  virtual ~DynarecBackend() = default;
  virtual JitFunc compile_block(uint32_t pc, const MemoryMap &memory, uint32_t &out_size) = 0;
};

class NullDynarecBackend final : public DynarecBackend {
public:
  JitFunc compile_block(uint32_t pc, const MemoryMap &memory, uint32_t &out_size) override;
};

class DynarecCache {
public:
  explicit DynarecCache(size_t max_blocks = 4096);

  JitBlock *lookup(uint32_t pc);
  JitBlock *compile(uint32_t pc, DynarecBackend &backend, const MemoryMap &memory);
  void invalidate_range(uint32_t start, uint32_t size);
  void invalidate_all();

private:
  void touch(JitBlock &block);
  void evict_if_needed();

  size_t max_blocks_;
  uint64_t tick_ = 0;
  std::unordered_map<uint32_t, JitBlock> blocks_;
};

} // namespace ps1emu

#endif
