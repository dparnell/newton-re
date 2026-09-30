#!/usr/bin/env python3
"""Check that every task world in the reconstruction says how big it is.

Usage:
    python tools/newton-rom/analysis/worldsizes.py build/MP2x00US [--src src] [--all]

A TUTaskWorld runs in a task of its own on a copy of the object that
started it, and the copy is GetSizeOf() bytes long (TUTaskWorld::StartTask
hands the kernel `this` and GetSizeOf(); TTask::Init copies that many bytes
to the top of the new task's stack block).  A class that adds fields and
does not answer its own size is copied short: its own fields lie past the
end of the stack block, and writing them damages whatever the heap put
there next.  (That was the one-off crash in TUPort::Receive: TInker had no
GetSizeOf, and its fNewtEventType wrote 'inkr' into a fork's
TAppWorldState - docs/work-log.md, 2026-09-30.)  On the host every such
class must answer sizeof itself, since the ROM's byte counts are too small
for a host object with pointer-sized fields.

Inputs: the reconstruction's headers and sources under --src (every class
deriving, directly or through others, from TUTaskWorld; the test programs
under */tests/ are left out) and, for the ROM's side, the build directory's
rom.bin and symbols.json (extract_rom.py, dump_symbols.py).

For each class it prints:
  - the host: `own` when the class defines GetSizeOf answering
    sizeof(itself), `WRONG` when it defines one answering something else,
    `MISSING` when it defines none;
  - the ROM: the vtable's +0x04 slot (the vtable read out of the
    constructor by verify_types.py - romfacts.json - or found as the table
    whose +0x00 is the class's destructor): `own 0x118` when the slot is
    the class's own GetSizeOf (named or not - a small one may be an unnamed
    stub) with the byte count it answers, `inherits TFoo` when it is a base
    class's, `abstract` when it is pure virtual (the class is never a
    task's object itself, so it needs none), `?` when no vtable is found
    (a GetSizeOf named for the class then still counts as its own).
Exit status 1 when any class is MISSING or WRONG (with --all every class
is listed, otherwise only those and the ones the ROM could not place).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ROOT_CLASS = "TUTaskWorld"


def world_classes(src):
    """{class: base} for every class in src deriving from TUTaskWorld."""
    base_of = {}
    pattern = re.compile(r"\bclass\s+(\w+)\s*:\s*public\s+(\w+)")
    for dirpath, dirnames, filenames in os.walk(src):
        if os.sep + "tests" in dirpath + os.sep or dirpath.endswith("tests"):
            continue
        for f in filenames:
            if not f.endswith((".h", ".cpp")):
                continue
            text = open(os.path.join(dirpath, f), encoding="utf-8", errors="replace").read()
            for m in pattern.finditer(text):
                base_of.setdefault(m.group(1), m.group(2))

    def descends(c):
        seen = set()
        while c in base_of and c not in seen:
            seen.add(c)
            c = base_of[c]
            if c == ROOT_CLASS:
                return True
        return False

    return {c: b for c, b in base_of.items() if descends(c)}


def host_sizes(src, classes):
    """{class: 'own' | 'WRONG <what>' | 'MISSING'}."""
    texts = []
    for dirpath, dirnames, filenames in os.walk(src):
        if os.sep + "tests" in dirpath + os.sep or dirpath.endswith("tests"):
            continue
        for f in filenames:
            if f.endswith((".h", ".cpp")):
                texts.append(open(os.path.join(dirpath, f), encoding="utf-8", errors="replace").read())
    text = "\n".join(texts)
    result = {}
    for c in classes:
        # out of line: TFoo::GetSizeOf() { ... return X; }
        m = re.search(r"\b%s::GetSizeOf\s*\(\s*(?:void)?\s*\)\s*\{(.*?)\}" % re.escape(c), text, re.S)
        body = m.group(1) if m else None
        if body is None:
            # inline in the class: GetSizeOf() { return sizeof(TFoo); }
            cm = re.search(r"\bclass\s+%s\s*:[^{]*\{(.*?)\n\};" % re.escape(c), text, re.S)
            if cm:
                im = re.search(r"GetSizeOf\s*\(\s*(?:void)?\s*\)\s*\{(.*?)\}", cm.group(1), re.S)
                if im:
                    body = im.group(1)
        if body is None:
            result[c] = "MISSING"
        elif re.search(r"sizeof\s*\(\s*%s\s*\)" % re.escape(c), body):
            result[c] = "own"
        else:
            result[c] = "WRONG (%s)" % " ".join(body.split())
    return result


def load_rom(build):
    with open(os.path.join(build, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    items = data["symbols"] if isinstance(data, dict) and "symbols" in data else data
    by_addr, by_name = {}, {}
    for s in items:
        by_addr.setdefault(s["address"], s["name"])
        by_name.setdefault(s["name"], []).append(s["address"])
    with open(os.path.join(build, "rom.bin"), "rb") as f:
        rom = f.read()
    return by_addr, by_name, rom


def word(rom, at):
    return struct.unpack(">I", rom[at:at + 4])[0]


def branch_target(w, at):
    if (w >> 24) != 0xEA:
        return None
    off = w & 0xFFFFFF
    if off & 0x800000:
        off -= 0x1000000
    return at + 8 + off * 4


def mangled(c):
    return "%d%s" % (len(c), c)


def rom_branches_to(rom, wanted):
    hits = []
    for at in range(0, len(rom) - 3, 4):
        w = word(rom, at)
        if (w >> 24) == 0xEA and branch_target(w, at) in wanted:
            hits.append(at)
    return hits


def mov_immediate(rom, at):
    """The value of a `mov r0,#imm` at `at` (a GetSizeOf's first word), or None."""
    if at is None or at + 4 > len(rom):
        return None
    w = word(rom, at)
    if (w & 0xFFFFF000) != 0xE3A00000:
        return None
    rot = ((w >> 8) & 0xF) * 2
    imm = w & 0xFF
    return ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF


def rom_size(by_addr, by_name, rom, vtables, c):
    """The ROM's +0x04 slot for the class: ('own', size) / ('inherits',
    name) / ('abstract', None) / ('?', why).  The vtable is the one
    verify_types.py read out of the constructor (romfacts.json), else the
    one a branch to the destructor sits at the start of; failing both, a
    GetSizeOf named for the class says it has its own."""
    hits = list(vtables.get(c, []))
    if not hits:
        dtor = "__dt__%sFv" % mangled(c)
        wanted = set(by_name.get(dtor, []))
        if wanted:
            hits = rom_branches_to(rom, wanted)
    if not hits:
        bodies = [a for a in by_name.get("GetSizeOf__%sFv" % mangled(c), []) if a < 0x01000000]
        if bodies:
            return ("own", mov_immediate(rom, bodies[0]))
        return ("?", "no vtable found")
    answers = set()
    for vt in hits:
        target = branch_target(word(rom, vt + 4), vt + 4)
        if target is None:
            continue
        name = by_addr.get(target)
        if name is None or name == "GetSizeOf__%sFv" % mangled(c):
            body = target
            if name is not None:
                bodies = [a for a in by_name.get(name, []) if a < 0x01000000]
                body = bodies[0] if bodies else None
            answers.add(("own", mov_immediate(rom, body)))
        elif name == "__pvfn__Fv":
            answers.add(("abstract", None))
        elif name.startswith("GetSizeOf__"):
            answers.add(("inherits", re.sub(r"^GetSizeOf__\d+(\w+?)Fv$", r"\1", name)))
        else:
            # (a branch to the destructor that is not a world's vtable - a
            # handler class of the same world's, say, reaching it)
            continue
    if not answers:
        return ("?", "no vtable found")
    if len(answers) == 1:
        return answers.pop()
    return ("?", "vtables disagree: %s" % sorted(answers, key=str))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", help="the ROM build directory (rom.bin, symbols.json)")
    ap.add_argument("--src", default=os.path.join(REPO, "src"), help="the reconstruction (default: src/)")
    ap.add_argument("--all", action="store_true", help="list every world class, not only the problems")
    a = ap.parse_args(argv)
    classes = world_classes(a.src)
    host = host_sizes(a.src, classes)
    by_addr, by_name, rom = load_rom(a.build)
    vtables = {}
    facts = os.path.join(a.build, "romfacts.json")
    if os.path.exists(facts):
        vtables = json.load(open(facts, encoding="utf-8")).get("vtables", {})
    bad = 0
    for c in sorted(classes):
        kind, what = rom_size(by_addr, by_name, rom, vtables, c)
        if kind == "own":
            rom_text = "own 0x%x" % what if what is not None else "own (not a mov)"
        elif kind == "inherits":
            rom_text = "inherits %s" % what
        elif kind == "abstract":
            rom_text = "abstract (pure virtual)"
        else:
            rom_text = "? (%s)" % what
        # an abstract class (the ROM's slot is pure virtual) is never a
        # task's object itself: its concrete subclasses answer
        problem = host[c] != "own" and not (kind == "abstract" and host[c] == "MISSING")
        bad += problem
        if a.all or problem or kind == "?":
            print("%-24s host %-10s ROM %s   (base %s)" % (c, host[c].split(" ")[0], rom_text, classes[c]))
            if host[c].startswith("WRONG"):
                print("%24s   answers %s" % ("", host[c][6:]))
    print("%d world classes, %d without their own sizeof" % (len(classes), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
