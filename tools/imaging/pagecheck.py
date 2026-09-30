#!/usr/bin/env python3
"""Check the pages the host's printer wrote (print/host/HostPrinter.h).

    python tools/imaging/pagecheck.py build/print [--pages 2]
        [--size 2550x3300] [--inked PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]]...

newton's host printer writes each page it prints as <dir>/print-001.png,
print-002.png, ...: a one-bit gray PNG of the whole sheet at 300 dots an
inch.  This reads them (pure Python: zlib and struct), checks that there
are as many as --pages says, that each is --size, that no page is blank,
and that each --inked rectangle of a page (in the page's dots) has at least
MIN black dots (200 unless given) - which is how ctest
host.NewtonHostPrinter knows the note's text and the Names card reached the
paper.  Only what the host's printer writes is read: gray, one or eight
bits a pixel, not interlaced, filter type 0 on every row.

Prints a line per page and per rectangle, then "pagecheck: passed", or the
first thing wrong and exits 1.
"""
import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    """(width, height, rows): each row a bytes of 0 (white) / 1 (black)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    idat = b""
    width = height = depth = None
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if color != 0 or interlace != 0 or depth not in (1, 8):
                raise ValueError("not a plain gray PNG")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    row_bytes = (width * depth + 7) // 8
    rows = []
    for y in range(height):
        start = y * (row_bytes + 1)
        if raw[start] != 0:
            raise ValueError("row %d has filter type %d" % (y, raw[start]))
        line = raw[start + 1:start + 1 + row_bytes]
        if depth == 1:
            bits = bytearray(width)
            for x in range(width):
                bits[x] = 0 if line[x >> 3] & (0x80 >> (x & 7)) else 1
        else:
            bits = bytearray(1 if v < 128 else 0 for v in line)
        rows.append(bytes(bits))
    return width, height, rows


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("dir")
    parser.add_argument("--pages", type=int)
    parser.add_argument("--size", default="2550x3300")
    parser.add_argument("--inked", action="append", default=[],
                        help="PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]")
    args = parser.parse_args(argv[1:])

    paths = []
    n = 1
    while os.path.exists(os.path.join(args.dir, "print-%03d.png" % n)):
        paths.append(os.path.join(args.dir, "print-%03d.png" % n))
        n += 1
    if args.pages is not None and len(paths) != args.pages:
        print("pagecheck: %d page(s) in %s, %d expected" % (len(paths), args.dir, args.pages))
        return 1
    want_w, want_h = (int(v) for v in args.size.split("x"))
    pages = []
    for number, path in enumerate(paths, 1):
        try:
            width, height, rows = read_png(path)
        except ValueError as e:
            print("pagecheck: %s: %s" % (path, e))
            return 1
        black = sum(row.count(1) for row in rows)
        print("pagecheck: page %d: %d x %d, %d black dots (%s)" % (number, width, height, black, path))
        if (width, height) != (want_w, want_h):
            print("pagecheck: page %d is %d x %d, %d x %d expected" % (number, width, height, want_w, want_h))
            return 1
        if black == 0:
            print("pagecheck: page %d is blank" % number)
            return 1
        pages.append(rows)
    for spec in args.inked:
        parts = spec.split(":")
        number = int(parts[0])
        left, top, right, bottom = (int(v) for v in parts[1].split(","))
        least = int(parts[2]) if len(parts) > 2 else 200
        if number > len(pages):
            print("pagecheck: no page %d" % number)
            return 1
        rows = pages[number - 1]
        count = sum(row[left:right].count(1) for row in rows[top:bottom])
        print("pagecheck: page %d [%d,%d,%d,%d]: %d black dots" % (number, left, top, right, bottom, count))
        if count < least:
            print("pagecheck: page %d [%d,%d,%d,%d] has %d black dots, at least %d expected"
                  % (number, left, top, right, bottom, count, least))
            return 1
    print("pagecheck: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
