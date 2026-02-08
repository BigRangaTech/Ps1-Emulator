# PS1 Emulator Roadmap

## Near-Term (Core Correctness)
**Phase 1 Bootable**
- CPU interpreter: full MIPS I decode/execute, exceptions, delay slots.
- Memory map + MMIO: GPU, SPU, CD-ROM, DMA, timers, interrupts, IO ports.
- BIOS/HLE: minimal syscall handling to reach boot, or require real BIOS.
- CD-ROM: full command set, sector timing, XA audio stub, DMA pacing.
- Input: controller polling with mapping layer, hotplug.
- Boot to menu: BIOS logo + shell boot, load first game scene.
**Phase 2 Visuals**
- GPU: completeness for GP0/GP1 commands, texture formats, page flips, display accuracy.
- GPU visuals: texture modulation, CLUT + texture window accuracy, UV wrapping, mask bit + mask test.
- GPU visuals: dithering, semi-transparency rules for all primitive types.
- GPU visuals: GPUSTAT correctness, VRAM readback (CPU->GPU and GPU->CPU transfers).
- GPU visuals: display timing details (PAL/NTSC, interlace fields, range->resolution mapping).
**Phase 3 Audio**
- SPU: basic mixing + ADPCM decode stub, timing and IRQs.

## Medium-Term (Playable Loop)

## Performance & Stability
- Dynarec backend (block compiler + invalidation on memory writes).
- Timing scheduler: accurate CPU/GPU/SPU/CDROM cycle alignment.
- Save states and deterministic replay.
- Regression tests: GPU command fixtures + CD-ROM command tests.

## UX & Tooling
- GUI: game library scanning, per-game settings, BIOS + disc management.
- Logging/profiling UI: FPS, CPU load, cache hit rate, GPU throughput.
- Test harness: CPU instruction tests + BIOS boot tests + render snapshots.
