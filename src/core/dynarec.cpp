#include "core/dynarec.h"

namespace ps1emu {

JitFunc NullDynarecBackend::compile_block(uint32_t pc, const MemoryMap &memory, uint32_t &out_size) {
  (void)pc;
  (void)memory;
  out_size = 0;
  return nullptr;
}

DynarecCache::DynarecCache(size_t max_blocks) : max_blocks_(max_blocks) {}

static bool is_branch_or_jump(uint32_t opcode) {
  uint32_t op = opcode >> 26;
  if (op == 0x02 || op == 0x03) { // J, JAL
    return true;
  }
  if (op == 0x04 || op == 0x05 || op == 0x06 || op == 0x07) { // BEQ/BNE/BLEZ/BGTZ
    return true;
  }
  if (op == 0x01) { // REGIMM
    return true;
  }
  if (op == 0x00) {
    uint32_t funct = opcode & 0x3F;
    if (funct == 0x08 || funct == 0x09) { // JR/JALR
      return true;
    }
  }
  return false;
}

static void decode_block(uint32_t pc, const MemoryMap &memory, std::vector<uint32_t> &out_opcodes, uint32_t &out_size) {
  out_opcodes.clear();
  constexpr uint32_t kMaxInstructions = 16;
  uint32_t cursor = pc;
  for (uint32_t i = 0; i < kMaxInstructions; ++i) {
    uint32_t opcode = memory.read32(cursor);
    out_opcodes.push_back(opcode);
    cursor += 4;
    if (is_branch_or_jump(opcode)) {
      break;
    }
  }
  out_size = static_cast<uint32_t>(out_opcodes.size() * 4);
}

void DynarecCache::touch(JitBlock &block) {
  block.last_used = ++tick_;
}

JitBlock *DynarecCache::lookup(uint32_t pc) {
  auto it = blocks_.find(pc);
  if (it == blocks_.end()) {
    return nullptr;
  }
  touch(it->second);
  return &it->second;
}

JitBlock *DynarecCache::compile(uint32_t pc, DynarecBackend &backend, const MemoryMap &memory) {
  uint32_t size = 0;
  JitFunc entry = backend.compile_block(pc, memory, size);

  std::vector<uint32_t> opcodes;
  uint32_t decoded_size = 0;
  decode_block(pc, memory, opcodes, decoded_size);
  if (size == 0) {
    size = decoded_size;
  }

  JitBlock block;
  block.pc = pc;
  block.size = size;
  block.entry = entry;
  block.last_used = ++tick_;
  block.opcodes = std::move(opcodes);

  auto result = blocks_.emplace(pc, block);
  if (!result.second) {
    result.first->second = block;
  }

  evict_if_needed();
  return &blocks_.find(pc)->second;
}

void DynarecCache::invalidate_range(uint32_t start, uint32_t size) {
  if (size == 0 || blocks_.empty()) {
    return;
  }
  uint32_t end = start + size;
  for (auto it = blocks_.begin(); it != blocks_.end();) {
    if (it->first >= start && it->first < end) {
      it = blocks_.erase(it);
    } else {
      ++it;
    }
  }
}

void DynarecCache::invalidate_all() {
  blocks_.clear();
}

void DynarecCache::evict_if_needed() {
  if (blocks_.size() <= max_blocks_) {
    return;
  }

  uint64_t oldest_tick = UINT64_MAX;
  uint32_t oldest_pc = 0;
  for (const auto &pair : blocks_) {
    if (pair.second.last_used < oldest_tick) {
      oldest_tick = pair.second.last_used;
      oldest_pc = pair.first;
    }
  }
  if (oldest_tick != UINT64_MAX) {
    blocks_.erase(oldest_pc);
  }
}

} // namespace ps1emu
