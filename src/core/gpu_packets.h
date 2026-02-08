#ifndef PS1EMU_GPU_PACKETS_H
#define PS1EMU_GPU_PACKETS_H

#include <cstdint>
#include <vector>

namespace ps1emu {

struct GpuPacket {
  uint8_t command = 0;
  std::vector<uint32_t> words;
};

std::vector<GpuPacket> parse_gp0_packets(const std::vector<uint32_t> &words,
                                         std::vector<uint32_t> &out_remainder);

} // namespace ps1emu

#endif
