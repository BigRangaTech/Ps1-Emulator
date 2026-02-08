# PS1 Emulator Agent Notes

## Overview
This is a from-scratch PlayStation 1 emulator for Linux, written in C++17 and licensed under GPLv2+. The codebase is currently a scaffold with a sandboxed plugin host, IPC protocol, config loader, BIOS loader, memory map, and CPU skeleton.

## Build
```bash
cmake -S . -B build
cmake --build build -j
```

## Run
```bash
./build/ps1emu --config ps1emu.conf
```

## Key Docs
- `docs/architecture.md`
- `docs/plugin_protocol.md`

## Configuration
- Default config: `ps1emu.conf`
- Set plugin paths and BIOS path there.

## Notes
- Plugins are separate processes and communicate via line-based IPC.
- The IPC protocol supports text control messages and a binary framed layer.
- BIOS files are not included in the repo. If none is configured, a minimal HLE BIOS stub is used.
- The GPU stub switches to framed mode and accepts a command buffer frame.
