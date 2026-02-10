#include "core/gte.h"

#include <algorithm>
#include <cstddef>

namespace ps1emu {

namespace {
static constexpr uint8_t kUnrTable[0x101] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE5, 0xE3,
    0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
    0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
    0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
    0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
    0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1F, 0x1E, 0x1D, 0x1D, 0x1C, 0x1C, 0x1B, 0x1A,
    0x1A, 0x19, 0x19, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11,
    0x11, 0x10, 0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0B, 0x0A, 0x0A, 0x09,
    0x09, 0x08, 0x08, 0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01,
    0x01
};
}

void Gte::reset() {
  data_.fill(0);
  ctrl_.fill(0);
}

int32_t Gte::sign_extend16(uint32_t value) {
  return static_cast<int32_t>(static_cast<int16_t>(value & 0xFFFFu));
}

int32_t Gte::clamp_ir(int64_t value, bool lm) {
  int32_t min_val = lm ? 0 : -0x8000;
  int32_t max_val = 0x7FFF;
  if (value < min_val) {
    return min_val;
  }
  if (value > max_val) {
    return max_val;
  }
  return static_cast<int32_t>(value);
}

int32_t Gte::clamp_ir0(int64_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > 0x1000) {
    return 0x1000;
  }
  return static_cast<int32_t>(value);
}

int32_t Gte::clamp_sxy(int64_t value) {
  if (value < -0x400) {
    return -0x400;
  }
  if (value > 0x3FF) {
    return 0x3FF;
  }
  return static_cast<int32_t>(value);
}

uint16_t Gte::clamp_sz(int64_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > 0xFFFF) {
    return 0xFFFF;
  }
  return static_cast<uint16_t>(value);
}

uint16_t Gte::clamp_u16(int64_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > 0xFFFF) {
    return 0xFFFF;
  }
  return static_cast<uint16_t>(value);
}

void Gte::update_lzcr(uint32_t value) {
  uint32_t msb = value & 0x80000000u;
  uint32_t count = 0;
  for (int i = 31; i >= 0; --i) {
    uint32_t bit = (value >> static_cast<uint32_t>(i)) & 1u;
    if ((bit != 0) == (msb != 0)) {
      count++;
    } else {
      break;
    }
  }
  data_[31] = count;
}

void Gte::clear_flags() {
  ctrl_[31] = 0;
}

void Gte::set_flag(int bit) {
  if (bit >= 0 && bit < 32) {
    ctrl_[31] |= (1u << static_cast<uint32_t>(bit));
  }
}

void Gte::finalize_flags() {
  uint32_t mask = 0;
  mask |= 0x7F800000u; // bits 30..23
  mask |= 0x0007E000u; // bits 18..13
  if (ctrl_[31] & mask) {
    ctrl_[31] |= (1u << 31);
  }
}

uint32_t Gte::pack_irgb() const {
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  auto to5 = [](int32_t v) {
    int32_t scaled = v / 0x80;
    return static_cast<uint32_t>(std::clamp(scaled, 0, 31));
  };
  uint32_t r = to5(ir1);
  uint32_t g = to5(ir2);
  uint32_t b = to5(ir3);
  return r | (g << 5) | (b << 10);
}

void Gte::write_irgb(uint32_t value) {
  uint32_t r = value & 0x1Fu;
  uint32_t g = (value >> 5) & 0x1Fu;
  uint32_t b = (value >> 10) & 0x1Fu;
  data_[9] = static_cast<uint32_t>(static_cast<int16_t>(r << 7));
  data_[10] = static_cast<uint32_t>(static_cast<int16_t>(g << 7));
  data_[11] = static_cast<uint32_t>(static_cast<int16_t>(b << 7));
}

void Gte::push_sxy(int32_t sx, int32_t sy) {
  if (sx < -0x400 || sx > 0x3FF) {
    set_flag(14);
  }
  if (sy < -0x400 || sy > 0x3FF) {
    set_flag(13);
  }
  sx = clamp_sxy(sx);
  sy = clamp_sxy(sy);
  uint32_t packed = static_cast<uint16_t>(sx) | (static_cast<uint32_t>(static_cast<uint16_t>(sy)) << 16);
  data_[12] = data_[13];
  data_[13] = data_[14];
  data_[14] = packed;
  data_[15] = data_[14];
}

void Gte::push_sz(int64_t value) {
  if (value < 0 || value > 0xFFFF) {
    set_flag(18);
  }
  uint16_t sz = clamp_sz(value);
  data_[16] = data_[17];
  data_[17] = data_[18];
  data_[18] = data_[19];
  data_[19] = sz;
}

void Gte::push_color(int64_t r, int64_t g, int64_t b, uint8_t code) {
  auto clamp8 = [&](int64_t value, int flag_bit) {
    if (value < 0) {
      set_flag(flag_bit);
      return 0;
    }
    if (value > 255) {
      set_flag(flag_bit);
      return 255;
    }
    return static_cast<int>(value);
  };
  int cr = clamp8(r, 21);
  int cg = clamp8(g, 20);
  int cb = clamp8(b, 19);
  uint32_t packed = static_cast<uint32_t>(cr & 0xFF) |
                    (static_cast<uint32_t>(cg & 0xFF) << 8) |
                    (static_cast<uint32_t>(cb & 0xFF) << 16) |
                    (static_cast<uint32_t>(code) << 24);
  data_[20] = data_[21];
  data_[21] = data_[22];
  data_[22] = packed;
}

void Gte::set_mac(int index, int64_t value) {
  if (index < 0 || index > 3) {
    return;
  }
  if (index == 0) {
    if (value > 0x7FFFFFFFLL) {
      set_flag(16);
    } else if (value < -0x80000000LL) {
      set_flag(15);
    }
  } else {
    if (value > ((1LL << 43) - 1)) {
      set_flag(30 - (index - 1));
    } else if (value < -(1LL << 43)) {
      set_flag(27 - (index - 1));
    }
  }
  data_[24 + index] = static_cast<uint32_t>(static_cast<int32_t>(value));
}

void Gte::set_ir(int index, int64_t value, bool lm) {
  if (index < 1 || index > 3) {
    return;
  }
  int32_t min_val = lm ? 0 : -0x8000;
  int32_t max_val = 0x7FFF;
  if (value < min_val || value > max_val) {
    set_flag(25 - index);
  }
  data_[8 + index] = static_cast<uint32_t>(static_cast<int16_t>(clamp_ir(value, lm)));
}

void Gte::set_ir0(int64_t value) {
  if (value < 0 || value > 0x1000) {
    set_flag(12);
  }
  data_[8] = static_cast<uint32_t>(static_cast<int16_t>(clamp_ir0(value)));
}

void Gte::apply_llm_lcm(int32_t vx, int32_t vy, int32_t vz, bool sf, bool lm) {
  Matrix3x3 ll = matrix_ll();
  int64_t mac1 = static_cast<int64_t>(ll.m11) * vx +
                 static_cast<int64_t>(ll.m12) * vy +
                 static_cast<int64_t>(ll.m13) * vz;
  int64_t mac2 = static_cast<int64_t>(ll.m21) * vx +
                 static_cast<int64_t>(ll.m22) * vy +
                 static_cast<int64_t>(ll.m23) * vz;
  int64_t mac3 = static_cast<int64_t>(ll.m31) * vx +
                 static_cast<int64_t>(ll.m32) * vy +
                 static_cast<int64_t>(ll.m33) * vz;

  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);

  Matrix3x3 lc = matrix_lc();
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);

  mac1 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[13])) << 12) +
         static_cast<int64_t>(lc.m11) * ir1 +
         static_cast<int64_t>(lc.m12) * ir2 +
         static_cast<int64_t>(lc.m13) * ir3;
  mac2 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[14])) << 12) +
         static_cast<int64_t>(lc.m21) * ir1 +
         static_cast<int64_t>(lc.m22) * ir2 +
         static_cast<int64_t>(lc.m23) * ir3;
  mac3 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[15])) << 12) +
         static_cast<int64_t>(lc.m31) * ir1 +
         static_cast<int64_t>(lc.m32) * ir2 +
         static_cast<int64_t>(lc.m33) * ir3;

  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);
}

void Gte::apply_depth_cue(int64_t &mac1, int64_t &mac2, int64_t &mac3, bool sf) {
  int32_t ir0 = clamp_ir0(sign_extend16(data_[8]));
  int64_t fc1 = static_cast<int64_t>(static_cast<int32_t>(ctrl_[21])) << 12;
  int64_t fc2 = static_cast<int64_t>(static_cast<int32_t>(ctrl_[22])) << 12;
  int64_t fc3 = static_cast<int64_t>(static_cast<int32_t>(ctrl_[23])) << 12;

  int64_t t1 = fc1 - mac1;
  int64_t t2 = fc2 - mac2;
  int64_t t3 = fc3 - mac3;
  if (sf) {
    t1 >>= 12;
    t2 >>= 12;
    t3 >>= 12;
  }
  set_ir(1, t1, false);
  set_ir(2, t2, false);
  set_ir(3, t3, false);
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);

  mac1 = mac1 + static_cast<int64_t>(ir1) * ir0;
  mac2 = mac2 + static_cast<int64_t>(ir2) * ir0;
  mac3 = mac3 + static_cast<int64_t>(ir3) * ir0;
}

void Gte::store_color_from_mac(int64_t mac1,
                               int64_t mac2,
                               int64_t mac3,
                               bool sf,
                               bool lm,
                               uint8_t code) {
  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);
  push_color(mac1 >> 4, mac2 >> 4, mac3 >> 4, code);
}

uint32_t Gte::compute_divide(uint16_t h, uint16_t sz3) {
  if (sz3 == 0 || h >= static_cast<uint32_t>(sz3) * 2u) {
    set_flag(17);
    return 0x1FFFFu;
  }

  uint16_t d = sz3;
  uint32_t shift = 0;
  while ((d & 0x8000u) == 0) {
    d <<= 1;
    shift++;
  }

  uint32_t n = static_cast<uint32_t>(h) << shift;
  uint32_t d_norm = static_cast<uint32_t>(sz3) << shift;
  uint32_t index = (d_norm - 0x7FC0u) >> 7;
  uint32_t u = static_cast<uint32_t>(kUnrTable[index]) + 0x101u;
  uint32_t d1 = (0x2000080u - d_norm * u) >> 8;
  uint32_t d2 = (0x0000080u + d1 * u) >> 8;
  uint32_t result = static_cast<uint32_t>((static_cast<uint64_t>(n) * d2 + 0x8000u) >> 16);
  if (result > 0x1FFFFu) {
    set_flag(17);
    result = 0x1FFFFu;
  }
  return result;
}

uint32_t Gte::read_data(uint32_t reg) const {
  if (reg >= data_.size()) {
    return 0;
  }
  switch (reg) {
    case 1:
    case 3:
    case 5:
    case 8:
    case 9:
    case 10:
    case 11:
      return static_cast<uint32_t>(sign_extend16(data_[reg]));
    case 7:
    case 16:
    case 17:
    case 18:
    case 19:
      return data_[reg] & 0xFFFFu;
    case 15:
      return data_[14];
    case 28:
    case 29:
      return pack_irgb();
    case 31:
      return data_[31];
    default:
      return data_[reg];
  }
}

void Gte::write_data(uint32_t reg, uint32_t value) {
  if (reg >= data_.size()) {
    return;
  }
  switch (reg) {
    case 1:
    case 3:
    case 5:
      data_[reg] = value & 0xFFFFu;
      break;
    case 8:
    case 9:
    case 10:
    case 11:
      data_[reg] = static_cast<uint32_t>(static_cast<int16_t>(value));
      break;
    case 15: {
      int32_t sx = sign_extend16(value);
      int32_t sy = sign_extend16(value >> 16);
      push_sxy(sx, sy);
      break;
    }
    case 28:
      write_irgb(value);
      data_[28] = value & 0x7FFFu;
      break;
    case 29:
      break;
    case 30:
      data_[30] = value;
      update_lzcr(value);
      break;
    default:
      data_[reg] = value;
      break;
  }
}

uint32_t Gte::read_ctrl(uint32_t reg) const {
  if (reg < 32 || reg > 63) {
    return 0;
  }
  uint32_t idx = reg - 32;
  switch (reg) {
    case 36:
    case 44:
    case 52:
    case 58:
      return static_cast<uint32_t>(sign_extend16(ctrl_[idx]));
    default:
      return ctrl_[idx];
  }
}

void Gte::write_ctrl(uint32_t reg, uint32_t value) {
  if (reg < 32 || reg > 63) {
    return;
  }
  uint32_t idx = reg - 32;
  ctrl_[idx] = value;
}

Gte::Matrix3x3 Gte::matrix_rt() const {
  Matrix3x3 m;
  m.m11 = sign_extend16(ctrl_[0]);
  m.m12 = sign_extend16(ctrl_[0] >> 16);
  m.m13 = sign_extend16(ctrl_[1]);
  m.m21 = sign_extend16(ctrl_[1] >> 16);
  m.m22 = sign_extend16(ctrl_[2]);
  m.m23 = sign_extend16(ctrl_[2] >> 16);
  m.m31 = sign_extend16(ctrl_[3]);
  m.m32 = sign_extend16(ctrl_[3] >> 16);
  m.m33 = sign_extend16(ctrl_[4]);
  return m;
}

Gte::Matrix3x3 Gte::matrix_ll() const {
  Matrix3x3 m;
  m.m11 = sign_extend16(ctrl_[8]);
  m.m12 = sign_extend16(ctrl_[8] >> 16);
  m.m13 = sign_extend16(ctrl_[9]);
  m.m21 = sign_extend16(ctrl_[9] >> 16);
  m.m22 = sign_extend16(ctrl_[10]);
  m.m23 = sign_extend16(ctrl_[10] >> 16);
  m.m31 = sign_extend16(ctrl_[11]);
  m.m32 = sign_extend16(ctrl_[11] >> 16);
  m.m33 = sign_extend16(ctrl_[12]);
  return m;
}

Gte::Matrix3x3 Gte::matrix_lc() const {
  Matrix3x3 m;
  m.m11 = sign_extend16(ctrl_[16]);
  m.m12 = sign_extend16(ctrl_[16] >> 16);
  m.m13 = sign_extend16(ctrl_[17]);
  m.m21 = sign_extend16(ctrl_[17] >> 16);
  m.m22 = sign_extend16(ctrl_[18]);
  m.m23 = sign_extend16(ctrl_[18] >> 16);
  m.m31 = sign_extend16(ctrl_[19]);
  m.m32 = sign_extend16(ctrl_[19] >> 16);
  m.m33 = sign_extend16(ctrl_[20]);
  return m;
}

Gte::Rgb Gte::rgbc() const {
  uint32_t packed = data_[6];
  Rgb out;
  out.r = static_cast<int32_t>(packed & 0xFFu);
  out.g = static_cast<int32_t>((packed >> 8) & 0xFFu);
  out.b = static_cast<int32_t>((packed >> 16) & 0xFFu);
  out.code = static_cast<uint8_t>((packed >> 24) & 0xFFu);
  return out;
}

Gte::Rgb Gte::rgb0() const {
  uint32_t packed = data_[20];
  Rgb out;
  out.r = static_cast<int32_t>(packed & 0xFFu);
  out.g = static_cast<int32_t>((packed >> 8) & 0xFFu);
  out.b = static_cast<int32_t>((packed >> 16) & 0xFFu);
  out.code = static_cast<uint8_t>((packed >> 24) & 0xFFu);
  return out;
}

void Gte::cmd_rtps(bool sf) {
  Matrix3x3 rt = matrix_rt();
  int32_t vx = sign_extend16(data_[0]);
  int32_t vy = sign_extend16(data_[0] >> 16);
  int32_t vz = sign_extend16(data_[1]);

  int64_t mac1 = (static_cast<int64_t>(ctrl_[5]) << 12) +
                 static_cast<int64_t>(rt.m11) * vx +
                 static_cast<int64_t>(rt.m12) * vy +
                 static_cast<int64_t>(rt.m13) * vz;
  int64_t mac2 = (static_cast<int64_t>(ctrl_[6]) << 12) +
                 static_cast<int64_t>(rt.m21) * vx +
                 static_cast<int64_t>(rt.m22) * vy +
                 static_cast<int64_t>(rt.m23) * vz;
  int64_t mac3 = (static_cast<int64_t>(ctrl_[7]) << 12) +
                 static_cast<int64_t>(rt.m31) * vx +
                 static_cast<int64_t>(rt.m32) * vy +
                 static_cast<int64_t>(rt.m33) * vz;

  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }

  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, false);
  set_ir(2, mac2, false);
  set_ir(3, mac3, false);

  int64_t sz3_raw = mac3;
  if (!sf) {
    sz3_raw >>= 12;
  }
  push_sz(sz3_raw);
  uint32_t h = static_cast<uint16_t>(ctrl_[26] & 0xFFFFu);
  uint32_t q = compute_divide(static_cast<uint16_t>(h),
                              static_cast<uint16_t>(data_[19] & 0xFFFFu));

  int64_t mac0x = static_cast<int64_t>(ctrl_[24]) + static_cast<int64_t>(sign_extend16(data_[9])) * q;
  int64_t mac0y = static_cast<int64_t>(ctrl_[25]) + static_cast<int64_t>(sign_extend16(data_[10])) * q;
  set_mac(0, mac0x);
  int32_t sx = clamp_sxy(mac0x >> 16);
  int32_t sy = clamp_sxy(mac0y >> 16);
  push_sxy(sx, sy);

  int64_t mac0 = static_cast<int64_t>(sign_extend16(ctrl_[27])) * q +
                 static_cast<int64_t>(static_cast<int32_t>(ctrl_[28]));
  set_mac(0, mac0);
  set_ir0(mac0 >> 12);
}

void Gte::cmd_rtpt(bool sf) {
  for (int i = 0; i < 3; ++i) {
    Matrix3x3 rt = matrix_rt();
    int32_t vx = sign_extend16(data_[i * 2]);
    int32_t vy = sign_extend16(data_[i * 2] >> 16);
    int32_t vz = sign_extend16(data_[i * 2 + 1]);

    int64_t mac1 = (static_cast<int64_t>(ctrl_[5]) << 12) +
                   static_cast<int64_t>(rt.m11) * vx +
                   static_cast<int64_t>(rt.m12) * vy +
                   static_cast<int64_t>(rt.m13) * vz;
    int64_t mac2 = (static_cast<int64_t>(ctrl_[6]) << 12) +
                   static_cast<int64_t>(rt.m21) * vx +
                   static_cast<int64_t>(rt.m22) * vy +
                   static_cast<int64_t>(rt.m23) * vz;
    int64_t mac3 = (static_cast<int64_t>(ctrl_[7]) << 12) +
                   static_cast<int64_t>(rt.m31) * vx +
                   static_cast<int64_t>(rt.m32) * vy +
                   static_cast<int64_t>(rt.m33) * vz;

    if (sf) {
      mac1 >>= 12;
      mac2 >>= 12;
      mac3 >>= 12;
    }

    set_mac(1, mac1);
    set_mac(2, mac2);
    set_mac(3, mac3);
    set_ir(1, mac1, false);
    set_ir(2, mac2, false);
    set_ir(3, mac3, false);

    int64_t sz3_raw = mac3;
    if (!sf) {
      sz3_raw >>= 12;
    }
    push_sz(sz3_raw);
    uint32_t h = static_cast<uint16_t>(ctrl_[26] & 0xFFFFu);
    uint32_t q = compute_divide(static_cast<uint16_t>(h),
                                static_cast<uint16_t>(data_[19] & 0xFFFFu));

    int64_t mac0x = static_cast<int64_t>(ctrl_[24]) + static_cast<int64_t>(sign_extend16(data_[9])) * q;
    int64_t mac0y = static_cast<int64_t>(ctrl_[25]) + static_cast<int64_t>(sign_extend16(data_[10])) * q;
    set_mac(0, mac0x);
    int32_t sx = clamp_sxy(mac0x >> 16);
    int32_t sy = clamp_sxy(mac0y >> 16);
    push_sxy(sx, sy);

    int64_t mac0 = static_cast<int64_t>(sign_extend16(ctrl_[27])) * q +
                   static_cast<int64_t>(static_cast<int32_t>(ctrl_[28]));
    set_mac(0, mac0);
    set_ir0(mac0 >> 12);
  }
}

void Gte::cmd_mvmva(uint32_t opcode) {
  uint32_t mx = (opcode >> 17) & 0x3u;
  uint32_t v = (opcode >> 15) & 0x3u;
  uint32_t cv = (opcode >> 13) & 0x3u;
  bool sf = (opcode & (1u << 19)) != 0;
  bool lm = (opcode & (1u << 10)) != 0;

  Matrix3x3 m;
  switch (mx) {
    case 0:
      m = matrix_rt();
      break;
    case 1:
      m = matrix_ll();
      break;
    case 2:
      m = matrix_lc();
      break;
    default:
      m = {};
      break;
  }

  int32_t vx = 0;
  int32_t vy = 0;
  int32_t vz = 0;
  if (v == 3) {
    vx = sign_extend16(data_[9]);
    vy = sign_extend16(data_[10]);
    vz = sign_extend16(data_[11]);
  } else {
    vx = sign_extend16(data_[v * 2]);
    vy = sign_extend16(data_[v * 2] >> 16);
    vz = sign_extend16(data_[v * 2 + 1]);
  }

  int64_t tx = 0;
  int64_t ty = 0;
  int64_t tz = 0;
  if (cv == 0) {
    tx = static_cast<int64_t>(static_cast<int32_t>(ctrl_[5]));
    ty = static_cast<int64_t>(static_cast<int32_t>(ctrl_[6]));
    tz = static_cast<int64_t>(static_cast<int32_t>(ctrl_[7]));
  } else if (cv == 1) {
    tx = static_cast<int64_t>(static_cast<int32_t>(ctrl_[13]));
    ty = static_cast<int64_t>(static_cast<int32_t>(ctrl_[14]));
    tz = static_cast<int64_t>(static_cast<int32_t>(ctrl_[15]));
  } else if (cv == 2) {
    tx = static_cast<int64_t>(static_cast<int32_t>(ctrl_[21]));
    ty = static_cast<int64_t>(static_cast<int32_t>(ctrl_[22]));
    tz = static_cast<int64_t>(static_cast<int32_t>(ctrl_[23]));
  }

  int64_t mac1 = (tx << 12) +
                 static_cast<int64_t>(m.m11) * vx +
                 static_cast<int64_t>(m.m12) * vy +
                 static_cast<int64_t>(m.m13) * vz;
  int64_t mac2 = (ty << 12) +
                 static_cast<int64_t>(m.m21) * vx +
                 static_cast<int64_t>(m.m22) * vy +
                 static_cast<int64_t>(m.m23) * vz;
  int64_t mac3 = (tz << 12) +
                 static_cast<int64_t>(m.m31) * vx +
                 static_cast<int64_t>(m.m32) * vy +
                 static_cast<int64_t>(m.m33) * vz;

  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }

  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);
}

void Gte::cmd_nclip() {
  int32_t sx0 = sign_extend16(data_[12]);
  int32_t sy0 = sign_extend16(data_[12] >> 16);
  int32_t sx1 = sign_extend16(data_[13]);
  int32_t sy1 = sign_extend16(data_[13] >> 16);
  int32_t sx2 = sign_extend16(data_[14]);
  int32_t sy2 = sign_extend16(data_[14] >> 16);
  int64_t mac0 = static_cast<int64_t>(sx0) * sy1 +
                 static_cast<int64_t>(sx1) * sy2 +
                 static_cast<int64_t>(sx2) * sy0 -
                 static_cast<int64_t>(sx0) * sy2 -
                 static_cast<int64_t>(sx1) * sy0 -
                 static_cast<int64_t>(sx2) * sy1;
  set_mac(0, mac0);
}

void Gte::cmd_avsz3() {
  uint16_t sz1 = static_cast<uint16_t>(data_[17] & 0xFFFFu);
  uint16_t sz2 = static_cast<uint16_t>(data_[18] & 0xFFFFu);
  uint16_t sz3 = static_cast<uint16_t>(data_[19] & 0xFFFFu);
  int64_t mac0 = static_cast<int64_t>(sign_extend16(ctrl_[29])) *
                 static_cast<int64_t>(sz1 + sz2 + sz3);
  set_mac(0, mac0);
  int64_t otz_raw = mac0 >> 12;
  if (otz_raw < 0 || otz_raw > 0xFFFF) {
    set_flag(18);
  }
  data_[7] = clamp_u16(otz_raw);
}

void Gte::cmd_avsz4() {
  uint16_t sz0 = static_cast<uint16_t>(data_[16] & 0xFFFFu);
  uint16_t sz1 = static_cast<uint16_t>(data_[17] & 0xFFFFu);
  uint16_t sz2 = static_cast<uint16_t>(data_[18] & 0xFFFFu);
  uint16_t sz3 = static_cast<uint16_t>(data_[19] & 0xFFFFu);
  int64_t mac0 = static_cast<int64_t>(sign_extend16(ctrl_[30])) *
                 static_cast<int64_t>(sz0 + sz1 + sz2 + sz3);
  set_mac(0, mac0);
  int64_t otz_raw = mac0 >> 12;
  if (otz_raw < 0 || otz_raw > 0xFFFF) {
    set_flag(18);
  }
  data_[7] = clamp_u16(otz_raw);
}

void Gte::cmd_sqr(bool sf) {
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(ir1) * ir1;
  int64_t mac2 = static_cast<int64_t>(ir2) * ir2;
  int64_t mac3 = static_cast<int64_t>(ir3) * ir3;
  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, false);
  set_ir(2, mac2, false);
  set_ir(3, mac3, false);
}

void Gte::cmd_op(bool sf, bool lm) {
  Matrix3x3 rt = matrix_rt();
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(rt.m22) * ir3 - static_cast<int64_t>(rt.m33) * ir2;
  int64_t mac2 = static_cast<int64_t>(rt.m33) * ir1 - static_cast<int64_t>(rt.m11) * ir3;
  int64_t mac3 = static_cast<int64_t>(rt.m11) * ir2 - static_cast<int64_t>(rt.m22) * ir1;
  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);
}

void Gte::cmd_gpf(bool sf, bool lm) {
  int32_t ir0 = clamp_ir0(sign_extend16(data_[8]));
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(ir1) * ir0;
  int64_t mac2 = static_cast<int64_t>(ir2) * ir0;
  int64_t mac3 = static_cast<int64_t>(ir3) * ir0;
  store_color_from_mac(mac1, mac2, mac3, sf, lm, rgbc().code);
}

void Gte::cmd_gpl(bool sf, bool lm) {
  int32_t ir0 = clamp_ir0(sign_extend16(data_[8]));
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(static_cast<int32_t>(data_[25]));
  int64_t mac2 = static_cast<int64_t>(static_cast<int32_t>(data_[26]));
  int64_t mac3 = static_cast<int64_t>(static_cast<int32_t>(data_[27]));
  if (sf) {
    mac1 <<= 12;
    mac2 <<= 12;
    mac3 <<= 12;
  }
  mac1 += static_cast<int64_t>(ir1) * ir0;
  mac2 += static_cast<int64_t>(ir2) * ir0;
  mac3 += static_cast<int64_t>(ir3) * ir0;
  store_color_from_mac(mac1, mac2, mac3, sf, lm, rgbc().code);
}

void Gte::cmd_dpcs(bool sf, bool lm) {
  Rgb c = rgbc();
  int64_t mac1 = static_cast<int64_t>(c.r) << 16;
  int64_t mac2 = static_cast<int64_t>(c.g) << 16;
  int64_t mac3 = static_cast<int64_t>(c.b) << 16;
  apply_depth_cue(mac1, mac2, mac3, sf);
  store_color_from_mac(mac1, mac2, mac3, sf, lm, c.code);
}

void Gte::cmd_dpct(bool sf, bool lm) {
  uint8_t code = rgbc().code;
  for (int i = 0; i < 3; ++i) {
    Rgb c = rgb0();
    int64_t mac1 = static_cast<int64_t>(c.r) << 16;
    int64_t mac2 = static_cast<int64_t>(c.g) << 16;
    int64_t mac3 = static_cast<int64_t>(c.b) << 16;
    apply_depth_cue(mac1, mac2, mac3, sf);
    store_color_from_mac(mac1, mac2, mac3, sf, lm, code);
  }
}

void Gte::cmd_intpl(bool sf, bool lm) {
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(ir1) << 12;
  int64_t mac2 = static_cast<int64_t>(ir2) << 12;
  int64_t mac3 = static_cast<int64_t>(ir3) << 12;
  apply_depth_cue(mac1, mac2, mac3, sf);
  store_color_from_mac(mac1, mac2, mac3, sf, lm, rgbc().code);
}

void Gte::cmd_dcpl(bool sf, bool lm) {
  Rgb c = rgbc();
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);
  int64_t mac1 = static_cast<int64_t>(c.r) * ir1;
  int64_t mac2 = static_cast<int64_t>(c.g) * ir2;
  int64_t mac3 = static_cast<int64_t>(c.b) * ir3;
  mac1 <<= 4;
  mac2 <<= 4;
  mac3 <<= 4;
  apply_depth_cue(mac1, mac2, mac3, sf);
  store_color_from_mac(mac1, mac2, mac3, sf, lm, c.code);
}

void Gte::cmd_cc(bool sf, bool lm, bool cdp) {
  Matrix3x3 lc = matrix_lc();
  int32_t ir1 = sign_extend16(data_[9]);
  int32_t ir2 = sign_extend16(data_[10]);
  int32_t ir3 = sign_extend16(data_[11]);

  int64_t mac1 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[13])) << 12) +
                 static_cast<int64_t>(lc.m11) * ir1 +
                 static_cast<int64_t>(lc.m12) * ir2 +
                 static_cast<int64_t>(lc.m13) * ir3;
  int64_t mac2 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[14])) << 12) +
                 static_cast<int64_t>(lc.m21) * ir1 +
                 static_cast<int64_t>(lc.m22) * ir2 +
                 static_cast<int64_t>(lc.m23) * ir3;
  int64_t mac3 = (static_cast<int64_t>(static_cast<int32_t>(ctrl_[15])) << 12) +
                 static_cast<int64_t>(lc.m31) * ir1 +
                 static_cast<int64_t>(lc.m32) * ir2 +
                 static_cast<int64_t>(lc.m33) * ir3;
  if (sf) {
    mac1 >>= 12;
    mac2 >>= 12;
    mac3 >>= 12;
  }
  set_mac(1, mac1);
  set_mac(2, mac2);
  set_mac(3, mac3);
  set_ir(1, mac1, lm);
  set_ir(2, mac2, lm);
  set_ir(3, mac3, lm);

  Rgb c = rgbc();
  ir1 = sign_extend16(data_[9]);
  ir2 = sign_extend16(data_[10]);
  ir3 = sign_extend16(data_[11]);
  mac1 = static_cast<int64_t>(c.r) * ir1;
  mac2 = static_cast<int64_t>(c.g) * ir2;
  mac3 = static_cast<int64_t>(c.b) * ir3;
  mac1 <<= 4;
  mac2 <<= 4;
  mac3 <<= 4;
  if (cdp) {
    apply_depth_cue(mac1, mac2, mac3, sf);
  }
  store_color_from_mac(mac1, mac2, mac3, sf, lm, c.code);
}

void Gte::cmd_ncs(bool sf, bool lm, bool triple) {
  uint8_t code = rgbc().code;
  int count = triple ? 3 : 1;
  for (int i = 0; i < count; ++i) {
    int32_t vx = sign_extend16(data_[i * 2]);
    int32_t vy = sign_extend16(data_[i * 2] >> 16);
    int32_t vz = sign_extend16(data_[i * 2 + 1]);
    apply_llm_lcm(vx, vy, vz, sf, lm);
    int64_t mac1 = static_cast<int32_t>(data_[25]);
    int64_t mac2 = static_cast<int32_t>(data_[26]);
    int64_t mac3 = static_cast<int32_t>(data_[27]);
    push_color(mac1 >> 4, mac2 >> 4, mac3 >> 4, code);
  }
}

void Gte::cmd_nccs(bool sf, bool lm, bool triple, bool depth_cue) {
  Rgb c = rgbc();
  int count = triple ? 3 : 1;
  for (int i = 0; i < count; ++i) {
    int32_t vx = sign_extend16(data_[i * 2]);
    int32_t vy = sign_extend16(data_[i * 2] >> 16);
    int32_t vz = sign_extend16(data_[i * 2 + 1]);
    apply_llm_lcm(vx, vy, vz, sf, lm);
    int32_t ir1 = sign_extend16(data_[9]);
    int32_t ir2 = sign_extend16(data_[10]);
    int32_t ir3 = sign_extend16(data_[11]);
    int64_t mac1 = static_cast<int64_t>(c.r) * ir1;
    int64_t mac2 = static_cast<int64_t>(c.g) * ir2;
    int64_t mac3 = static_cast<int64_t>(c.b) * ir3;
    mac1 <<= 4;
    mac2 <<= 4;
    mac3 <<= 4;
    if (depth_cue) {
      apply_depth_cue(mac1, mac2, mac3, sf);
    }
    store_color_from_mac(mac1, mac2, mac3, sf, lm, c.code);
  }
}

void Gte::execute(uint32_t opcode) {
  uint32_t op = opcode & 0x3Fu;
  bool sf = (opcode & (1u << 19)) != 0;
  bool lm = (opcode & (1u << 10)) != 0;
  clear_flags();

  switch (op) {
    case 0x01:
      cmd_rtps(sf);
      break;
    case 0x30:
      cmd_rtpt(sf);
      break;
    case 0x12:
      cmd_mvmva(opcode);
      break;
    case 0x06:
      cmd_nclip();
      break;
    case 0x10:
      cmd_dpcs(sf, lm);
      break;
    case 0x11:
      cmd_intpl(sf, lm);
      break;
    case 0x13:
      cmd_nccs(sf, lm, false, true);
      break;
    case 0x14:
      cmd_cc(sf, lm, true);
      break;
    case 0x16:
      cmd_nccs(sf, lm, true, true);
      break;
    case 0x1B:
      cmd_nccs(sf, lm, false, false);
      break;
    case 0x1C:
      cmd_cc(sf, lm, false);
      break;
    case 0x1E:
      cmd_ncs(sf, lm, false);
      break;
    case 0x20:
      cmd_ncs(sf, lm, true);
      break;
    case 0x28:
      cmd_sqr(sf);
      break;
    case 0x29:
      cmd_dcpl(sf, lm);
      break;
    case 0x2A:
      cmd_dpct(sf, lm);
      break;
    case 0x2D:
      cmd_avsz3();
      break;
    case 0x2E:
      cmd_avsz4();
      break;
    case 0x0C:
      cmd_op(sf, lm);
      break;
    case 0x3D:
      cmd_gpf(sf, lm);
      break;
    case 0x3E:
      cmd_gpl(sf, lm);
      break;
    case 0x3F:
      cmd_nccs(sf, lm, true, false);
      break;
    default:
      break;
  }

  finalize_flags();
}

} // namespace ps1emu
