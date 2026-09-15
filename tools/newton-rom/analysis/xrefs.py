#!/usr/bin/env python3
"""List the functions that reference a symbol (code or data) in the Ghidra project.

Usage:
    python xrefs.py --project <dir> --name MP2100D [--ghidra <dir>] <symbol or 0xaddress>...

For each referencing instruction prints the function it is in, the address
and the reference type (READ / WRITE / CALL / ...).  The way to find who
initialises a global or who calls an unnamed routine.
"""

from __future__ import annotations

import argparse
import os
import sys


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="NewtonROM")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("targets", nargs="+")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)
    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            st = program.getSymbolTable()
            rm = program.getReferenceManager()
            fm = program.getFunctionManager()
            space = program.getAddressFactory().getDefaultAddressSpace()
            for target in args.targets:
                if target[:2].lower() == "0x":
                    addrs = [space.getAddress(int(target, 0))]
                else:
                    addrs = [s.getAddress() for s in st.getSymbols(target)]
                if not addrs:
                    print(f"{target}: no such symbol")
                    continue
                for addr in addrs:
                    print(f"== {target} @ {addr}")
                    for ref in rm.getReferencesTo(addr):
                        src = ref.getFromAddress()
                        f = fm.getFunctionContaining(src)
                        where = f.getName(True) if f is not None else "?"
                        print(f"  {src}  {ref.getReferenceType()}  in {where}")
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
