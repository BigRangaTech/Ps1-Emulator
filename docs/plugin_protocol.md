# Plugin Protocol (v1)

This document defines the initial line-based IPC protocol between the host (`ps1emu`) and plugins.

## Transport
- Control messages use newline-delimited UTF-8 text.
- Bulk messages use a framed binary format (see below).
- Host and plugin must handle unknown commands by replying `ERROR` (text) or closing the frame stream.

## Handshake
1. Host sends `HELLO <TYPE> <VERSION>`
2. Plugin responds `READY <TYPE> <VERSION>`

Example:
```
HELLO GPU 1
READY GPU 1
```

## Common Messages
- `PING` -> `PONG`
- `SHUTDOWN` -> plugin exits cleanly
- `ERROR <reason>` for error reporting

## Framed Mode Switch
To switch to framed binary messages:
1. Host sends `FRAME_MODE`
2. Plugin replies `FRAME_READY`
3. Both sides must use framed messages for the rest of the session

## Framed Binary Messages
Frame header (8 bytes, little-endian):
- `uint32 length` (payload size)
- `uint16 type`
- `uint16 flags` (reserved, must be 0)

Payload follows the header. Maximum payload size is 16 MiB.

**Notes**
- Do not mix line-based and framed messages on the same channel without a clear boundary.
- Framed messages are intended for high-throughput GPU/SPU data.

## Frame Types (Stub)
- `0x0001` GPU command buffer (payload is raw 32-bit GP0 words)
- `0x0002` ACK (payload optional; GPU stub returns a 32-bit count)
- `0x0003` GPU control buffer (payload is raw 32-bit GP1 words)
- `0x0004` GPU VRAM read request (payload: x,y,w,h as little-endian uint16)
- `0x0005` GPU VRAM read response (payload: raw 16-bit pixel data, little-endian)
- `0x0100` SPU XA audio sector (payload: `u32 lba`, `u8 mode`, `u8 file`, `u8 channel`, `u8 submode`,
  `u8 coding`, `u8 reserved`, `u16 data_len`, followed by XA audio bytes)
- `0x0101` SPU PCM chunk (payload: `u32 lba`, `u16 sample_rate`, `u8 channels`, `u8 reserved`,
  `u32 sample_count`, followed by interleaved `s16le` PCM samples)
- `0x0102` SPU master volume (payload: `s16le left`, `s16le right`)

Notes:
- GP1 display commands (start/range/mode) are forwarded via `0x0003`.
- VRAM readback currently returns 16-bit data regardless of display depth.

## Plugin Types
- `GPU`
- `SPU`
- `INPUT`
- `CDROM`

## Versioning
- `VERSION` is the protocol version. Current version is `1`.
- Plugins must reject incompatible versions with `ERROR`.

## Future Extensions
- Binary framing for large payloads.
- Shared memory rings for GPU/SPU throughput.
- Cap'n Proto or similar schema for structured messages.
