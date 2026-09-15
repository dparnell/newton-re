#!/usr/bin/env python3
"""Search the extracted symbol table by regular expression.

Usage:
    python symbols.py <build_dir> <regex> [--mangled]

Matches the demangled signature (or, with --mangled, the raw name) of every
symbol in <build_dir>/symbols.json and prints address, mangled name and
signature, sorted by address.  Quick way to find where a function lives and
which names are related, without opening Ghidra.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("regex")
    ap.add_argument("--mangled", action="store_true", help="match the mangled name instead of the signature")
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    items = data["symbols"] if isinstance(data, dict) and "symbols" in data else data
    pat = re.compile(args.regex)
    hits = []
    for s in items:
        d = s.get("demangled") or {}
        sig = d.get("signature") if isinstance(d, dict) else None
        text = s["name"] if args.mangled else (sig or s["name"])
        if pat.search(text):
            hits.append((s["address"], s["name"], sig or "", s.get("class", "")))
    for addr, name, sig, cls in sorted(hits):
        print(f"0x{addr:08x}  {cls:5s} {name}  {sig}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
