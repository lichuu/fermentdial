#!/usr/bin/env python3
"""Decode raw RGB565 big-endian framebuffer to PNG (stdlib only)."""

import struct
import sys
import zlib

# usage: decode_screen.py <in.bin> <w> <h> <stride> <out.png>
inp, W, H = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
stride = int(sys.argv[4]) if len(sys.argv) > 4 else W
out = sys.argv[5] if len(sys.argv) > 5 else "frame.png"
data = open(inp, "rb").read()

raw = bytearray()
for y in range(H):
    raw.append(0)  # PNG filter type 0 per scanline
    base = y * stride * 2
    for x in range(W):
        i = base + x * 2
        v = (data[i] << 8) | data[i + 1]
        r = (v >> 11) & 31
        g = (v >> 5) & 63
        b = v & 31
        raw += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))


def chunk(typ, d):
    return (
        struct.pack(">I", len(d))
        + typ
        + d
        + struct.pack(">I", zlib.crc32(typ + d) & 0xFFFFFFFF)
    )


png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
png += chunk(b"IEND", b"")
open(out, "wb").write(png)
print("wrote", out, "from", len(data), "bytes", W, "x", H, "stride", stride)
