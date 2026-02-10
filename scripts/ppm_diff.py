#!/usr/bin/env python3
import argparse
import math
from pathlib import Path

def read_ppm(path: Path):
    with path.open('rb') as f:
        magic = f.readline().strip()
        if magic != b'P6':
            raise ValueError(f"{path} is not P6 PPM")
        def next_token():
            while True:
                token = f.readline()
                if not token:
                    return b''
                token = token.strip()
                if not token.startswith(b'#') and token:
                    return token
        wh = next_token()
        if not wh:
            raise ValueError("Missing width/height")
        width, height = map(int, wh.split())
        maxv = int(next_token())
        if maxv != 255:
            raise ValueError("Unsupported max value")
        data = f.read()
    expected = width * height * 3
    if len(data) < expected:
        raise ValueError("PPM data too short")
    return width, height, data[:expected]

def compare(a, b):
    if a[0] != b[0] or a[1] != b[1]:
        raise ValueError("Dimensions differ")
    width, height = a[0], a[1]
    da = a[2]
    db = b[2]
    total = width * height
    sad = 0
    mse = 0
    max_delta = 0
    worst_idx = 0
    for i in range(0, total * 3, 3):
        dr = abs(da[i] - db[i])
        dg = abs(da[i+1] - db[i+1])
        dbb = abs(da[i+2] - db[i+2])
        delta = dr + dg + dbb
        if delta > max_delta:
            max_delta = delta
            worst_idx = i // 3
        sad += delta
        mse += dr * dr + dg * dg + dbb * dbb
    mad = sad / (total * 3)
    rmse = math.sqrt(mse / (total * 3))
    wx = worst_idx % width
    wy = worst_idx // width
    return {
        "width": width,
        "height": height,
        "mad": mad,
        "rmse": rmse,
        "max_delta": max_delta,
        "worst_x": wx,
        "worst_y": wy,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    args = ap.parse_args()
    a = read_ppm(Path(args.a))
    b = read_ppm(Path(args.b))
    metrics = compare(a, b)
    print("PPM Diff")
    print(f"Size: {metrics['width']}x{metrics['height']}")
    print(f"MAD: {metrics['mad']:.4f}")
    print(f"RMSE: {metrics['rmse']:.4f}")
    print(f"Max delta: {metrics['max_delta']} at ({metrics['worst_x']}, {metrics['worst_y']})")

if __name__ == "__main__":
    main()
