#!/usr/bin/env python3
"""Report what a Ghidra project produced by import_rom.py contains.

Usage:
    python check_import.py --project <dir> --name MP2100D [--ghidra <dir>] [--lookup NAME ...] [--type NAME ...]

Prints memory blocks, function / thunk / class / label counts and, for each
--lookup name, the matching symbols with their address, namespace and
signature.  Handy for confirming an import (or a re-run) did what was intended.
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
    ap.add_argument("--lookup", nargs="*", default=[], help="symbol names to show")
    ap.add_argument("--type", nargs="*", default=[], help="data type names to show (structure members)")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)
    from ghidra.program.model.symbol import SymbolType

    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            print(f"{program.getName()}: {program.getLanguageID()} / {program.getCompilerSpec().getCompilerSpecID()}")
            print("memory blocks:")
            for b in program.getMemory().getBlocks():
                perms = ("r" if b.isRead() else "-") + ("w" if b.isWrite() else "-") + ("x" if b.isExecute() else "-")
                print(f"  {b.getName():12s} {b.getStart()}-{b.getEnd()} {perms} {b.getSize():#x}")
            fm = program.getFunctionManager()
            funcs = list(fm.getFunctions(True))
            thunks = sum(1 for f in funcs if f.isThunk())
            st = program.getSymbolTable()
            classes = sum(1 for _ in st.getClassNamespaces())
            labels = sum(1 for s in st.getAllSymbols(False) if s.getSymbolType() == SymbolType.LABEL)
            with_params = sum(1 for f in funcs if not f.isThunk() and f.getParameterCount() > 0)
            print(f"functions: {len(funcs)} ({thunks} thunks, {with_params} with parameters)")
            print(f"classes: {classes}   labels: {labels}")
            dtm = program.getDataTypeManager()
            for name in args.type:
                found = list(dtm.getAllDataTypes())
                found = [t for t in found if t.getName() == name]
                if not found:
                    print(f"type {name}: not found")
                for t in found:
                    print(f"type {t.getPathName()} length {t.getLength()}  {t.getDescription() or ''}")
                    if hasattr(t, "getComponents"):
                        for c in t.getComponents():
                            print(f"   {c.getOffset():#06x} {c.getDataType().getName():28s} {c.getFieldName()}")
            for name in args.lookup:
                syms = list(st.getSymbols(name))
                if not syms:
                    print(f"{name}: not found")
                for s in syms:
                    f = fm.getFunctionAt(s.getAddress())
                    where = f"{s.getAddress()} {s.getParentNamespace().getName(True)}::{s.getName()}"
                    if f is not None:
                        thunk = f" -> thunk of {f.getThunkedFunction(False).getName(True)}" if f.isThunk() else ""
                        print(f"{where}  {f.getPrototypeString(False, False)}{thunk}")
                    else:
                        print(f"{where}  [{s.getSymbolType()}]")
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
