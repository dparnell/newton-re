#!/usr/bin/env python3
"""Apply romfacts.json (from verify_types.py) to an existing Ghidra project.

Usage:
    python apply_romfacts.py <build dir> --project <dir> --name MP2100D [--ghidra <dir>] [--analyze]

Sets class structure sizes from the constructors' allocations, labels and
types the vtables (and makes every vtable entry a thunk of its method), and
names functions only reachable through vtable slots.  With --analyze, Ghidra
auto-analysis runs afterwards.  import_rom.py does the same automatically when
romfacts.json already exists in the build directory.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="NewtonROM")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("--analyze", action="store_true", help="run auto-analysis afterwards")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    t0 = time.time()

    def log(msg):
        print(f"[{time.time() - t0:6.1f}s] {msg}", flush=True)

    pyghidra.start(install_dir=args.ghidra)
    from newtonrom import ghidra_import

    layout, symbols, rom, types, romfacts = ghidra_import.load_inputs(args.build_dir)
    if romfacts is None:
        print("error: no romfacts.json in the build directory; run verify_types.py first", file=sys.stderr)
        return 1
    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            monitor = pyghidra.task_monitor()
            with pyghidra.transaction(program, "Newton ROM facts"):
                stats = ghidra_import.Stats()
                classes = ghidra_import.create_classes(program, symbols["symbols"], log, stats)
                from newtonrom.headers import HeaderTypes
                header = HeaderTypes(types) if types else None
                mapper = ghidra_import.TypeMapper(program, classes, stats, header)
                ghidra_import.apply_romfacts(program, romfacts, mapper, classes, monitor, log, stats)
                for key in sorted(stats):
                    log(f"  {key}: {stats[key]}")
            program.save("Newton ROM facts", monitor)
            if args.analyze:
                log("running auto-analysis ...")
                pyghidra.analyze(program, monitor)
                program.save("auto-analysis", monitor)
        log("done")
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
