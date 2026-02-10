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

## Text UI
```bash
./build/ps1emu_ui --config ps1emu.conf
```

## GUI (SDL2)
The GUI builds when SDL2 and SDL2_ttf are available.
```bash
./build/ps1emu_gui --config ps1emu.conf
```
Use the Settings view to edit and save the BIOS path.
Use **Browse BIOS** to open the built-in BIOS picker (scans `./Bios` and `./bios`).
Use **Import BIOS** to copy a BIOS into the app data folder for Flatpak-safe access.

## Flatpak
Manifest: `flatpak/org.ps1emu.PS1Emu.yml`
The manifest runs `ps1emu_gui_wrapper`, which creates a writable config under XDG config.

## Testing
See `docs/testing.md` for test guidance and GPU frame dump calibration.

## Config
`ps1emu.conf` controls plugin paths, BIOS path, CPU mode, and sandbox limits.
If `bios.path` is empty, a minimal HLE BIOS stub is used.

## CPU Stub
Use `--cycles N` to execute a fixed number of interpreter steps for now.

## Status
Scaffold plus early core: IPC, plugin launching, config, BIOS loader, memory map, CPU interpreter/dynarec skeleton, and a growing GTE. The GPU stub now handles GP0/GP1 packets, basic rendering (polygons/rects/lines), texture sampling, masking, dithering, semi-transparency, draw-to-display gating, GPUSTAT timing approximations, and display modes (including a best-effort 24-bit output path). SPU/CD-ROM/Input remain stub-level.
