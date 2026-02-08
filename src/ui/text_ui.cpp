#include "ui/text_ui.h"

#include <iostream>

namespace ps1emu {

void TextUi::print_header() {
  std::cout << "PS1 Emulator (Text UI)\n";
  std::cout << "Type a number and press Enter.\n\n";
}

void TextUi::print_menu() {
  std::cout << "1. Run 60 cycles\n";
  std::cout << "2. Run 1000 cycles\n";
  std::cout << "3. Dump dynarec profile\n";
  std::cout << "4. Quit\n";
  std::cout << "> ";
}

bool TextUi::run(EmulatorCore &core, const std::string &config_path) {
  if (!core.initialize(config_path)) {
    return false;
  }

  print_header();
  for (;;) {
    print_menu();
    std::string input;
    if (!std::getline(std::cin, input)) {
      break;
    }

    if (input == "1") {
      core.run_for_cycles(60);
      std::cout << "Executed 60 cycles.\n";
      continue;
    }
    if (input == "2") {
      core.run_for_cycles(1000);
      std::cout << "Executed 1000 cycles.\n";
      continue;
    }
    if (input == "3") {
      core.dump_dynarec_profile();
      continue;
    }
    if (input == "4" || input == "q" || input == "quit") {
      break;
    }

    std::cout << "Unknown option.\n";
  }

  core.shutdown();
  return true;
}

} // namespace ps1emu
