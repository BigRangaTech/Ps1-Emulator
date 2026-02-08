#include "core/mmio.h"

#include <cstring>

namespace ps1emu {

void MmioBus::reset() {
  std::memset(raw_.data(), 0, raw_.size());
  gpu_gp0_ = 0;
  gpu_gp1_ = 0x14802000;
  gpu_gp0_fifo_.clear();
  irq_stat_ = 0;
  irq_mask_ = 0;
  std::memset(dma_madr_, 0, sizeof(dma_madr_));
  std::memset(dma_bcr_, 0, sizeof(dma_bcr_));
  std::memset(dma_chcr_, 0, sizeof(dma_chcr_));
  dma_dpcr_ = 0;
  dma_dicr_ = 0;
  std::memset(timer_count_, 0, sizeof(timer_count_));
  std::memset(timer_mode_, 0, sizeof(timer_mode_));
  std::memset(timer_target_, 0, sizeof(timer_target_));
  std::memset(spu_regs_.data(), 0, spu_regs_.size() * sizeof(uint16_t));
  std::memset(cdrom_regs_.data(), 0, cdrom_regs_.size());
  std::memset(timer_irq_enable_, 0, sizeof(timer_irq_enable_));
  std::memset(timer_irq_repeat_, 0, sizeof(timer_irq_repeat_));
  std::memset(timer_irq_on_overflow_, 0, sizeof(timer_irq_on_overflow_));
  std::memset(timer_irq_on_target_, 0, sizeof(timer_irq_on_target_));
  dma_active_channel_ = 0xFFFFFFFFu;
}

static uint32_t recompute_dma_master(uint32_t dicr) {
  bool master = (dicr & (1u << 23)) != 0;
  uint32_t enables = (dicr >> 16) & 0x7F;
  uint32_t flags = (dicr >> 24) & 0x7F;
  bool irq = master && ((enables & flags) != 0);
  if (irq) {
    dicr |= (1u << 31);
  } else {
    dicr &= ~(1u << 31);
  }
  return dicr;
}

uint32_t MmioBus::offset(uint32_t addr) const {
  return addr - kBase;
}

uint8_t MmioBus::read8(uint32_t addr) {
  uint32_t off = offset(addr);
  if (off < kSize) {
    return raw_[off];
  }
  return 0xFF;
}

uint16_t MmioBus::read16(uint32_t addr) {
  uint32_t off = offset(addr);
  if (off + 1 < kSize) {
    return static_cast<uint16_t>(raw_[off] | (raw_[off + 1] << 8));
  }
  return 0xFFFF;
}

uint32_t MmioBus::read32(uint32_t addr) {
  uint32_t off = offset(addr);
  if (off + 3 >= kSize) {
    return 0xFFFFFFFF;
  }

  if (addr == 0x1F801070) { // I_STAT
    return irq_stat_;
  }
  if (addr == 0x1F801074) { // I_MASK
    return irq_mask_;
  }
  if (addr == 0x1F801810) { // GPU GP0
    return gpu_gp0_;
  }
  if (addr == 0x1F801814) { // GPU GP1
    return gpu_gp1_;
  }

  if (addr >= 0x1F801080 && addr < 0x1F8010F0) { // DMA channels
    uint32_t index = (addr - 0x1F801080) / 0x10;
    uint32_t reg = (addr - 0x1F801080) % 0x10;
    if (index < 7) {
      if (reg == 0x0) {
        return dma_madr_[index];
      }
      if (reg == 0x4) {
        return dma_bcr_[index];
      }
      if (reg == 0x8) {
        return dma_chcr_[index];
      }
    }
  }
  if (addr == 0x1F8010F0) { // DPCR
    return dma_dpcr_;
  }
  if (addr == 0x1F8010F4) { // DICR
    return dma_dicr_;
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) { // Timers
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        return timer_count_[timer];
      }
      if (reg == 0x4) {
        return timer_mode_[timer];
      }
      if (reg == 0x8) {
        return timer_target_[timer];
      }
    }
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) { // SPU
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      return spu_regs_[index];
    }
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) { // CD-ROM
    uint32_t index = addr - 0x1F801800;
    return cdrom_regs_[index];
  }

  return static_cast<uint32_t>(raw_[off]) |
         (static_cast<uint32_t>(raw_[off + 1]) << 8) |
         (static_cast<uint32_t>(raw_[off + 2]) << 16) |
         (static_cast<uint32_t>(raw_[off + 3]) << 24);
}

void MmioBus::write8(uint32_t addr, uint8_t value) {
  uint32_t off = offset(addr);
  if (off < kSize) {
    raw_[off] = value;
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    cdrom_regs_[addr - 0x1F801800] = value;
  }
}

void MmioBus::write16(uint32_t addr, uint16_t value) {
  uint32_t off = offset(addr);
  if (off + 1 < kSize) {
    raw_[off] = static_cast<uint8_t>(value & 0xFF);
    raw_[off + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) {
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      spu_regs_[index] = value;
    }
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        timer_count_[timer] = value;
      } else if (reg == 0x4) {
        timer_mode_[timer] = value;
        timer_irq_enable_[timer] = (value & (1u << 4)) != 0;
        timer_irq_repeat_[timer] = (value & (1u << 5)) != 0;
        timer_irq_on_overflow_[timer] = (value & (1u << 6)) != 0;
        timer_irq_on_target_[timer] = (value & (1u << 7)) != 0;
        if (value & (1u << 3)) {
          timer_count_[timer] = 0;
        }
        if (value & (1u << 8)) {
          irq_stat_ &= static_cast<uint16_t>(~(1u << (4 + timer)));
        }
      } else if (reg == 0x8) {
        timer_target_[timer] = value;
      }
    }
  }

  if (addr == 0x1F801070) { // I_STAT
    irq_stat_ &= static_cast<uint16_t>(~value);
  } else if (addr == 0x1F801074) { // I_MASK
    irq_mask_ = value;
  }
}

void MmioBus::write32(uint32_t addr, uint32_t value) {
  uint32_t off = offset(addr);
  if (off + 3 >= kSize) {
    return;
  }

  if (addr == 0x1F801070) { // I_STAT
    irq_stat_ &= static_cast<uint16_t>(~value);
  } else if (addr == 0x1F801074) { // I_MASK
    irq_mask_ = static_cast<uint16_t>(value);
  }

  if (addr == 0x1F801810) { // GPU GP0
    gpu_gp0_ = value;
    gpu_gp0_fifo_.push_back(value);
  } else if (addr == 0x1F801814) { // GPU GP1
    gpu_gp1_ = value;
  }

  if (addr >= 0x1F801080 && addr < 0x1F8010F0) {
    uint32_t index = (addr - 0x1F801080) / 0x10;
    uint32_t reg = (addr - 0x1F801080) % 0x10;
    if (index < 7) {
      if (reg == 0x0) {
        dma_madr_[index] = value;
      } else if (reg == 0x4) {
        dma_bcr_[index] = value;
      } else if (reg == 0x8) {
        dma_chcr_[index] = value;
        if (value & (1u << 24)) {
          dma_active_channel_ = index;
        }
      }
    }
  } else if (addr == 0x1F8010F0) {
    dma_dpcr_ = value;
  } else if (addr == 0x1F8010F4) {
    uint32_t clear = (value >> 24) & 0x7F;
    dma_dicr_ &= ~(clear << 24);
    dma_dicr_ = (dma_dicr_ & 0xFF000000u) | (value & 0x00FFFFFFu);
    dma_dicr_ = recompute_dma_master(dma_dicr_);
  }

  if (addr >= 0x1F801100 && addr < 0x1F801130) {
    uint32_t timer = (addr - 0x1F801100) / 0x10;
    uint32_t reg = (addr - 0x1F801100) % 0x10;
    if (timer < 3) {
      if (reg == 0x0) {
        timer_count_[timer] = static_cast<uint16_t>(value);
      } else if (reg == 0x4) {
        timer_mode_[timer] = static_cast<uint16_t>(value);
      } else if (reg == 0x8) {
        timer_target_[timer] = static_cast<uint16_t>(value);
      }
    }
  }

  if (addr >= 0x1F801C00 && addr < 0x1F801E00) {
    uint32_t index = (addr - 0x1F801C00) / 2;
    if (index < spu_regs_.size()) {
      spu_regs_[index] = static_cast<uint16_t>(value);
    }
  }

  if (addr >= 0x1F801800 && addr < 0x1F801804) {
    cdrom_regs_[addr - 0x1F801800] = static_cast<uint8_t>(value & 0xFF);
  }

  raw_[off] = static_cast<uint8_t>(value & 0xFF);
  raw_[off + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  raw_[off + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  raw_[off + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool MmioBus::irq_pending() const {
  return (irq_stat_ & irq_mask_) != 0;
}

uint16_t MmioBus::irq_stat() const {
  return irq_stat_;
}

uint16_t MmioBus::irq_mask() const {
  return irq_mask_;
}

void MmioBus::tick(uint32_t cycles) {
  for (int i = 0; i < 3; ++i) {
    uint32_t before = timer_count_[i];
    uint16_t mode = timer_mode_[i];
    if (mode & (1u << 2)) { // halted
      continue;
    }
    uint32_t after = (before + cycles) & 0xFFFFu;
    timer_count_[i] = static_cast<uint16_t>(after);
    uint16_t target = timer_target_[i];
    if (target != 0 && before < target && after >= target) {
      if (timer_irq_enable_[i] && timer_irq_on_target_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
      }
      if (mode & (1u << 3)) { // reset on target
        timer_count_[i] = 0;
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
    if (before > after && timer_irq_on_overflow_[i]) {
      if (timer_irq_enable_[i]) {
        irq_stat_ |= static_cast<uint16_t>(1u << (4 + i));
      }
      if (!timer_irq_repeat_[i]) {
        timer_irq_enable_[i] = false;
      }
    }
  }
}

uint32_t MmioBus::consume_dma_channel() {
  uint32_t channel = dma_active_channel_;
  dma_active_channel_ = 0xFFFFFFFFu;
  if (channel < 7) {
    bool master = (dma_dicr_ & (1u << 23)) != 0;
    bool enable = (dma_dicr_ & (1u << (16 + channel))) != 0;
    if (master && enable) {
      dma_dicr_ |= (1u << (24 + channel));
      dma_dicr_ = recompute_dma_master(dma_dicr_);
      irq_stat_ |= (1u << 3); // DMA IRQ
    }
    dma_chcr_[channel] &= ~(1u << 24);
    return channel;
  }
  return 0xFFFFFFFFu;
}

uint32_t MmioBus::dma_madr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_madr_[channel];
}

uint32_t MmioBus::dma_bcr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_bcr_[channel];
}

uint32_t MmioBus::dma_chcr(uint32_t channel) const {
  if (channel >= 7) {
    return 0;
  }
  return dma_chcr_[channel];
}

void MmioBus::set_dma_madr(uint32_t channel, uint32_t value) {
  if (channel >= 7) {
    return;
  }
  dma_madr_[channel] = value;
}

bool MmioBus::has_gpu_commands() const {
  return !gpu_gp0_fifo_.empty();
}

std::vector<uint32_t> MmioBus::take_gpu_commands() {
  std::vector<uint32_t> out;
  out.swap(gpu_gp0_fifo_);
  return out;
}

void MmioBus::restore_gpu_commands(std::vector<uint32_t> remainder) {
  gpu_gp0_fifo_ = std::move(remainder);
}

} // namespace ps1emu
