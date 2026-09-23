#!/usr/bin/env python3
"""Print the entries of a ROM vtable, or find the vtable a method is in.

Usage:
    python vtable.py <build_dir> <vtable address> [<count>]
    python vtable.py build/MP2100D 0x1a884 20
    python vtable.py build/MP2x00US --find HandleTap__14TParagraphViewFR6TPoint
    python vtable.py build/MP2x00US --find 0x001752c4 --slot 0x11c

Apple's ARM C++ lays a vtable out as an array of `B` instructions, one per
virtual function in declaration order (destructor first, base entries
first).  Given the vtable's address (the word a constructor stores at
[this, #0]; `decompile.py --asm` shows it as `; TFoo::vtable`) this decodes
each branch and names its target from symbols.json.  Vtables follow one
another without a terminator, so the count (default 24) is the reader's to
judge: the entries of the next class's vtable are unrelated names.

The view classes are made by `BuildView` rather than by a self-allocating
constructor, so `verify_types.py` never sees where their vtables are and
`romfacts.json` does not have them.  `--find` is the way in from the other
end: it scans the ROM for every `B` that lands on a method (by mangled name,
or by address - a vtable branches to the patchable jump table slot, so both
forms of an exported method's address are accepted) and prints where those
branches are.  With `--slot` - the offset a virtual call uses, which
`decompile.py`'s `add pc,rN,#0x11c` shows - it prints the vtable each hit
implies, which is what names the other slots of the same class.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys


def load(build_dir):
    with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    items = data["symbols"] if isinstance(data, dict) and "symbols" in data else data
    by_addr = {}
    for s in items:
        by_addr.setdefault(s["address"], s["name"])
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    return by_addr, rom


def branch_target(word, at):
    """The address a `B` at `at` goes to, or None when the word is not one."""
    if (word >> 24) != 0xEA:
        return None
    off = word & 0xFFFFFF
    if off & 0x800000:
        off -= 0x1000000
    return at + 8 + off * 4


def print_table(by_addr, rom, address, count):
    for i in range(count):
        at = address + 4 * i
        if at + 4 > len(rom):
            break
        word = struct.unpack(">I", rom[at:at + 4])[0]
        target = branch_target(word, at)
        if target is None:
            print(f"  +0x{4 * i:02x}  0x{word:08x} (not a branch)")
        else:
            print(f"  +0x{4 * i:02x}  0x{target:08x}  {by_addr.get(target, '?')}")


def find(by_addr, rom, what, slot, count):
    """Every `B` in the ROM landing on `what` (a mangled name or an address)."""
    try:
        wanted = {int(what, 0)}
    except ValueError:
        wanted = set()
        names = {what}
    else:
        names = {by_addr[a] for a in wanted if a in by_addr}
    # an exported method is reached through its jump table slot, and a vtable
    # branches to the slot rather than to the body; take every address the
    # name is at, so either form of it is found
    if names:
        for addr, name in by_addr.items():
            if name in names:
                wanted.add(addr)
    if not wanted:
        print(f"no symbol named {what}", file=sys.stderr)
        return 1
    print("looking for a branch to " + ", ".join(f"0x{a:08x}" for a in sorted(wanted)))
    hits = []
    for at in range(0, len(rom) - 3, 4):
        word = struct.unpack(">I", rom[at:at + 4])[0]
        if (word >> 24) != 0xEA:
            continue
        if branch_target(word, at) in wanted:
            hits.append(at)
    for at in hits:
        if slot is None:
            print(f"  branch at 0x{at:08x}")
        else:
            base = at - slot
            print(f"  branch at 0x{at:08x}: a vtable at 0x{base:08x} would have it at +0x{slot:x}")
            print_table(by_addr, rom, base, count)
    if not hits:
        print("  (none)")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("address", nargs="?", type=lambda s: int(s, 0))
    ap.add_argument("count", nargs="?", type=int, default=24, help="entries to print (default 24)")
    ap.add_argument("--find", metavar="NAME|ADDR", help="find the branches to this method instead")
    ap.add_argument("--slot", type=lambda s: int(s, 0), help="with --find, the virtual call's offset: print the vtable it implies")
    args = ap.parse_args(argv)
    by_addr, rom = load(args.build_dir)
    if args.find is not None:
        return find(by_addr, rom, args.find, args.slot, args.count)
    if args.address is None:
        ap.error("a vtable address (or --find) is needed")
    print_table(by_addr, rom, args.address, args.count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
