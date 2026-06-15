#!/usr/bin/env python3
"""Convert a TrueType/OpenType font to the M5GFX VLW bitmap font format."""

from __future__ import annotations

import argparse
import math
import struct
from dataclasses import dataclass
from pathlib import Path

import freetype


DEFAULT_RANGE = "0x20-0x7E,0xB0"


@dataclass
class Glyph:
    codepoint: int
    height: int
    width: int
    x_advance: int
    dy: int
    dx: int
    bitmap: bytes


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def parse_ranges(spec: str) -> set[int]:
    codepoints: set[int] = set()
    if not spec:
        return codepoints

    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            start_text, end_text = part.split("-", 1)
            start = parse_int(start_text)
            end = parse_int(end_text)
            if end < start:
                raise ValueError(f"Range end before start: {part}")
            codepoints.update(range(start, end + 1))
        else:
            codepoints.add(parse_int(part))

    return codepoints


def bitmap_to_rows(bitmap: freetype.Bitmap) -> bytes:
    width = bitmap.width
    rows = bitmap.rows
    pitch = bitmap.pitch
    if width == 0 or rows == 0:
        return b""

    raw = bytes(bitmap.buffer)
    stride = abs(pitch)
    out = bytearray()

    for row in range(rows):
        src_row = row if pitch >= 0 else rows - 1 - row
        start = src_row * stride
        out.extend(raw[start : start + width])

    return bytes(out)


def render_glyph(face: freetype.Face, codepoint: int) -> Glyph | None:
    if face.get_char_index(codepoint) == 0:
        return None

    flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL
    face.load_char(chr(codepoint), flags)
    slot = face.glyph
    bitmap = slot.bitmap

    width = int(bitmap.width)
    height = int(bitmap.rows)
    x_advance = int(round(slot.advance.x / 64.0))
    dx = int(slot.bitmap_left)
    dy = int(slot.bitmap_top)

    if not -128 <= dx <= 127:
        raise ValueError(f"Glyph U+{codepoint:04X} dx {dx} is outside int8")
    if not -32768 <= dy <= 32767:
        raise ValueError(f"Glyph U+{codepoint:04X} dy {dy} is outside int16")
    if not 0 <= width <= 255:
        raise ValueError(f"Glyph U+{codepoint:04X} width {width} is outside uint8")
    if not 0 <= x_advance <= 255:
        raise ValueError(
            f"Glyph U+{codepoint:04X} x advance {x_advance} is outside uint8"
        )

    pixels = bitmap_to_rows(bitmap)
    expected = width * height
    if len(pixels) != expected:
        raise ValueError(
            f"Glyph U+{codepoint:04X} bitmap has {len(pixels)} bytes, "
            f"expected {expected}"
        )

    return Glyph(codepoint, height, width, x_advance, dy, dx, pixels)


def pack_i32(value: int) -> bytes:
    return struct.pack(">i", value)


def write_vlw(path: Path, size_px: int, glyphs: list[Glyph]) -> None:
    visible = [g for g in glyphs if g.width and g.height and g.codepoint != 0x20]
    ascent = max((g.dy for g in visible), default=size_px)
    descent = max((g.height - g.dy for g in visible), default=0)
    y_advance = max(size_px, ascent + descent)

    with path.open("wb") as out:
        out.write(pack_i32(len(glyphs)))
        out.write(pack_i32(11))
        out.write(pack_i32(y_advance))
        out.write(pack_i32(0))
        out.write(pack_i32(ascent))
        out.write(pack_i32(descent))

        for glyph in glyphs:
            out.write(pack_i32(glyph.codepoint))
            out.write(pack_i32(glyph.height))
            out.write(pack_i32(glyph.width))
            out.write(pack_i32(glyph.x_advance))
            out.write(pack_i32(glyph.dy))
            out.write(pack_i32(glyph.dx))
            out.write(pack_i32(0))

        for glyph in glyphs:
            out.write(glyph.bitmap)


def build_font(font_path: Path, size_px: int, codepoints: set[int]) -> list[Glyph]:
    face = freetype.Face(str(font_path))
    face.set_pixel_sizes(0, size_px)

    glyphs: list[Glyph] = []
    missing: list[int] = []
    for codepoint in sorted(codepoints):
        if codepoint > 0xFFFF:
            missing.append(codepoint)
            continue
        glyph = render_glyph(face, codepoint)
        if glyph is None:
            missing.append(codepoint)
            continue
        glyphs.append(glyph)

    if missing:
        formatted = ", ".join(f"U+{cp:04X}" for cp in missing)
        raise SystemExit(f"Font does not contain supported glyphs: {formatted}")
    if not glyphs:
        raise SystemExit("No glyphs selected")

    return glyphs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path, help="TTF/OTF font file")
    parser.add_argument("--size", required=True, type=int, help="Pixel size")
    parser.add_argument("--output", required=True, type=Path, help="Output .vlw path")
    parser.add_argument(
        "--range",
        default=DEFAULT_RANGE,
        help=f"Comma-separated codepoint ranges; default: {DEFAULT_RANGE}",
    )
    parser.add_argument(
        "--symbols",
        default="",
        help="Literal characters to include in addition to --range",
    )
    args = parser.parse_args()

    if args.size <= 0:
        raise SystemExit("--size must be positive")
    if not args.font.is_file():
        raise SystemExit(f"Font file not found: {args.font}")

    codepoints = parse_ranges(args.range)
    codepoints.update(ord(ch) for ch in args.symbols)

    glyphs = build_font(args.font, args.size, codepoints)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_vlw(args.output, args.size, glyphs)

    size = args.output.stat().st_size
    print(f"Wrote {args.output} ({len(glyphs)} glyphs, {math.ceil(size / 1024)} KiB)")


if __name__ == "__main__":
    main()
