#include "core/memory_map.h"

#include <cstring>

namespace ps1emu {

void MemoryMap::reset() {
  std::memset(ram_.data(), 0, ram_.size());
  std::memset(scratchpad_.data(), 0, scratchpad_.size());
}

void MemoryMap::load_bios(const BiosImage &bios) {
  bios_ = bios.valid() ? &bios : nullptr;
}

uint32_t MemoryMap::mask_address(uint32_t addr) const {
  return addr & 0x1FFFFFFF;
}

uint8_t MemoryMap::read8(uint32_t addr) const {
  uint32_t phys = mask_address(addr);
  if (phys < kRamSize) {
    return ram_[phys];
  }
  if (phys >= 0x1F800000 && phys < 0x1F800000 + kScratchpadSize) {
    return scratchpad_[phys - 0x1F800000];
  }
  if (phys >= 0x1FC00000 && phys < 0x1FC00000 + BiosImage::kExpectedSize && bios_) {
    return bios_->read8(phys - 0x1FC00000);
  }
  return 0xFF;
}

uint16_t MemoryMap::read16(uint32_t addr) const {
  uint16_t lo = read8(addr);
  uint16_t hi = read8(addr + 1);
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t MemoryMap::read32(uint32_t addr) const {
  uint32_t b0 = read8(addr);
  uint32_t b1 = read8(addr + 1);
  uint32_t b2 = read8(addr + 2);
  uint32_t b3 = read8(addr + 3);
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

void MemoryMap::write8(uint32_t addr, uint8_t value) {
  uint32_t phys = mask_address(addr);
  if (phys < kRamSize) {
    ram_[phys] = value;
    return;
  }
  if (phys >= 0x1F800000 && phys < 0x1F800000 + kScratchpadSize) {
    scratchpad_[phys - 0x1F800000] = value;
  }
}

void MemoryMap::write16(uint32_t addr, uint16_t value) {
  write8(addr, static_cast<uint8_t>(value & 0xFF));
  write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
}

void MemoryMap::write32(uint32_t addr, uint32_t value) {
  write8(addr, static_cast<uint8_t>(value & 0xFF));
  write8(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
  write8(addr + 2, static_cast<uint8_t>((value >> 16) & 0xFF));
  write8(addr + 3, static_cast<uint8_t>((value >> 24) & 0xFF));
}

} // namespace ps1emu
