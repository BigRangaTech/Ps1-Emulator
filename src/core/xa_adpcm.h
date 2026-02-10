#ifndef PS1EMU_XA_ADPCM_H
#define PS1EMU_XA_ADPCM_H

#include <cstdint>
#include <vector>

namespace ps1emu {

struct XaDecodeState {
  int16_t old[2] = {0, 0};
  int16_t older[2] = {0, 0};
};

struct XaDecodeInfo {
  uint16_t sample_rate = 0;
  uint8_t channels = 0;
};

bool decode_xa_adpcm(const uint8_t *data,
                     size_t size,
                     uint8_t coding,
                     XaDecodeState &state,
                     XaDecodeInfo &info,
                     std::vector<int16_t> &out_left,
                     std::vector<int16_t> &out_right);

} // namespace ps1emu

#endif
