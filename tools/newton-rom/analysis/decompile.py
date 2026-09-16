#!/usr/bin/env python3
"""Print the decompilation (and optionally disassembly) of functions in the project.

Usage:
    python decompile.py --project <dir> --name MP2100D [--ghidra <dir>]
                        (--class TObjectTable | --function Name | --address 0x... | --range 0x... 0x...) [--asm] [--callers]

Selects functions by class namespace, by name (any namespace), by address or
by address range (every function starting in it - one Ghidra start for a
whole subsystem),
and prints Ghidra's decompiler output for each, with the function's ROM
address and mangled name in the header so the source we write can cite it.
`--asm` adds the disassembly, `--callers` lists calling functions (thunks and
jump-table slots resolved to their real callers).
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
    ap.add_argument("--class", dest="cls", action="append", default=[], help="all functions of this class")
    ap.add_argument("--function", action="append", default=[], help="functions with this name")
    ap.add_argument("--address", action="append", default=[], help="function at this address")
    ap.add_argument("--range", nargs=2, metavar=("START", "END"), action="append", default=[],
                    help="every function with its entry in [START, END) (repeatable)")
    ap.add_argument("--asm", action="store_true", help="also print the disassembly")
    ap.add_argument("--callers", action="store_true", help="list callers of each function")
    ap.add_argument("--timeout", type=int, default=60, help="decompiler timeout per function (s)")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)
    from ghidra.app.decompiler import DecompInterface, DecompileOptions

    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            fm = program.getFunctionManager()
            st = program.getSymbolTable()
            listing = program.getListing()
            rm = program.getReferenceManager()
            space = program.getAddressFactory().getDefaultAddressSpace()
            di = DecompInterface()
            di.setOptions(DecompileOptions())
            di.openProgram(program)

            funcs = []
            for cls in args.cls:
                ns = st.getNamespace(cls, program.getGlobalNamespace())
                if ns is None:
                    print(f"class {cls}: not found", file=sys.stderr)
                    continue
                for s in st.getSymbols(ns):
                    f = fm.getFunctionAt(s.getAddress())
                    if f is not None and not f.isThunk():
                        funcs.append(f)
            for name in args.function:
                for s in st.getSymbols(name):
                    f = fm.getFunctionAt(s.getAddress())
                    if f is not None and not f.isThunk():
                        funcs.append(f)
            for a in args.address:
                f = fm.getFunctionAt(space.getAddress(int(a, 0)))
                if f is None:
                    print(f"no function at {a}", file=sys.stderr)
                else:
                    funcs.append(f)
            for rng in args.range:
                start, end = (int(x, 0) for x in rng)
                for f in fm.getFunctions(space.getAddress(start), True):
                    if f.getEntryPoint().getOffset() >= end:
                        break
                    if not f.isThunk():
                        funcs.append(f)
            funcs = sorted({f.getEntryPoint().getOffset(): f for f in funcs}.values(),
                           key=lambda f: f.getEntryPoint().getOffset())

            for f in funcs:
                comment = (f.getComment() or "").splitlines()
                mangled = comment[1] if len(comment) > 1 else ""
                print("=" * 100)
                print(f"// {f.getName(True)}  @ {f.getEntryPoint()}  {mangled}")
                if args.callers:
                    callers = set()
                    targets = [f.getEntryPoint()]
                    # calls arrive through jump-table / vtable thunks: include them
                    for t in fm.getFunctions(True):
                        if t.isThunk() and t.getThunkedFunction(True) == f:
                            targets.append(t.getEntryPoint())
                    for t in targets:
                        for r in rm.getReferencesTo(t):
                            c = fm.getFunctionContaining(r.getFromAddress())
                            if c is not None and not c.isThunk():
                                callers.add(f"{c.getName(True)}@{c.getEntryPoint()}")
                    print("// callers:", ", ".join(sorted(callers)) or "none")
                if args.asm:
                    for ins in listing.getInstructions(f.getBody(), True):
                        print(f"//   {ins.getAddress()}  {ins}")
                res = di.decompileFunction(f, args.timeout, None)
                if res.decompileCompleted():
                    print(res.getDecompiledFunction().getC())
                else:
                    print(f"// decompilation failed: {res.getErrorMessage()}")
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
