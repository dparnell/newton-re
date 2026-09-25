#!/usr/bin/env python3
"""Emit the handwriting engine's common info and its character tables as C++.

Usage:
    python rosci.py <build_dir> -o <src/recognition/RosCITables.cpp>
    build/venv/Scripts/python tools/newton-rom/analysis/rosci.py build/MP2x00US -o src/recognition/RosCITables.cpp

`RosCI` is the block of trained numbers the whole Rosetta engine measures
against: how small a stroke may be, which of the 256 character codes it may
answer, what each of them is worth on eight different scales, and how a
character that is really two characters is put together.  `CharInitialize`
(ROM 0x00057074) makes it by copying a 0x10c-byte template, `rosCI`
(0x0036b6c0), into a fresh block, so that the engine may change the parts of
it an area sets - which is why the template is constant and the block is not.

This script writes the template out as a `RosCommonInfo` initialiser, with
the ten tables it points at emitted beside it and named as the ROM's own
debug symbols name them, so the pointers can be written as C names rather
than as ROM addresses.  The struct's field order is `recognition/RosEngine.h`'s
and is repeated in FIELDS below; a field nothing has been seen to read is
`fFieldNNN` there and here.

The output should not be edited by hand: regenerate it after a re-import.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romid			# the ROM names itself in what this writes


# The tables `rosCI` points at, in the order they are emitted: name, the
# element type, and how many.  The eight `rosCharParam` tables are one
# number per character code on eight scales of the engine's own; the two
# `legal` tables are 256 bits each, saying which character codes may be
# used at all; the rest are one byte per character code.
TABLES = [
    ("rosCharParam0", "Fixed", 256), ("rosCharParam1", "Fixed", 256),
    ("rosCharParam2", "Fixed", 256), ("rosCharParam3", "Fixed", 256),
    ("rosCharParam4", "Fixed", 256), ("rosCharParam5", "Fixed", 256),
    ("rosCharParam6", "Fixed", 256), ("rosCharParam7", "Fixed", 256),
    ("rosCharLegalNet", "ULong", 8), ("rosCharLegalUse", "ULong", 8),
    ("rosCharOfNetNode", "UByte", 256), ("rosCharToNetNode", "UByte", 256),
    ("rosCharCompoundPart1", "UByte", 256), ("rosCharCompoundPart2", "UByte", 256),
    ("rosCapHackCaseFlags", "UByte", 256), ("rosCapHackAltCase1", "UByte", 256),
    ("rosCapHackAltCase2", "UByte", 256),
]

# `rosCharParams` is an array of pointers to the eight parameter tables.
PARAMS_TABLE = "rosCharParams"

# The 0x10c-byte template, slot by slot: the C field name, whether the slot
# holds a pointer to one of the tables above, and how many slots the field
# covers.  This is `RosCommonInfo`'s declaration in RosEngine.h.
FIELDS = [
    ("fField00", "word", 3),			# +0x00
    ("fCharParams", "ptr", 1),			# +0x0c
    ("fField10", "word", 1),			# +0x10
    ("fLegalNet", "ptr", 1),			# +0x14
    ("fLegalUse", "ptr", 1),			# +0x18
    ("fCharOfNetNode", "ptr", 1),		# +0x1c
    ("fCharToNetNode", "ptr", 1),		# +0x20
    ("fCompoundPart1", "ptr", 1),		# +0x24
    ("fCompoundPart2", "ptr", 1),		# +0x28
    ("fCapCaseFlags", "ptr", 1),		# +0x2c
    ("fCapAltCase1", "ptr", 1),			# +0x30
    ("fCapAltCase2", "ptr", 1),			# +0x34
    ("fField38", "word", 5),			# +0x38 .. +0x48
    ("fMinStrokeSize", "word", 1),		# +0x4c
    ("fField50", "word", 1),			# +0x50
    ("fMinCharWidth", "word", 1),		# +0x54
    ("fField58", "word", 2),			# +0x58 .. +0x5c
    ("fCharWidthFraction", "word", 1),	# +0x60
    ("fReachFraction", "word", 1),		# +0x64
    ("fField68", "word", 6),			# +0x68 .. +0x7c
    ("fLinkOverlap", "word", 1),		# +0x80
    ("fCrossOverlap", "word", 1),		# +0x84
    ("fJoinOverlap", "word", 1),		# +0x88
    ("fBreakOverlap", "word", 1),		# +0x8c
    ("fEndFraction", "word", 1),		# +0x90
    ("fLinkDistance", "word", 1),		# +0x94
    ("fField98", "word", 29),			# +0x98 .. +0x108
]

# A `Fixed` is signed and is written in decimal, as the other generated
# tables of signed numbers are; the bit sets and the byte tables are
# written in hexadecimal, because what matters about them is their bits.
ELEM = {"Fixed": (4, ">i", "%8d"), "ULong": (4, ">I", "0x%08x"), "UByte": (1, "B", "0x%02x")}
PER_LINE = {"Fixed": 8, "ULong": 8, "UByte": 12}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    with open(os.path.join(args.build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    by_name = {}
    for s in data["symbols"]:
        if "jt_index" not in s:
            by_name.setdefault(s["name"], s["address"])

    for name in [n for n, _, _ in TABLES] + [PARAMS_TABLE, "rosCI"]:
        if name not in by_name:
            print(f"error: no symbol {name} in {args.build_dir}", file=sys.stderr)
            return 1
    by_addr = {by_name[n]: n for n, _, _ in TABLES}

    out = ["// Generated by tools/newton-rom/analysis/rosci.py; do not edit.",
           f"//   build/venv/Scripts/python tools/newton-rom/analysis/rosci.py {args.build_dir.replace(os.sep, '/')} -o {args.output.replace(os.sep, '/')}",
           f"// The handwriting engine's common info and its character tables, read",
           f"// from the {romid.rom_version(args.build_dir)} ROM at the addresses cited.",
           "",
           '#include "RosEngine.h"',
           ""]

    for name, kind, count in TABLES:
        addr = by_name[name]
        size, fmt, form = ELEM[kind]
        values = [struct.unpack(fmt, rom[addr + i * size: addr + (i + 1) * size])[0] for i in range(count)]
        out.append(f"// ROM 0x{addr:08x} {name}")
        out.append(f"extern const {kind}\t{name}[{count}];")
        out.append(f"const {kind}\t{name}[{count}] = {{")
        per = PER_LINE[kind]
        for i in range(0, count, per):
            row = ", ".join(form % (v if kind == "Fixed" else v & 0xffffffff) for v in values[i:i + per])
            out.append(f"\t{row},")
        out[-1] = out[-1][:-1]
        out.append("};")
        out.append("")

    # the eight parameter tables, by name
    addr = by_name[PARAMS_TABLE]
    names = []
    for i in range(8):
        target = struct.unpack(">I", rom[addr + i * 4: addr + i * 4 + 4])[0]
        names.append(by_addr.get(target, f"/* 0x{target:08x} */ nil"))
    out.append(f"// ROM 0x{addr:08x} {PARAMS_TABLE}")
    out.append(f"extern const Fixed* const\t{PARAMS_TABLE}[8];")
    out.append(f"const Fixed* const\t{PARAMS_TABLE}[8] = {{")
    for n in names:
        out.append(f"\t{n},")
    out[-1] = out[-1][:-1]
    out.append("};")
    out.append("")

    # ... and the template itself
    addr = by_name["rosCI"]
    slots = [struct.unpack(">I", rom[addr + i * 4: addr + i * 4 + 4])[0] for i in range(0x10c // 4)]
    pointers = dict(by_addr)
    pointers[by_name[PARAMS_TABLE]] = PARAMS_TABLE
    out.append(f"// ROM 0x{addr:08x} rosCI")
    out.append("// The template `CharInitialize` copies into a block of its own.")
    out.append("const RosCommonInfo\trosCI = {")
    at = 0
    for name, kind, count in FIELDS:
        values = slots[at:at + count]
        at += count
        if kind == "ptr":
            target = values[0]
            if target not in pointers:
                print(f"error: {name} points at 0x{target:08x}, which is not one of the tables", file=sys.stderr)
                return 1
            out.append(f"\t{pointers[target]},\t// {name}")
        elif count == 1:
            out.append(f"\t0x{values[0]:08x},\t// {name}")
        elif count <= 6:
            row = ", ".join("0x%08x" % v for v in values)
            out.append(f"\t{{ {row} }},\t// {name}")
        else:
            out.append(f"\t{{\t\t\t\t// {name}")
            for i in range(0, count, 6):
                out.append("\t\t" + ", ".join("0x%08x" % v for v in values[i:i + 6]) + ",")
            out[-1] = out[-1][:-1]
            out.append("\t},")
    if out[-1] == "\t},":
        out[-1] = "\t}"
    else:
        out[-1] = out[-1].replace(",\t//", "\t//", 1)
    out.append("};")
    out.append("")

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"wrote {args.output}: {len(TABLES) + 2} tables")
    return 0


if __name__ == "__main__":
    sys.exit(main())
