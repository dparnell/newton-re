#!/usr/bin/env python3
"""The ROM's public jump table, and which of its entries a package's native code calls.

Usage:
    python tools/newton-rom/analysis/gluetable.py build/MP2x00US [--all] [-o table.cpp]
    python tools/newton-rom/analysis/gluetable.py build/MP2x00US --package fixtures/.../x.pkg

NTK compiles a NewtonScript function marked native into ARM code (see
pkgns.py --natives).  That code calls the ROM through stubs at the front of
the package's native binary, each `ldr pc,[pc,#-4]` followed by an address
at 0x018xxxxx.  0x01800000 is where the MMU maps the ROM's *public jump
table*, gROMPublicJumpTable (physical ROM 0x00013000-0x00015e0c, 2947
entries, mapped through gROMPublicJumpTablePageTable at 0x00018000; set up
by UseROMJumpTables 0x001832e8): a table of `B` instructions, one per
function the ROM promises to third-party code, each branching to that
function's slot in the private patchable jump table at 0x01A00000
(jumptable.py), which is named in the debug symbols.  The public table is
mapped linearly - entry at physical 0x13000 + n is at virtual
0x01800000 + n - and its branches are encoded relative to that virtual
address, which is how the names are found: an entry's branch target is a
named slot of the private table.  The public table's offsets are fixed
across ROMs (that is its point), so a package's addresses resolve on any.

  (default)   the entries, as `offset  address  symbol  demangled`, those
              whose word is not a branch into the private table marked;
  --all       include the entries that are not branches (padding, reserved);
  --package   the stubs at the front of each native binary of a package, the
              public-table entry each one reaches and its symbol, and the
              other words those stubs read (the version-dependent stubs that
              load a ROM global's address - `mov ip,#0x1300; ldr ip,[ip,#0xdc]`
              reads the ROM's version word);
  -o FILE     a C++ table {offset, symbol} for src/armcpu's adapter.

Inputs: the build directory (rom.bin, symbols.txt); a package for --package.
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

PUBLIC_START = 0x00013000
PUBLIC_END = 0x00015e0c
PUBLIC_VIRTUAL = 0x01800000


def load(build_dir):
    rom = open(os.path.join(build_dir, "rom.bin"), "rb").read()
    syms = {}
    demangled = {}
    for line in open(os.path.join(build_dir, "symbols.txt"), encoding="latin-1"):
        p = line.rstrip("\n").split(None, 3)
        if len(p) < 3:
            continue
        try:
            a = int(p[0], 16)
        except ValueError:
            continue
        if a not in syms:
            syms[a] = p[2]
            rest = p[3] if len(p) > 3 else ""
            demangled[a] = rest.split("  [jump table")[0].strip()
    return rom, syms, demangled


def branch_target(word, pc):
    imm = word & 0xFFFFFF
    if imm & 0x800000:
        imm -= 0x1000000
    return (pc + 8 + imm * 4) & 0xFFFFFFFF


def entries(rom, syms, demangled):
    """[(offset, word, target or None, symbol, demangled)]"""
    out = []
    for off in range(0, PUBLIC_END - PUBLIC_START, 4):
        word = struct.unpack(">I", rom[PUBLIC_START + off:PUBLIC_START + off + 4])[0]
        if word >> 24 == 0xEA:
            t = branch_target(word, PUBLIC_VIRTUAL + off)
            out.append((off, word, t, syms.get(t), demangled.get(t, "")))
        else:
            out.append((off, word, None, None, ""))
    return out


def package_stubs(path):
    """{binary: [(stub offset, public address)]} and the other literal words."""
    import pkgns
    pkg = pkgns.PackageImage(path)
    rows = pkgns.natives(pkg)
    result = {}
    for binary in sorted({r[1] for r in rows}):
        data = pkg.data(binary)
        first = min(r[3] for r in rows if r[1] == binary)
        stubs = []
        for off in range(0, first - 4, 4):
            w0, w1 = struct.unpack(">II", data[off:off + 8])
            if w0 == 0xE51FF004 and (w1 >> 20) == 0x018:
                stubs.append((off, w1))
        result[binary] = (first, stubs)
    return result


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--package")
    ap.add_argument("-o", dest="output")
    args = ap.parse_args(argv)
    rom, syms, demangled = load(args.build_dir)
    table = entries(rom, syms, demangled)
    by_off = {e[0]: e for e in table}
    if args.package:
        for binary, (first, stubs) in package_stubs(args.package).items():
            print("binary %#x: code from %#x, %d stubs" % (binary, first, len(stubs)))
            for off, addr in stubs:
                e = by_off.get(addr - PUBLIC_VIRTUAL)
                name = e[3] if e and e[3] else "?"
                print("  stub %06x -> %#010x  %s  %s" % (off, addr, name, e[4] if e else ""))
        return 0
    if args.output:
        with open(args.output, "w") as f:
            f.write("// Generated by tools/newton-rom/analysis/gluetable.py; do not edit.\n")
            f.write("//   python tools/newton-rom/analysis/gluetable.py <build_dir> -o %s\n" % args.output.replace("\\", "/"))
            f.write("// The ROM's public jump table (gROMPublicJumpTable, virtual 0x01800000):\n")
            f.write("// each entry's offset and the function its branch reaches.\n\n")
            f.write("#include \"PublicJumpTable.h\"\n\n")
            f.write("const PublicJumpEntry kPublicJumpTable[] = {\n")
            n = 0
            for off, word, t, name, dem in table:
                if name:
                    f.write("\t{ 0x%04x, \"%s\" },\n" % (off, name))
                    n += 1
            f.write("};\n\nconst unsigned long kPublicJumpTableCount = %d;\n" % n)
        print("%d entries -> %s" % (sum(1 for e in table if e[3]), args.output))
        return 0
    for off, word, t, name, dem in table:
        if name or args.all:
            print("%06x  %#010x  %-40s %s" % (off, PUBLIC_VIRTUAL + off, name or ("(%08x)" % word), dem))
    return 0


if __name__ == "__main__":
    sys.exit(main())
