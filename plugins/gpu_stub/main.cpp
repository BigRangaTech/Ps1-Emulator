#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

static bool write_all(int fd, const void *data, size_t size) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  size_t written = 0;
  while (written < size) {
    ssize_t rc = write(fd, bytes + written, size - written);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    written += static_cast<size_t>(rc);
  }
  return true;
}

static bool read_exact(int fd, void *data, size_t size) {
  uint8_t *bytes = static_cast<uint8_t *>(data);
  size_t read_total = 0;
  while (read_total < size) {
    ssize_t rc = read(fd, bytes + read_total, size - read_total);
    if (rc == 0) {
      return false;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    read_total += static_cast<size_t>(rc);
  }
  return true;
}

static bool read_line_fd(std::string &out) {
  static std::string buffer;
  for (;;) {
    size_t pos = buffer.find('\n');
    if (pos != std::string::npos) {
      out = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      return true;
    }
    char temp[256];
    ssize_t rc = read(STDIN_FILENO, temp, sizeof(temp));
    if (rc == 0) {
      return false;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    buffer.append(temp, static_cast<size_t>(rc));
  }
}

static bool write_line_fd(const std::string &line) {
  std::string data = line;
  data.push_back('\n');
  return write_all(STDOUT_FILENO, data.data(), data.size());
}

static bool read_frame(uint16_t &out_type, std::vector<uint8_t> &out_payload) {
  uint8_t header[8];
  if (!read_exact(STDIN_FILENO, header, sizeof(header))) {
    return false;
  }

  uint32_t length = static_cast<uint32_t>(header[0]) |
                    (static_cast<uint32_t>(header[1]) << 8) |
                    (static_cast<uint32_t>(header[2]) << 16) |
                    (static_cast<uint32_t>(header[3]) << 24);
  out_type = static_cast<uint16_t>(header[4]) |
             (static_cast<uint16_t>(header[5]) << 8);

  if (length > 16 * 1024 * 1024) {
    return false;
  }
  out_payload.resize(length);
  if (length == 0) {
    return true;
  }
  return read_exact(STDIN_FILENO, out_payload.data(), length);
}

static bool write_frame(uint16_t type, const std::vector<uint8_t> &payload) {
  uint32_t length = static_cast<uint32_t>(payload.size());
  uint16_t flags = 0;
  uint8_t header[8];
  header[0] = static_cast<uint8_t>(length & 0xFF);
  header[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
  header[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
  header[3] = static_cast<uint8_t>((length >> 24) & 0xFF);
  header[4] = static_cast<uint8_t>(type & 0xFF);
  header[5] = static_cast<uint8_t>((type >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>(flags & 0xFF);
  header[7] = static_cast<uint8_t>((flags >> 8) & 0xFF);

  if (!write_all(STDOUT_FILENO, header, sizeof(header))) {
    return false;
  }
  if (payload.empty()) {
    return true;
  }
  return write_all(STDOUT_FILENO, payload.data(), payload.size());
}

int main() {
  std::string line;
  if (!read_line_fd(line)) {
    return 1;
  }

  if (line == "HELLO GPU 1") {
    if (!write_line_fd("READY GPU 1")) {
      return 1;
    }
  } else {
    write_line_fd("ERROR");
    return 1;
  }

  while (read_line_fd(line)) {
    if (line == "PING") {
      write_line_fd("PONG");
      continue;
    }
    if (line == "FRAME_MODE") {
      write_line_fd("FRAME_READY");
      break;
    }
    if (line == "SHUTDOWN") {
      break;
    }
    write_line_fd("ERROR");
  }

  for (;;) {
    uint16_t type = 0;
    std::vector<uint8_t> payload;
    if (!read_frame(type, payload)) {
      break;
    }
    if (type == 0x0001) {
      uint32_t count = static_cast<uint32_t>(payload.size() / 4);
      std::vector<uint8_t> ack(4);
      ack[0] = static_cast<uint8_t>(count & 0xFF);
      ack[1] = static_cast<uint8_t>((count >> 8) & 0xFF);
      ack[2] = static_cast<uint8_t>((count >> 16) & 0xFF);
      ack[3] = static_cast<uint8_t>((count >> 24) & 0xFF);
      write_frame(0x0002, ack);
      continue;
    }
    write_frame(0x0002, {});
  }

  return 0;
}
