#!/usr/bin/env python3
"""Move the reconstruction's ROM citations from one ROM to another.

Usage:
    python recite.py --from build/MP2100D --to build/MP2x00US [--check] [path...]
    python recite.py --from build/MP2100D --to build/MP2x00US src/frames

Every reconstructed function carries a `// ROM 0x<address> <name>` comment
naming where it came from (see coverage.py, which checks them).  The
addresses belong to one particular ROM image, so pointing the
reconstruction at a different one - a different localisation, or a
different build of the same OS - means rewriting all of them.  Almost all
map straight across: the two images are builds of the same source, so a
function keeps its mangled name and only moves.

The four citation shapes, and what happens to each:

  `// ROM 0x002ba618 InvalFaultBlock__FRC6RefVar`
        looked up by name in the new ROM's symbols and rewritten.
  `// ROM 0x003a4018 SWIBoot +0xb8`
        the same, keeping the offset: the routine moves, the offset into
        it does not (worth an eye afterwards - hand-written assembly can
        be laid out differently).
  `// ROM 0x006278bd (object) unionSoupPrototype.Add`
        a NewtonScript object rather than code: the path is resolved in
        both ROMs (as nsfunctions.py --object does) and rewritten.
  `// ROM 0x002ebce8 (unnamed)`
        nothing to look the address up by, so it is left alone and
        reported.  These need a person.

Nothing is rewritten unless the citation's current address really is that
name in the old ROM: a citation that does not check out is reported and
left, because it means either the citation or the build directory is
wrong, and guessing would bury that.  --check reports without writing.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

H = r"[^\S\r\n]"			# a space or a tab, but not the end of the line
CITE = re.compile(r"(//%s*ROM%s+)(0x[0-9A-Fa-f]+)(%s+)([^\s+]\S*)((?:%s+\+0x[0-9A-Fa-f]+)?)"
                  % (H, H, H, H))
OBJECT_CITE = re.compile(r"(//%s*ROM%s+)(0x[0-9A-Fa-f]+)(%s+\(object\)%s+)(\S+)"
                         % (H, H, H, H))


def load(build_dir: str):
    with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    by_name: dict[str, int] = {}
    by_addr: dict[int, str] = {}
    for s in data["symbols"]:
        if "jt_index" in s:
            continue
        by_name.setdefault(s["name"], s["address"])
        by_addr.setdefault(s["address"], s["name"])
    return by_name, by_addr


def object_address(rom, path: str):
    """The address of a ROM object named by a path like `frame.slot`."""
    import nsfunctions as nf

    path = path.rstrip(":,;.")			# the citations write it in a sentence
    parts = path.split(".")
    ref = nf.resolve(rom, parts[0])
    if ref is None:
        return None
    for part in parts[1:]:
        if not rom.is_ptr(ref) or rom.flags(ref) & 3 != 3:
            return None
        ref = rom.frame_get(ref, part, True)		# as a message send finds it
        if ref is None or ref == 2:
            return None
    return ref


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="old", required=True, help="the build dir the citations are against now")
    ap.add_argument("--to", dest="new", required=True, help="the build dir to move them to")
    ap.add_argument("--check", action="store_true", help="report without writing")
    ap.add_argument("paths", nargs="*", default=["src"], help="files or directories (default: src)")
    args = ap.parse_args(argv)

    old_name, old_addr = load(args.old)
    new_name, _ = load(args.new)

    import nsfunctions as nf
    old_rom = nf.ROM(args.old)
    new_rom = nf.ROM(args.new)

    files = []
    for p in args.paths:
        if os.path.isfile(p):
            files.append(p)
            continue
        for root, dirs, names in os.walk(p):
            dirs[:] = [d for d in dirs if d != "ddk"]
            files.extend(os.path.join(root, n) for n in sorted(names) if n.endswith((".cpp", ".h")))

    moved = unchanged = 0
    problems: list[str] = []

    for path in sorted(files):
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        out = []
        at = 0
        changed = False
        for m in CITE.finditer(text):
            addr_text, name = m.group(2), m.group(4)
            here = f"{path}:{text.count(chr(10), 0, m.start()) + 1}"
            address = int(addr_text, 16)
            new_address = None
            if name == "(object)":
                om = OBJECT_CITE.match(text, m.start())
                if om is None:
                    problems.append(f"{here}: an (object) citation that does not parse")
                else:
                    was = object_address(old_rom, om.group(4))
                    if was is None or was != address:
                        problems.append(f"{here}: {om.group(4)} is not at {addr_text} in {args.old}")
                    else:
                        now = object_address(new_rom, om.group(4))
                        if now is None:
                            problems.append(f"{here}: no object {om.group(4)} in {args.new}")
                        else:
                            new_address = now
                    m = om
            elif name.startswith("("):
                problems.append(f"{here}: {name} - nothing to look it up by")
            elif old_addr.get(address) != name and old_name.get(name) != address:
                problems.append(f"{here}: {name} is not at {addr_text} in {args.old}")
            elif name not in new_name:
                problems.append(f"{here}: no {name} in {args.new}")
            else:
                new_address = new_name[name]
            if new_address is None:
                unchanged += 1
                continue
            if new_address == address:
                unchanged += 1
                continue
            out.append(text[at:m.start(2)])
            out.append(f"0x{new_address:08x}")
            at = m.end(2)
            moved += 1
            changed = True
        out.append(text[at:])
        if changed and not args.check:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write("".join(out))

    for p in problems:
        print(p)
    verb = "would move" if args.check else "moved"
    print(f"{verb} {moved} citations, left {unchanged}, {len(problems)} need a person")
    return 0


if __name__ == "__main__":
    sys.exit(main())
