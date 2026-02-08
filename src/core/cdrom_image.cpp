#include "core/cdrom_image.h"

#include <cctype>
#include <filesystem>
#include <sstream>

namespace ps1emu {

static bool parse_bcd_time(const std::string &token, int &mm, int &ss, int &ff) {
  mm = 0;
  ss = 0;
  ff = 0;
  int parts[3] = {0, 0, 0};
  size_t start = 0;
  for (int i = 0; i < 3; ++i) {
    size_t colon = token.find(':', start);
    std::string part = (colon == std::string::npos) ? token.substr(start) :
                                                      token.substr(start, colon - start);
    if (part.empty()) {
      return false;
    }
    try {
      parts[i] = std::stoi(part);
    } catch (...) {
      return false;
    }
    if (colon == std::string::npos) {
      if (i < 2) {
        return false;
      }
      break;
    }
    start = colon + 1;
  }
  mm = parts[0];
  ss = parts[1];
  ff = parts[2];
  return true;
}

std::string CdromImage::trim(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string CdromImage::to_lower(std::string value) {
  for (char &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool CdromImage::loaded() const {
  return file_.is_open() && track_.sector_size > 0;
}

bool CdromImage::open_track_file(const std::string &path, std::string &error) {
  file_.close();
  file_.clear();
  file_.open(path, std::ios::binary);
  if (!file_.is_open()) {
    error = "Unable to open CD-ROM image: " + path;
    return false;
  }
  file_.seekg(0, std::ios::end);
  file_size_ = static_cast<uint64_t>(file_.tellg());
  file_.seekg(0, std::ios::beg);
  track_.path = path;
  return true;
}

bool CdromImage::load_iso(const std::string &path, std::string &error) {
  track_ = {};
  track_.sector_size = 2048;
  track_.data_offset = 0;
  track_.data_size = 2048;
  track_.start_lba = 0;
  return open_track_file(path, error);
}

static bool select_bin_layout(uint64_t file_size, uint32_t &sector_size, uint32_t &data_offset) {
  bool divisible_2352 = (file_size % 2352u) == 0;
  bool divisible_2048 = (file_size % 2048u) == 0;
  if (divisible_2352 && !divisible_2048) {
    sector_size = 2352;
    data_offset = 24;
    return true;
  }
  if (divisible_2048 && !divisible_2352) {
    sector_size = 2048;
    data_offset = 0;
    return true;
  }
  if (divisible_2352) {
    sector_size = 2352;
    data_offset = 24;
    return true;
  }
  return false;
}

bool CdromImage::load_bin(const std::string &path, std::string &error) {
  track_ = {};
  if (!open_track_file(path, error)) {
    return false;
  }
  uint32_t sector_size = 0;
  uint32_t data_offset = 0;
  if (!select_bin_layout(file_size_, sector_size, data_offset)) {
    error = "Unrecognized BIN image size";
    return false;
  }
  track_.sector_size = sector_size;
  track_.data_offset = data_offset;
  track_.data_size = 2048;
  track_.start_lba = 0;
  return true;
}

bool CdromImage::load_cue(const std::string &path, std::string &error) {
  track_ = {};
  std::ifstream cue(path);
  if (!cue.is_open()) {
    error = "Unable to open CUE file: " + path;
    return false;
  }

  std::string file_ref;
  std::string track_mode;
  int index_mm = 0;
  int index_ss = 0;
  int index_ff = 0;
  bool have_index = false;

  std::string line;
  while (std::getline(cue, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    std::istringstream iss(trimmed);
    std::string keyword;
    iss >> keyword;
    keyword = to_lower(keyword);
    if (keyword == "file") {
      size_t first = trimmed.find('"');
      size_t second = (first == std::string::npos) ? std::string::npos : trimmed.find('"', first + 1);
      if (first != std::string::npos && second != std::string::npos) {
        file_ref = trimmed.substr(first + 1, second - first - 1);
      } else {
        std::string name;
        iss >> name;
        file_ref = name;
      }
    } else if (keyword == "track") {
      std::string number;
      iss >> number;
      iss >> track_mode;
      track_mode = to_lower(track_mode);
    } else if (keyword == "index") {
      std::string index_no;
      std::string time;
      iss >> index_no >> time;
      if (index_no == "01") {
        have_index = parse_bcd_time(time, index_mm, index_ss, index_ff);
      }
    }
  }

  if (file_ref.empty()) {
    error = "CUE missing FILE entry";
    return false;
  }

  uint32_t sector_size = 2352;
  uint32_t data_offset = 24;
  if (track_mode.find("2048") != std::string::npos) {
    sector_size = 2048;
    data_offset = 0;
  } else if (track_mode.find("mode1") != std::string::npos) {
    sector_size = 2352;
    data_offset = 16;
  } else if (track_mode.find("mode2") != std::string::npos) {
    sector_size = 2352;
    data_offset = 24;
  }

  int32_t start_lba = 0;
  if (have_index) {
    start_lba = static_cast<int32_t>((index_mm * 60 + index_ss) * 75 + index_ff) - 150;
  }

  std::filesystem::path cue_path(path);
  std::filesystem::path data_path = cue_path.parent_path() / file_ref;

  track_.sector_size = sector_size;
  track_.data_offset = data_offset;
  track_.data_size = 2048;
  track_.start_lba = start_lba;
  return open_track_file(data_path.string(), error);
}

bool CdromImage::load(const std::string &path, std::string &error) {
  std::filesystem::path p(path);
  std::string ext = to_lower(p.extension().string());
  if (ext == ".iso") {
    return load_iso(path, error);
  }
  if (ext == ".cue") {
    return load_cue(path, error);
  }
  if (ext == ".bin") {
    return load_bin(path, error);
  }
  error = "Unsupported CD-ROM image type";
  return false;
}

bool CdromImage::read_sector(uint32_t lba, std::vector<uint8_t> &out) {
  if (!loaded()) {
    return false;
  }

  int64_t sector_index = static_cast<int64_t>(lba) - static_cast<int64_t>(track_.start_lba);
  if (sector_index < 0) {
    return false;
  }

  uint64_t offset = static_cast<uint64_t>(sector_index) * track_.sector_size + track_.data_offset;
  if (offset + track_.data_size > file_size_) {
    return false;
  }

  out.resize(track_.data_size);
  file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  file_.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(track_.data_size));
  return file_.gcount() == static_cast<std::streamsize>(track_.data_size);
}

} // namespace ps1emu
