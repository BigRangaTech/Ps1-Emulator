# Testing

## Philosophy
- Add tests for new modules alongside implementation work.
- Prefer small, deterministic unit tests in `tests/emulator_tests.cpp`.
- Add integration tests when a subsystem crosses IPC or DMA boundaries.

## Running Tests
```bash
cmake --build build -j
./build/ps1emu_tests
```

## GPU Frame Dumps (Calibration)
Set the frame dump environment variables when launching the emulator:

```bash
export PS1EMU_FRAME_DUMP_DIR=./frame_dumps
export PS1EMU_FRAME_DUMP_EVERY=30
./build/ps1emu --config ps1emu.conf
```

Notes:
- Frames are written as `P6` PPM files (`frame_000000.ppm`, ...).
- Dumps work in headless mode as well as with SDL output.
- Use a known GPU test ROM or a game with 24-bit textures to calibrate 24-bit display mapping.

## GPU Test Pattern (No ROM Required)
Generate a 24-bit calibration frame without running a ROM:

```bash
scripts/gpu_frame_dump.py --depth 24 --width 320 --height 240 --dump-dir ./frame_dumps
```

This will start the GPU stub, load a synthetic RGB gradient into VRAM, and dump a frame.

## PPM Diff Tool
Use `scripts/ppm_diff.py` to compare two PPM dumps (e.g., PS1Emu vs reference emulator):

```bash
scripts/ppm_diff.py frame_dumps/frame_000120.ppm reference.ppm
```

The tool reports MAD/RMSE and the worst pixel delta to help quantify mapping drift.

## Bootstrapping a Reference (Tool Check)
If you just want to confirm the diff tool works end-to-end, you can create a temporary reference
from a PS1Emu dump:

```bash
scripts/ppm_make_reference.py frame_dumps/frame_000000.ppm reference.ppm
scripts/ppm_diff.py frame_dumps/frame_000000.ppm reference.ppm
```
