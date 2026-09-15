#!/usr/bin/env python3
"""Derive the OS600 kernel system-call table from the ROM.

Usage:
    python swi_table.py <build dir> --project <dir> --name MP2100D [--ghidra <dir>] -o docs/os600/swi-table.md

Newton OS enters the kernel with ARM `swi #n`.  Three things describe the
interface completely and all three are recovered here:

1. The user-side stubs (`_PortSendSWI`, `_MonitorDispatchSWI`, ...): tiny
   functions consisting of `swi #n; mov pc,lr`, found by scanning the ROM for
   SWI instructions inside named functions.
2. The kernel's SWI dispatch: the SWI vector handler (`SWIBoot`) reads the
   SWI number from the instruction and jumps through a 35-entry table; each
   case is assembly that calls one or more kernel C++ routines.
3. `GenericSWI` (SWI 5) multiplexes 76 further operations on a selector in
   r0; `GenericSWIHandler` dispatches them with `add pc,pc,lr,lsl #2` to
   short case blocks that each call one kernel routine.

The output is a Markdown document; regenerate it after re-importing rather
than editing it by hand.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

RETURNS = ("movs pc", "subs pc", "mov pc,lr")


def is_return(text: str) -> bool:
    return text.startswith(RETURNS) or (text.startswith("ldm") and "pc}" in text)


def user_stubs(build_dir: str):
    """SWI number -> [(address, name)] from the stub functions."""
    rom = open(os.path.join(build_dir, "rom.bin"), "rb").read()
    symbols = json.load(open(os.path.join(build_dir, "symbols.json")))["symbols"]
    ro_end = min(s["address"] for s in symbols if s["name"] == "Image$$RO$$Limit")
    # stubs are plain assembly routines; C++ functions that happen to contain a
    # swi (e.g. inline MonitorDispatch) are not stubs
    names = sorted({(s["address"], s["name"]) for s in symbols
                    if "jt_index" not in s and s["address"] < ro_end and s["demangled"] is None})
    by_swi = defaultdict(list)
    for i, (a, n) in enumerate(names):
        end = min(names[i + 1][0] if i + 1 < len(names) else a + 64, a + 64)
        for x in range(a, end, 4):
            w = struct.unpack(">I", rom[x:x + 4])[0]
            if (w >> 24) == 0xEF:
                by_swi[w & 0xFFFFFF].append((a, n))
                break
    return by_swi


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="NewtonROM")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    stubs = user_stubs(args.build_dir)

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)
    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            listing = program.getListing()
            fm = program.getFunctionManager()
            st = program.getSymbolTable()
            mem = program.getMemory()
            space = program.getAddressFactory().getDefaultAddressSpace()
            addr = space.getAddress

            def sym(a):
                f = fm.getFunctionAt(addr(a))
                if f is not None and f.isThunk():
                    f = f.getThunkedFunction(True)
                if f is not None:
                    return f.getName(True)
                s = st.getPrimarySymbol(addr(a))
                return s.getName(True) if s else f"{a:#x}"

            def function_named(name):
                for s in st.getSymbols(name):
                    f = fm.getFunctionAt(s.getAddress())
                    if f is not None and not f.isThunk():
                        return f
                raise SystemExit(f"function {name} not found")

            def calls_in_block(start, limit_instructions=80):
                """Kernel routines called from an assembly case block, in order."""
                out, a, n = [], start, 0
                while n < limit_instructions:
                    ins = listing.getInstructionAt(addr(a))
                    if ins is None:
                        break
                    text = str(ins)
                    for r in ins.getReferencesFrom():
                        if r.getReferenceType().isCall():
                            out.append(sym(r.getToAddress().getOffset()))
                    if text.startswith("b 0x"):
                        # a tail branch to a named function is a call too
                        tgt = ins.getFlows()[0].getOffset()
                        if fm.getFunctionAt(addr(tgt)) is not None and n > 0:
                            out.append(sym(tgt))
                        if n > 0:
                            break
                    if is_return(text):
                        break
                    a += ins.getLength()
                    n += 1
                return out

            # --- SWIBoot dispatch table: `ldr r0,[lit]; ldr pc,[r0,r1,lsl #2]` -----
            swiboot = function_named("SWIBoot")
            table = None
            for ins in listing.getInstructions(swiboot.getBody(), True):
                if str(ins).startswith("ldr pc,[r0,r1"):
                    prev = listing.getInstructionBefore(ins.getAddress())
                    lit = prev.getOpObjects(1)[0].getUnsignedValue()
                    table = mem.getInt(addr(lit)) & 0xFFFFFFFF
                    break
            if table is None:
                raise SystemExit("SWI dispatch table not found in SWIBoot")
            kernel = {}
            for n in range(35):
                case = mem.getInt(addr(table + 4 * n)) & 0xFFFFFFFF
                kernel[n] = (case, calls_in_block(case))

            # --- GenericSWIHandler: `cmp lr,#max; addls pc,pc,lr,lsl #2; b default; b case0 ...`
            gen = function_named("GenericSWIHandler")
            generic = {}
            it = listing.getInstructions(gen.getBody(), True)
            for ins in it:
                if str(ins).startswith("addls pc,pc,lr"):
                    default = listing.getInstructionAfter(ins.getAddress())
                    dflt = default.getFlows()[0].getOffset()
                    a = default.getAddress().getOffset() + 4
                    sel = 0
                    while True:
                        j = listing.getInstructionAt(addr(a))
                        if j is None or not str(j).startswith("b 0x"):
                            break
                        tgt = j.getFlows()[0].getOffset()
                        if tgt != dflt:
                            generic[sel] = (tgt, calls_in_block(tgt))
                        sel += 1
                        a += 4
                    break
    finally:
        project.close()

    lines = ["# OS600 system-call table (MP2100 D)", "",
             "Generated by `tools/newton-rom/analysis/swi_table.py` from the ROM; do not edit by hand.", "",
             "The kernel is entered with `swi #n`. `SWIBoot` (the SWI vector target) dispatches",
             "on `n` through a 35-entry table; `GenericSWI` (5) carries a selector in r0 that",
             "`GenericSWIHandler` dispatches to one kernel routine each. User code never issues",
             "SWIs directly: it calls the stubs listed here (which the OS600 user-side classes",
             "such as `TUPort`, `TUMonitor`, `TUSharedMem` wrap).", "",
             "## SWI numbers", "",
             "| SWI | user-side stub(s) | kernel case | kernel routines called |", "|---|---|---|---|"]
    for n in range(35):
        names = ", ".join(f"`{nm}`" for _, nm in sorted(set(stubs.get(n, [])), key=lambda x: x[1])) or "—"
        case, calls = kernel[n]
        called = ", ".join(f"`{c}`" for c in dict.fromkeys(calls)) or "(inline assembly)"
        lines.append(f"| {n} | {names} | `{case:#x}` | {called} |")
    lines += ["", "## GenericSWI (SWI 5) selectors", "",
              "The selector is passed in r0 by `GenericSWI`/`GenericWithReturnSWI`; r1-r3 are the",
              "arguments handed to the kernel routine.", "",
              "| selector | kernel routine(s) | case |", "|---|---|---|"]
    for sel in sorted(generic):
        tgt, calls = generic[sel]
        called = ", ".join(f"`{c}`" for c in dict.fromkeys(calls)) or "(inline)"
        lines.append(f"| {sel} ({sel:#x}) | {called} | `{tgt:#x}` |")
    lines.append("")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"wrote {args.out}: {len(kernel)} SWIs, {len(generic)} GenericSWI selectors")
    return 0


if __name__ == "__main__":
    sys.exit(main())
