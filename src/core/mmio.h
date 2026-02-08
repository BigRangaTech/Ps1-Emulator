#ifndef PS1EMU_MMIO_H
#define PS1EMU_MMIO_H

#include <array>
#include <cstdint>
#include <vector>

namespace ps1emu {

class MmioBus {
public:
  void reset();

  uint8_t read8(uint32_t addr);
  uint16_t read16(uint32_t addr);
  uint32_t read32(uint32_t addr);

  void write8(uint32_t addr, uint8_t value);
  void write16(uint32_t addr, uint16_t value);
  void write32(uint32_t addr, uint32_t value);
  bool irq_pending() const;
  void tick(uint32_t cycles);
  bool has_gpu_commands() const;
  std::vector<uint32_t> take_gpu_commands();

private:
  static constexpr uint32_t kBase = 0x1F801000;
  static constexpr uint32_t kSize = 0x2000;

  uint32_t offset(uint32_t addr) const;

  std::array<uint8_t, kSize> raw_ {};

  uint32_t gpu_gp0_ = 0;
  uint32_t gpu_gp1_ = 0;
  std::vector<uint32_t> gpu_gp0_fifo_;

  uint16_t irq_stat_ = 0;
  uint16_t irq_mask_ = 0;

  uint32_t dma_madr_[7] = {};
  uint32_t dma_bcr_[7] = {};
  uint32_t dma_chcr_[7] = {};
  uint32_t dma_dpcr_ = 0;
  uint32_t dma_dicr_ = 0;

  uint16_t timer_count_[3] = {};
  uint16_t timer_mode_[3] = {};
  uint16_t timer_target_[3] = {};

  std::array<uint16_t, 0x200 / 2> spu_regs_ {};
  std::array<uint8_t, 4> cdrom_regs_ {};
};

} // namespace ps1emu

#endif
