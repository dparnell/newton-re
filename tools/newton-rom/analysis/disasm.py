#!/usr/bin/env python3
"""Print the disassembly of an address range, whether or not it is inside a function.

Usage:
    python disasm.py --project <dir> --name MP2100D [--ghidra <dir>] --start 0x38abf8 [--end 0x38acf0 | --count 40]

Useful for the hand-written assembly the decompiler cannot reach as a
function of its own: SWI dispatch cases, glue in the middle of SWIBoot,
the reset and exception vectors.  Labels and jump-table targets are
resolved to symbol names, and the operand's referenced symbol (if any)
is appended after `;`.

Some of that assembly is not disassembled at all: the auto-analysis leaves
a routine nothing calls directly as raw bytes, and the unrolled inner loop
of `BPNetEvaluate` (which is entered through a computed jump) is the worst
case.  `--force` clears the range and has Ghidra disassemble it as ARM
before printing, which changes the project and is remembered.
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
    ap.add_argument("--start", required=True, help="first address")
    ap.add_argument("--end", help="stop before this address")
    ap.add_argument("--count", type=int, default=64, help="instructions to print when --end is not given")
    ap.add_argument("--force", action="store_true",
                    help="clear the range and disassemble it as ARM first (changes the project)")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)

    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            listing = program.getListing()
            st = program.getSymbolTable()
            rm = program.getReferenceManager()
            space = program.getAddressFactory().getDefaultAddressSpace()
            addr = space.getAddress(int(args.start, 0))
            end = space.getAddress(int(args.end, 0)) if args.end else None

            if args.force:
                if end is None:
                    ap.error("--force needs --end")
                from ghidra.app.cmd.disassemble import ArmDisassembleCommand
                from ghidra.program.model.address import AddressSet
                tx = program.startTransaction("disassemble")
                try:
                    listing.clearCodeUnits(addr, end.subtract(1), False)
                    ArmDisassembleCommand(AddressSet(addr, end.subtract(1)), None, False).applyTo(program)
                finally:
                    program.endTransaction(tx, True)
            printed = 0
            while addr is not None and (end is None and printed < args.count or end is not None and addr < end):
                label = st.getPrimarySymbol(addr)
                if label is not None:
                    print(f"{label.getName(True)}:")
                cu = listing.getCodeUnitAt(addr)
                if cu is None:
                    print(f"  {addr}  ??")
                    addr = addr.add(4)
                    continue
                refs = []
                for r in rm.getReferencesFrom(addr):
                    s = st.getPrimarySymbol(r.getToAddress())
                    if s is not None and not s.getName().startswith(("LAB_", "DAT_", "PTR_")):
                        refs.append(s.getName(True))
                text = f"  {addr}  {cu}"
                if refs:
                    text += "  ; " + ", ".join(refs)
                print(text)
                printed += 1
                addr = addr.add(cu.getLength())
    finally:
        project.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
