cmake_minimum_required(VERSION 3.20)
project(ps1emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PS1EMU_ENABLE_WERROR "Treat warnings as errors" OFF)

add_library(ps1emu_core
  src/core/bios.cpp
  src/core/config.cpp
  src/core/cpu.cpp
  src/core/dynarec.cpp
  src/core/emu_core.cpp
  src/core/memory_map.cpp
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

add_executable(ps1emu_gpu_stub plugins/gpu_stub/main.cpp)
add_executable(ps1emu_spu_stub plugins/spu_stub/main.cpp)
add_executable(ps1emu_input_stub plugins/input_stub/main.cpp)
add_executable(ps1emu_cdrom_stub plugins/cdrom_stub/main.cpp)

foreach(tgt ps1emu_gpu_stub ps1emu_spu_stub ps1emu_input_stub ps1emu_cdrom_stub)
  target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
