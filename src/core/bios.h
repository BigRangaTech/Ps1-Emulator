#ifndef PS1EMU_BIOS_H
#define PS1EMU_BIOS_H

#include <cstdint>
#include <string>
#include <vector>

namespace ps1emu {

class BiosImage {
public:
  static constexpr size_t kExpectedSize = 512 * 1024;

  bool load_from_file(const std::string &path, std::string &error);
  void load_hle_stub();
  bool valid() const;
  bool is_hle() const;

  const std::vector<uint8_t> &data() const;
  uint8_t read8(size_t offset) const;

private:
  std::vector<uint8_t> data_;
  bool is_hle_ = false;
};

} // namespace ps1emu

#endif
