"""The QuickDraw pictures in the ROM and its packages, and the opcodes they use.

Usage:
    python pictures.py <build_dir> [--list] [--opcodes]

Every 'picture binary in the ROM's object area, and in the frames parts
of the packages built into the ROM extension (whose objects sit in the
image at their own addresses), is walked opcode by opcode the way
DrawPicture's ParsePicCodes (ROM 0x0033249c, src/qd/PicPlay.cpp) reads
it: a version 1 picture has byte opcodes, a version 2 one (0x0011 0x02ff)
word opcodes on word boundaries; each opcode's data is passed over by the
rules of Apple's picture format as the ROM applies them (the Newton's own
curves 0x0c80-0x0c84/0x8088-0x808c, paths 0x8190-0x8194 and styled text
0x81a0-0x81a4 included; the reserved 0x6d-0x6f read as eight bytes, as
the ROM does).  The bitmap opcodes (0x90-0x9b) are passed over as Apple's
format lays them out: a bitmap or pixel map (with its colour table), the
two rectangles and the mode, a mask region for the odd ones, then the
rows - as they are when the row bytes are under 8, else each with a
count byte (a count word from 251 row bytes up).

--list prints each picture (where it is, its frame, its version and its
opcodes in order); --opcodes (the default) counts the opcodes over them
all, which is what says which parts of DrawPicture the ROM's own pictures
use (docs/qd/README.md, "Pictures").

Inputs: <build_dir>/rom.bin, symbols.json, layout.json (nsfunctions.ROM,
packages.rex_packages).  Output: a table on stdout.
"""

from __future__ import annotations

import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import nsfunctions			# the ROM's objects
import packages				# the extension's packages

NAMES = {
    0x00: "NOP", 0x01: "ClipRgn", 0x02: "BkPat", 0x03: "TxFont", 0x04: "TxFace",
    0x05: "TxMode", 0x06: "SpExtra", 0x07: "PnSize", 0x08: "PnMode", 0x09: "PnPat",
    0x0a: "FillPat", 0x0b: "OvSize", 0x0c: "Origin", 0x0d: "TxSize", 0x0e: "FgColor",
    0x0f: "BkColor", 0x10: "TxRatio", 0x11: "Version", 0x12: "BkPixPat",
    0x13: "PnPixPat", 0x14: "FillPixPat", 0x15: "PnLocHFrac", 0x16: "ChExtra",
    0x1a: "RGBFgCol", 0x1b: "RGBBkCol", 0x1d: "HiliteColor", 0x1e: "DefHilite",
    0x1f: "OpColor", 0x20: "Line", 0x21: "LineFrom", 0x22: "ShortLine",
    0x23: "ShortLineFrom", 0x28: "LongText", 0x29: "DHText", 0x2a: "DVText",
    0x2b: "DHDVText", 0x90: "BitsRect", 0x91: "BitsRgn", 0x98: "PackBitsRect",
    0x99: "PackBitsRgn", 0x9a: "DirectBitsRect", 0x9b: "DirectBitsRgn",
    0xa0: "ShortComment", 0xa1: "LongComment", 0xff: "OpEndPic",
    0x0c00: "HeaderOp", 0x81a0: "TextOptions", 0x81a1: "TextStyle",
    0x81a2: "StyleRuns", 0x81a3: "StyledText", 0x81a4: "TextFamilies",
}
SHAPES = {0x30: "Rect", 0x40: "RRect", 0x50: "Oval", 0x60: "Arc", 0x70: "Poly", 0x80: "Rgn"}
VERBS = ["frame", "paint", "erase", "invert", "fill"]


def name(op: int) -> str:
    if op in NAMES:
        return NAMES[op]
    if 0x30 <= op <= 0x8f and (op & 7) <= 4:
        return ("Same" if op & 8 else "") + VERBS[op & 7] + SHAPES[op & 0xf0]
    if 0x0c80 <= op <= 0x0c84 or 0x8088 <= op <= 0x808c:
        return "Curve:" + VERBS[op & 7]
    if 0x8190 <= op <= 0x8194:
        return "Paths:" + VERBS[op & 7]
    return "op%#x" % op


class Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def byte(self) -> int:
        v = self.d[self.p]
        self.p += 1
        return v

    def sbyte(self) -> int:
        v = self.byte()
        return v - 256 if v >= 128 else v

    def word(self) -> int:
        v = struct.unpack_from(">H", self.d, self.p)[0]
        self.p += 2
        return v

    def long(self) -> int:
        v = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        return v

    def skip(self, n: int):
        if n < 0 or self.p + n > len(self.d):
            raise ValueError("runs past the end at %#x" % self.p)
        self.p += n

    def handle(self):						# a region or polygon: its size word counts itself
        self.skip(self.word() - 2)


def skip_bits(r: Reader, op: int):
    direct = op in (0x9a, 0x9b)
    if direct:
        r.skip(4)							# baseAddr
    row_bytes = r.word()
    pixmap = direct or (row_bytes & 0x8000) != 0
    row_bytes &= 0x7fff
    top, left, bottom, right = struct.unpack_from(">hhhh", r.d, r.p)
    r.skip(8)
    pack_type = 0
    if pixmap:
        r.skip(2)							# version
        pack_type = r.word()
        r.skip(4 + 4 + 4)					# packSize, hRes, vRes
        r.skip(2 + 2 + 2 + 2 + 4 + 4 + 4)	# pixelType ... pmReserved
        if not direct:						# the colour table
            r.skip(4 + 2)
            r.skip((r.word() + 1) * 8)
    r.skip(8 + 8 + 2)						# source, destination, mode
    if op & 1:
        r.handle()							# the mask region
    rows = bottom - top
    packed = row_bytes >= 8 and op not in (0x90, 0x91)
    if direct and pack_type in (1, 2):
        packed = False
        if pack_type == 2:
            row_bytes = (row_bytes * 3) >> 2
    if not packed:
        r.skip(rows * row_bytes)
    else:
        for _ in range(rows):
            r.skip(r.word() if row_bytes > 250 else r.byte())


def walk(data: bytes):
    """The opcodes of one picture, in order; ends at OpEndPic, the data's
    end, or an opcode the ROM would not go on from (answered as 'stop')."""
    r = Reader(data)
    r.skip(2 + 8)							# size, frame
    version = 1
    ops = []
    while r.p < len(r.d):
        if version == 1:
            op = r.byte()
        else:
            if r.p & 1:
                r.byte()
            op = r.word()
        ops.append(op)
        if op == 0xff:
            break
        if op == 0x11:
            first = r.byte()
            if first != 1:
                version = (first << 8) | r.byte()
                if version != 0x2ff:
                    break
            continue
        if op < 0x20:
            size = {0x01: None, 0x02: 8, 0x03: 2, 0x04: 1, 0x05: 2, 0x06: 4, 0x07: 4, 0x08: 2,
                    0x09: 8, 0x0a: 8, 0x0b: 4, 0x0c: 4, 0x0d: 2, 0x0e: 4, 0x0f: 4, 0x10: 8,
                    0x15: 2, 0x16: 2, 0x1a: 6, 0x1b: 6, 0x1d: 6, 0x1f: 6}.get(op, 0)
            if op == 0x01:
                r.handle()
            elif op in (0x12, 0x13, 0x14):
                if r.word() == 2:
                    r.skip(8 + 6)
                else:
                    raise ValueError("a type 1 pixel pattern (not walked)")
            else:
                r.skip(size)
            continue
        if op <= 0x100:
            group = op & 0xfff0
            if group == 0x20:
                if op <= 0x23:
                    r.skip((0 if op & 1 else 4) + (2 if op & 2 else 4))
                elif 0x28 <= op <= 0x2b:
                    r.skip({0x28: 4, 0x29: 1, 0x2a: 1, 0x2b: 2}[op])
                    r.skip(r.byte())
                else:
                    r.skip(r.word())
            elif group in (0x30, 0x40, 0x50):
                if not op & 8 or (op & 7) > 4:
                    r.skip(8)
            elif group == 0x60:
                if (op & 7) <= 4:
                    r.skip((0 if op & 8 else 8) + 4)
                else:
                    r.skip(8 if op & 8 else 0xc)
            elif group in (0x70, 0x80):
                if (op & 7) <= 4:
                    r.handle()
                else:
                    r.skip(r.word())
            elif group == 0x90:
                if op in (0x90, 0x91, 0x98, 0x99, 0x9a, 0x9b):
                    skip_bits(r, op)
                else:
                    r.skip(r.word())
            elif group == 0xa0:
                if op == 0xa0:
                    r.skip(2)
                elif op == 0xa1:
                    r.skip(2)
                    r.skip(r.word())
                else:
                    r.skip(r.word())
            elif group in (0xd0, 0xe0, 0xf0):
                r.skip(r.long())
            continue
        if op < 0x8000:
            if 0x0c80 <= op <= 0x0c84:
                if not op & 8:
                    r.skip(0x18)
            else:
                r.skip((op >> 8) << 1)
            continue
        if 0x8088 <= op <= 0x808c:
            if not op & 8:
                r.skip(0x18)
        elif 0x8190 <= op <= 0x8194:
            r.skip(r.long())
        elif op in (0x81a0, 0x81a1, 0x81a2, 0x81a3):
            r.skip(r.long())
        elif op == 0x81a4:
            size = r.long()
            r.skip(size)
        elif op >= 0x8100:
            r.skip(r.long())
    return version, ops


def pictures(rom):
    """(where, bytes) of every 'picture binary: the ROM's object area, then
    the extension's packages' frames parts."""
    found = []

    def look(ref, where):
        f = rom.flags(ref)
        if f & 1:
            return
        c = rom.cls(ref)
        if rom.is_ptr(c) and rom.symname(c) == "picture":
            found.append((where % ref, bytes(rom.data(ref))))

    for ref in nsfunctions.objects(rom):
        look(ref, "ROM %#x")
    _, pkgs = packages.rex_packages(rom.build_dir)
    for pkg in pkgs:
        for part in pkg["parts"]:
            if part["flags"] & 3 != 1:
                continue
            a = pkg["base"] + pkg["directory_size"] + part["offset"]
            end = a + part["size"]
            align = 8 if pkg["signature"] == "package0" else 4
            while a < end:
                ref = a + 1
                look(ref, pkg["name"] + " %#x")
                a += (rom.size(ref) + align - 1) & ~(align - 1)
    return found


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--list", action="store_true", help="each picture's opcodes in order")
    ap.add_argument("--opcodes", action="store_true", help="the opcodes counted over them all (the default)")
    args = ap.parse_args(argv)
    rom = nsfunctions.ROM(args.build_dir)
    rom.build_dir = args.build_dir
    counts = collections.Counter()
    users = collections.defaultdict(set)
    for where, data in pictures(rom):
        top, left, bottom, right = struct.unpack_from(">hhhh", data, 2)
        try:
            version, ops = walk(data)
            note = ""
        except (ValueError, struct.error, IndexError) as e:
            version, ops, note = 0, [], "  (not walked: %s)" % e
        for op in ops:
            counts[op] += 1
            users[op].add(where)
        if args.list:
            print("%-28s %5d bytes  frame (%d,%d,%d,%d)  version %s%s"
                  % (where, len(data), left, top, right, bottom, hex(version), note))
            print("    " + " ".join(name(op) for op in ops))
    if args.opcodes or not args.list:
        print("%-8s %-20s %6s  pictures" % ("opcode", "name", "count"))
        for op in sorted(counts):
            print("%#-8x %-20s %6d  %d" % (op, name(op), counts[op], len(users[op])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
