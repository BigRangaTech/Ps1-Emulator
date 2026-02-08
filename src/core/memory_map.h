#ifndef PS1EMU_MEMORY_MAP_H
#define PS1EMU_MEMORY_MAP_H

#include "core/bios.h"
#include "core/mmio.h"

#include <array>
#include <cstdint>

namespace ps1emu {

class MemoryMap {
public:
  static constexpr size_t kRamSize = 2 * 1024 * 1024;
  static constexpr size_t kScratchpadSize = 1024;

  void reset();
  void load_bios(const BiosImage &bios);
  void attach_mmio(MmioBus &mmio);
  bool irq_pending() const;
  uint16_t irq_stat() const;
  uint16_t irq_mask() const;

  uint8_t read8(uint32_t addr) const;
  uint16_t read16(uint32_t addr) const;
  uint32_t read32(uint32_t addr) const;

  void write8(uint32_t addr, uint8_t value);
  void write16(uint32_t addr, uint16_t value);
  void write32(uint32_t addr, uint32_t value);

private:
  uint32_t mask_address(uint32_t addr) const;

  std::array<uint8_t, kRamSize> ram_ {};
  std::array<uint8_t, kScratchpadSize> scratchpad_ {};
  const BiosImage *bios_ = nullptr;
  MmioBus *mmio_ = nullptr;
};

} // namespace ps1emu

#endif
