#ifndef PS1EMU_PLUGIN_API_H
#define PS1EMU_PLUGIN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define PS1EMU_PLUGIN_API_VERSION 1

typedef enum ps1emu_plugin_type {
  PS1EMU_PLUGIN_GPU = 1,
  PS1EMU_PLUGIN_SPU = 2,
  PS1EMU_PLUGIN_INPUT = 3,
  PS1EMU_PLUGIN_CDROM = 4
} ps1emu_plugin_type_t;

typedef struct ps1emu_plugin_info {
  int api_version;
  ps1emu_plugin_type_t type;
  const char *name;
} ps1emu_plugin_info_t;

#ifdef __cplusplus
}
#endif

#endif
