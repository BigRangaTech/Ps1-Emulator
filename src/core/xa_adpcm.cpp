#include "core/xa_adpcm.h"

#include <algorithm>

namespace ps1emu {

static int16_t clamp_sample(int32_t value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return static_cast<int16_t>(value);
}

static void decode_28_nibbles(const uint8_t *group,
                              int block,
                              int nibble,
                              int16_t &old,
                              int16_t &older,
                              std::vector<int16_t> &out) {
  static const int kPos[5] = {0, 60, 115, 98, 122};
  static const int kNeg[5] = {0, 0, -52, -55, -60};

  uint8_t header = group[4 + block * 2 + nibble];
  int shift_n = header & 0x0F;
  if (shift_n > 12) {
    shift_n = 9;
  }
  int shift = 12 - shift_n;
  int filter = (header >> 4) & 0x03;
  if (filter > 4) {
    filter = 4;
  }

  for (int j = 0; j < 28; ++j) {
    uint8_t data = group[16 + block + j * 4];
    int nib = (data >> (nibble * 4)) & 0x0F;
    int sample = (nib >= 8) ? (nib - 16) : nib;
    int32_t predicted = (old * kPos[filter] + older * kNeg[filter] + 32) / 64;
    int32_t pcm = (sample << shift) + predicted;
    int16_t clamped = clamp_sample(pcm);
    older = old;
    old = clamped;
    out.push_back(clamped);
  }
}

static void decode_28_bytes(const uint8_t *block_data,
                            uint8_t header,
                            int16_t &old,
                            int16_t &older,
                            std::vector<int16_t> &out) {
  static const int kPos[5] = {0, 60, 115, 98, 122};
  static const int kNeg[5] = {0, 0, -52, -55, -60};

  int shift_n = header & 0x0F;
  if (shift_n > 8) {
    shift_n = 8;
  }
  int shift = 8 - shift_n;
  int filter = (header >> 4) & 0x03;
  if (filter > 4) {
    filter = 4;
  }

  for (int j = 0; j < 28; ++j) {
    int sample = static_cast<int8_t>(block_data[j]);
    int32_t predicted = (old * kPos[filter] + older * kNeg[filter] + 32) / 64;
    int32_t pcm = (sample << shift) + predicted;
    int16_t clamped = clamp_sample(pcm);
    older = old;
    old = clamped;
    out.push_back(clamped);
  }
}

bool decode_xa_adpcm(const uint8_t *data,
                     size_t size,
                     uint8_t coding,
                     XaDecodeState &state,
                     XaDecodeInfo &info,
                     std::vector<int16_t> &out_left,
                     std::vector<int16_t> &out_right) {
  if (!data || size == 0) {
    return false;
  }

  uint8_t channel_mode = coding & 0x03;
  uint8_t sample_rate_flag = (coding >> 2) & 0x01;
  uint8_t bits_per_sample = (coding >> 4) & 0x03;

  info.sample_rate = sample_rate_flag ? 18900 : 37800;
  info.channels = (channel_mode == 0) ? 1 : 2;

  out_left.clear();
  out_right.clear();

  size_t max_bytes = std::min<size_t>(size, 0x900);
  size_t groups = max_bytes / 128;
  for (size_t g = 0; g < groups; ++g) {
    const uint8_t *group = data + g * 128;
    if (bits_per_sample == 0) {
      for (int block = 0; block < 4; ++block) {
        if (info.channels == 1) {
          decode_28_nibbles(group, block, 0, state.old[0], state.older[0], out_left);
          decode_28_nibbles(group, block, 1, state.old[0], state.older[0], out_left);
        } else {
          decode_28_nibbles(group, block, 0, state.old[0], state.older[0], out_left);
          decode_28_nibbles(group, block, 1, state.old[1], state.older[1], out_right);
        }
      }
    } else if (bits_per_sample == 1) {
      for (int block = 0; block < 4; ++block) {
        uint8_t header = group[4 + block];
        const uint8_t *block_data = group + 16 + block * 28;
        if (info.channels == 1) {
          decode_28_bytes(block_data, header, state.old[0], state.older[0], out_left);
        } else if ((block & 1) == 0) {
          decode_28_bytes(block_data, header, state.old[0], state.older[0], out_left);
        } else {
          decode_28_bytes(block_data, header, state.old[1], state.older[1], out_right);
        }
      }
    } else {
      return false;
    }
  }
  if (info.channels == 2) {
    size_t min_samples = std::min(out_left.size(), out_right.size());
    out_left.resize(min_samples);
    out_right.resize(min_samples);
  }
  return true;
}

} // namespace ps1emu
