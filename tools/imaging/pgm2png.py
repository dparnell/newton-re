#!/usr/bin/env python3
"""Convert a binary PGM (P5), PBM (P4) or PPM (P6) image - what the host
screen driver writes (hal/host/HostScreen.h, ScreenSnapshot in
newtonscript; a .ppm is in colour on the colour screen, docs/qd/colour.md)
- to a PNG, so that the reconstructed display can be looked at anywhere.

    python tools/imaging/pgm2png.py build/views-demo.pgm [build/views-demo.png]

Pure Python (zlib + struct): no imaging library needed.  The output is an
8-bit grayscale PNG of the same size (a PBM's set bits black), or an 8-bit
RGB one for a PPM.  read_pnm (gray only) and write_png are used by
tools/modem/faxcheck.py; read_any also reads a PPM.
"""
import struct
import sys
import zlib


def read_pnm(path):
    data = open(path, "rb").read()
    fields = []
    pos = 0
    while len(fields) < 4 if data[:2] == b"P5" else len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1  # the single whitespace before the pixels
    magic = fields[0]
    width, height = int(fields[1]), int(fields[2])
    if magic == b"P5":
        maxval = int(fields[3])
        pixels = data[pos:pos + width * height]
        if maxval != 255:
            pixels = bytes(v * 255 // maxval for v in pixels)
        return width, height, pixels
    if magic == b"P4":
        row_bytes = (width + 7) // 8
        out = bytearray()
        for y in range(height):
            row = data[pos + y * row_bytes:pos + (y + 1) * row_bytes]
            for x in range(width):
                out.append(0 if row[x >> 3] & (0x80 >> (x & 7)) else 255)
        return width, height, bytes(out)
    raise SystemExit("not a binary PGM or PBM: %s" % path)


def read_any(path):
    """(width, height, pixels, channels): a PPM's RGB bytes (3), else what
    read_pnm reads (1)."""
    data = open(path, "rb").read()
    if data[:2] != b"P6":
        width, height, gray = read_pnm(path)
        return width, height, gray, 1
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    width, height, maxval = int(fields[1]), int(fields[2]), int(fields[3])
    pixels = data[pos:pos + width * height * 3]
    if maxval != 255:
        pixels = bytes(v * 255 // maxval for v in pixels)
    return width, height, pixels, 3


def write_png(path, width, height, gray, channels=1):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    row = width * channels
    raw = b"".join(b"\0" + gray[y * row:(y + 1) * row] for y in range(height))
    colour_type = 2 if channels == 3 else 0			# (RGB, else gray)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    source = argv[1]
    target = argv[2] if len(argv) > 2 else source.rsplit(".", 1)[0] + ".png"
    width, height, pixels, channels = read_any(source)
    write_png(target, width, height, pixels, channels)
    print("%s: %d x %d -> %s" % (source, width, height, target))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
