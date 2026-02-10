#ifndef PS1EMU_GTE_H
#define PS1EMU_GTE_H

#include <array>
#include <cstdint>

namespace ps1emu {

class Gte {
public:
  void reset();
  uint32_t read_data(uint32_t reg) const;
  void write_data(uint32_t reg, uint32_t value);
  uint32_t read_ctrl(uint32_t reg) const;
  void write_ctrl(uint32_t reg, uint32_t value);
  void execute(uint32_t opcode);
  uint32_t command_cycles(uint32_t opcode) const;

private:
  struct Rgb {
    int32_t r = 0;
    int32_t g = 0;
    int32_t b = 0;
    uint8_t code = 0;
  };

  static int32_t sign_extend16(uint32_t value);
  static int32_t clamp_ir(int64_t value, bool lm);
  static int32_t clamp_ir0(int64_t value);
  static int32_t clamp_sxy(int64_t value);
  static uint16_t clamp_sz(int64_t value);
  static uint16_t clamp_u16(int64_t value);

  void update_lzcr(uint32_t value);
  uint32_t pack_irgb() const;
  void write_irgb(uint32_t value);

  void push_sxy(int32_t sx, int32_t sy);
  void push_sz(int64_t value);
  void push_color(int64_t r, int64_t g, int64_t b, uint8_t code);

  void clear_flags();
  void finalize_flags();
  void set_flag(int bit);
  void flag_mac_overflow(int index, int64_t value);
  void set_mac(int index, int64_t value);
  void set_ir(int index, int64_t value, bool lm);
  void set_ir0(int64_t value);
  void apply_llm_lcm(int32_t vx, int32_t vy, int32_t vz, bool sf, bool lm);
  void apply_depth_cue(int64_t &mac1, int64_t &mac2, int64_t &mac3, bool sf);
  void store_color_from_mac(int64_t mac1, int64_t mac2, int64_t mac3, bool sf, bool lm, uint8_t code);
  uint32_t compute_divide(uint16_t h, uint16_t sz3);

  void cmd_rtps(bool sf);
  void cmd_rtpt(bool sf);
  void cmd_mvmva(uint32_t opcode);
  void cmd_nclip();
  void cmd_avsz3();
  void cmd_avsz4();
  void cmd_sqr(bool sf);
  void cmd_op(bool sf, bool lm);
  void cmd_gpf(bool sf, bool lm);
  void cmd_gpl(bool sf, bool lm);
  void cmd_dpcs(bool sf, bool lm);
  void cmd_dpct(bool sf, bool lm);
  void cmd_intpl(bool sf, bool lm);
  void cmd_dcpl(bool sf, bool lm);
  void cmd_cc(bool sf, bool lm, bool cdp);
  void cmd_ncs(bool sf, bool lm, bool triple);
  void cmd_nccs(bool sf, bool lm, bool triple, bool depth_cue);

  struct Matrix3x3 {
    int32_t m11 = 0;
    int32_t m12 = 0;
    int32_t m13 = 0;
    int32_t m21 = 0;
    int32_t m22 = 0;
    int32_t m23 = 0;
    int32_t m31 = 0;
    int32_t m32 = 0;
    int32_t m33 = 0;
  };

  Matrix3x3 matrix_rt() const;
  Matrix3x3 matrix_ll() const;
  Matrix3x3 matrix_lc() const;
  Rgb rgbc() const;
  Rgb rgb0() const;

  std::array<uint32_t, 32> data_ {};
  std::array<uint32_t, 32> ctrl_ {};
};

} // namespace ps1emu

#endif
