#!/usr/bin/env python3
import argparse
import os
import struct
import subprocess
import sys
from pathlib import Path


def send_line(proc, line: str):
    proc.stdin.write((line + "\n").encode("utf-8"))
    proc.stdin.flush()


def read_line(proc) -> str:
    line = proc.stdout.readline()
    if not line:
        raise RuntimeError("EOF while reading line")
    return line.decode("utf-8").strip()


def send_frame(proc, msg_type: int, payload: bytes):
    header = struct.pack("<IHH", len(payload), msg_type, 0)
    proc.stdin.write(header + payload)
    proc.stdin.flush()


def recv_frame(proc):
    header = proc.stdout.read(8)
    if len(header) != 8:
        raise RuntimeError("EOF while reading frame header")
    length, msg_type, _flags = struct.unpack("<IHH", header)
    payload = proc.stdout.read(length) if length else b""
    if len(payload) != length:
        raise RuntimeError("EOF while reading frame payload")
    return msg_type, payload


def words_to_payload(words):
    payload = bytearray()
    for w in words:
        payload += struct.pack("<I", w & 0xFFFFFFFF)
    return bytes(payload)


def make_test_pattern(width, height):
    data = bytearray()
    for y in range(height):
        for x in range(width):
            r = int((x / max(1, width - 1)) * 255)
            g = int((y / max(1, height - 1)) * 255)
            b = 128
            data += bytes([r, g, b])
    return data


def pack_bytes_to_words(data: bytes):
    if len(data) % 2:
        data += b"\x00"
    words = []
    for i in range(0, len(data), 2):
        word = data[i] | (data[i + 1] << 8)
        words.append(word)
    return words


def build_image_load(x, y, w_words, h_rows, words16):
    # GP0 image load (0xA0)
    cmd = 0xA0000000
    xy = (y << 16) | (x & 0xFFFF)
    size = (h_rows << 16) | (w_words & 0xFFFF)
    total_pixels = w_words * h_rows
    data_words = []
    for i in range(0, total_pixels, 2):
        lo = words16[i] if i < len(words16) else 0
        hi = words16[i + 1] if i + 1 < len(words16) else 0
        data_words.append((hi << 16) | lo)
    return [cmd, xy, size] + data_words


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gpu", default="./build/ps1emu_gpu_stub")
    ap.add_argument("--depth", type=int, choices=[15, 24], default=24)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--dump-dir", default="./frame_dumps")
    ap.add_argument("--dump-every", type=int, default=1)
    args = ap.parse_args()

    gpu_path = Path(args.gpu)
    if not gpu_path.exists():
        raise SystemExit(f"GPU stub not found: {gpu_path}")

    env = os.environ.copy()
    env["PS1EMU_FRAME_DUMP_DIR"] = args.dump_dir
    env["PS1EMU_FRAME_DUMP_EVERY"] = str(max(1, args.dump_every))
    env["PS1EMU_HEADLESS"] = "1"

    os.makedirs(args.dump_dir, exist_ok=True)

    proc = subprocess.Popen([
        str(gpu_path)
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

    try:
        send_line(proc, "HELLO GPU 1")
        if read_line(proc) != "READY GPU 1":
            raise RuntimeError("GPU handshake failed")

        send_line(proc, "FRAME_MODE")
        if read_line(proc) != "FRAME_READY":
            raise RuntimeError("GPU frame mode failed")

        # GP1 setup
        gp1 = []
        gp1.append(0x00000000)  # reset
        gp1.append(0x03000000)  # display enable (0=on)
        mode = 0x00000001  # 320x240 NTSC
        if args.depth == 24:
            mode |= (1 << 4)
        gp1.append(0x08000000 | mode)
        gp1.append(0x05000000)  # display start 0,0
        gp1.append(0x06000000 | (0x200) | ((0x200 + 320 * 8) << 12))
        gp1.append(0x07000000 | (0x10) | ((0x10 + args.height) << 10))

        send_frame(proc, 0x0003, words_to_payload(gp1))
        _t, _p = recv_frame(proc)

        # Build a 24-bit pattern in VRAM via image load
        width = args.width
        height = args.height
        bytes_per_row = width * 3
        words_per_row = (bytes_per_row + 1) // 2

        pattern = make_test_pattern(width, height)
        words16 = pack_bytes_to_words(pattern)

        gp0_words = build_image_load(0, 0, words_per_row, height, words16)
        send_frame(proc, 0x0001, words_to_payload(gp0_words))
        _t, _p = recv_frame(proc)

        print("Frame dump triggered.")
        print(f"Display width={width}, height={height}, words/row={words_per_row}")
        print(f"Dump dir: {args.dump_dir}")
    finally:
        try:
            send_line(proc, "SHUTDOWN")
        except Exception:
            pass
        proc.terminate()


if __name__ == "__main__":
    main()
