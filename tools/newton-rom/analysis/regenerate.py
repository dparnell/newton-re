#!/usr/bin/env python3
"""Regenerate every generated file in src/ and docs/ from one ROM.

Usage:
    python regenerate.py build/MP2x00US [--list] [--only NAME]...

Fifteen files under `src/` and four under `docs/` are read out of the
ROM rather than written by hand, each by one of the scripts in this
directory, and each says in its header which command made it.  That is
fine while there is one ROM, but pointing the reconstruction at another
image (see recite.py) means running all of them again, in the right way,
against the new build directory - so the commands live here as well, in
one list, and this runs them.

The one table that cannot be named is `kResampleFilter`: the ROM has no
symbol on it, so romtable.py has to be given its address.  It is found
here instead, by looking for the German ROM's copy of it in whichever ROM
is being read, which works because the filter is the same in both.

`docs/os600/swi-table.md` is not in the list: swi_table.py disassembles
SWIBoot's dispatch through Ghidra, so it needs a project built for the
ROM first (pipeline.py), and is run by hand afterwards.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

# (name, script, arguments; {build} is the build directory)
GENERATED = [
    ("romconstants",
     "romconstants.py", ["{build}", "-o", "src/frames"]),
    ("natives",
     "nsfunctions.py", ["{build}", "--natives", "-o", "src/frames/ROMNatives.cpp"]),
    ("grammar",
     "nsgrammar.py", ["{build}", "-o", "src/frames", "--doc", "docs/frames/grammar.md"]),
    ("lz",
     "romtable.py", ["{build}", "O10", "O9", "O8", "O7", "O6", "O5", "O4", "O3", "O2", "O1",
                     "CL", "CLBase", "CLB", "LL", "LLB", "LLBase", "CopyValue:u8", "LZCopyBits:u8",
                     "-o", "src/compression/LZTables.cpp"]),
    ("unicode",
     "romtable.py", ["{build}", "gUnicodeLookupTable:u8:32", "-o", "src/compression/UnicodeTables.cpp"]),
    ("printliterals",
     "romtable.py", ["{build}", "gPrintLiterals:cstr:35", "-o", "src/frames/PrintLiterals.cpp"]),
    ("charheight",
     "romtable.py", ["{build}", "CharHeight:i32:512",
                     "-o", "src/recognition/CharHeightTable.cpp"]),
    ("easteregg",
     "romtable.py", ["{build}", "kSearchEasterWords@0x0037757c:str9:8",
                     "kSearchEasterReplies@0x003775c4:str23:8",
                     "-o", "src/recognition/SearchEasterEgg.cpp"]),
    ("fragment",
     "romtable.py", ["{build}", "gFragmentParams@0x0c100de4:i32:6",
                     "gXProjectionParams@0x0c100dfc:i16:2",
                     "-o", "src/recognition/FragmentTables.cpp"]),
    ("geocontext",
     "romtable.py", ["{build}", "kGeoWeights@0x003714d0:i32:81",
                     "-o", "src/recognition/GeoTables.cpp"]),
    ("angles",
     "romtable.py", ["{build}", "kSlopeWhole@0x00380cbd:u8:27", "kSlopeFraction@0x00380cd8:u16:91",
                     "kDegreesOfFraction@0x00380d8e:u8:64", "kTangentBelowOne@0x00380dd0:u32:46",
                     "kDegreesOfWhole@0x00380e80:u8:64", "kTangentAboveOne@0x00380ec0:u32:46",
                     "-o", "src/toolbox/AngleTables.cpp"]),
    ("shapes",
     "romtable.py", ["{build}", "displayAngle:i32:25",
                     "-o", "src/recognition/ShapeTables.cpp"]),
    ("lelang",
     "romtable.py", ["{build}", "AckNodeSizeTab:u8:4",
                     "-o", "src/recognition/LELangTables.cpp"]),
    ("arprob",
     "romtable.py", ["{build}", "ArProbEncodeLu1:i16:512", "ArProbEncodeLu2:i16:1024",
                     "ArProbDecodeLu:i32:1024",
                     "ArSigLu:i32:355", "ArSigSlopeLu:i32:355",
                     "-o", "src/recognition/ArProbTables.cpp"]),
    ("crc16",
     "romtable.py", ["{build}", "kCrc16HTbl:u16:16", "kCrc16LTbl:u16:16", "IrCRCLookupTable:u16:256",
                     "-o", "src/utility/CRC16Tables.cpp"]),
    ("resample",
     "romtable.py", ["{build}", "kResampleFilter@{resample:#010x}:i32:262",
                     "-o", "src/sound/ResampleTables.cpp"]),
    ("memobj",
     "memobj_tables.py", ["{build}", "-o", "docs/os600/memobj-tables.md",
                          "--cpp", "src/os600/kernel/MemObjTables.cpp"]),
    ("exceptions",
     "exception_names.py", ["{build}", "-o", "src/os600/user/ExceptionNames.cpp"]),
    ("packages",
     "packages.py", ["{build}", "--doc", "docs/packages/rex-packages.md"]),
    ("classinfos",
     "classinfo.py", ["{build}", "--all", "-o", "docs/protocols/classinfos.md"]),
    ("spellmaps",
     "spellmaps.py", ["{build}", "-o", "src/recognition"]),
    ("romdicts",
     "romdicts.py", ["{build}", "-o", "src/recognition"]),
    ("rosci",
     "rosci.py", ["{build}", "-o", "src/recognition/RosCITables.cpp"]),
    ("bigrammar",
     "bigrammar.py", ["{build}", "-o", "src/recognition/ROMGrammar.cpp",
                      "--doc", "docs/recognition/grammar.md"]),
    ("bpnet",
     "bpnet.py", ["{build}", "-o", "src/recognition/BPNetTables.cpp"]),
    ("render",
     "render.py", ["{build}", "-o", "src/recognition/RenderTables.cpp"]),
    ("mmumap",
     "mmumap.py", ["{build}", "--doc", "docs/memory/mmu-map.md"]),
    ("factorysoups",
     "soupdefs.py", ["{build}", "-o", "src/host/FactorySoups.cpp"]),
]

# The windowed sinc of ResampleTables.cpp, by its first words: the ROM has
# no symbol on it (it is a static in the sound code), and it is identical
# in both images, so its address is found by looking for it.
kResampleFilterHead = bytes.fromhex(
    "00000000" "ffffffd9" "ffffffb2" "ffffff8e" "ffffff6f" "ffffff58"
    "ffffff49" "ffffff44" "ffffff49" "ffffff59" "ffffff75" "ffffff9b")


def resample_filter(build_dir: str) -> int:
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    found = []
    at = rom.find(kResampleFilterHead)
    while at >= 0:
        if at % 4 == 0:
            found.append(at)
        at = rom.find(kResampleFilterHead, at + 1)
    if len(found) != 1:
        raise ValueError("%s: %d resample filters, expected one" % (build_dir, len(found)))
    return found[0]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir", help="the build directory to read (build/MP2100D, build/MP2x00US)")
    ap.add_argument("--list", action="store_true", help="print the commands without running them")
    ap.add_argument("--only", action="append", default=[], metavar="NAME",
                    help="run only these (by the names --list shows)")
    args = ap.parse_args(argv)

    build = args.build_dir.replace(os.sep, "/")
    values = {"build": build, "resample": resample_filter(args.build_dir)}

    failed = 0
    for name, script, arguments in GENERATED:
        if args.only and name not in args.only:
            continue
        command = [sys.executable, os.path.join(HERE, script)] + [a.format(**values) for a in arguments]
        shown = " ".join(["python", "tools/newton-rom/analysis/" + script]
                         + [a.format(**values) for a in arguments])
        if args.list:
            print(f"{name:15} {shown}")
            continue
        print(f"== {name}: {shown}")
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.DEVNULL)
        if result.returncode != 0:
            print(f"   FAILED ({result.returncode})", file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
