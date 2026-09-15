#!/usr/bin/env python3
"""Dump the debug symbol table of a Newton debug ROM, demangled and annotated.

Usage:
    python dump_symbols.py "<image file>" -o symbols.json [--text symbols.txt]

Produces `symbols.json` for the Ghidra import script and, optionally, a
human-readable listing.  Every symbol carries:

    name        the raw (mangled) name from the debug table
    address     the symbol value
    class       code | data | zinit | abs  (from the symbol flags)
    global      whether the symbol is exported
    demangled   structured C++ information (scope, name, kind, params, ...) or
                null for plain C / assembler names
    jt_index    present when the symbol is a slot in the patchable jump table;
                the real function is at jumptable.entries[jt_index].target

and the file also contains the decoded jump table (see newtonrom/jumptable.py).
The jump table is verified against the symbols: every slot must branch to a
same-named function, otherwise the tool exits with an error.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from newtonrom.aif import AIFImage  # noqa: E402
from newtonrom.demangle import demangle  # noqa: E402
from newtonrom.jumptable import JumpTable, is_virtual_slot, virtual_to_index  # noqa: E402
from newtonrom.symbols import read_symbols  # noqa: E402


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="AIF debug ROM image ('Senior ... image')")
    ap.add_argument("-o", "--out", required=True, help="output symbols.json")
    ap.add_argument("--text", help="also write a sorted human-readable listing")
    ap.add_argument("--jump-table", type=lambda s: int(s, 0), default=None,
                    help="ROM address of the jump table (default: auto-detect)")
    args = ap.parse_args(argv)

    img = AIFImage.from_file(args.image)
    syms = read_symbols(img)

    jt_addr = args.jump_table if args.jump_table is not None else JumpTable.locate(img)
    jt = JumpTable(img, jt_addr)
    matched, mismatches = jt.verify(syms)
    unexpected = [(e, n) for e, n in mismatches if n != "_DebugStr"]
    print(f"jump table at {jt_addr:#x}: {jt.count} entries, {matched} slot symbols verified")
    if unexpected:
        for e, n in unexpected[:10]:
            print(f"  slot {e.virtual:#x} '{n}' -> {e.target:#x} has no matching symbol", file=sys.stderr)
        print(f"error: {len(unexpected)} jump table slots do not match their symbols", file=sys.stderr)
        return 1

    records = []
    stats = Counter()
    for s in syms:
        d = demangle(s.name)
        rec = {"name": s.name, "address": s.value, "class": s.sym_class, "global": s.is_global,
               "demangled": d.to_json() if d else None}
        if is_virtual_slot(s.value, jt.count):
            rec["jt_index"] = virtual_to_index(s.value)
            stats["jump table slots"] += 1
        stats["demangled" if d else "plain"] += 1
        records.append(rec)

    out = {
        "image": os.path.basename(args.image),
        "symbol_count": len(records),
        "jumptable": {
            "rom_address": jt.rom_address,
            "virtual_base": jt.entries[0].virtual if jt.entries else None,
            "entries_per_page": 32,
            "count": jt.count,
            "entries": [[e.virtual, e.target] for e in jt.entries],
        },
        "symbols": records,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {args.out}: {len(records)} symbols "
          f"({stats['demangled']} C++, {stats['plain']} plain, {stats['jump table slots']} jump table slots)")

    if args.text:
        with open(args.text, "w", encoding="utf-8") as f:
            f.write(f"# {len(records)} symbols from {os.path.basename(args.image)}\n")
            f.write("# address  class  name  [demangled]\n")
            for rec in sorted(records, key=lambda r: (r["address"], r["name"])):
                line = f"{rec['address']:08x} {rec['class']:5s} {rec['name']}"
                if rec["demangled"]:
                    line += f"  {rec['demangled']['signature']}"
                if "jt_index" in rec:
                    line += f"  [jump table slot {rec['jt_index']} -> {jt.entries[rec['jt_index']].target:08x}]"
                f.write(line + "\n")
        print(f"wrote {args.text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
