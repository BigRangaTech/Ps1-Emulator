#include "core/memory_map.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace ps1emu {

namespace {
constexpr uint32_t kRamMirrorLimit = 0x1F000000;
}

void MemoryMap::reset() {
  std::memset(ram_.data(), 0, ram_.size());
  std::memset(scratchpad_.data(), 0, scratchpad_.size());
}

void MemoryMap::load_bios(const BiosImage &bios) {
  bios_ = bios.valid() ? &bios : nullptr;
}

void MemoryMap::attach_mmio(MmioBus &mmio) {
  mmio_ = &mmio;
}

bool MemoryMap::irq_pending() const {
  if (!mmio_) {
    return false;
  }
  return mmio_->irq_pending();
}

uint16_t MemoryMap::irq_stat() const {
  if (!mmio_) {
    return 0;
  }
  return mmio_->irq_stat();
}

uint16_t MemoryMap::irq_mask() const {
  if (!mmio_) {
    return 0;
  }
  return mmio_->irq_mask();
}

uint32_t MemoryMap::mask_address(uint32_t addr) const {
  return addr & 0x1FFFFFFF;
}

uint8_t MemoryMap::read8(uint32_t addr) const {
  uint32_t phys = mask_address(addr);
  if (phys < kRamMirrorLimit) {
    return ram_[phys & (kRamSize - 1)];
  }
  if (phys >= 0x1F800000 && phys < 0x1F800000 + kScratchpadSize) {
    return scratchpad_[phys - 0x1F800000];
  }
  if (phys >= 0x1FC00000 && phys < 0x1FC00000 + BiosImage::kExpectedSize && bios_) {
    return bios_->read8(phys - 0x1FC00000);
  }
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    return mmio_->read8(phys);
  }
  return 0xFF;
}

uint16_t MemoryMap::read16(uint32_t addr) const {
  uint32_t phys = mask_address(addr);
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    return mmio_->read16(phys);
  }
  uint16_t lo = read8(addr);
  uint16_t hi = read8(addr + 1);
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t MemoryMap::read32(uint32_t addr) const {
  uint32_t phys = mask_address(addr);
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    return mmio_->read32(phys);
  }
  uint32_t b0 = read8(addr);
  uint32_t b1 = read8(addr + 1);
  uint32_t b2 = read8(addr + 2);
  uint32_t b3 = read8(addr + 3);
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

void MemoryMap::write8(uint32_t addr, uint8_t value) {
  uint32_t phys = mask_address(addr);
  static bool watch_init = false;
  static bool watch_enabled = false;
  static uint32_t watch_start = 0;
  static uint32_t watch_end = 0;
  if (!watch_init) {
    watch_init = true;
    const char *env = std::getenv("PS1EMU_WATCH_PHYS");
    if (env && *env) {
      std::string spec(env);
      size_t colon = spec.find(':');
      try {
        if (colon == std::string::npos) {
          uint32_t addr_val = static_cast<uint32_t>(std::stoul(spec, nullptr, 0));
          watch_start = addr_val;
          watch_end = addr_val;
        } else {
          watch_start = static_cast<uint32_t>(std::stoul(spec.substr(0, colon), nullptr, 0));
          watch_end = static_cast<uint32_t>(std::stoul(spec.substr(colon + 1), nullptr, 0));
        }
        watch_enabled = true;
      } catch (...) {
        watch_enabled = false;
      }
    }
  }
  if (watch_enabled && phys >= watch_start && phys <= watch_end) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << "[watch-phys] SB vaddr=0x" << std::setw(8) << addr;
    oss << " paddr=0x" << std::setw(8) << phys;
    oss << " value=0x" << std::setw(2) << static_cast<uint32_t>(value) << "\n";
    std::cerr << oss.str();
  }
  if (phys < kRamMirrorLimit) {
    ram_[phys & (kRamSize - 1)] = value;
    return;
  }
  if (phys >= 0x1F800000 && phys < 0x1F800000 + kScratchpadSize) {
    scratchpad_[phys - 0x1F800000] = value;
    return;
  }
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    mmio_->write8(phys, value);
  }
}

void MemoryMap::write16(uint32_t addr, uint16_t value) {
  uint32_t phys = mask_address(addr);
  static bool watch_init = false;
  static bool watch_enabled = false;
  static uint32_t watch_start = 0;
  static uint32_t watch_end = 0;
  if (!watch_init) {
    watch_init = true;
    const char *env = std::getenv("PS1EMU_WATCH_PHYS");
    if (env && *env) {
      std::string spec(env);
      size_t colon = spec.find(':');
      try {
        if (colon == std::string::npos) {
          uint32_t addr_val = static_cast<uint32_t>(std::stoul(spec, nullptr, 0));
          watch_start = addr_val;
          watch_end = addr_val;
        } else {
          watch_start = static_cast<uint32_t>(std::stoul(spec.substr(0, colon), nullptr, 0));
          watch_end = static_cast<uint32_t>(std::stoul(spec.substr(colon + 1), nullptr, 0));
        }
        watch_enabled = true;
      } catch (...) {
        watch_enabled = false;
      }
    }
  }
  if (watch_enabled && phys >= watch_start && phys <= watch_end) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << "[watch-phys] SH vaddr=0x" << std::setw(8) << addr;
    oss << " paddr=0x" << std::setw(8) << phys;
    oss << " value=0x" << std::setw(4) << static_cast<uint32_t>(value) << "\n";
    std::cerr << oss.str();
  }
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    mmio_->write16(phys, value);
    return;
  }
  write8(addr, static_cast<uint8_t>(value & 0xFF));
  write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
}

void MemoryMap::write32(uint32_t addr, uint32_t value) {
  uint32_t phys = mask_address(addr);
  static bool watch_init = false;
  static bool watch_enabled = false;
  static uint32_t watch_start = 0;
  static uint32_t watch_end = 0;
  if (!watch_init) {
    watch_init = true;
    const char *env = std::getenv("PS1EMU_WATCH_PHYS");
    if (env && *env) {
      std::string spec(env);
      size_t colon = spec.find(':');
      try {
        if (colon == std::string::npos) {
          uint32_t addr_val = static_cast<uint32_t>(std::stoul(spec, nullptr, 0));
          watch_start = addr_val;
          watch_end = addr_val;
        } else {
          watch_start = static_cast<uint32_t>(std::stoul(spec.substr(0, colon), nullptr, 0));
          watch_end = static_cast<uint32_t>(std::stoul(spec.substr(colon + 1), nullptr, 0));
        }
        watch_enabled = true;
      } catch (...) {
        watch_enabled = false;
      }
    }
  }
  if (watch_enabled && phys >= watch_start && phys <= watch_end) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << "[watch-phys] SW vaddr=0x" << std::setw(8) << addr;
    oss << " paddr=0x" << std::setw(8) << phys;
    oss << " value=0x" << std::setw(8) << value << "\n";
    std::cerr << oss.str();
  }
  if (phys >= 0x1F801000 && phys < 0x1F803000 && mmio_) {
    mmio_->write32(phys, value);
    return;
  }
  write8(addr, static_cast<uint8_t>(value & 0xFF));
  write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
  write8(addr + 2, static_cast<uint8_t>((value >> 16) & 0xFF));
  write8(addr + 3, static_cast<uint8_t>((value >> 24) & 0xFF));
}

} // namespace ps1emu
