#!/usr/bin/env python3
"""Decode the MMU map the machine starts from, and say what an address maps to.

Usage:
    python mmumap.py <build_dir> [--doc docs/memory/mmu-map.md]
    python mmumap.py <build_dir> --where 0x038948f0
    python mmumap.py build/MP2x00US --where 0x03894000 --doc docs/memory/mmu-map.md

**Use `--where` when an address in the ROM does not make sense.**  The same
bytes are mapped at more than one virtual address with different cache and
protection flags, and ROM code picks the mapping it wants: the handwriting
engine's `BPNetEvaluate` adds 0x03500000 to its weight pointer, which looks
like a fault until you find that the whole ROM is mapped a second time
there, uncached, so that streaming ninety-one kilobytes of weights does not
flush the StrongARM's data cache.  An address that looks out of range is
usually a second mapping and not a mystery.

The table is `g8MegContinuousTableStart` (ROM 0x100) with
`g8MegContinuousTableStartFor4MbK` (0x174) beside it for a four-megabyte
machine; the boot code walks whichever suits the RAM it finds.  Each entry
is four words - virtual start, physical start, size, and the ARM level-one
descriptor to put in the page tables - and the list ends with 0xffffffff.

The descriptor's low two bits say what kind of entry it is (01 a coarse
page table, in which case the second word is that table's address rather
than a physical base; 10 a section), bits 3 and 2 are C and B, bits 8 to 5
the domain and bits 11 and 10 the access permission.

This is the map the machine *starts* from, not the whole story: the kernel
builds real page tables over it as it comes up, so an address `--where`
finds nothing for may still be mapped later.  What it is good for is the
ROM's own fixed aliases, which are what strange constants in ROM code point
into.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romid			# the ROM names itself in what this writes


TABLES = ["g8MegContinuousTableStart", "g8MegContinuousTableStartFor4MbK"]


def describe(flags: int) -> str:
    """What an ARM level-one descriptor says, in words."""
    kind = flags & 3
    if kind == 1:
        return "coarse page table, domain %d" % ((flags >> 5) & 0xf)
    if kind != 2:
        return "descriptor type %d" % kind
    cache = "cached and buffered" if (flags & 0xc) == 0xc else \
            "cached" if (flags & 8) else \
            "buffered" if (flags & 4) else "UNCACHED"
    return "section, %s, domain %d, AP %d" % (cache, (flags >> 5) & 0xf, (flags >> 10) & 3)


def read_table(rom: bytes, addr: int):
    """The entries of one table, as (virtual, physical, size, flags)."""
    out = []
    while True:
        words = struct.unpack(">4I", rom[addr:addr + 16])
        if words[0] == 0xffffffff:
            return out
        out.append(words)
        addr += 16


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--where", action="append", default=[],
                    help="a virtual address: say which entry covers it and what it maps to")
    ap.add_argument("--doc", help="write the map out as a document")
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    with open(os.path.join(args.build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    by_name = {s["name"]: s["address"] for s in data["symbols"] if "jt_index" not in s}

    tables = []
    for name in TABLES:
        if name not in by_name:
            print(f"error: no symbol {name} in {args.build_dir}", file=sys.stderr)
            return 1
        tables.append((name, by_name[name], read_table(rom, by_name[name])))

    lines = []
    for name, addr, entries in tables:
        lines.append("### `%s` (ROM 0x%08x)" % (name, addr))
        lines.append("")
        lines.append("| virtual | physical | size | descriptor | |")
        lines.append("|---|---|---|---|---|")
        for v, p, size, flags in entries:
            lines.append("| 0x%08x | 0x%08x | %s | 0x%04x | %s |"
                         % (v, p, ("%d MB" % (size >> 20)) if size >= (1 << 20) else ("%d KB" % (size >> 10)),
                            flags, describe(flags)))
        lines.append("")
    print("\n".join(lines))

    for text in args.where:
        want = int(text, 0)
        found = False
        for name, _, entries in tables:
            for v, p, size, flags in entries:
                if v <= want < v + size:
                    print("0x%08x is in %s: virtual 0x%08x + 0x%x"
                          % (want, name, v, want - v))
                    print("    -> physical 0x%08x   (%s)" % (p + (want - v), describe(flags)))
                    found = True
        if not found:
            print("0x%08x is in no entry of either table" % want)

    if args.doc:
        head = ["# The MMU map the machine starts from", "",
                "Generated by `tools/newton-rom/analysis/mmumap.py`; do not edit.",
                "",
                "**When an address in the ROM does not make sense, look here first.**",
                "The same bytes are mapped at more than one virtual address with",
                "different cache and protection flags, and ROM code picks the mapping",
                "it wants. `mmumap.py <build> --where 0x...` says which entry covers",
                "an address and what it maps to.",
                "",
                "Each entry is four words - virtual start, physical start, size, and",
                "the ARM level-one descriptor - and the list ends with 0xffffffff. The",
                "boot code walks whichever of the two tables suits the RAM it finds.",
                "",
                "This is the map the machine *starts* from, not the whole story: the",
                "kernel builds real page tables over it as it comes up, so an address",
                "this map has nothing for may still be mapped later. What it is good",
                "for is the ROM's own fixed aliases, which are what strange constants",
                "in ROM code point into.",
                "",
                f"Read from the {romid.rom_version(args.build_dir)} ROM.",
                ""]
        tail = ["## Why it matters", "",
                "The ROM is mapped twice over: cached at 0x00100000, where everything",
                "reads it, and **uncached at 0x03500000**, where the handwriting",
                "engine's `BPNetEvaluate` reads its ninety-one kilobytes of trained",
                "weights. Streaming those through the StrongARM's sixteen-kilobyte",
                "data cache, once, in order, would evict everything else; reading them",
                "through the uncached alias does not disturb it at all.",
                "",
                "So an address that looks out of range is usually a second mapping,",
                "and the *flags* on the entry usually say why that mapping was chosen.",
                ""]
        with open(args.doc, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(head + lines + tail))
        print(f"wrote {args.doc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
