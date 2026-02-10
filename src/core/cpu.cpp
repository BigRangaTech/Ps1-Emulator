#include "core/cpu.h"

#include <cstdint>

namespace {
constexpr uint32_t kCop0StatusBev = 1u << 22;
constexpr uint32_t kCop0StatusIsc = 1u << 16;
} // namespace

namespace ps1emu {

CpuCore::CpuCore(MemoryMap &memory, Scheduler &scheduler)
    : memory_(&memory),
      scheduler_(&scheduler),
      dynarec_cache_(4096),
      dynarec_backend_(std::make_unique<NullDynarecBackend>()) {}

void CpuCore::reset() {
  // TODO: reset registers and pipeline state.
  state_ = {};
  state_.cop0.sr = kCop0StatusBev;
  state_.pc = 0xBFC00000;
  state_.next_pc = state_.pc + 4;
  gte_.reset();
  gte_pending_writes_.clear();
  load_delay_ = {};
  load_delay_shadow_valid_ = false;
  load_delay_shadow_reg_ = 0;
  load_delay_shadow_value_ = 0;
  branch_pending_ = false;
  skip_next_ = false;
  exception_pending_ = false;
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

bool CpuCore::consume_exception(CpuExceptionInfo &out) {
  if (!exception_pending_) {
    return false;
  }
  out = last_exception_;
  exception_pending_ = false;
  return true;
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
  load_delay_shadow_valid_ = false;
  if (load_delay_.valid) {
    if (load_delay_.reg != 0) {
      load_delay_shadow_valid_ = true;
      load_delay_shadow_reg_ = load_delay_.reg;
      load_delay_shadow_value_ = state_.gpr[load_delay_.reg];
      write_reg(load_delay_.reg, load_delay_.value);
    }
    load_delay_ = {};
  }

  if (check_interrupts()) {
    load_delay_shadow_valid_ = false;
    skip_next_ = false;
    branch_pending_ = false;
    if (scheduler_) {
      scheduler_->advance(1);
    }
    state_.gpr[0] = 0;
    return 1;
  }

  if (skip_next_) {
    skip_next_ = false;
    state_.pc = state_.next_pc;
    state_.next_pc = state_.pc + 4;
    branch_pending_ = false;
    state_.gpr[0] = 0;
    flush_gte_writes(1);
    load_delay_shadow_valid_ = false;
    if (scheduler_) {
      scheduler_->advance(1);
    }
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

  branch_pending_ = branch_now && !exception;
  if (!exception && new_load.valid) {
    load_delay_ = new_load;
  } else if (exception) {
    load_delay_ = {};
  }
  state_.gpr[0] = 0;

  flush_gte_writes(cycles);
  load_delay_shadow_valid_ = false;
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
  if (load_delay_shadow_valid_ && index == load_delay_shadow_reg_) {
    return load_delay_shadow_value_;
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

void CpuCore::raise_exception(uint32_t excode,
                              uint32_t badaddr,
                              bool in_delay,
                              uint32_t instr_pc,
                              uint32_t epc_pc) {
  uint32_t ip_bits = state_.cop0.cause & 0x0000FF00;
  state_.cop0.cause = ip_bits | (excode << 2);
  if (in_delay) {
    state_.cop0.cause |= (1u << 31);
    state_.cop0.epc = epc_pc;
  } else {
    state_.cop0.epc = instr_pc;
  }
  state_.cop0.badvaddr = badaddr;
  uint32_t mode = state_.cop0.sr & 0x3Fu;
  state_.cop0.sr = (state_.cop0.sr & ~0x3Fu) | ((mode << 2) & 0x3Fu);
  state_.cop0.sr |= (1u << 1);
  state_.cop0.sr &= ~(1u << 0);
  if (in_delay) {
    state_.cop0.cause |= (1u << 31);
  } else {
    state_.cop0.cause &= ~(1u << 31);
  }
  exception_pending_ = true;
  last_exception_.code = excode;
  last_exception_.pc = instr_pc;
  last_exception_.badvaddr = badaddr;
  last_exception_.in_delay = in_delay;
  last_exception_.cause = state_.cop0.cause;
  uint32_t base = (state_.cop0.sr & (1u << 22)) ? 0xBFC00000 : state_.cop0.ebase;
  state_.pc = base + 0x80;
  state_.next_pc = state_.pc + 4;
  branch_pending_ = false;
}

bool CpuCore::check_interrupts() {
  bool ie = (state_.cop0.sr & 0x1) != 0;
  bool exl = (state_.cop0.sr & (1u << 1)) != 0;
  if (memory_) {
    uint32_t bits = (static_cast<uint32_t>(memory_->irq_stat()) &
                     static_cast<uint32_t>(memory_->irq_mask())) &
                    0x3F;
    state_.cop0.cause &= ~(0x3F << 10);
    state_.cop0.cause |= (bits << 10);
  }
  if (ie && !exl && memory_ && memory_->irq_pending()) {
    raise_exception(0, 0, false, state_.pc, state_.pc);
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
  uint32_t cycles = 1;

  uint32_t op = instr >> 26;
  uint32_t rs = (instr >> 21) & 0x1F;
  uint32_t rt = (instr >> 16) & 0x1F;
  uint32_t rd = (instr >> 11) & 0x1F;
  uint32_t sh = (instr >> 6) & 0x1F;
  uint32_t funct = instr & 0x3F;
  uint16_t imm = static_cast<uint16_t>(instr & 0xFFFF);
  uint32_t imm_se = sign_extend16(imm);
  const bool cache_isolated = (state_.cop0.sr & kCop0StatusIsc) != 0;

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
          raise_exception(8, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
          out_exception = true;
          break;
        case 0x0D: // BREAK
          raise_exception(9, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
            raise_exception(12, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
            raise_exception(12, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
          raise_exception(10, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
      } else if (rt == 0x02) { // BLTZL
        if (s < 0) {
          set_branch_target(target);
          out_branch = true;
        } else {
          skip_next_ = true;
        }
      } else if (rt == 0x03) { // BGEZL
        if (s >= 0) {
          set_branch_target(target);
          out_branch = true;
        } else {
          skip_next_ = true;
        }
      } else if (rt == 0x12) { // BLTZALL
        write_reg(31, instr_pc + 8);
        if (s < 0) {
          set_branch_target(target);
          out_branch = true;
        } else {
          skip_next_ = true;
        }
      } else if (rt == 0x13) { // BGEZALL
        write_reg(31, instr_pc + 8);
        if (s >= 0) {
          set_branch_target(target);
          out_branch = true;
        } else {
          skip_next_ = true;
        }
      } else {
        raise_exception(10, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
    case 0x14: { // BEQL
      if (read_reg(rs) == read_reg(rt)) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
        out_branch = true;
      } else {
        skip_next_ = true;
      }
      break;
    }
    case 0x15: { // BNEL
      if (read_reg(rs) != read_reg(rt)) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
        out_branch = true;
      } else {
        skip_next_ = true;
      }
      break;
    }
    case 0x16: { // BLEZL
      if (static_cast<int32_t>(read_reg(rs)) <= 0) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
        out_branch = true;
      } else {
        skip_next_ = true;
      }
      break;
    }
    case 0x17: { // BGTZL
      if (static_cast<int32_t>(read_reg(rs)) > 0) {
        set_branch_target(instr_pc + 4 + (static_cast<int32_t>(imm_se) << 2));
        out_branch = true;
      } else {
        skip_next_ = true;
      }
      break;
    }
    case 0x08: { // ADDI
      int32_t a = static_cast<int32_t>(read_reg(rs));
      int32_t b = static_cast<int32_t>(imm_se);
      int32_t res = a + b;
      if (((a ^ res) & (b ^ res)) < 0) {
        raise_exception(12, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
        out_load = {true, rt, gte_.read_data(rd)};
      } else if (cop_op == 0x04) { // MTC2
        enqueue_gte_write(rd, read_reg(rt), 1, false);
      } else if (cop_op == 0x02) { // CFC2
        out_load = {true, rt, gte_.read_ctrl(rd + 32)};
      } else if (cop_op == 0x06) { // CTC2
        enqueue_gte_write(rd + 32, read_reg(rt), 1, true);
      } else {
        gte_.execute(instr);
        cycles = gte_.command_cycles(instr);
      }
      break;
    }
    case 0x13: { // COP3
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x10: { // COP0
      uint32_t cop_op = (instr >> 21) & 0x1F;
      if (instr == 0x42000010) { // RFE
        uint32_t mode = state_.cop0.sr & 0x3F;
        state_.cop0.sr = (state_.cop0.sr & ~0x3F) | ((mode >> 2) & 0x0F);
        break;
      }
      if (cop_op == 0x02) { // CFC0
        out_load = {true, rt, 0};
        break;
      }
      if (cop_op == 0x06) { // CTC0
        break;
      }
      if (cop_op == 0x00) { // MFC0
        uint32_t value = 0;
        switch (rd) {
          case 8:
            value = state_.cop0.badvaddr;
            break;
          case 9:
            value = 0;
            break;
          case 11:
            value = 0;
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
          case 16:
            value = state_.cop0.ebase;
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
          case 9:
            break;
          case 11:
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
          case 16:
            state_.cop0.ebase = (value & 0xFFFFF000u);
            break;
          default:
            break;
        }
      } else {
        raise_exception(10, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
      }
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
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
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
      if (cache_isolated) {
        break;
      }
      memory_->write8(addr, static_cast<uint8_t>(read_reg(rt) & 0xFF));
      break;
    }
    case 0x29: { // SH
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 1) {
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      if (cache_isolated) {
        break;
      }
      memory_->write16(addr, static_cast<uint16_t>(read_reg(rt) & 0xFFFF));
      break;
    }
    case 0x2A: { // SWL
      uint32_t addr = read_reg(rs) + imm_se;
      if (cache_isolated) {
        break;
      }
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
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      if (cache_isolated) {
        break;
      }
      memory_->write32(addr, read_reg(rt));
      break;
    }
    case 0x2E: { // SWR
      uint32_t addr = read_reg(rs) + imm_se;
      if (cache_isolated) {
        break;
      }
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
    case 0x30: { // LWC0
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)memory_->read32(addr);
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x31: { // LWC1
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)memory_->read32(addr);
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x32: { // LWC2
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      uint32_t value = memory_->read32(addr);
      enqueue_gte_write(rt, value, 3, false);
      break;
    }
    case 0x33: { // LWC3
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(4, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)memory_->read32(addr);
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x38: { // SWC0
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)addr;
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x39: { // SWC1
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)addr;
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    case 0x3A: { // SWC2
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      if (cache_isolated) {
        break;
      }
      uint32_t value = gte_.read_data(rt);
      memory_->write32(addr, value);
      break;
    }
    case 0x3B: { // SWC3
      uint32_t addr = read_reg(rs) + imm_se;
      if (addr & 3) {
        raise_exception(5, addr, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
        out_exception = true;
        break;
      }
      (void)addr;
      raise_exception(11, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
    }
    default:
      raise_exception(10, 0, in_delay, instr_pc, in_delay ? (instr_pc - 4) : instr_pc);
      out_exception = true;
      break;
  }

  return cycles;
}

void CpuCore::enqueue_gte_write(uint32_t reg, uint32_t value, uint32_t delay, bool is_ctrl) {
  gte_pending_writes_.push_back({reg, value, delay, is_ctrl});
}

void CpuCore::flush_gte_writes(uint32_t cycles) {
  if (gte_pending_writes_.empty()) {
    return;
  }
  size_t out = 0;
  for (size_t i = 0; i < gte_pending_writes_.size(); ++i) {
    auto &pending = gte_pending_writes_[i];
    if (pending.delay <= cycles) {
      if (pending.is_ctrl) {
        gte_.write_ctrl(pending.reg, pending.value);
      } else {
        gte_.write_data(pending.reg, pending.value);
      }
      continue;
    }
    pending.delay -= cycles;
    gte_pending_writes_[out++] = pending;
  }
  gte_pending_writes_.resize(out);
}

} // namespace ps1emu
