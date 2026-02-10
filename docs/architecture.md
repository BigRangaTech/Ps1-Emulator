# Architecture Overview

## Goals
- Balance accuracy and speed via selectable CPU cores (interpreter + dynarec)
- Sandboxed plugins for GPU, SPU, Input, CD-ROM
- Stable plugin API and protocol
- Linux-first, GPLv2+

## Process Model
- `ps1emu` is the host process.
- Each plugin is a separate process (sandboxed).
- IPC is line-based for control and switches to framed binary mode for GPU bulk data.
- Later: replace IPC transport with a shared-memory ring or Cap'n Proto.

## Core Responsibilities
- CPU: interpreter + dynarec
- Memory map + MMIO (includes GPUSTAT tracking and GP0/GP1 register latches)
- Scheduler + timing (DMA, timers, approximate GPU field timing)
- BIOS loading

## Plugin Responsibilities
- GPU: GP0/GP1 command processor, VRAM model, display output
- SPU: audio synthesis
- Input: controller polling
- CD-ROM: disc image I/O and XA decoding

## Security
- Plugins are separate processes.
- Planned: seccomp filters, namespaces, and resource limits per plugin.
- Crash isolation: host can restart or replace plugins.

## Configuration
- `ps1emu.conf` provides plugin paths, BIOS path, CPU mode, and sandbox limits.
- CLI accepts `--config path` to override.

## Performance Strategy
- Fast path in core (DMA, CPU, hot MMIO)
- Optional dynarec with block cache + invalidation
- Batch GPU commands over IPC
- Shared memory rings for high-volume data (future)

## BIOS
- If a real BIOS is not configured, the emulator uses a small HLE BIOS stub.
- Real BIOS is required for accuracy once CPU execution is implemented.

## GPU Stub Notes
- GP0 state (draw mode, texture window, draw areas, mask) is applied in-core.
- GP1 state (display ranges, display mode, DMA direction) is tracked in MMIO for GPUSTAT.
- The GPU stub renders into a 1024x512 16-bit VRAM and presents from the display area.
- Draw-to-display gating is respected for draw commands that target the active display region.
- 24-bit display output is a best-effort byte-level view of VRAM and will need refinement.
- 24-bit mode reduces effective horizontal resolution (approx. 2/3 scaling).
- GPUSTAT ready/busy bits are approximated using FIFO occupancy and a simple busy-cycle counter.
- GPUSTAT DMA request bit reflects the configured DMA direction and data availability.
- Interlace field toggling is approximate (CPU-cycle based, half-frame cadence) and will need calibration later.
- VRAM readback is scheduled with a small delay to model transfer latency.
- DMA channel 2 supports GPU->CPU transfers by streaming GPUREAD words into RAM.
- GP0/GP1 writes add small busy penalties, especially if the FIFO grows large.
- VRAM image transfers respect mask bit settings (write mask + mask test).
- DMA channel 2 supports linked-list mode (GP0 command chains).
- GPU DMA packets are queued and drained based on GPUSTAT ready/busy to simulate backpressure.

## CD-ROM Stub Notes
- The MMIO layer implements a wider command set (Sync/Getstat/Setloc/ReadN/ReadS/Stop/Pause/Init/Setmode/Getparam/GetlocL/GetlocP/SetSession/GetTN/GetTD/Seek/GetID/Test/ReadTOC/Mute/Demute/Reset).
- Read timing models 75 Hz sector cadence (double speed when mode bit `0x80` is set).
- Data FIFO fills on read timer expiry and raises IRQ `0x02` for data-ready.
- CD-ROM DMA (channel 3) only completes when data FIFO has data available.
- Data reads return 2048-byte user data from ISO/BIN/CUE images with cue index offset handling.
