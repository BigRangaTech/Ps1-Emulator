#ifndef PS1EMU_TEXT_UI_H
#define PS1EMU_TEXT_UI_H

#include "core/emu_core.h"

#include <string>

namespace ps1emu {

class TextUi {
public:
  bool run(EmulatorCore &core, const std::string &config_path);

private:
  void print_header();
  void print_menu();
};

} // namespace ps1emu

#endif
