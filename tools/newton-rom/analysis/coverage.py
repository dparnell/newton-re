#!/usr/bin/env python3
"""Report how much of the ROM the reconstruction in src/ covers.

Usage:
    python coverage.py <build dir> [--src src] [--by-class] [--check]

Reads every `// ROM 0x<address> <mangled name>` citation in src/**/*.cpp and
src/**/*.h and compares it with symbols.json:

  * a citation whose address or name does not match a ROM symbol is an error
    (with --check the exit status is 1), so typos are caught early;
  * the report shows reconstructed vs. total functions, overall and per class
    (--by-class), counting each ROM function once.

Only real function bodies count (not jump-table slots).  Data symbols cited
with the same syntax are checked but not counted.  Code reconstructed from
the middle of a larger assembly routine (a case of SWIBoot, say) cites the
routine and adds the offset: `// ROM 0x003a4018 SWIBoot +0xb8`; the routine
is checked, the offset is documentation.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

CITE = re.compile(r"//\s*ROM\s+(0x[0-9A-Fa-f]+)\s+([^\s+]\S*)(?:\s+\+0x[0-9A-Fa-f]+)?")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--src", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "src"))
    ap.add_argument("--by-class", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 on bad citations")
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "symbols.json")) as f:
        data = json.load(f)
    by_addr = collections.defaultdict(set)
    functions = {}          # address -> (class, signature) for real C++/C function bodies
    for s in data["symbols"]:
        if "jt_index" in s:
            continue
        by_addr[s["address"]].add(s["name"])
        d = s["demangled"]
        if d and d["kind"] != "data":
            functions[s["address"]] = (d["scope"][-1] if d["scope"] else "", d["signature"])

    cited = {}
    errors = []
    for root, _, files in os.walk(args.src):
        if os.path.basename(root) == "ddk":
            continue
        for fn in files:
            if not fn.endswith((".cpp", ".h")):
                continue
            path = os.path.join(root, fn)
            with open(path, encoding="utf-8") as f:
                for lineno, line in enumerate(f, 1):
                    m = CITE.search(line)
                    if not m:
                        continue
                    addr, name = int(m.group(1), 16), m.group(2)
                    where = f"{os.path.relpath(path, args.src)}:{lineno}"
                    if addr not in by_addr:
                        errors.append(f"{where}: no symbol at {addr:#x}")
                    elif name not in by_addr[addr]:
                        errors.append(f"{where}: {name} is not at {addr:#x} (there: {', '.join(sorted(by_addr[addr]))})")
                    else:
                        cited.setdefault(addr, where)

    for e in errors:
        print("error:", e)
    done = [a for a in cited if a in functions]
    print(f"citations: {len(cited)} ({len(errors)} bad); functions reconstructed: {len(done)} of {len(functions)} "
          f"({100.0 * len(done) / len(functions):.2f}%)")
    if args.by_class:
        total = collections.Counter(cls for cls, _ in functions.values())
        have = collections.Counter(functions[a][0] for a in done)
        for cls in sorted(have, key=lambda c: (-have[c], c)):
            print(f"  {cls or '(free functions)':32s} {have[cls]:3d} / {total[cls]}")
    return 1 if (errors and args.check) else 0


if __name__ == "__main__":
    sys.exit(main())
