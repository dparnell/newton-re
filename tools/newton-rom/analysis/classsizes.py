#!/usr/bin/env python3
"""How big a group of ROM classes is, and how much of it is reconstructed.

Usage:
    python classsizes.py <build_dir> REGEX [--src src] [--functions]
    python classsizes.py build/MP2x00US "^(TSharpIRTool|TIrSIR|THermesIRControl|TIRQTimer)$"

Every code symbol whose class (the part of its demangled name before the
last "::") matches REGEX is counted: its size is its extent, from its
symbol to the next code symbol in the ROM (so a function followed by
unnamed static code is counted with it), and it is done when some file
under --src cites its address (`// ROM 0x... name`, the convention
coverage.py checks).  One line per class: functions, bytes, and how many
of each are done, with the class's lowest and highest address; then the
total.  --functions lists each function as well, marking the ones left.

This is how a subsystem is sized before its reconstruction is planned
(docs/comms/README.md's IR plan was sized with it), where callgraph.py
only sees what a root reaches through direct calls - virtual methods,
reached through a vtable, are invisible to it.

Inputs:  <build_dir>/symbols.json, layout.json, rom.bin; the source tree.
Output:  a table on stdout.
"""

from __future__ import annotations

import argparse
import bisect
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from callgraph import load, cited_addresses  # noqa: E402


def class_of(sym):
    d = sym.get("demangled") or {}
    sig = d.get("signature") if isinstance(d, dict) else None
    if not sig:
        return "(free functions)"
    head = sig.split("(", 1)[0]
    if "::" not in head:
        return "(free functions)"
    return head.rsplit("::", 1)[0]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("regex", help="matched (re.search) against each function's class name")
    ap.add_argument("--src", default="src")
    ap.add_argument("--functions", action="store_true", help="list every function too")
    args = ap.parse_args(argv)

    data, layout, rom = load(args.build_dir)
    rom_size = len(rom)
    code = {}
    for s in data["symbols"]:
        if "jt_index" in s or s["class"] != "code" or s["address"] >= rom_size:
            continue
        code.setdefault(s["address"], s)
    starts = sorted(code)
    done = cited_addresses(args.src)
    pat = re.compile(args.regex)

    groups = collections.defaultdict(list)
    for i, a in enumerate(starts):
        cls = class_of(code[a])
        if pat.search(cls):
            end = starts[i + 1] if i + 1 < len(starts) else rom_size
            groups[cls].append((a, end - a, code[a]["name"]))

    tot = [0, 0, 0, 0]
    print(f"{'class':34} {'funcs':>5} {'done':>5} {'bytes':>7} {'done':>7}  range")
    for cls in sorted(groups, key=lambda c: -sum(n for _, n, _ in groups[c])):
        fs = groups[cls]
        nb = sum(n for _, n, _ in fs)
        df = [f for f in fs if f[0] in done]
        db = sum(n for _, n, _ in df)
        lo, hi = min(a for a, _, _ in fs), max(a + n for a, n, _ in fs)
        print(f"{cls:34} {len(fs):5} {len(df):5} {nb:7} {db:7}  0x{lo:08x}-0x{hi:08x}")
        if args.functions:
            for a, n, name in fs:
                print(f"    0x{a:08x} {n:6}  {'done' if a in done else 'TODO'}  {name}")
        tot[0] += len(fs); tot[1] += len(df); tot[2] += nb; tot[3] += db
    print(f"{'total':34} {tot[0]:5} {tot[1]:5} {tot[2]:7} {tot[3]:7}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
