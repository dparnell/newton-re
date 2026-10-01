#!/usr/bin/env python3
"""Report how much of the ROM the reconstruction in src/ covers.

Usage:
    python coverage.py <build dir> [--src src] [--by-class] [--left N] [--check]
                       [--categories RULES.tsv [--show CATEGORY [--show-count N]]]

Reads every `// ROM 0x<address> <mangled name>` citation in src/**/*.cpp and
src/**/*.h and compares it with symbols.json:

  * a citation whose address or name does not match a ROM symbol is an error
    (with --check the exit status is 1), so typos are caught early;
  * the report shows reconstructed vs. total functions, overall and per class
    (--by-class), counting each ROM function once;
  * --left N ranks what is still to do: the N classes with the most bytes of
    uncited code (a function's size being the distance to the next code
    symbol), then the N largest uncited free functions - for choosing the
    next piece of work;
  * --categories RULES.tsv sorts the uncited code into categories by the
    rules in the file (`category<TAB>regex`, the regex searched for in the
    mangled name, the first rule that matches winning; `#` starts a
    comment) and prints each category's functions and bytes - what "done"
    leaves out and why (uncited-categories.tsv beside this script is the
    project's own); --show CATEGORY lists that category's largest
    functions (--show-count, default 40), which is how a rule is written
    for what is still "other".  A function's size here is the distance to
    the next symbol of any kind, so the data after the last function of a
    module is not counted as code.  Before the rules, a function that is
    nothing but a branch to a reconstructed one - a name the compiler or
    the patchable jump table gave a function that lives under another
    (the x-prefixed stubs, Scan to Scan1) - is counted as "aliases of
    reconstructed functions" rather than as work left.

Only real function bodies count (not jump-table slots).  Data symbols cited
with the same syntax are checked but not counted.  Code reconstructed from
the middle of a larger assembly routine (a case of SWIBoot, say) cites the
routine and adds the offset: `// ROM 0x003a4018 SWIBoot +0xb8`; the routine
is checked, the offset is documentation.

A few static functions have no debug symbol at all (the ROM's symbol table
only names externally visible functions and the static ones the linker
happened to keep), and so do some of the tables in the initialised
read-write data.  These are cited as `// ROM 0x002ebce8 (unnamed)`; the
address must lie in the ROM, in the initialised data, or in the
zero-initialised data (layout.json's
ram_init region, which is where romtable.py finds such a table) and must
*not* carry a symbol (otherwise cite the symbol).  They are counted as
citations but not as reconstructed functions, since the function total
comes from the symbol table.

NewtonScript functions the ROM keeps as objects (the script methods of its
prototype frames, its script built-ins) have no symbol either; code that
re-expresses one as NewtonScript source cites the object's ref:
`// ROM 0x006278bd (object) unionSoupPrototype.Add` (nsfunctions.py
--disasm unionsoupprototype.Add shows the bytecode).  The ref must be a
pointer (low bits 01) into the ROM's object area (gROMSoupData); counted as
a citation, not as a function.
"""

from __future__ import annotations

import argparse
import bisect
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
    ap.add_argument("--left", type=int, metavar="N", help="rank the uncited code: N classes, N free functions")
    ap.add_argument("--categories", metavar="RULES", help="sort the uncited code into categories (category<TAB>regex)")
    ap.add_argument("--show", metavar="CATEGORY", help="with --categories: list a category's largest functions")
    ap.add_argument("--show-count", type=int, default=40)
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "symbols.json")) as f:
        data = json.load(f)
    with open(os.path.join(args.build_dir, "layout.json")) as f:
        layout = json.load(f)
    rom_size = layout["rom_size"]
    # the initialised read-write data: a global there is as much the ROM's
    # as one in the code area, and the tables romtable.py reads out of the
    # ROM's copy of it are cited by their RAM address
    ram_init = next(((r["address"], r["address"] + r["size"])
                     for r in layout["regions"] if r["kind"] == "ram_init"), (0, 0))
    ram_zero = next(((r["address"], r["address"] + r["size"])
                     for r in layout["regions"] if r["kind"] == "ram_zero"), (0, 0))
    by_addr = collections.defaultdict(set)
    by_name = {s["name"]: s["address"] for s in data["symbols"] if "jt_index" not in s}
    with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
        f.seek(by_name["gROMSoupDataSize"])
        soup_size = int.from_bytes(f.read(4), "big")
    soup_area = (by_name["gROMSoupData"], by_name["gROMSoupData"] + soup_size)
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
                    if name == "(unnamed)":
                        if addr in by_addr:
                            errors.append(f"{where}: {addr:#x} has a symbol ({', '.join(sorted(by_addr[addr]))}); cite it")
                        # a table of bytes need not be aligned at all (the angle
                        # tables' kDegreesOfFraction starts at an odd halfword,
                        # QuickDraw's kDepthPixelsPerByteShift at an odd byte)
                        elif not (addr < rom_size
                                              or ram_init[0] <= addr < ram_init[1]
                                              or ram_zero[0] <= addr < ram_zero[1]):
                            errors.append(f"{where}: {addr:#x} is not a ROM address nor one in the read-write data")
                        else:
                            cited.setdefault(addr, where)
                    elif name == "(object)":
                        if addr % 4 != 1 or not (soup_area[0] <= addr < soup_area[1]):
                            errors.append(f"{where}: {addr:#x} is not a ref into the ROM's object area")
                        else:
                            cited.setdefault(addr, where)
                    elif addr not in by_addr:
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
    if args.left:
        starts = sorted(a for a in functions if a < rom_size)
        size = {a: (b - a) for a, b in zip(starts, starts[1:] + [rom_size])}
        names = {s["address"]: s["name"] for s in data["symbols"] if "jt_index" not in s and s["address"] in functions}
        left_bytes = collections.Counter()
        left_count = collections.Counter()
        total = collections.Counter(cls for cls, _ in functions.values())
        free = []
        for a, (cls, sig) in functions.items():
            if a in cited or a not in size:
                continue
            if cls:
                left_bytes[cls] += size[a]
                left_count[cls] += 1
            else:
                free.append((size[a], a, names.get(a, sig)))
        print("\nclasses with the most uncited code (bytes, functions left / total):")
        for cls, n in left_bytes.most_common(args.left):
            print(f"  {cls:32s} {n:7d}  {left_count[cls]:3d} / {total[cls]}")
        print(f"\nthe largest uncited free functions (of {len(free)}):")
        for n, a, name in sorted(free, reverse=True)[:args.left]:
            print(f"  {a:#010x} {n:6d}  {name}")
    if args.categories:
        rules = []
        with open(args.categories, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].rstrip()
                if not line.strip():
                    continue
                category, pattern = line.split("\t", 1)
                rules.append((category.strip(), re.compile(pattern.strip())))
        # a function's size: the distance to the next symbol of any kind in
        # the ROM (the last function of a module is followed by its data)
        every = sorted(a for a in by_addr if a < rom_size)
        names = {s["address"]: s["name"] for s in data["symbols"] if "jt_index" not in s and s["address"] in functions}
        sizes = collections.Counter()
        counts = collections.Counter()
        members = collections.defaultdict(list)
        total_bytes = 0
        with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
            rom = f.read()
        slots = {s["address"]: s["name"] for s in data["symbols"] if "jt_index" in s}

        def branch_target(a):
            """Where a function that is one unconditional branch goes (the
            function a jump-table slot stands for), else None."""
            word = int.from_bytes(rom[a:a + 4], "big") if a + 4 <= len(rom) else 0
            if (word >> 24) != 0xEA:
                return None
            offset = word & 0xFFFFFF
            if offset & 0x800000:
                offset -= 0x1000000
            target = (a + 8 + 4 * offset) & 0xFFFFFFFF
            if target in slots:
                target = by_name.get(slots[target], target)
            return target
        for a in functions:
            if a >= rom_size:
                continue
            i = bisect.bisect_right(every, a)
            n = (every[i] if i < len(every) else rom_size) - a
            total_bytes += n
            if a in cited:
                sizes["(reconstructed)"] += n
                counts["(reconstructed)"] += 1
                continue
            name = names.get(a, "")
            if branch_target(a) in cited:
                category = "aliases of reconstructed functions"
            else:
                category = next((c for c, r in rules if r.search(name)), "other")
            sizes[category] += n
            counts[category] += 1
            members[category].append((n, a, name))
        print(f"\nthe ROM's {len([a for a in functions if a < rom_size])} functions, {total_bytes} bytes, by category:")
        width = max(len(c) for c in sizes)
        for category, n in sorted(sizes.items(), key=lambda kv: -kv[1]):
            print(f"  {category:{width}s} {counts[category]:6d} functions {n:9d} bytes  {100.0 * n / total_bytes:5.1f}%")
        if args.show:
            print(f"\nthe largest in {args.show}:")
            for n, a, name in sorted(members[args.show], reverse=True)[:args.show_count]:
                print(f"  {a:#010x} {n:6d}  {name}")
    return 1 if (errors and args.check) else 0


if __name__ == "__main__":
    sys.exit(main())
