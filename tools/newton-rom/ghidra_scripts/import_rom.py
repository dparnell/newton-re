#!/usr/bin/env python3
"""Create a Ghidra project for an extracted Newton ROM and apply its symbols.

Usage:
    python ghidra_scripts/import_rom.py <build dir> --project <dir> [--name MP2100D]
                         [--ghidra <ghidra install dir>] [--no-analyze]

<build dir> is the output of extract_rom.py + dump_symbols.py (rom.bin,
layout.json, symbols.json).  The script:

  1. creates (or opens) the Ghidra project <dir>/<name>.gpr
  2. imports rom.bin with the Binary loader at address 0 as
     language ARM:BE:32:v4 (StrongARM SA-110, big-endian, no Thumb),
     compiler spec "apcs"
  3. applies the memory map, jump table, classes, functions, thunks and labels
     (newtonrom.ghidra_import)
  4. runs Ghidra auto-analysis (unless --no-analyze) and saves

Requires the `pyghidra` package that ships with Ghidra 11.3+:
    pip install --no-index --find-links "<ghidra>/Ghidra/Features/PyGhidra/pypkg/dist" pyghidra
The Ghidra install directory is taken from --ghidra or $GHIDRA_INSTALL_DIR.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

LANGUAGE = "ARM:BE:32:v4"
COMPILER = "apcs"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir", help="directory containing rom.bin, layout.json, symbols.json")
    ap.add_argument("--project", required=True, help="directory to create the Ghidra project in")
    ap.add_argument("--name", default="NewtonROM", help="project and program name (default: NewtonROM)")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"),
                    help="Ghidra installation directory (default: $GHIDRA_INSTALL_DIR)")
    ap.add_argument("--no-analyze", action="store_true", help="skip Ghidra auto-analysis")
    args = ap.parse_args(argv)

    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")
    rom_bin = os.path.abspath(os.path.join(args.build_dir, "rom.bin"))
    for f in ("rom.bin", "layout.json", "symbols.json"):
        if not os.path.exists(os.path.join(args.build_dir, f)):
            ap.error(f"{f} not found in {args.build_dir}; run extract_rom.py / dump_symbols.py first")

    import pyghidra
    t0 = time.time()

    def log(msg: str) -> None:
        print(f"[{time.time() - t0:6.1f}s] {msg}", flush=True)

    log(f"starting Ghidra from {args.ghidra}")
    pyghidra.start(install_dir=args.ghidra)
    from newtonrom import ghidra_import

    layout, symbols, rom, types, romfacts = ghidra_import.load_inputs(args.build_dir)
    log("DDK types: " + ("types.json loaded" if types else "no types.json, skipping header types"))
    log("ROM facts: " + ("romfacts.json loaded" if romfacts else "no romfacts.json (run verify_types.py to create it)"))
    project_dir = os.path.abspath(args.project)
    os.makedirs(project_dir, exist_ok=True)
    project = pyghidra.open_project(project_dir, args.name, create=True)
    try:
        if project.getProjectData().getFile("/" + args.name) is not None:
            print(f"error: program /{args.name} already exists in the project; "
                  f"delete it or choose another --name", file=sys.stderr)
            return 1
        log(f"importing {rom_bin} as {LANGUAGE}/{COMPILER}")
        results = (pyghidra.program_loader().source(rom_bin).project(project).name(args.name)
                   .language(LANGUAGE).compiler(COMPILER).loaders("BinaryLoader").load())
        monitor = pyghidra.task_monitor()
        results.save(monitor)
        results.close()

        with pyghidra.program_context(project, "/" + args.name) as program:
            with pyghidra.transaction(program, "Newton ROM symbols"):
                ghidra_import.apply(program, layout, symbols, rom, monitor, log, types, romfacts)
            program.save("Newton ROM symbols", monitor)
            if not args.no_analyze:
                log("running auto-analysis (this takes a while) ...")
                pyghidra.analyze(program, monitor)
                program.save("auto-analysis", monitor)
        log(f"done: project {os.path.join(project_dir, args.name + '.gpr')}")
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
