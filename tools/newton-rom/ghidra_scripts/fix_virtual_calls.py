#!/usr/bin/env python3
"""Re-mark the virtual call sites of an existing Ghidra project.

Usage:
    python fix_virtual_calls.py --project <dir> --name MP2100D [--ghidra <dir>]

The ROM's virtual calls (`mov lr,pc / add pc,rN,#slot*4`) are jumps to
Ghidra; import_rom.py marks them as calls that fall through
(newtonrom.ghidra_import.fix_virtual_calls) so that functions are not
truncated after their first virtual call.  Projects imported before that
fix used a call-and-return override, which kept the disassembly going but
made the decompiler show a `return` after every virtual call.  This script
runs the fix over the whole program again: every site gets the fall-through
call override, and any code it opens up is disassembled.  Re-decompile
afterwards (analysis/decompile.py); no auto-analysis is needed.
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
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="NewtonROM")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    t0 = time.time()

    def log(msg):
        print(f"[{time.time() - t0:6.1f}s] {msg}", flush=True)

    pyghidra.start(install_dir=args.ghidra)
    from newtonrom import ghidra_import

    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            monitor = pyghidra.task_monitor()
            with pyghidra.transaction(program, "Newton ROM virtual calls"):
                stats = ghidra_import.Stats()
                ghidra_import.fix_virtual_calls(program, monitor, log, stats)
                for key in sorted(stats):
                    log(f"  {key}: {stats[key]}")
            program.save("virtual calls re-marked", monitor)
    finally:
        project.close()
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
