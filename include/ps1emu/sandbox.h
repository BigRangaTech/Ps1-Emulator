#ifndef PS1EMU_SANDBOX_H
#define PS1EMU_SANDBOX_H

namespace ps1emu {

struct SandboxOptions {
  bool enabled = true;
  bool seccomp_strict = false;
  int rlimit_cpu_seconds = 2;
  int rlimit_as_mb = 512;
  int rlimit_nofile = 64;
};

} // namespace ps1emu

#endif
