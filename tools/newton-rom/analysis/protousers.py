#!/usr/bin/env python3
"""Find the ROM's own users of a proto: the frames that inherit from it or
name its view class, and the built-in packages that refer to it.

Usage:
    python protousers.py build/MP2x00US @826 [--view-class 108] [--packages DIR]

`@826` is the proto's magic pointer (the REP prints protoTXView as @826);
a hex ROM address of the frame works too.  The ROM's object area
(gROMSoupData) is walked frame by frame, and every frame whose `_proto`
slot is the proto (as the frame's address or as the magic pointer) or
whose `viewClass` is the one given is listed with its address and its
slots' names.  With --packages, each .pkg file in DIR (as
`packages.py build/MP2x00US --extract DIR` writes them) is searched for
the magic pointer as a word - a package refers to a ROM object only
through a magic pointer, so a package that uses the proto has the word
somewhere in its frames (the search is by word, so a hit is to be
confirmed, and no hit is conclusive).

Inputs: the build directory's rom.bin and symbols.json (extract_rom.py,
dump_symbols.py).  Output: a report on stdout.

This is how the text engine was found to have no users in the MP2x00 US
ROM (docs/text/README.md): protoTXView (@826, view class 108) is the only
frame of its class, nothing inherits from it, no C function refers to
Rprototxview, and none of the ten built-in packages mentions it.
"""

from __future__ import annotations

import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nsfunctions import ROM  # noqa: E402


def frame_names(rom: ROM, cache: dict, m: int) -> list:
    """The slot names of a frame map, its supermaps' first."""
    if m in cache:
        return cache[m]
    ms = rom.slots(m)
    sup = ms[0]
    names = frame_names(rom, cache, sup) if rom.is_ptr(sup) else []
    names = names + [rom.symname(x) for x in ms[1:]]
    cache[m] = names
    return names


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", help="the build directory (rom.bin, symbols.json)")
    ap.add_argument("proto", help="@n (a magic pointer) or 0x... (a frame's address)")
    ap.add_argument("--view-class", type=int, help="also list the frames with this viewClass")
    ap.add_argument("--packages", help="a directory of extracted .pkg files to search")
    args = ap.parse_args(argv)

    rom = ROM(args.build)
    magic = None
    if args.proto.startswith("@"):
        magic = (int(args.proto[1:]) << 2) | 3
        proto = rom.resolve_magic(magic)
    else:
        proto = int(args.proto, 16) | 1
    print(f"proto: {args.proto} -> frame at 0x{proto - 1:x}")

    cache = {}
    frames = 0
    a = rom.soup
    end = rom.soup + rom.soup_size
    while a < end:
        header = rom.word(a)
        size = header >> 8
        if size < 12:
            print(f"stopped: a bad object header at 0x{a:x}")
            break
        ref = a + 1
        if header & 3 == 3:					# a frame
            frames += 1
            try:
                names = frame_names(rom, cache, rom.cls(ref))
            except Exception:
                names = []
            slots = dict(zip(names, rom.slots(ref)))
            p = slots.get("_proto")
            vc = slots.get("viewClass")
            reasons = []
            if p is not None and (p == proto or (magic is not None and p == magic)):
                reasons.append("_proto")
            if args.view_class is not None and vc == args.view_class << 2:
                reasons.append(f"viewClass {args.view_class}")
            if ref == proto:
                reasons.append("the proto itself")
            if reasons:
                print(f"frame at 0x{a:x} ({', '.join(reasons)}): {', '.join(n for n in names if n)}")
        a += (size + 3) & ~3
    print(f"{frames} frames walked")

    if args.packages and magic is not None:
        for path in sorted(glob.glob(os.path.join(args.packages, "*.pkg"))):
            data = open(path, "rb").read()
            hits = [o for o in range(0, len(data) - 3, 4) if struct.unpack(">I", data[o:o + 4])[0] == magic]
            where = ", ".join(f"0x{o:x}" for o in hits) if hits else "none"
            print(f"{os.path.basename(path)}: {args.proto} as a word at {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
