#!/usr/bin/env python3
"""Make Newton.app's icon (macOS .icns) from one of the ROM's bitmaps.

Usage:
    python tools/host/macicon.py OUT.icns [--bitmap romsrc/resources/bits/3d594d.png]
                                 [--iconset DIR] [--no-icns]

The bitmap (by default the ROM's smallLogoBitmap, the Newton lightbulb -
romsrc/objects/misc-000.ns) is drawn at every size of a macOS iconset
(16 to 1024 pixels, each at 1x and 2x) onto a rounded tile the colour of
the MessagePad 2x00's screen: scaled by a whole number of pixels where the
size allows, so the Newton's pixels stay sharp, and averaged down where the
size is smaller than the bitmap.  The iconset (the PNG files) goes to
--iconset (default: OUT's name with .iconset), and Apple's iconutil makes
OUT from it; --no-icns stops after the PNG files (iconutil is macOS's own,
so elsewhere only the iconset can be made).

Standard library only: the PNG reading (grey or palette, any bit depth,
the five row filters) and writing (8-bit RGBA) are below.  The build
(src/host/CMakeLists.txt, Newton.app) runs it.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib

DEFAULT_BITMAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                              "romsrc", "resources", "bits", "3d594d.png")

# (name in the iconset, its size in pixels)
ICONSET = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]

# the tile: macOS's icon grid (an 824-pixel tile in 1024, corners of 185),
# the MessagePad 2x00's screen green, a darker edge, the ink near black
TILE = 824 / 1024
CORNER = 185 / 824
EDGE = 12 / 824
SCREEN = (0xd3, 0xdb, 0xc8)
BORDER = (0x5a, 0x63, 0x55)
INK = (0x16, 0x1a, 0x14)
LOGO = 0.72                   # how much of the tile the bitmap may take


def read_png(path: str) -> tuple[int, int, list[list[int]]]:
    """A grey or palette PNG as rows of grey levels (0 black .. 255 white)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, palette = 8, b"", None
    width = height = depth = colour = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                raise ValueError(f"{path}: interlaced PNGs are not read")
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            idat += body
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    step = max(1, channels * depth // 8)
    stride = (width * channels * depth + 7) // 8
    raw = zlib.decompress(idat)
    rows, previous, at = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for x in range(stride):
            a = line[x - step] if x >= step else 0
            b = previous[x]
            c = previous[x - step] if x >= step else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        previous = line
    grey = []
    for line in rows:
        row = []
        for x in range(width):
            if depth < 8:
                per = 8 // depth
                v = (line[x // per] >> (8 - depth * (x % per + 1))) & ((1 << depth) - 1)
                if colour != 3:
                    v = v * 255 // ((1 << depth) - 1)
            else:
                v = line[x * step]
            if colour == 3 and palette:
                v = palette[v * 3]
            row.append(v)
        grey.append(row)
    return width, height, grey


def write_png(path: str, size: int, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def inside_tile(x: float, y: float, left: float, right: float, radius: float) -> bool:
    """Whether a point is on the rounded tile from left to right (square)."""
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, left + radius), right - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 and left <= x <= right and left <= y <= right


def ink_at(grey: list[list[int]], width: int, height: int, size: int) -> list[list[float]]:
    """The bitmap's ink (0 none .. 1 full) over a size-by-size square, centred
    in the tile: a whole number of pixels to each bitmap pixel, or averaged."""
    tile = size * TILE
    room = tile * LOGO
    ink = [[0.0] * size for _ in range(size)]
    scale = room / max(width, height)
    if scale >= 1:
        k = int(scale)
        ox = (size - width * k) // 2
        oy = (size - height * k) // 2
        for y in range(height):
            for x in range(width):
                if grey[y][x] < 128:
                    for dy in range(k):
                        for dx in range(k):
                            ink[oy + y * k + dy][ox + x * k + dx] = 1.0
        return ink
    # smaller than the bitmap: each output pixel the share of the bitmap's
    # pixels under it that are inked
    w, h = width * scale, height * scale
    ox, oy = (size - w) / 2, (size - h) / 2
    for py in range(size):
        for px in range(size):
            x0, x1 = (px - ox) / scale, (px + 1 - ox) / scale
            y0, y1 = (py - oy) / scale, (py + 1 - oy) / scale
            total = covered = 0.0
            for by in range(max(0, int(y0)), min(height, int(y1) + 1)):
                hy = min(y1, by + 1) - max(y0, by)
                if hy <= 0:
                    continue
                for bx in range(max(0, int(x0)), min(width, int(x1) + 1)):
                    hx = min(x1, bx + 1) - max(x0, bx)
                    if hx <= 0:
                        continue
                    total += hx * hy
                    if grey[by][bx] < 128:
                        covered += hx * hy
            ink[py][px] = covered / ((x1 - x0) * (y1 - y0)) if total > 0 else 0.0
    return ink


def render(grey: list[list[int]], width: int, height: int, size: int) -> list[list[tuple[int, int, int, int]]]:
    tile = size * TILE
    left = (size - tile) / 2
    right = left + tile
    radius = tile * CORNER
    edge = max(1.0, tile * EDGE)
    ink = ink_at(grey, width, height, size)
    samples = 4                    # (each pixel's edge coverage, 4 x 4 points)
    out = []
    for py in range(size):
        row = []
        for px in range(size):
            on = rim = 0
            for sy in range(samples):
                for sx in range(samples):
                    x = px + (sx + 0.5) / samples
                    y = py + (sy + 0.5) / samples
                    if inside_tile(x, y, left, right, radius):
                        on += 1
                        if not inside_tile(x, y, left + edge, right - edge, max(radius - edge, 0)):
                            rim += 1
            if on == 0:
                row.append((0, 0, 0, 0))
                continue
            n = samples * samples
            alpha = on / n
            r = rim / on
            colour = [SCREEN[i] * (1 - r) + BORDER[i] * r for i in range(3)]
            k = ink[py][px]
            colour = [colour[i] * (1 - k) + INK[i] * k for i in range(3)]
            row.append((round(colour[0]), round(colour[1]), round(colour[2]), round(alpha * 255)))
        out.append(row)
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", help="the .icns to make")
    parser.add_argument("--bitmap", default=DEFAULT_BITMAP, help="the PNG to draw (default: the ROM's smallLogoBitmap)")
    parser.add_argument("--iconset", help="where the iconset's PNG files go (default: OUTPUT with .iconset)")
    parser.add_argument("--no-icns", action="store_true", help="make the iconset only (no iconutil)")
    args = parser.parse_args(argv)

    width, height, grey = read_png(args.bitmap)
    iconset = args.iconset or os.path.splitext(args.output)[0] + ".iconset"
    os.makedirs(iconset, exist_ok=True)
    made: dict[int, list] = {}
    for name, size in ICONSET:
        if size not in made:
            made[size] = render(grey, width, height, size)
        write_png(os.path.join(iconset, name), size, made[size])
    if args.no_icns:
        print(f"wrote {iconset}")
        return 0
    iconutil = shutil.which("iconutil")
    if iconutil is None:
        print("macicon: no iconutil (macOS's own): --no-icns makes the iconset alone", file=sys.stderr)
        return 1
    subprocess.run([iconutil, "-c", "icns", iconset, "-o", args.output], check=True)
    print(f"wrote {args.output} ({width}x{height} bitmap {os.path.basename(args.bitmap)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
