# PS1 Emulator Roadmap

## Near-Term (Core Correctness)
- CPU interpreter: full MIPS I decode/execute, exceptions, delay slots.
- Memory map + MMIO: GPU, SPU, CD-ROM, DMA, timers, interrupts, IO ports.
- BIOS/HLE: minimal syscall handling to reach boot, or require real BIOS.

## Medium-Term (Playable Loop)
- GPU command decoder + basic rasterizer backend (start with software renderer).
- CD-ROM: ISO/CUE parsing, sector timing, XA audio stub.
- Input: controller polling with mapping layer.

## Performance & Stability
- Dynarec backend (block compiler + invalidation on memory writes).
- Timing scheduler: accurate CPU/GPU/SPU cycle alignment.
- Save states and deterministic replay.

## UX & Tooling
- GUI: game library scanning, per-game settings, BIOS management.
- Logging/profiling UI: FPS, CPU load, cache hit rate, GPU throughput.
- Test harness: CPU instruction tests + BIOS boot tests.
