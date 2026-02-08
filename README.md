# PS1 Emulator (Linux) — GPLv2+

This is a from-scratch PlayStation 1 emulator targeting Linux with a sandboxed, plugin-friendly architecture. The initial focus is on a fast/accurate balance with both interpreter and dynarec CPU cores.

## Goals
- Fast + accurate balance (configurable per subsystem)
- Sandboxed plugins (GPU, SPU, Input, CD-ROM)
- Clear separation of core emulation and frontend/IO
- GPLv2+ licensing

## Build
```bash
cmake -S . -B build
cmake --build build -j
```

## Run (stub)
```bash
./build/ps1emu --config ps1emu.conf
```

## Config
`ps1emu.conf` controls plugin paths, BIOS path, CPU mode, and sandbox limits.
If `bios.path` is empty, a minimal HLE BIOS stub is used.

## CPU Stub
Use `--cycles N` to execute a fixed number of interpreter steps for now.

## Status
Scaffold only: IPC, plugin launching, config, BIOS loader, memory map, and CPU skeleton are in place. GPU, SPU, CD-ROM emulation is not implemented yet.
