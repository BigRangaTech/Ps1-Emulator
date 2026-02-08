#include "core/emu_core.h"

#include <iostream>
#include <string>

static void print_usage() {
  std::cout << "Usage: ps1emu [--config path] [--cycles N]\n";
}

int main(int argc, char **argv) {
  std::string config_path = "ps1emu.conf";
  uint32_t run_cycles = 0;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
      continue;
    }
    if (arg == "--cycles" && i + 1 < argc) {
      run_cycles = static_cast<uint32_t>(std::stoul(argv[++i]));
      continue;
    }
  }

  ps1emu::EmulatorCore core;
  if (!core.initialize(config_path)) {
    std::cerr << "Initialization failed\n";
    return 1;
  }

  std::cout << "PS1 emulator core initialized (stub).\n";
  if (run_cycles > 0) {
    core.run_for_cycles(run_cycles);
    std::cout << "Executed " << run_cycles << " CPU cycles (stub).\n";
  }
  core.shutdown();
  return 0;
}
