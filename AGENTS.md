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
- GPU: verify GPUSTAT timing (ready/DMA request now FIFO-based; busy no longer gates ready bits) against real hardware.
- GPU: tighten DMA pacing and GPUREAD/VRAM transfer timing (busy penalties scaled down; GP0/GP1 penalty=1; DMA busy scaled by /32).
- GPU: verify 24-bit display mapping against real hardware behavior.
- GPU: validate GPU->CPU DMA transfers with real BIOS/ROM paths.
- GPU: confirm 24-bit horizontal scaling and display range mapping with known test ROMs.
- GPU: validate GP0/GP1 FIFO busy penalties vs real hardware stall behavior (FIFO limit now 16; ready/DMA request tests cover FIFO gating).
- GPU: validate VRAM transfer mask behavior with hardware tests.
- GPU: validate linked-list DMA behavior with real BIOS/ROM command chains.
- GPU: validate interlace field cadence and display range interaction.
- GPU: validate DMA backpressure draining behavior against real timing.
- DMA: honor DPCR priority ordering (currently services pending channels in index order).
- Timers: verify root counter sync/clock source edge cases against hardware (blank-reset + cycle-accum resets added; 32-bit timer read/write supported; IRQ request/toggle semantics + mode-flag read clearing now implemented with tests; repeat-no-toggle IRQ test added; timer0/2 divisors (/8,/32,/128) implemented; dotclock still approximated).
- Timers: confirm BIOS tick at 0x00089DDC advances (use `PS1EMU_WATCH_PHYS`; currently only initial writes observed).
- Tests: add coverage for each module as new features land (CPU/GPU/CD-ROM/SPU/Input).
- CD-ROM: validate command semantics (GetID/ReadTOC/Seek timing), raw sector modes, XA audio stub, and IRQ/DRQ gating (command/IRQ cadence delayed + IRQ enable gating + index1 IRQ ack tests; BIOS still not issuing CD-ROM commands in current boot trace).
- CD-ROM: validate XA filter behavior and subheader parsing against known test discs; add real ADPCM decode.
- CD-ROM: validate XA 8-bit ADPCM behavior and channel interleave accuracy against real discs.
- SPU: wire PCM mixer into a real audio backend (SDL2) and implement volume/mix controls.
- SPU: mixing + ADPCM decode, timing and IRQs.
- Input: controller polling with mapping layer, hotplug (SIO0 byte delay + DTR gating + IRQ rearm on I_STAT clear + DSR/ACK delay pulse implemented).
- Boot: BIOS logo + shell boot, load first game scene.
