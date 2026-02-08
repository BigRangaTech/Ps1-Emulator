#include "core/cpu.h"

#include <cstdint>

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
  load_delay_ = {};
  branch_pending_ = false;
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

std::vector<JitBlock> CpuCore::dynarec_blocks() const {
  return dynarec_cache_.snapshot();
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
  if (load_delay_.valid) {
    write_reg(load_delay_.reg, load_delay_.value);
    load_delay_ = {};
  }

  if (check_interrupts()) {
    if (scheduler_) {
      scheduler_->advance(1);
    }
    state_.gpr[0] = 0;
    return 1;
  }

  uint32_t instr_pc = state_.pc;
  uint32_t instr = memory_->read32(instr_pc);

  state_.pc = state_.next_pc;
  state_.next_pc = state_.pc + 4;

  PendingLoad new_load;
  bool branch_now = false;
  bool exception = false;
  bool in_delay = branch_pending_;

  uint32_t cycles = execute_instruction(instr, instr_pc, in_delay, new_load, branch_now, exception);

  branch_pending_ = branch_now;
  if (!exception) {
    load_delay_ = new_load;
  }
  state_.gpr[0] = 0;

  if (scheduler_) {
    scheduler_->advance(cycles);
  }
  return cycles;
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

uint32_t CpuCore::read_reg(uint32_t index) const {
  if (index == 0) {
    return 0;
  }
  return state_.gpr[index];
}

void CpuCore::write_reg(uint32_t index, uint32_t value) {
  if (index == 0) {
    return;
  }
  state_.gpr[index] = value;
}

void CpuCore::set_branch_target(uint32_t target) {
  state_.next_pc = target;
}

static uint32_t sign_extend16(uint16_t value) {
  return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
}

void CpuCore::raise_exception(uint32_t excode, uint32_t badaddr, bool in_delay, uint32_t instr_pc) {
  uint32_t ip_bits = state_.cop0.cause & 0x0000FF00;
  state_.cop0.cause = ip_bits | (excode << 2);
  if (in_delay) {
    state_.cop0.cause |= (1u << 31);
    state_.cop0.epc = instr_pc - 4;
  } else {
    state_.cop0.epc = instr_pc;
  }
  state_.cop0.badvaddr = badaddr;
  state_.cop0.sr |= (1u << 1);
  uint32_t vector = (state_.cop0.sr & (1u << 22)) ? 0xBFC00180 : 0x80000080;
  state_.pc = vector;
  state_.next_pc = state_.pc + 4;
  branch_pending_ = false;
}

bool CpuCore::check_interrupts() {
  bool ie = (state_.cop0.sr & 0x1) != 0;
  bool exl = (state_.cop0.sr & (1u << 1)) != 0;
  if (ie && !exl && memory_ && memory_->irq_pending()) {
    state_.cop0.cause |= (1u << 10);
    raise_exception(0, 0, false, state_.pc);
    return true;
  }
  return false;
}

uint32_t CpuCore::execute_instruction(uint32_t instr,
                                      uint32_t instr_pc,
                                      bool in_delay,
                                      PendingLoad &out_load,
                                      bool &out_branch,
                                      bool &out_exception) {
  out_load = {};
  out_branch = false;
  out_exception = false;

  uint32_t op = instr >> 26;
  uint32_t rs = (instr >> 21) & 0x1F;
  uint32_t rt = (instr >> 16) & 0x1F;
  uint32_t rd = (instr >> 11) & 0x1F;
  uint32_t sh = (instr >> 6) & 0x1F;
  uint32_t funct = instr & 0x3F;
  uint16_t imm = static_cast<uint16_t>(instr & 0xFFFF);
  uint32_t imm_se = sign_extend16(imm);

  switch (op) {
    case 0x00: { // SPECIAL
      switch (funct) {
        case 0x00: // SLL
          write_reg(rd, read_reg(rt) << sh);
          break;
        case 0x02: // SRL
          write_reg(rd, read_reg(rt) >> sh);
          break;
        case 0x03: // SRA
          write_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(read_reg(rt)) >> sh));
          break;
        case 0x04: // SLLV
          write_reg(rd, read_reg(rt) << (read_reg(rs) & 0x1F));
          break;
        case 0x06: // SRLV
          write_reg(rd, read_reg(rt) >> (read_reg(rs) & 0x1F));
          break;
        case 0x07: // SRAV
          write_reg(rd, static_cast<uint32_t>(static_cast<int32_t>(read_reg(rt)) >> (read_reg(rs) & 0x1F)));
          break;
        case 0x08: // JR
          set_branch_target(read_reg(rs));
          out_branch = true;
          break;
        case 0x09: // JALR
          write_reg(rd ? rd : 31, instr_pc + 8);
          set_branch_target(read_reg(rs));
          out_branch = true;
          break;
        case 0x0C: // SYSCALL
          raise_exception(8, 0, in_delay, instr_pc);
          out_exception = true;
          break;
        case 0x0D: // BREAK
          raise_exception(9, 0, in_delay, instr_pc);
          out_exception = true;
          break;
        case 0x10: // MFHI
          write_reg(rd, state_.hi);
          break;
        case 0x11: // MTHI
          state_.hi = read_reg(rs);
          break;
        case 0x12: // MFLO
          write_reg(rd, state_.lo);
          break;
        case 0x13: // MTLO
          state_.lo = read_reg(rs);
          break;
        case 0x18: { // MULT
          int64_t res = static_cast<int64_t>(static_cast<int32_t>(read_reg(rs))) *
                        static_cast<int64_t>(static_cast<int32_t>(read_reg(rt)));
          state_.lo = static_cast<uint32_t>(res & 0xFFFFFFFF);
          state_.hi = static_cast<uint32_t>((res >> 32) & 0xFFFFFFFF);
          break;
        }
        case 0x19: { // MULTU
          uint64_t res = static_cast<uint64_t>(read_reg(rs)) * static_cast<uint64_t>(read_reg(rt));
          state_.lo = static_cast<uint32_t>(res & 0xFFFFFFFF);
          state_.hi = static_cast<uint32_t>((res >> 32) & 0xFFFFFFFF);
          break;
        }
        case 0x1A: { // DIV
          int32_t a = static_cast<int32_t>(read_reg(rs));
          int32_t b = static_cast<int32_t>(read_reg(rt));
          if (b == 0) {
            state_.lo = (a >= 0) ? 0xFFFFFFFFu : 1u;
            state_.hi = static_cast<uint32_t>(a);
          } else if (a == static_cast<int32_t>(0x80000000) && b == -1) {
            state_.lo = static_cast<uint32_t>(a);
            state_.hi = 0;
          } else {
            state_.lo = static_cast<uint32_t>(a / b);
            state_.hi = static_cast<uint32_t>(a % b);
          }
          break;
        }
        case 0x1B: { // DIVU
          uint32_t a = read_reg(rs);
          uint32_t b = read_reg(rt);
          if (b == 0) {
            state_.lo = 0xFFFFFFFFu;
            state_.hi = a;
          } else {
            state_.lo = a / b;
            state_.hi = a % b;
          }
          break;
        }
        case 0x20: { // ADD
          int32_t a = static_cast<int32_t>(read_reg(rs));
          int32_t b = static_cast<int32_t>(read_reg(rt));
          int32_t res = a + b;
          if (((a ^ res) & (b ^ res)) < 0) {
            raise_exception(12, 0, in_delay, instr_pc);
            out_exception = true;
          } else {
            write_reg(rd, static_cast<uint32_t>(res));
          }
          break;
        }
        case 0x21: // ADDU
          write_reg(rd, read_reg(rs) + read_reg(rt));
          break;
        case 0x22: { // SUB
          int32_t a = static_cast<int32_t>(read_reg(rs));
          int32_t b = static_cast<int32_t>(read_reg(rt));
          int32_t res = a - b;
          if (((a ^ b) & (a ^ res)) < 0) {
            raise_exception(12, 0, in_delay, instr_pc);
            out_exception = true;
          } else {
            write_reg(rd, static_cast<uint32_t>(res));
          }
          break;
        }
        case 0x23: // SUBU
          write_reg(rd, read_reg(rs) - read_reg(rt));
          break;
        case 0x24: // AND
          write_reg(rd, read_reg(rs) & read_reg(rt));
          break;
        case 0x25: // OR
          write_reg(rd, read_reg(rs) | read_reg(rt));
          break;
        case 0x26: // XOR
          write_reg(rd, read_reg(rs) ^ read_reg(rt));
          break;
        case 0x27: // NOR
          write_reg(rd, ~(read_reg(rs) | read_reg(rt)));
          break;
        case 0x2A: { // SLT
          int32_t a = static_cast<int32_t>(read_reg(rs));
          int32_t b = static_cast<int32_t>(read_reg(rt));
          write_reg(rd, a < b ? 1 : 0);
          break;
        }
        case 0x2B: { // SLTU
          uint32_t a = read_reg(rs);
          uint32_t b = read_reg(rt);
          write_reg(rd, a < b ? 1 : 0);
          break;
        }
        default:
          raise_exception(10, 0, in_delay, instr_pc);
          out_exception = true;
          break;
      }
      break;
    }
    case 0x01: { // REGIMM
      int32_t s = static_cast<int32_t>(read_reg(rs));
      uint32_t target = instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2);
      if (rt == 0x00) { // BLTZ
        if (s < 0) {
          set_branch_target(target);
        }
        out_branch = true;
      } else if (rt == 0x01) { // BGEZ
        if (s >= 0) {
          set_branch_target(target);
        }
        out_branch = true;
      } else if (rt == 0x10) { // BLTZAL
        write_reg(31, instr_pc + 8);
        if (s < 0) {
          set_branch_target(target);
        }
        out_branch = true;
      } else if (rt == 0x11) { // BGEZAL
        write_reg(31, instr_pc + 8);
        if (s >= 0) {
          set_branch_target(target);
        }
        out_branch = true;
      } else {
        raise_exception(10, 0, in_delay, instr_pc);
        out_exception = true;
      }
      break;
    }
    case 0x02: { // J
      uint32_t target = (instr_pc & 0xF0000000) | ((instr & 0x03FFFFFF) << 2);
      set_branch_target(target);
      out_branch = true;
      break;
    }
    case 0x03: { // JAL
      write_reg(31, instr_pc + 8);
      uint32_t target = (instr_pc & 0xF0000000) | ((instr & 0x03FFFFFF) << 2);
      set_branch_target(target);
      out_branch = true;
      break;
    }
    case 0x04: { // BEQ
      if (read_reg(rs) == read_reg(rt)) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
      }
      out_branch = true;
      break;
    }
    case 0x05: { // BNE
      if (read_reg(rs) != read_reg(rt)) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
      }
      out_branch = true;
      break;
    }
    case 0x06: { // BLEZ
      if (static_cast<int32_t>(read_reg(rs)) <= 0) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
      }
      out_branch = true;
      break;
    }
    case 0x07: { // BGTZ
      if (static_cast<int32_t>(read_reg(rs)) > 0) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
      }
      out_branch = true;
      break;
    }
    case 0x08: { // ADDI
      int32_t a = static_cast<int32_t>(read_reg(rs));
      int32_t b = static_cast<int32_t>(imm_se);
      int32_t res = a + b;
      if (((a ^ res) & (b ^ res)) < 0) {
        raise_exception(12, 0, in_delay, instr_pc);
        out_exception = true;
      } else {
        write_reg(rt, static_cast<uint32_t>(res));
      }
      break;
    }
    case 0x09: { // ADDIU
      write_reg(rt, read_reg(rs) + imm_se);
      break;
    }
    case 0x0A: { // SLTI
      int32_t a = static_cast<int32_t>(read_reg(rs));
      int32_t b = static_cast<int32_t>(imm_se);
      write_reg(rt, a < b ? 1 : 0);
      break;
    }
    case 0x0B: { // SLTIU
      uint32_t a = read_reg(rs);
      uint32_t b = imm_se;
      write_reg(rt, a < b ? 1 : 0);
      break;
    }
    case 0x0C: // ANDI
      write_reg(rt, read_reg(rs) & imm);
      break;
    case 0x0D: // ORI
      write_reg(rt, read_reg(rs) | imm);
      break;
    case 0x0E: // XORI
      write_reg(rt, read_reg(rs) ^ imm);
      break;
    case 0x0F: // LUI
      write_reg(rt, static_cast<uint32_t>(imm) << 16);
      break;
    case 0x12: { // COP2 (GTE)
      uint32_t cop_op = (instr >> 21) & 0x1F;
      if (cop_op == 0x00) { // MFC2
        out_load = {true, rt, 0};
      } else if (cop_op == 0x04) { // MTC2
        // ignore for now
      } else {
        // GTE command, not implemented yet.
      }
      break;
    }
    case 0x10: { // COP0
      uint32_t cop_op = (instr >> 21) & 0x1F;
      if (instr == 0x42000010) { // RFE
        uint32_t mode = state_.cop0.sr & 0x3F;
        state_.cop0.sr = (state_.cop0.sr & ~0x3F) | ((mode >> 2) & 0x0F);
        break;
      }
      if (cop_op == 0x00) { // MFC0
        uint32_t value = 0;
        switch (rd) {
          case 8:
            value = state_.cop0.badvaddr;
            break;
          case 12:
            value = state_.cop0.sr;
            break;
          case 13:
            value = state_.cop0.cause;
            break;
          case 14:
            value = state_.cop0.epc;
            break;
          case 15:
            value = state_.cop0.prid;
            break;
          default:
            value = 0;
            break;
        }
        out_load = {true, rt, value};
      } else if (cop_op == 0x04) { // MTC0
        uint32_t value = read_reg(rt);
        switch (rd) {
          case 8:
            state_.cop0.badvaddr = value;
            break;
          case 12:
            state_.cop0.sr = value;
            break;
          case 13:
            state_.cop0.cause = value;
            break;
          case 14:
            state_.cop0.epc = value;
            break;
          default:
            break;
        }
      } else {
        raise_exception(10, 0, in_delay, instr_pc);
        out_exception = true;
      }
      break;
    }
    case 0x32: { // LWC2
      uint32_t addr = read_reg(rs) + imm_se;
      (void)memory_->read32(addr);
      break;
    }
    case 0x3A: { // SWC2
      uint32_t addr = read_reg(rs) + imm_se;
      (void)addr;
      break;
    }
    case 0x20: { // LB
      uint32_t addr = read_reg(rs) + imm_se;
      int8_t val = static_cast<int8_t>(memory_->read8(addr));
      out_load = {true, rt, static_cast<uint32_t>(static_cast<int32_t>(val))};
      break;
    }
    case 0x21: { // LH
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 1) {
        raise_exception(4, addr, in_delay, instr_pc);
        out_exception = true;
        break;
      }
      int16_t val = static_cast<int16_t>(memory_->read16(addr));
      out_load = {true, rt, static_cast<uint32_t>(static_cast<int32_t>(val))};
      break;
    }
    case 0x22: { // LWL
      uint32_t addr = read_reg(rs) + imm_se;
      uint32_t aligned = addr & ~3u;
      uint32_t word = memory_->read32(aligned);
      uint32_t reg = read_reg(rt);
      switch (addr & 3u) {
        case 0:
          reg = (reg & 0x00FFFFFFu) | (word << 24);
          break;
        case 1:
          reg = (reg & 0x0000FFFFu) | (word << 16);
          break;
        case 2:
          reg = (reg & 0x000000FFu) | (word << 8);
          break;
        case 3:
          reg = word;
          break;
      }
      out_load = {true, rt, reg};
      break;
    }
    case 0x23: { // LW
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(4, addr, in_delay, instr_pc);
        out_exception = true;
        break;
      }
      out_load = {true, rt, memory_->read32(addr)};
      break;
    }
    case 0x24: { // LBU
      uint32_t addr = read_reg(rs) + imm_se;
      out_load = {true, rt, memory_->read8(addr)};
      break;
    }
    case 0x25: { // LHU
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 1) {
        raise_exception(4, addr, in_delay, instr_pc);
        out_exception = true;
        break;
      }
      out_load = {true, rt, memory_->read16(addr)};
      break;
    }
    case 0x26: { // LWR
      uint32_t addr = read_reg(rs) + imm_se;
      uint32_t aligned = addr & ~3u;
      uint32_t word = memory_->read32(aligned);
      uint32_t reg = read_reg(rt);
      switch (addr & 3u) {
        case 0:
          reg = word;
          break;
        case 1:
          reg = (reg & 0xFF000000u) | (word >> 8);
          break;
        case 2:
          reg = (reg & 0xFFFF0000u) | (word >> 16);
          break;
        case 3:
          reg = (reg & 0xFFFFFF00u) | (word >> 24);
          break;
      }
      out_load = {true, rt, reg};
      break;
    }
    case 0x28: { // SB
      uint32_t addr = read_reg(rs) + imm_se;
      memory_->write8(addr, static_cast<uint8_t>(read_reg(rt) & 0xFF));
      break;
    }
    case 0x29: { // SH
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 1) {
        raise_exception(5, addr, in_delay, instr_pc);
        out_exception = true;
        break;
      }
      memory_->write16(addr, static_cast<uint16_t>(read_reg(rt) & 0xFFFF));
      break;
    }
    case 0x2A: { // SWL
      uint32_t addr = read_reg(rs) + imm_se;
      uint32_t aligned = addr & ~3u;
      uint32_t word = memory_->read32(aligned);
      uint32_t reg = read_reg(rt);
      switch (addr & 3u) {
        case 0:
          word = (word & 0xFFFFFF00u) | (reg >> 24);
          break;
        case 1:
          word = (word & 0xFFFF0000u) | (reg >> 16);
          break;
        case 2:
          word = (word & 0xFF000000u) | (reg >> 8);
          break;
        case 3:
          word = reg;
          break;
      }
      memory_->write32(aligned, word);
      break;
    }
    case 0x2B: { // SW
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(5, addr, in_delay, instr_pc);
        out_exception = true;
        break;
      }
      memory_->write32(addr, read_reg(rt));
      break;
    }
    case 0x2E: { // SWR
      uint32_t addr = read_reg(rs) + imm_se;
      uint32_t aligned = addr & ~3u;
      uint32_t word = memory_->read32(aligned);
      uint32_t reg = read_reg(rt);
      switch (addr & 3u) {
        case 0:
          word = reg;
          break;
        case 1:
          word = (word & 0x000000FFu) | (reg << 8);
          break;
        case 2:
          word = (word & 0x0000FFFFu) | (reg << 16);
          break;
        case 3:
          word = (word & 0x00FFFFFFu) | (reg << 24);
          break;
      }
      memory_->write32(aligned, word);
      break;
    }
    default:
      raise_exception(10, 0, in_delay, instr_pc);
      out_exception = true;
      break;
  }

  return 1;
}

} // namespace ps1emu
