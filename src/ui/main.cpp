#include "core/config_paths.h"
#include "ui/text_ui.h"

#include <iostream>
#include <string>

static void print_usage() {
  std::cout << "Usage: ps1emu_ui [--config path]\n";
}

int main(int argc, char **argv) {
  std::string config_path = ps1emu::default_config_path();
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
  }

  ps1emu::EmulatorCore core;
  ps1emu::TextUi ui;
  if (!ui.run(core, config_path)) {
    std::cerr << "Failed to start UI.\n";
    return 1;
  }
  return 0;
}
