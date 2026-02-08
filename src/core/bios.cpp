#include "core/bios.h"

#include <fstream>

namespace ps1emu {

bool BiosImage::load_from_file(const std::string &path, std::string &error) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    error = "Unable to open BIOS file: " + path;
    return false;
  }

  std::streamsize size = file.tellg();
  if (size != static_cast<std::streamsize>(kExpectedSize)) {
    error = "Unexpected BIOS size (expected 512KB)";
    return false;
  }
  file.seekg(0, std::ios::beg);

  data_.resize(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char *>(data_.data()), size)) {
    error = "Failed to read BIOS data";
    data_.clear();
    return false;
  }

  is_hle_ = false;
  return true;
}

void BiosImage::load_hle_stub() {
  data_.assign(kExpectedSize, 0x00);
  const char *marker = "PS1EMU_HLE_BIOS";
  for (size_t i = 0; marker[i] != '\0' && i < data_.size(); ++i) {
    data_[i] = static_cast<uint8_t>(marker[i]);
  }
  is_hle_ = true;
}

bool BiosImage::valid() const {
  return data_.size() == kExpectedSize;
}

bool BiosImage::is_hle() const {
  return is_hle_;
}

const std::vector<uint8_t> &BiosImage::data() const {
  return data_;
}

uint8_t BiosImage::read8(size_t offset) const {
  if (offset >= data_.size()) {
    return 0xFF;
  }
  return data_[offset];
}

} // namespace ps1emu
