#!/usr/bin/env python3
"""Verify DDK-derived types against the ROM and recover class vtables.

Usage:
    python verify_types.py <build dir> --project <dir> --name MP2100D [--ghidra <dir>]
                           [--report report.txt] [--romfacts romfacts.json]

Walks every C++ constructor in the imported program (they follow the cfront
pattern: `teq r0,#0 / bne / mov r0,#size / bl operator new`, then store the
vtable pointer and initialise members) and checks, per class:

  size     the self-allocation size equals the size clang computed from the
           DDK headers (types.json)
  vptr     polymorphic classes store a vtable at offset 0, others do not
  vtable   the vtable entries (an array of `B` instructions, one per virtual
           function, base entries first) name the virtual methods the header
           declares, in declaration order
  fields   every store to `this` in the constructor lands on a declared field
           (or base subobject / vptr) boundary

Classes without DDK headers are still walked; their sizes and vtable addresses
are recorded.  The vtable addresses of all classes are written to
romfacts.json for later use.  Exit status is 1 if any DDK check fails.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

from newtonrom.headers import HeaderTypes  # noqa: E402


# --------------------------------------------------------------------------
# Constructor walking
# --------------------------------------------------------------------------

class CtorInfo:
    def __init__(self, cls: str, address: int):
        self.cls = cls
        self.address = address
        self.alloc_size = None
        self.vtable_stores = []      # (offset, vtable address)
        self.stores = []             # (offset, width)
        self.base_ctors = []         # names of constructors called with this
        self.notes = []


def walk_ctor(program, func, ctor_names, is_vtable_word, log) -> CtorInfo:
    """Track `this` through the constructor and collect stores to it."""
    from ghidra.program.model.lang import Register
    from ghidra.program.model.scalar import Scalar

    listing = program.getListing()
    mem = program.getMemory()
    fm = program.getFunctionManager()
    space = program.getAddressFactory().getDefaultAddressSpace()
    info = CtorInfo(func.getName(True), func.getEntryPoint().getOffset())
    this_regs = {"r0"}
    literal = {}          # register -> value loaded from a literal pool
    pending_size = None   # value of the last `mov r0,#imm` / `ldr r0,[lit]`
    seen_teq = False
    count = 0
    for ins in listing.getInstructions(func.getBody(), True):
        count += 1
        if count > 400:
            info.notes.append("constructor longer than 400 instructions; truncated")
            break
        m = ins.getMnemonicString().lower()
        ops = [list(ins.getOpObjects(i)) for i in range(ins.getNumOperands())]

        def reg(i):
            return ops[i][0].getName() if i < len(ops) and ops[i] and isinstance(ops[i][0], Register) else None

        def scalar(i):
            return next((o.getUnsignedValue() for o in ops[i]), None) if i < len(ops) and ops[i] and isinstance(ops[i][0], Scalar) else None

        # Any instruction that writes its first register operand (everything
        # except stores, compares and branches) invalidates what we knew about it.
        writes = (reg(0) is not None and not m.startswith(("str", "stm", "teq", "cmp", "cmn", "tst", "b")))
        if writes and not (m in ("mov", "movs") and reg(1) in this_regs):
            this_regs.discard(reg(0))
        if writes and not m.startswith("ldr"):
            literal.pop(reg(0), None)

        if m == "teq" and reg(0) == "r0" and scalar(1) == 0:
            seen_teq = True                  # `teq r0,#0 / bne` guard
        elif m in ("mov", "movs"):
            dst, src = reg(0), reg(1)
            if dst and src in this_regs:
                this_regs.add(dst)
                if m == "movs":
                    seen_teq = True          # `movs r4,r0 / bne` guard
            if dst == "r0" and scalar(1) is not None:
                pending_size = scalar(1)
        elif m.startswith("ldr") and reg(0):
            dst = reg(0)
            # a PC-relative literal load shows as `ldr rN,[0x....]`: operand 1 is a
            # lone scalar (the literal's address) with no base register
            if len(ops) > 1 and len(ops[1]) == 1 and isinstance(ops[1][0], Scalar):
                try:
                    literal[dst] = mem.getInt(space.getAddress(ops[1][0].getUnsignedValue())) & 0xFFFFFFFF
                except Exception:  # noqa: BLE001
                    literal.pop(dst, None)
                if dst == "r0":
                    pending_size = literal.get("r0")
            else:
                literal.pop(dst, None)
        elif m == "bl":
            flows = ins.getFlows()
            target = fm.getFunctionAt(flows[0]) if flows else None
            tname = target.getName(True) if target else "?"
            if target is not None and target.isThunk():
                tname = target.getThunkedFunction(True).getName(True)
            if tname.endswith("operator_new") and seen_teq and info.alloc_size is None:
                info.alloc_size = pending_size
                this_regs = {"r0"}          # new returns the object
            elif tname in ctor_names:
                if "r0" in this_regs:
                    info.base_ctors.append(tname)
                # cfront constructors return this in r0
            else:
                this_regs.discard("r0")
            literal.clear()
        elif m.startswith("str") and reg(0) and len(ops) > 1:
            base = next((o.getName() for o in ops[1] if isinstance(o, Register)), None)
            off = next((o.getSignedValue() for o in ops[1] if isinstance(o, Scalar)), 0)
            if base in this_regs:
                width = 1 if m.startswith("strb") else 2 if m.startswith("strh") else 4
                src = reg(0)
                if src in literal and is_vtable_word(literal[src]):
                    info.vtable_stores.append((off, literal[src]))
                info.stores.append((off, width))
        elif m.startswith("stm") and reg(0):
            base = reg(0)
            if base in this_regs and m.startswith(("stmia", "stmea")) or m == "stm":
                # Ghidra puts the base and the whole register list into operand 0
                regs = [o.getName() for op in ops for o in op if isinstance(o, Register)][1:]
                for i, r in enumerate(regs):
                    if r in literal and is_vtable_word(literal[r]):
                        info.vtable_stores.append((4 * i, literal[r]))
                    info.stores.append((4 * i, 4))
        elif m in ("ldmdb", "ldmia", "ldm") and "pc" in str(ins):
            break   # epilogue
    return info


# --------------------------------------------------------------------------
# Expected layouts from types.json
# --------------------------------------------------------------------------

def is_opaque(rec) -> bool:
    """Declared in the DDK without members (implementation hidden)."""
    return not rec["fields"] and not any(b["name"] != "SingleObject" for b in rec["bases"]) and rec["size"] <= 1


def _scalar_size(t: dict, header: HeaderTypes) -> int:
    t = header.resolve(t)
    if t["k"] in ("ptr", "ref", "memptr", "func"):
        return 4
    if t["k"] == "array":
        return t["n"] * _scalar_size(t["t"], header)
    if t["k"] == "qual":
        return _scalar_size(t["t"], header)
    name = t["name"]
    if name in header.records:
        return header.records[name]["size"]
    if name in header.enums:
        return header.enums[name]["size"]
    return {"char": 1, "signed char": 1, "unsigned char": 1, "bool": 1, "short": 2, "unsigned short": 2,
            "wchar_t": 2, "double": 8, "long long": 8, "unsigned long long": 8}.get(name, 4)


def field_extents(rec, header: HeaderTypes, base_off=0, out=None, depth=0):
    """(start, end) of every scalar member, recursing into bases and embedded objects."""
    out = [] if out is None else out
    if depth > 8:
        return out
    if rec["introduces_vptr"]:
        out.append((base_off, base_off + 4))
    for b in rec["bases"]:
        base = header.records.get(b["name"])
        if base and b.get("offset") is not None:
            field_extents(base, header, base_off + b["offset"], out, depth + 1)
    for f in rec["fields"]:
        elem = header.resolve(f["type"])
        count = 1
        while elem["k"] == "array":
            count *= elem["n"]
            elem = header.resolve(elem["t"])
        if elem["k"] == "named" and elem["name"] in header.records and not is_opaque(header.records[elem["name"]]):
            sub = header.records[elem["name"]]
            for i in range(count):
                field_extents(sub, header, base_off + f["offset"] + i * sub["size"], out, depth + 1)
        else:
            out.append((base_off + f["offset"], base_off + f["offset"] + _scalar_size(f["type"], header)))
    return out


def classify_store(off: int, width: int, extents) -> str:
    for start, end in extents:
        if start == off and off + width <= end:
            return "ok"
    for start, end in extents:
        if start <= off < end:
            return "partial"           # writes part of a wider member (byte of a short, ...)
    return "gap"


def expected_vtable(rec, header: HeaderTypes):
    """Virtual method names in vtable order: inherited entries first, overrides in place."""
    entries = []
    for b in rec["bases"]:
        base = header.records.get(b["name"])
        if base and base["polymorphic"]:
            entries = expected_vtable(base, header)
            break                      # single polymorphic base assumed
    for m in rec["methods"]:
        if not m["virtual"]:
            continue
        name = m["name"]
        key = "dtor" if m["kind"] == "dtor" else name
        for i, (k, _) in enumerate(entries):
            if k == key:
                entries[i] = (key, name)
                break
        else:
            entries.append((key, name))
    return entries


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="NewtonROM")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("--report", default=None, help="write the full report here (default: <build dir>/verify-report.txt)")
    ap.add_argument("--romfacts", default=None, help="write romfacts.json here (default: <build dir>/romfacts.json)")
    ap.add_argument("--debug", nargs="*", default=[], help="class names to dump constructor findings for")
    args = ap.parse_args(argv)
    if not args.ghidra:
        ap.error("Ghidra install dir not given (--ghidra or GHIDRA_INSTALL_DIR)")

    types_path = os.path.join(args.build_dir, "types.json")
    if os.path.exists(types_path):
        with open(types_path) as f:
            header = HeaderTypes(json.load(f))
    else:
        print("no types.json: only recovering sizes and vtables, no header checks")
        header = HeaderTypes({"records": {}, "enums": {}, "typedefs": {}, "functions": []})
    with open(os.path.join(args.build_dir, "symbols.json")) as f:
        symbols = json.load(f)
    jt_slots = {v for v, _ in symbols["jumptable"]["entries"]}
    ctors = defaultdict(list)          # class -> [address]
    for s in symbols["symbols"]:
        d = s["demangled"]
        if d and d["kind"] == "ctor" and s["address"] not in jt_slots:
            ctors[d["scope"][-1]].append(s["address"])

    import pyghidra
    pyghidra.start(install_dir=args.ghidra)
    lines = []

    def log(msg=""):
        lines.append(msg)
        print(msg)

    project = pyghidra.open_project(os.path.abspath(args.project), args.name, create=False)
    failures = 0
    vtables = {}
    rom_sizes = {}
    recovered = {}
    try:
        with pyghidra.program_context(project, "/" + args.name) as program:
            mem = program.getMemory()
            fm = program.getFunctionManager()
            space = program.getAddressFactory().getDefaultAddressSpace()
            ro_end = mem.getBlock("ROM_RO").getEnd().getOffset()

            def word(a):
                return mem.getInt(space.getAddress(a)) & 0xFFFFFFFF

            def is_vtable_word(v):
                return 0 < v < ro_end and (word(v) >> 24) == 0xEA

            def entry_name(a):
                w = word(a)
                if w >> 24 != 0xEA:
                    return None
                imm = w & 0xFFFFFF
                imm -= 0x1000000 if imm & 0x800000 else 0
                target = a + 8 + imm * 4
                fn = fm.getFunctionAt(space.getAddress(target))
                if fn is None:
                    return f"?{target:#x}"
                if fn.isThunk():
                    fn = fn.getThunkedFunction(True)
                return fn.getName(True)

            ctor_names = set()
            for cls, addrs in ctors.items():
                for a in addrs:
                    fn = fm.getFunctionAt(space.getAddress(a))
                    if fn:
                        ctor_names.add(fn.getName(True))

            stats = defaultdict(int)
            for cls in sorted(ctors):
                infos = []
                for a in ctors[cls]:
                    fn = fm.getFunctionAt(space.getAddress(a))
                    if fn is None:
                        continue
                    infos.append(walk_ctor(program, fn, ctor_names, is_vtable_word, log))
                if not infos:
                    continue
                if cls in args.debug:
                    for i in infos:
                        log(f"DEBUG {cls} @ {i.address:#x}: size={i.alloc_size} vtables={[(o, hex(v)) for o, v in i.vtable_stores]} "
                            f"stores={i.stores} base_ctors={i.base_ctors} {i.notes}")
                sizes = {i.alloc_size for i in infos if i.alloc_size is not None}
                vts = {v for i in infos for _, v in i.vtable_stores}
                vt_offs = {o for i in infos for o, _ in i.vtable_stores}
                if vts:
                    vtables[cls] = sorted(vts)
                if len({i.alloc_size for i in infos if i.alloc_size is not None}) == 1:
                    rom_sizes[cls] = next(i.alloc_size for i in infos if i.alloc_size is not None)
                rec = header.records.get(cls)
                if rec is None:
                    stats["classes without DDK header"] += 1
                    continue
                if is_opaque(rec):
                    stats["DDK classes declared opaque (size taken from ROM)"] += 1
                    continue
                stats["DDK classes checked"] += 1
                problems = []
                # size
                if sizes:
                    if sizes == {rec["size"]}:
                        stats["size ok"] += 1
                    else:
                        problems.append(f"size: ROM allocates {sorted(hex(s) for s in sizes)}, header says {rec['size']:#x}")
                        stats["size MISMATCH"] += 1
                else:
                    stats["size not observable"] += 1
                # vptr
                if rec["polymorphic"]:
                    if vt_offs == {0}:
                        stats["vptr ok"] += 1
                    elif not vts:
                        problems.append("vptr: header says polymorphic but constructor stores no vtable")
                        stats["vptr MISMATCH"] += 1
                    else:
                        problems.append(f"vptr: vtable stored at offsets {sorted(vt_offs)}, expected 0")
                        stats["vptr MISMATCH"] += 1
                elif vts:
                    problems.append(f"vptr: header says not polymorphic but constructor stores vtable {[hex(v) for v in vts]}")
                    stats["vptr MISMATCH"] += 1
                else:
                    stats["vptr ok"] += 1
                # vtable contents
                if rec["polymorphic"] and vts:
                    expected = expected_vtable(rec, header)
                    vt = max(vts)   # derived class stores its own vtable last / highest? use each
                    for vt in sorted(vts):
                        got = [entry_name(vt + 4 * i) for i in range(len(expected))]
                        bad, unnamed = [], []
                        for i, ((_, exp), g) in enumerate(zip(expected, got)):
                            if g is None:
                                bad.append((i, exp, "not a B instruction"))
                            elif g.startswith("?"):
                                unnamed.append((i, exp, g[1:]))
                            elif g.split("::")[-1] != exp and g != "__pvfn" and not (g.split("::")[-1].startswith("~") and exp.startswith("~")):
                                bad.append((i, exp, g))
                        if bad:
                            problems.append(f"vtable {vt:#x}: " + "; ".join(f"[{i}] expected {e}, got {g}" for i, e, g in bad[:4]))
                            stats["vtable MISMATCH"] += 1
                        else:
                            stats["vtable ok"] += 1
                        for i, exp, addr in unnamed:
                            # the entry points at code without a symbol: the vtable names it for us
                            recovered.setdefault(addr, f"{cls}::{exp}")
                            stats["functions named from vtables"] += 1
                # field boundaries
                extents = field_extents(rec, header)
                kinds = {(o, w): classify_store(o, w, extents) for i in infos for o, w in i.stores if 0 <= o < max(rec["size"], 1)}
                gaps = sorted({o for (o, w), k in kinds.items() if k == "gap"})
                partial = sorted({o for (o, w), k in kinds.items() if k == "partial"})
                if gaps:
                    problems.append(f"fields: stores at {[hex(o) for o in gaps]} hit no declared member")
                    stats["fields MISMATCH"] += 1
                else:
                    stats["fields ok"] += 1
                if partial:
                    problems.append(f"fields: byte/half stores at {[hex(o) for o in partial]} write part of a wider member (note)")
                if problems:
                    failures += 1
                    log(f"{cls} ({rec['file']}:{rec['line']}, ctors at {[hex(i.address) for i in infos]}):")
                    for p in problems:
                        log("   " + p)
            log("")
            log("summary:")
            for k in sorted(stats):
                log(f"   {k}: {stats[k]}")
            log(f"   classes with a recovered vtable: {len(vtables)}; with a recovered size: {len(rom_sizes)}")
    finally:
        project.close()

    report = args.report or os.path.join(args.build_dir, "verify-report.txt")
    with open(report, "w") as f:
        f.write("\n".join(lines) + "\n")
    vt_path = args.romfacts or os.path.join(args.build_dir, "romfacts.json")
    with open(vt_path, "w") as f:
        json.dump({"vtables": vtables, "sizes": rom_sizes, "functions": recovered}, f, indent=1)
    print(f"wrote {report} and {vt_path}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
