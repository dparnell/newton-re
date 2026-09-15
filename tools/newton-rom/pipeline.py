#!/usr/bin/env python3
"""Run the whole ROM -> Ghidra pipeline in one go.

Usage:
    python pipeline.py "DebugRom/MP2100 D" -o build/MP2100D [--ghidra <dir>] [--name MP2100D]
                       [--project build/ghidra] [--no-headers] [--no-ghidra] [--no-analyze]

Steps (each can also be run on its own, see README.md):
    1. extract_rom.py   -> <out>/rom.bin, <out>/layout.json
    2. dump_symbols.py  -> <out>/symbols.json, <out>/symbols.txt
    3. parse_headers.py -> <out>/types.json                    (needs libclang; skipped with --no-headers)
    4. ghidra_scripts/import_rom.py -> <project>/<name>.gpr   (needs pyghidra)
    5. ghidra_scripts/verify_types.py -> <out>/romfacts.json, <out>/verify-report.txt
    6. ghidra_scripts/apply_romfacts.py (+ auto-analysis unless --no-analyze)

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
    ap.add_argument("--headers", default=None, help="DDK header directory (default: <repo>/headers)")
    ap.add_argument("--no-headers", action="store_true", help="skip the DDK header types")
    ap.add_argument("--no-ghidra", action="store_true", help="stop before the Ghidra import")
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
    if not args.no_headers:
        hdr = args.headers or os.path.join(os.path.dirname(HERE), "..", "headers")
        run(py, os.path.join(HERE, "parse_headers.py"), os.path.normpath(hdr), "-o", os.path.join(args.out, "types.json"))
    if args.no_ghidra:
        return 0
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")
    gs = os.path.join(HERE, "ghidra_scripts")
    run(py, os.path.join(gs, "import_rom.py"), args.out, "--project", project, "--name", name,
        "--ghidra", args.ghidra, "--no-analyze")
    # verification needs a program to read; its findings (sizes, vtables) then go back in
    verify = subprocess.run([py, os.path.join(gs, "verify_types.py"), args.out, "--project", project,
                             "--name", name, "--ghidra", args.ghidra])
    if verify.returncode not in (0, 1):
        verify.check_returncode()
    if verify.returncode == 1:
        print("note: verify_types.py reported header/ROM differences; see verify-report.txt", flush=True)
    cmd = [py, os.path.join(gs, "apply_romfacts.py"), args.out, "--project", project, "--name", name,
           "--ghidra", args.ghidra]
    if not args.no_analyze:
        cmd.append("--analyze")
    run(*cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
