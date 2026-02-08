#include "plugins/ipc.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>
#ifdef __linux__
#include <linux/seccomp.h>
#include <sys/prctl.h>
#endif

namespace ps1emu {

IpcChannel::IpcChannel(int read_fd, int write_fd) : read_fd_(read_fd), write_fd_(write_fd) {}

IpcChannel::~IpcChannel() {
  if (read_fd_ >= 0) {
    close(read_fd_);
  }
  if (write_fd_ >= 0 && write_fd_ != read_fd_) {
    close(write_fd_);
  }
}

IpcChannel::IpcChannel(IpcChannel &&other) noexcept {
  read_fd_ = other.read_fd_;
  write_fd_ = other.write_fd_;
  read_buffer_ = std::move(other.read_buffer_);
  other.read_fd_ = -1;
  other.write_fd_ = -1;
}

IpcChannel &IpcChannel::operator=(IpcChannel &&other) noexcept {
  if (this != &other) {
    if (read_fd_ >= 0) {
      close(read_fd_);
    }
    if (write_fd_ >= 0 && write_fd_ != read_fd_) {
      close(write_fd_);
    }
    read_fd_ = other.read_fd_;
    write_fd_ = other.write_fd_;
    read_buffer_ = std::move(other.read_buffer_);
    other.read_fd_ = -1;
    other.write_fd_ = -1;
  }
  return *this;
}

bool IpcChannel::valid() const {
  return read_fd_ >= 0 && write_fd_ >= 0;
}

static bool write_all(int fd, const char *data, size_t size) {
  size_t written = 0;
  while (written < size) {
    ssize_t rc = write(fd, data + written, size - written);
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

static bool read_exact(int fd, char *data, size_t size) {
  size_t read_total = 0;
  while (read_total < size) {
    ssize_t rc = read(fd, data + read_total, size - read_total);
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

bool IpcChannel::send_line(const std::string &line) {
  if (!valid()) {
    return false;
  }
  std::string data = line;
  data.push_back('\n');
  return write_all(write_fd_, data.data(), data.size());
}

bool IpcChannel::recv_line(std::string &out_line) {
  if (!valid()) {
    return false;
  }
  for (;;) {
    size_t pos = read_buffer_.find('\n');
    if (pos != std::string::npos) {
      out_line = read_buffer_.substr(0, pos);
      read_buffer_.erase(0, pos + 1);
      return true;
    }

    char buf[256];
    ssize_t rc = read(read_fd_, buf, sizeof(buf));
    if (rc == 0) {
      return false;
    }
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    read_buffer_.append(buf, static_cast<size_t>(rc));
  }
}

bool IpcChannel::send_frame(uint16_t type, const std::vector<uint8_t> &payload) {
  if (!valid()) {
    return false;
  }

  constexpr uint32_t kMaxPayload = 16 * 1024 * 1024;
  if (payload.size() > kMaxPayload) {
    return false;
  }

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

  if (!write_all(write_fd_, reinterpret_cast<const char *>(header), sizeof(header))) {
    return false;
  }
  if (payload.empty()) {
    return true;
  }
  return write_all(write_fd_, reinterpret_cast<const char *>(payload.data()), payload.size());
}

bool IpcChannel::recv_frame(uint16_t &out_type, std::vector<uint8_t> &out_payload) {
  if (!valid()) {
    return false;
  }

  uint8_t header[8];
  if (!read_exact(read_fd_, reinterpret_cast<char *>(header), sizeof(header))) {
    return false;
  }

  uint32_t length = static_cast<uint32_t>(header[0]) |
                    (static_cast<uint32_t>(header[1]) << 8) |
                    (static_cast<uint32_t>(header[2]) << 16) |
                    (static_cast<uint32_t>(header[3]) << 24);
  out_type = static_cast<uint16_t>(header[4]) |
             (static_cast<uint16_t>(header[5]) << 8);

  constexpr uint32_t kMaxPayload = 16 * 1024 * 1024;
  if (length > kMaxPayload) {
    return false;
  }

  out_payload.resize(length);
  if (length == 0) {
    return true;
  }
  return read_exact(read_fd_, reinterpret_cast<char *>(out_payload.data()), length);
}

static void set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags >= 0) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
}

static void apply_resource_limits(const SandboxOptions &sandbox) {
  if (!sandbox.enabled) {
    return;
  }

  if (sandbox.rlimit_cpu_seconds > 0) {
    struct rlimit lim {};
    lim.rlim_cur = static_cast<rlim_t>(sandbox.rlimit_cpu_seconds);
    lim.rlim_max = lim.rlim_cur;
    setrlimit(RLIMIT_CPU, &lim);
  }

  if (sandbox.rlimit_as_mb > 0) {
    struct rlimit lim {};
    lim.rlim_cur = static_cast<rlim_t>(sandbox.rlimit_as_mb) * 1024 * 1024;
    lim.rlim_max = lim.rlim_cur;
    setrlimit(RLIMIT_AS, &lim);
  }

  if (sandbox.rlimit_nofile > 0) {
    struct rlimit lim {};
    lim.rlim_cur = static_cast<rlim_t>(sandbox.rlimit_nofile);
    lim.rlim_max = lim.rlim_cur;
    setrlimit(RLIMIT_NOFILE, &lim);
  }
}

static void apply_linux_sandbox(const SandboxOptions &sandbox) {
#ifdef __linux__
  if (!sandbox.enabled) {
    return;
  }

  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  prctl(PR_SET_DUMPABLE, 0);

  if (sandbox.seccomp_strict) {
    prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT);
  }
#else
  (void)sandbox;
#endif
}

SpawnResult spawn_plugin_process(const std::string &path,
                                 const std::vector<std::string> &args,
                                 const SandboxOptions &sandbox) {
  int to_child[2] = {-1, -1};
  int from_child[2] = {-1, -1};

  if (pipe(to_child) != 0) {
    return {};
  }
  if (pipe(from_child) != 0) {
    close(to_child[0]);
    close(to_child[1]);
    return {};
  }

  set_cloexec(to_child[0]);
  set_cloexec(to_child[1]);
  set_cloexec(from_child[0]);
  set_cloexec(from_child[1]);

  pid_t pid = fork();
  if (pid < 0) {
    close(to_child[0]);
    close(to_child[1]);
    close(from_child[0]);
    close(from_child[1]);
    return {};
  }

  if (pid == 0) {
    dup2(to_child[0], STDIN_FILENO);
    dup2(from_child[1], STDOUT_FILENO);

    close(to_child[0]);
    close(to_child[1]);
    close(from_child[0]);
    close(from_child[1]);

    apply_resource_limits(sandbox);
    apply_linux_sandbox(sandbox);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char *>(path.c_str()));
    for (const auto &arg : args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execv(path.c_str(), argv.data());
    _exit(127);
  }

  close(to_child[0]);
  close(from_child[1]);

  IpcChannel channel(from_child[0], to_child[1]);
  return {static_cast<int>(pid), std::move(channel)};
}

} // namespace ps1emu
