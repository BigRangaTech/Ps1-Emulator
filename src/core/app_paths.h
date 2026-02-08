#ifndef PS1EMU_APP_PATHS_H
#define PS1EMU_APP_PATHS_H

#include <string>

namespace ps1emu {

std::string app_data_dir();
bool ensure_directory(const std::string &path, std::string &error);

} // namespace ps1emu

#endif
