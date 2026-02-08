cmake_minimum_required(VERSION 3.20)
project(ps1emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PS1EMU_ENABLE_WERROR "Treat warnings as errors" OFF)

add_library(ps1emu_core
  src/core/app_paths.cpp
  src/core/bios.cpp
  src/core/config.cpp
  src/core/config_paths.cpp
  src/core/cpu.cpp
  src/core/dynarec.cpp
  src/core/emu_core.cpp
  src/core/gpu_commands.cpp
  src/core/gpu_packets.cpp
  src/core/memory_map.cpp
  src/core/mmio.cpp
  src/core/scheduler.cpp
  src/plugins/ipc.cpp
  src/plugins/plugin_host.cpp
)

target_include_directories(ps1emu_core PUBLIC include src)

target_compile_options(ps1emu_core PRIVATE -Wall -Wextra -Wpedantic)
if(PS1EMU_ENABLE_WERROR)
  target_compile_options(ps1emu_core PRIVATE -Werror)
endif()

add_executable(ps1emu src/main.cpp)

target_link_libraries(ps1emu PRIVATE ps1emu_core)

target_compile_options(ps1emu PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ps1emu_tests tests/emulator_tests.cpp)
target_link_libraries(ps1emu_tests PRIVATE ps1emu_core)
target_compile_options(ps1emu_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ps1emu_gpu_stub plugins/gpu_stub/main.cpp)
add_executable(ps1emu_spu_stub plugins/spu_stub/main.cpp)
add_executable(ps1emu_input_stub plugins/input_stub/main.cpp)
add_executable(ps1emu_cdrom_stub plugins/cdrom_stub/main.cpp)

foreach(tgt ps1emu_gpu_stub ps1emu_spu_stub ps1emu_input_stub ps1emu_cdrom_stub)
  target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

add_executable(ps1emu_ui src/ui/text_ui.cpp src/ui/main.cpp)
target_link_libraries(ps1emu_ui PRIVATE ps1emu_core)
target_compile_options(ps1emu_ui PRIVATE -Wall -Wextra -Wpedantic)

option(PS1EMU_ENABLE_GUI "Build SDL2 GUI" ON)
if(PS1EMU_ENABLE_GUI)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    pkg_check_modules(SDL2TTF QUIET SDL2_ttf)
  endif()

  if(SDL2_FOUND AND SDL2TTF_FOUND)
    add_executable(ps1emu_gui src/ui/gui_app.cpp src/ui/gui_main.cpp)
    target_include_directories(ps1emu_gui PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS} src)
    target_link_libraries(ps1emu_gui PRIVATE ps1emu_core ${SDL2_LIBRARIES} ${SDL2TTF_LIBRARIES})
    target_compile_options(ps1emu_gui PRIVATE -Wall -Wextra -Wpedantic ${SDL2_CFLAGS_OTHER} ${SDL2TTF_CFLAGS_OTHER})
  else()
    message(WARNING "SDL2/SDL2_ttf not found; ps1emu_gui will not be built.")
  endif()
endif()

install(TARGETS ps1emu ps1emu_ui
  RUNTIME DESTINATION bin
)

if(TARGET ps1emu_gui)
  install(TARGETS ps1emu_gui
    RUNTIME DESTINATION bin
  )
endif()

install(FILES ps1emu.conf DESTINATION share/ps1emu)
install(DIRECTORY assets/ DESTINATION share/ps1emu/assets)
install(FILES README.md LICENSE DESTINATION share/doc/ps1emu)
install(FILES docs/architecture.md docs/plugin_protocol.md docs/ui_ux.md docs/gui_setup.md
        DESTINATION share/doc/ps1emu)

install(PROGRAMS scripts/ps1emu_gui_wrapper.sh DESTINATION bin RENAME ps1emu_gui_wrapper)
install(FILES assets/org.ps1emu.PS1Emu.desktop DESTINATION share/applications)
install(FILES assets/icons/org.ps1emu.PS1Emu.svg DESTINATION share/icons/hicolor/scalable/apps)
