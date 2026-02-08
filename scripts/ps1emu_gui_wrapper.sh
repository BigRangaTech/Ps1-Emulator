#!/bin/sh

cfg=""

if [ -n "$PS1EMU_CONFIG" ]; then
  cfg="$PS1EMU_CONFIG"
elif [ -f "ps1emu.conf" ]; then
  cfg="ps1emu.conf"
else
  if [ -n "$XDG_CONFIG_HOME" ]; then
    cfg="$XDG_CONFIG_HOME/ps1emu/ps1emu.conf"
  else
    cfg="$HOME/.config/ps1emu/ps1emu.conf"
  fi

  if [ ! -f "$cfg" ]; then
    cfg_dir="$(dirname "$cfg")"
    mkdir -p "$cfg_dir"
    if [ -f "/app/share/ps1emu/ps1emu.conf" ]; then
      cp "/app/share/ps1emu/ps1emu.conf" "$cfg"
    fi
  fi
fi

exec ps1emu_gui --config "$cfg" "$@"
