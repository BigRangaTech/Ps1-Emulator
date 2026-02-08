# Architecture Overview

## Goals
- Balance accuracy and speed via selectable CPU cores (interpreter + dynarec)
- Sandboxed plugins for GPU, SPU, Input, CD-ROM
- Stable plugin API and protocol
- Linux-first, GPLv2+

## Process Model
- `ps1emu` is the host process.
- Each plugin is a separate process (sandboxed). IPC is line-based for now.
- Later: replace IPC transport with a framed binary protocol or Cap'n Proto.

## Core Responsibilities
- CPU: interpreter + dynarec
- Memory map + MMIO
- Scheduler + timing
- DMA, timers, interrupts
- BIOS loading

## Plugin Responsibilities
- GPU: command processor + renderer output
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
