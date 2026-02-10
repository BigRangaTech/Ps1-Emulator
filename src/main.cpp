#include "core/config_paths.h"
#include "core/emu_core.h"

#include <iostream>
#include <string>

static void print_usage() {
  std::cout << "Usage: ps1emu [--config path] [--cycles N] [--frames N] [--trace]\n";
  std::cout << "             [--trace-period N] [--watchdog] [--dump-dynarec]\n";
  std::cout << "             [--dump-ram addr words]\n";
}

int main(int argc, char **argv) {
  std::string config_path = ps1emu::default_config_path();
  uint32_t run_cycles = 0;
  uint32_t run_frames = 0;
  uint32_t frame_cycles = 33868800 / 60;
  bool dump_dynarec = false;
  bool trace_enabled = false;
  bool watchdog_enabled = false;
  uint32_t trace_period = 1000000;
  bool dump_ram = false;
  uint32_t dump_ram_addr = 0;
  uint32_t dump_ram_words = 0;
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
    if (arg == "--frames" && i + 1 < argc) {
      run_frames = static_cast<uint32_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--frame-cycles" && i + 1 < argc) {
      frame_cycles = static_cast<uint32_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--trace") {
      trace_enabled = true;
      continue;
    }
    if (arg == "--trace-period" && i + 1 < argc) {
      trace_period = static_cast<uint32_t>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--watchdog") {
      watchdog_enabled = true;
      continue;
    }
    if (arg == "--dump-ram" && i + 2 < argc) {
      dump_ram_addr = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 0));
      dump_ram_words = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 0));
      dump_ram = true;
      continue;
    }
    if (arg == "--dump-dynarec") {
      dump_dynarec = true;
      continue;
    }
  }

  ps1emu::EmulatorCore core;
  if (!core.initialize(config_path)) {
    std::cerr << "Initialization failed\n";
    return 1;
  }

  core.set_trace_enabled(trace_enabled);
  core.set_trace_period_cycles(trace_period);
  core.set_watchdog_enabled(watchdog_enabled);

  std::cout << "PS1 emulator core initialized (stub).\n";
  if (run_cycles > 0) {
    core.run_for_cycles(run_cycles);
    std::cout << "Executed " << run_cycles << " CPU cycles (stub).\n";
    if (dump_dynarec) {
      core.dump_dynarec_profile();
    }
  } else if (run_frames > 0) {
    for (uint32_t i = 0; i < run_frames; ++i) {
      core.run_for_cycles(frame_cycles);
    }
    std::cout << "Executed " << run_frames << " frames at " << frame_cycles << " cycles/frame.\n";
    if (dump_dynarec) {
      core.dump_dynarec_profile();
    }
  }
  if (dump_ram) {
    core.dump_memory_words(dump_ram_addr, dump_ram_words);
  }
  core.shutdown();
  return 0;
}
