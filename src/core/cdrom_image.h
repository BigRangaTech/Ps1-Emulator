#ifndef PS1EMU_CDROM_IMAGE_H
#define PS1EMU_CDROM_IMAGE_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ps1emu {

class CdromImage {
public:
  bool load(const std::string &path, std::string &error);
  bool loaded() const;
  bool read_sector(uint32_t lba, std::vector<uint8_t> &out);
  uint32_t sector_size() const;
  uint32_t data_size() const;
  int32_t start_lba() const;
  uint32_t total_sectors() const;
  uint32_t end_lba() const;

private:
  struct TrackInfo {
    std::string path;
    uint32_t sector_size = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    int32_t start_lba = 0;
  };

  bool load_iso(const std::string &path, std::string &error);
  bool load_bin(const std::string &path, std::string &error);
  bool load_cue(const std::string &path, std::string &error);
  bool open_track_file(const std::string &path, std::string &error);

  static std::string to_lower(std::string value);
  static std::string trim(const std::string &value);

  TrackInfo track_;
  std::ifstream file_;
  uint64_t file_size_ = 0;
};

} // namespace ps1emu

#endif
