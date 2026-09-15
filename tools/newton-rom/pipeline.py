#!/usr/bin/env python3
"""Run the whole ROM -> Ghidra pipeline in one go.

Usage:
    python pipeline.py "DebugRom/MP2100 D" -o build/MP2100D [--ghidra <dir>] [--name MP2100D]
                       [--project build/ghidra] [--no-ghidra] [--no-analyze]

Steps (each can also be run on its own, see README.md):
    1. extract_rom.py   -> <out>/rom.bin, <out>/layout.json
    2. dump_symbols.py  -> <out>/symbols.json, <out>/symbols.txt
    3. ghidra_scripts/import_rom.py -> <project>/<name>.gpr   (needs pyghidra)

The ROM directory must contain exactly one "* image" and one "* high" file.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom_dir", help="directory with the '... image' and '... high' files")
    ap.add_argument("-o", "--out", required=True, help="output directory for rom.bin/layout/symbols")
    ap.add_argument("--name", default=None, help="Ghidra program name (default: derived from rom_dir)")
    ap.add_argument("--project", default=None, help="Ghidra project dir (default: <out>/../ghidra)")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("--no-ghidra", action="store_true", help="stop after symbols.json")
    ap.add_argument("--no-analyze", action="store_true", help="skip Ghidra auto-analysis")
    args = ap.parse_args(argv)

    images = glob.glob(os.path.join(args.rom_dir, "* image"))
    highs = glob.glob(os.path.join(args.rom_dir, "* high"))
    if len(images) != 1 or len(highs) != 1:
        ap.error(f"expected one '* image' and one '* high' file in {args.rom_dir}")
    image, high = images[0], highs[0]
    name = args.name or os.path.basename(os.path.normpath(args.rom_dir)).replace(" ", "")
    project = args.project or os.path.join(os.path.dirname(os.path.normpath(args.out)), "ghidra")
    py = sys.executable

    def run(*cmd):
        print("+", " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
        subprocess.run(cmd, check=True)

    run(py, os.path.join(HERE, "extract_rom.py"), image, "--rex", high, "-o", args.out)
    run(py, os.path.join(HERE, "dump_symbols.py"), image, "-o", os.path.join(args.out, "symbols.json"),
        "--text", os.path.join(args.out, "symbols.txt"))
    if args.no_ghidra:
        return 0
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")
    cmd = [py, os.path.join(HERE, "ghidra_scripts", "import_rom.py"), args.out, "--project", project,
           "--name", name, "--ghidra", args.ghidra]
    if args.no_analyze:
        cmd.append("--no-analyze")
    run(*cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
