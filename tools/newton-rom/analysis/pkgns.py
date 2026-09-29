#!/usr/bin/env python3
"""A package's NewtonScript: its functions, disassembled, and its native ones.

Usage:
    python pkgns.py <file.pkg> --functions
    python pkgns.py <file.pkg> --disasm <holder or 0xref>...
    python pkgns.py <file.pkg> --refs SYMBOL...
    python pkgns.py <file.pkg> --natives [--native-disasm <holder or 0xref>]

A package's frames parts hold its objects as the ROM's object area holds the
ROM's (big-endian, the ARM layout: a header word of size << 8 | flags, a
word the GC uses, the class or map, then the slots); a pointer ref is the
object's offset in the package file plus one.  This reads them where they
lie, as nsfunctions.py reads the ROM's, and prints:

  --functions  every function object, named by the frame slot that holds it
               (frame.slot, the frame by its own slot in the part's top
               frame when it has one, else its offset): NewtonScript ones
               (class 0x32) with their argument counts, and native ones
               (class 0x232: code in a binary);
  --disasm     a NewtonScript function's bytecode (nsfunctions.py's
               disassembler: literals, the frequently called functions);
  --refs       every NewtonScript function whose literals include the
               symbol (who calls a function or sends a message by name);
  --natives    the native functions: NTK compiles a NewtonScript function
               marked native into ARM code, all of a package's into one
               binary, and the function object is [0x232, the binary,
               numArgs, closure, offset of its code in the binary, ...]
               (TInterpreter's NativeEntry 0x002f6558 reads it so); each is
               listed with its code offset and length (to the next one);
  --native-disasm  such a function's ARM code (capstone; its calls go to
               the runtime glue at the front of the binary).

A magic pointer (@n) is the ROM's and is shown as such.  Needs capstone
only for --native-disasm (tools/newton-rom/requirements.txt).
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsfunctions  # noqa: E402
import packages  # noqa: E402


class PackageImage(nsfunctions.ROM):
    """The package's bytes read as nsfunctions.ROM reads the ROM's."""

    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.rom = f.read()
        self.symbols = {}
        self.by_name = {}
        self.jump = {}
        self.parts = []
        p = packages.parse_package(self.rom, 0)
        reloc = 0
        if p["flags"] & 0x04000000:
            reloc = struct.unpack_from(">I", self.rom, p["directory_size"] + 4)[0]
        for i, part in enumerate(p["parts"]):
            start = p["directory_size"] + reloc + part["offset"]
            self.parts.append((i, part["flags"] & 3, start, part["size"]))

    def resolve_magic(self, ref: int) -> int:
        return ref

    def objects(self):
        """Every object ref in the package's frames parts, in order."""
        for _, kind, start, size in self.parts:
            if kind != 1:
                continue
            a = start
            end = start + size
            while a + 12 <= end:
                n = self.word(a) >> 8
                if n < 12:
                    break
                yield a + 1
                a += (n + 3) & ~3


def holders(pkg: PackageImage):
    """ref -> "frame.slot" for every value held in a frame slot (a frame
    named by the slot that holds it, where one does)."""
    frames = [r for r in pkg.objects() if pkg.flags(r) & 3 == 3 and pkg.is_ptr(pkg.cls(r))]
    names = {}
    for frame in frames:
        try:
            pairs = pkg.frame_slots(frame)
        except Exception:
            continue
        for tag, value in pairs:
            if tag is not None and pkg.is_ptr(value) and value not in names:
                names[value] = tag
    held = {}
    for frame in frames:
        try:
            pairs = pkg.frame_slots(frame)
        except Exception:
            continue
        fname = names.get(frame, "%#x" % frame)
        for tag, value in pairs:
            if tag is not None and pkg.is_ptr(value):
                held.setdefault(value, "%s.%s" % (fname, tag))
    return held


def functions(pkg: PackageImage):
    """(ref, kind, numArgs) of every function object: kind 'script' or 'native'."""
    out = []
    for ref in pkg.objects():
        if pkg.flags(ref) & 1 == 0 or pkg.size(ref) < 20:
            continue
        s = pkg.slots(ref)
        if s[0] == 0x32 and len(s) >= 5:
            out.append((ref, "script", (s[4] >> 2) & 0xffff))
        elif s[0] == 0x232 and len(s) >= 5:
            out.append((ref, "native", s[2] >> 2))
    return out


def natives(pkg: PackageImage):
    """(ref, binary, numArgs, offset, length) of each native function, the
    length running to the next function's code in the same binary."""
    rows = []
    for ref, kind, n in functions(pkg):
        if kind != "native":
            continue
        s = pkg.slots(ref)
        rows.append([ref, s[1], n, s[4] >> 2, None])
    by_binary = {}
    for row in rows:
        by_binary.setdefault(row[1], []).append(row)
    for binary, group in by_binary.items():
        group.sort(key=lambda r: r[3])
        size = len(pkg.data(binary))
        for i, row in enumerate(group):
            row[4] = (group[i + 1][3] if i + 1 < len(group) else size) - row[3]
    return sorted(rows, key=lambda r: r[3])


def find(pkg: PackageImage, held, name: str):
    if name.lower().startswith("0x"):
        return int(name, 16)
    for ref, h in held.items():
        if h.lower() == name.lower() or h.split(".")[-1].lower() == name.lower():
            return ref
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("--functions", action="store_true")
    ap.add_argument("--disasm", action="append", default=[])
    ap.add_argument("--refs", action="append", default=[])
    ap.add_argument("--natives", action="store_true")
    ap.add_argument("--native-disasm", action="append", default=[])
    ap.add_argument("--rom", help="a build directory: name the ROM functions NTK glue calls reach")
    args = ap.parse_args(argv)

    pkg = PackageImage(args.package)
    held = holders(pkg)
    if args.functions:
        for ref, kind, n in functions(pkg):
            print("%-60s %-6s %d args  (%#x)" % (held.get(ref, "%#x" % ref), kind, n, ref))
    for name in args.disasm:
        ref = find(pkg, held, name)
        if ref is None:
            print("%s: not found" % name)
            continue
        out = []
        nsfunctions.disassemble(pkg, ref, out)
        print("%s (%#x):" % (held.get(ref, name), ref))
        print("\n".join(out))
    if args.refs:
        wanted = {n.lower() for n in args.refs}
        for ref, kind, n in functions(pkg):
            if kind != "script":
                continue
            lits = pkg.slots(ref)[2]
            if not pkg.is_ptr(lits) or not pkg.flags(lits) & 1:
                continue
            for lit in pkg.slots(lits):
                sym = pkg.symname(lit) if pkg.is_ptr(lit) else None
                if sym is not None and sym.lower() in wanted:
                    print("%s (%#x): %s" % (held.get(ref, "%#x" % ref), ref, sym))
    if args.natives:
        for ref, binary, n, off, length in natives(pkg):
            print("%-50s %d args  code %#x+%#x, %d bytes  (%#x)" % (held.get(ref, "%#x" % ref), n, binary, off, length, ref))
    for name in args.native_disasm:
        import pkgdisasm
        if args.rom:
            pkgdisasm.load_rom(args.rom)
        ref = find(pkg, held, name)
        rows = [r for r in natives(pkg) if r[0] == ref]
        if not rows:
            print("%s: not a native function" % name)
            continue
        _, binary, n, off, length = rows[0]
        print("%s (%#x): %d args, %d bytes" % (held.get(ref, name), ref, n, length))
        pkgdisasm.disassemble(pkg.data(binary), off, off + length)
    return 0


if __name__ == "__main__":
    sys.exit(main())
