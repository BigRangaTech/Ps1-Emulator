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
- `docs/testing.md`

## Configuration
- Default config: `ps1emu.conf`
- Set plugin paths and BIOS path there.

## Notes
- Plugins are separate processes and communicate via line-based IPC.
- The IPC protocol supports text control messages and a binary framed layer.
- BIOS files are not included in the repo. If none is configured, a minimal HLE BIOS stub is used.
- The GPU stub switches to framed mode and accepts a command buffer frame.
- A simple text UI is available via `ps1emu_ui`.
- An SDL2 GUI is available via `ps1emu_gui` when SDL2/SDL2_ttf are installed.
- Flatpak manifest lives at `flatpak/org.ps1emu.PS1Emu.yml`.
- Desktop entry: `assets/org.ps1emu.PS1Emu.desktop` (uses `ps1emu_gui_wrapper`).

## TODO
- GPU: verify GPUSTAT timing (ready/busy, DMA request) against real hardware.
- GPU: tighten DMA pacing and GPUREAD/VRAM transfer timing.
- GPU: verify 24-bit display mapping against real hardware behavior.
- GPU: validate GPU->CPU DMA transfers with real BIOS/ROM paths.
- GPU: confirm 24-bit horizontal scaling and display range mapping with known test ROMs.
- GPU: validate GP0/GP1 FIFO busy penalties vs real hardware stall behavior.
- GPU: validate VRAM transfer mask behavior with hardware tests.
- GPU: validate linked-list DMA behavior with real BIOS/ROM command chains.
- GPU: validate interlace field cadence and display range interaction.
- GPU: validate DMA backpressure draining behavior against real timing.
- Tests: add coverage for each module as new features land (CPU/GPU/CD-ROM/SPU/Input).
- CD-ROM: finalize command semantics (GetID/ReadTOC/Seek timing), raw sector modes, XA audio stub, and IRQ/DRQ gating.
- SPU: mixing + ADPCM decode, timing and IRQs.
- Input: controller polling with mapping layer, hotplug.
- Boot: BIOS logo + shell boot, load first game scene.
