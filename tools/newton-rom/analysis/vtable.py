#!/usr/bin/env python3
"""Print the entries of a ROM vtable.

Usage:
    python vtable.py <build_dir> <vtable address> [<count>]
    python vtable.py build/MP2100D 0x1a884 20

Apple's ARM C++ lays a vtable out as an array of `B` instructions, one per
virtual function in declaration order (destructor first, base entries
first).  Given the vtable's address (the word a constructor stores at
[this, #0]; `decompile.py --asm` shows it as `; TFoo::vtable`) this decodes
each branch and names its target from symbols.json.  Vtables follow one
another without a terminator, so the count (default 24) is the reader's to
judge: the entries of the next class's vtable are unrelated names.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("address", type=lambda s: int(s, 0))
    ap.add_argument("count", nargs="?", type=int, default=24, help="entries to print (default 24)")
    args = ap.parse_args(argv)
    with open(os.path.join(args.build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    items = data["symbols"] if isinstance(data, dict) and "symbols" in data else data
    by_addr = {}
    for s in items:
        by_addr.setdefault(s["address"], s["name"])
    with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    i = 0
    while i < args.count:
        at = args.address + 4 * i
        if at + 4 > len(rom):
            break
        word = struct.unpack(">I", rom[at:at + 4])[0]
        if (word >> 24) != 0xEA:      # B (always)
            print(f"  +0x{4 * i:02x}  0x{word:08x} (not a branch)")
        else:
            off = word & 0xFFFFFF
            if off & 0x800000:
                off -= 0x1000000
            target = at + 8 + off * 4
            print(f"  +0x{4 * i:02x}  0x{target:08x}  {by_addr.get(target, '?')}")
        i += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
