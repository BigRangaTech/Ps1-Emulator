#include "core/gpu_packets.h"

namespace ps1emu {

static size_t gp0_packet_length(const std::vector<uint32_t> &words, size_t index) {
  uint32_t word = words[index];
  uint8_t cmd = static_cast<uint8_t>(word >> 24);

  if (cmd == 0x00 || cmd == 0x01) {
    return 1;
  }
  if (cmd >= 0xE0 && cmd <= 0xE7) {
    return 1;
  }
  if (cmd >= 0xE1 && cmd <= 0xE6) {
    return 1;
  }
  if (cmd == 0x02) { // Fill rectangle
    return 3;
  }
  if (cmd == 0xA0) { // Load image
    if (index + 2 >= words.size()) {
      return 0;
    }
    uint32_t size = words[index + 2];
    uint32_t width = size & 0xFFFFu;
    uint32_t height = (size >> 16) & 0xFFFFu;
    uint64_t pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    uint64_t data_words = (pixels + 1) / 2;
    uint64_t total = 3 + data_words;
    if (total > 0x1FFFFFFF) {
      return 0;
    }
    return static_cast<size_t>(total);
  }
  if (cmd == 0xC0) { // Store image (data read from GPUREAD)
    return 3;
  }

  if (cmd >= 0x20 && cmd <= 0x3F) {
    bool textured = (cmd & 0x04) != 0;
    bool gouraud = (cmd & 0x10) != 0;
    bool quad = (cmd & 0x08) != 0;
    uint32_t vertices = quad ? 4 : 3;
    uint32_t words_count = 1 + vertices;
    if (gouraud) {
      words_count += (vertices - 1);
    }
    if (textured) {
      words_count += vertices;
    }
    return words_count;
  }

  if (cmd >= 0x40 && cmd <= 0x5F) {
    bool gouraud = (cmd & 0x10) != 0;
    bool polyline = (cmd & 0x08) != 0;
    if (polyline) {
      return 0; // variable-length, handled as remainder for now
    }
    uint32_t words_count = 1 + 2;
    if (gouraud) {
      words_count += 1;
    }
    return words_count;
  }

  if (cmd >= 0x60 && cmd <= 0x7F) {
    bool textured = (cmd & 0x04) != 0;
    bool has_size = (cmd & 0x18) == 0x00;
    uint32_t words_count = 1 + 1; // cmd + xy
    if (textured) {
      words_count += 1; // uv/clut
    }
    if (has_size) {
      words_count += 1; // size
    }
    return words_count;
  }

  if (cmd >= 0x80 && cmd <= 0x9F) { // copy rectangle / move image / etc.
    return 4;
  }
  if (cmd >= 0xA0 && cmd <= 0xBF) { // image load/store
    return 3;
  }
  if (cmd >= 0xC0 && cmd <= 0xDF) { // image store
    return 3;
  }

  return 1;
}

std::vector<GpuPacket> parse_gp0_packets(const std::vector<uint32_t> &words,
                                         std::vector<uint32_t> &out_remainder) {
  std::vector<GpuPacket> packets;
  out_remainder.clear();

  size_t index = 0;
  bool in_polyline = false;
  GpuPacket polyline;
  while (index < words.size()) {
    if (in_polyline) {
      uint32_t word = words[index];
      polyline.words.push_back(word);
      index += 1;
      if ((word & 0xF000F000u) == 0x50005000u) {
        packets.push_back(std::move(polyline));
        polyline = {};
        in_polyline = false;
      }
      continue;
    }

    size_t len = gp0_packet_length(words, index);
    if (len == 0 || index + len > words.size()) {
      uint32_t word = words[index];
      uint8_t cmd = static_cast<uint8_t>(word >> 24);
      if (cmd >= 0x40 && cmd <= 0x5F && (cmd & 0x08)) {
        in_polyline = true;
        polyline.command = cmd;
        polyline.words.push_back(word);
        index += 1;
        continue;
      }

      out_remainder.assign(words.begin() + static_cast<long>(index), words.end());
      break;
    }

    GpuPacket pkt;
    pkt.command = static_cast<uint8_t>(words[index] >> 24);
    pkt.words.insert(pkt.words.end(), words.begin() + static_cast<long>(index),
                     words.begin() + static_cast<long>(index + len));
    packets.push_back(std::move(pkt));
    index += len;
  }

  if (in_polyline && !polyline.words.empty()) {
    out_remainder = polyline.words;
  }

  return packets;
}

} // namespace ps1emu
