#!/usr/bin/env python3
"""A native-compiled NewtonScript function, listed as the operations it does.

NTK compiles a NewtonScript function marked native into ARM code of a very
regular shape: every value lives in a RefHandle (allocated at the top, each
handle's pointer kept in a word of the stack frame, `[sp,#N]`), a literal is
read out of the closure's literals array (`Slots(GetArraySlotRef(closure,
3))`, kept in a register), and everything else is a call - to the runtime
routines at the front of the code binary (find a variable, a global
function, a send, arithmetic and comparison with integer fast paths) or,
through NTK's glue, to the ROM.  Reading the raw disassembly means tracking
which stack word holds which handle through thousands of instructions; this
does that tracking and prints each function as a list of statements:

    h14 := lit[1] 'DoEvent_Check              a value put in handle [sp,#0x14]
    r0 = FindVar(&h80, &h7c)                  a routine, its arguments
    if (r0 == nil) goto L_d8ac                a test and its branch

    python ntknative.py <file.pkg> <holder or 0xref> [--rom BUILD] [--raw]

`hN` is the handle whose pointer is at [sp,#N], `&hN` a RefArg to it (the
address of that stack word), `*hN` the handle pointer itself.  A call through
NativeEntry has a native fast path and an interpreted one doing the same
thing; the fast path is elided and the interpreted one (PushValue of each
argument, then Call/Send) printed.  An instruction the tracker does not model
is printed as it is (--raw prints every instruction beside its statement).
At a label the registers keep only what every branch to it (and the
instruction before it, when that falls through) agrees on; a loop head
knows only what falls into it.  Outgoing stack arguments (a push before a
call, `add sp` after it) are tracked, so [sp,#N] still names the same
handle meanwhile.  NTK_DEBUG=1 in the environment shows that depth on each
line.

The routine names at the front of the binary are the NIE's (inetenbl.pkg's
code binary, docs/comms/README.md); another package's binary gets its glue
calls named and its own routines shown by offset.  Needs capstone and a
build directory for the glue names (--rom).
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkgns  # noqa: E402
import pkgdisasm  # noqa: E402

import capstone  # noqa: E402
from capstone import arm as carm  # noqa: E402

# The NIE binary's runtime routines (offset -> name, arguments shown).
ROUTINES = {
    0x0000: ("GetGInterpreter", 0),
    0x0048: ("GetGFunctionFrame", 0),
    0x0aa8: ("NewIterator", 2),
    0x1020: ("(no-op on 2.x)", 0),
    0x12f8: ("PushValue", 2),
    0x1354: ("GlobalFn", 1),
    0x13ac: ("Call", 3),                     # (interp, fn, n)
    0x13d8: ("SetupSend", 4),
    0x150c: ("Send", 4),                     # (interp, rcvr, impl, fn, [n])

    0x154c: ("NativeEntry", 3),
    0x171c: ("FindVar", 2),
    0x1794: ("SetVar", 3),
    0x17bc: ("VarExists", 2),
    0x17fc: ("HasPath", 2),
    0x1998: ("NewIterator", 2),
    0x19f8: ("ForEachLoopDone", 1),
    0x19fc: ("ForEachLoopNext", 1),
    0x1a2c: ("LessThan", 2),
    0x1a98: ("LessOrEqual", 2),
    0x1af0: ("GreaterThan", 2),
    0x1b48: ("GreaterOrEqual", 2),
    0x1ba0: ("Equal", 2),
    0x1bf8: ("NotEqual", 2),
    0x1c24: ("Add", 2),
    0x1c68: ("Subtract", 2),
    0x1c98: ("Multiply", 2),
    0x1ccc: ("Divide", 2),
    0x1cdc: ("Aref", 2),
    0x1cec: ("SetAref", 3),
    0x1d00: ("Negate", 1),
}

CONDS = {
    carm.ARM_CC_EQ: "==", carm.ARM_CC_NE: "!=", carm.ARM_CC_GT: ">", carm.ARM_CC_GE: ">=",
    carm.ARM_CC_LT: "<", carm.ARM_CC_LE: "<=", carm.ARM_CC_HI: ">u", carm.ARM_CC_HS: ">=u",
    carm.ARM_CC_LO: "<u", carm.ARM_CC_LS: "<=u", carm.ARM_CC_MI: "<0", carm.ARM_CC_PL: ">=0",
}
CONDNAME = {
    carm.ARM_CC_EQ: "eq", carm.ARM_CC_NE: "ne", carm.ARM_CC_GT: "gt", carm.ARM_CC_GE: "ge",
    carm.ARM_CC_LT: "lt", carm.ARM_CC_LE: "le", carm.ARM_CC_HI: "hi", carm.ARM_CC_HS: "hs",
    carm.ARM_CC_LO: "lo", carm.ARM_CC_LS: "ls", carm.ARM_CC_MI: "mi", carm.ARM_CC_PL: "pl",
}


def ref_text(v):
    if v == 2:
        return "nil/2"
    if v == 0x1a:
        return "true"
    if v & 3 == 0:
        n = v if v < 0x80000000 else v - 0x100000000
        return "%d(=%d)" % (v, n >> 2)
    return "%#x" % v


class Lister:
    def __init__(self, pkg, code, start, end, lits):
        self.pkg, self.code, self.start, self.end, self.lits = pkg, code, start, end, lits
        self.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
        self.md.detail = True
        self.stubs = pkgdisasm.glue_stubs(code) if pkgdisasm._rom is not None else {}
        self.regs = {}
        self.flags = None
        self.stack_args = []
        self.spills = {}                # stack words holding the literals pointer
        self.spdelta = 0                # bytes pushed below the frame (outgoing stack arguments)
        self.out = []

    def lit_text(self, i):
        if 0 <= i < len(self.lits):
            return "lit[%d] %s" % (i, self.pkg.describe(self.lits[i]))
        return "lit[%d]" % i

    def text(self, v):
        if v is None:
            return "?"
        kind = v[0]
        if kind == "imm":
            return ref_text(v[1] & 0xffffffff)
        if kind == "hptr":
            return "*h%x" % v[1]
        if kind == "harg":
            return "&h%x" % v[1]
        if kind == "lits":
            return "LITS+%d" % v[1]
        return v[1]

    def reg(self, insn, r):
        name = insn.reg_name(r)
        if name == "sb":
            name = "r9"
        return name

    def call_name(self, target):
        if target in ROUTINES:
            return ROUTINES[target]
        if target in self.stubs:
            return (self.stubs[target], 4)
        if pkgdisasm._rom is not None:
            reached = pkgdisasm.trampoline_target(self.code, self.stubs, target)
            if reached:
                return (reached, 4)
        return ("sub_%x" % target, 4)

    def emit(self, off, s):
        self.out.append("  %06x  %s%s" % (off, s, "  {sp%+d}" % self.spdelta if os.environ.get("NTK_DEBUG") else ""))

    def state(self):
        st = dict(self.regs)
        st["__sp"] = self.spdelta
        st["__args"] = tuple(self.stack_args)
        return st

    def run(self, raw=False):
        labels = set()
        insns = list(self.md.disasm(self.code[self.start:self.end], self.start))
        for insn in insns:
            if insn.mnemonic.startswith("b") and insn.operands and insn.operands[0].type == carm.ARM_OP_IMM \
                    and not insn.mnemonic.startswith("bl") and not insn.mnemonic.startswith("bic"):
                labels.add(insn.operands[0].imm)
        skip_to = None
        falls = True                    # whether the previous instruction can fall through
        self.pending = {}               # label -> register states of the branches to it
        for k, r in enumerate(("r0", "r1", "r2", "r3")):
            self.regs[r] = ("expr", "in_" + r)
        i = 0
        while i < len(insns):
            insn = insns[i]
            off = insn.address
            if skip_to is not None:
                if off < skip_to:
                    i += 1
                    continue
                skip_to = None
            if off in labels:
                self.out.append("L_%x:" % off)
                # the registers every way in agrees on (a backward branch's
                # state is not known yet, so a loop head keeps only what
                # falls into it)
                sources = list(self.pending.pop(off, []))
                if falls:
                    sources.append(self.state())
                merged = {}
                if sources:
                    for r, v in sources[0].items():
                        if all(src.get(r) == v for src in sources[1:]):
                            merged[r] = v
                self.spdelta = merged.pop("__sp", 0)
                self.stack_args = list(merged.pop("__args", ()))
                self.regs = merged
                self.flags = None
            before = len(self.out)
            skip = self.step(insn, insns, i, labels)
            m = insn.mnemonic
            if m.startswith("b") and not m.startswith("bl") and not m.startswith("bic") and insn.operands \
                    and insn.operands[0].type == carm.ARM_OP_IMM:
                self.pending.setdefault(insn.operands[0].imm, []).append(self.state())
            falls = not (m == "b" or m in ("pop", "ldmdb") or (m == "ldr" and insn.op_str.startswith("pc"))
                         or (m == "mov" and insn.op_str.startswith("pc")))
            if raw and len(self.out) == before:
                self.emit(off, "; %s %s" % (insn.mnemonic, insn.op_str))
            if skip:
                skip_to = skip
                falls = False
            i += 1
        return self.out

    def value(self, insn, op):
        if op.type == carm.ARM_OP_IMM:
            return ("imm", op.imm & 0xffffffff)
        if op.type == carm.ARM_OP_REG:
            if self.reg(insn, op.reg) == "sp":
                return ("harg", -self.spdelta)
            return self.regs.get(self.reg(insn, op.reg))
        return None

    def step(self, insn, insns, i, labels):
        m = insn.mnemonic
        cc = insn.cc
        ops = insn.operands
        off = insn.address
        base = m
        for suffix in CONDNAME.values():
            if m.endswith(suffix) and len(m) > len(suffix) and m[:-len(suffix)] in (
                    "mov", "ldr", "str", "b", "bl", "movs", "add", "sub", "mvn", "ldmdb", "push", "pop", "stmdb"):
                base = m[:-len(suffix)]
        conditional = cc not in (carm.ARM_CC_AL, carm.ARM_CC_INVALID)
        prefix = ""
        if conditional:
            prefix = "if (%s) " % self.cond_text(cc)

        # branches
        if base == "b" and ops and ops[0].type == carm.ARM_OP_IMM:
            self.emit(off, "%sgoto L_%x" % (prefix, ops[0].imm))
            return None
        if base == "bl" and ops and ops[0].type == carm.ARM_OP_IMM:
            target = ops[0].imm
            name, nargs = self.call_name(target)
            if name.startswith("AllocateRefHandle"):
                self.regs["r0"] = ("newhandle", "RESULT")
                return None
            if name.startswith("DisposeRefHandle"):
                return None
            args = [self.text(self.regs.get("r%d" % k)) for k in range(nargs)]
            if self.stack_args:
                args += ["[stack %s]" % ", ".join(self.stack_args)]
                self.stack_args = []
            call = "%s(%s)" % (name, ", ".join(args))
            if name == "NativeEntry":
                # movs rX, r0; beq L_interp: the fast path runs to L_interp
                nxt = None
                for k in range(i + 1, min(i + 7, len(insns))):
                    if insns[k].mnemonic == "beq":
                        nxt = insns[k]
                        break
                if nxt is not None:
                    self.emit(off, "%s-- call %s with %s args (native fast path elided)" % (
                        prefix, self.text(self.regs.get("r0")), self.text(self.regs.get("r1"))))
                    self.regs["r0"] = None
                    for r in ("r1", "r2", "r3", "ip", "lr"):
                        self.regs.pop(r, None)
                    self.pending.setdefault(nxt.operands[0].imm, []).append(self.state())
                    return nxt.operands[0].imm
            self.emit(off, "%sr0 = %s" % (prefix, call))
            for r in ("r0", "r1", "r2", "r3", "ip", "lr"):
                self.regs.pop(r, None)
            self.regs["r0"] = ("expr", "%s(...)" % name.split("__")[0])
            self.flags = None
            return None
        if base in ("push", "stmdb") and ops and all(o.type == carm.ARM_OP_REG for o in ops):
            regs = [self.reg(insn, o.reg) for o in ops if self.reg(insn, o.reg) != "sp"]
            if "lr" in regs or "pc" in regs:
                return None
            self.stack_args = [self.text(self.regs.get(r)) for r in regs]
            self.spdelta += 4 * len(regs)
            return None
        if base == "add" and len(ops) == 3 and ops[0].type == carm.ARM_OP_REG and self.reg(insn, ops[0].reg) == "sp" \
                and ops[2].type == carm.ARM_OP_IMM and self.spdelta > 0:
            self.spdelta -= ops[2].imm
            return None
        if base in ("pop", "ldmdb") or (base == "add" and ops and self.reg(insn, ops[0].reg) == "sp") \
                or (base == "sub" and ops and self.reg(insn, ops[0].reg) == "sp"):
            if base in ("pop", "ldmdb"):
                self.emit(off, "%sreturn" % prefix)
            return None
        # moves
        if base in ("mov", "movs", "mvn") and len(ops) == 2:
            dst = self.reg(insn, ops[0].reg)
            v = self.value(insn, ops[1])
            if base == "mvn" and v is not None and v[0] == "imm":
                v = ("imm", ~v[1] & 0xffffffff)
            if dst == "pc":
                self.emit(off, "%s-- jump to %s" % (prefix, self.text(v)))
                return None
            if dst == "lr":
                return None
            if conditional:
                self.emit(off, "%s%s = %s" % (prefix, dst, self.text(v)))
                self.regs[dst] = ("expr", "(%s ? %s : %s)" % (self.cond_text(cc), self.text(v), self.text(self.regs.get(dst))))
            else:
                self.regs[dst] = v
            if base == "movs":
                self.flags = (self.text(v), "0")
            return None
        if base == "mov" and len(ops) == 2 and ops[1].type == carm.ARM_OP_REG and self.reg(insn, ops[1].reg) == "sp":
            self.regs[self.reg(insn, ops[0].reg)] = ("harg", 0)
            return None
        if base == "add" and len(ops) == 3 and ops[1].type == carm.ARM_OP_REG and ops[2].type == carm.ARM_OP_IMM \
                and self.reg(insn, ops[1].reg) != "sp":
            src = self.regs.get(self.reg(insn, ops[1].reg))
            if src is not None and src[0] in ("lits", "harg"):
                self.regs[self.reg(insn, ops[0].reg)] = (src[0], src[1] + ops[2].imm)
                return None
        if base == "add" and len(ops) == 3 and ops[1].type == carm.ARM_OP_REG and self.reg(insn, ops[1].reg) == "sp" \
                and ops[2].type == carm.ARM_OP_IMM:
            self.regs[self.reg(insn, ops[0].reg)] = ("harg", ops[2].imm - self.spdelta)
            return None
        if base == "mov" or (base == "add" and len(ops) == 2):
            pass
        # loads
        if base == "ldr" and len(ops) == 2 and ops[1].type == carm.ARM_OP_MEM:
            dst = self.reg(insn, ops[0].reg)
            mem = ops[1].mem
            b = self.reg(insn, mem.base)
            disp = mem.disp if mem.index == 0 else None
            if dst == "pc":
                self.emit(off, "%s-- jump through %s" % (prefix, self.text(self.regs.get(b))))
                return None
            v = None
            if b == "sp" and disp is not None and (disp - self.spdelta) in self.spills:
                v = self.spills[disp - self.spdelta]
            elif b == "sp" and disp is not None:
                v = ("hptr", disp - self.spdelta)
            elif b == "pc" and disp is not None:
                word = struct.unpack_from(">I", self.code, off + 8 + disp)[0]
                v = ("imm", word)
            else:
                bv = self.regs.get(b)
                if bv is not None and bv[0] == "hptr" and disp == 0:
                    v = ("expr", "h%x" % bv[1])
                elif bv is not None and bv[0] == "lits" and disp is not None:
                    idx = (bv[1] + disp) // 4
                    v = ("expr", self.lit_text(idx))
                    if insn.writeback:
                        self.regs[b] = ("lits", bv[1] + disp)
                elif bv is not None and bv[0] == "expr" and disp is not None:
                    v = ("expr", "[%s+%d]" % (bv[1], disp) if disp else "[%s]" % bv[1])
            if conditional:
                self.emit(off, "%s%s = %s" % (prefix, dst, self.text(v)))
                self.regs[dst] = ("expr", "(%s ? %s : ?)" % (self.cond_text(cc), self.text(v)))
            else:
                self.regs[dst] = v
            return None
        # stores
        if base == "str" and len(ops) == 2 and ops[1].type == carm.ARM_OP_MEM:
            src = self.regs.get(self.reg(insn, ops[0].reg))
            mem = ops[1].mem
            b = self.reg(insn, mem.base)
            bv = self.regs.get(b)
            if b == "sp":
                if src is not None and src[0] == "newhandle":
                    return None
                if src is not None and src[0] == "lits":
                    self.spills[mem.disp - self.spdelta] = src
                    return None
                self.emit(off, "%s[sp,#%#x] := %s" % (prefix, mem.disp, self.text(src)))
                return None
            if bv is not None and bv[0] == "hptr" and mem.disp == 0:
                self.emit(off, "%sh%x := %s" % (prefix, bv[1], self.text(src)))
                return None
            if bv is not None and bv[0] == "newhandle" and mem.disp == 0:
                self.emit(off, "%sRESULT := %s" % (prefix, self.text(src)))
                return None
            self.emit(off, "%s[%s+%d] := %s" % (prefix, self.text(bv), mem.disp, self.text(src)))
            return None
        # compares
        if base in ("teq", "cmp", "tst") and len(ops) == 2:
            a = self.text(self.value(insn, ops[0]))
            bb = self.text(self.value(insn, ops[1]))
            self.flags = (a, bb) if base != "tst" else ("%s & %s" % (a, bb), "0")
            return None
        if base == "bl" or base == "blx":
            self.emit(off, "%s-- %s %s" % (prefix, m, insn.op_str))
            return None
        # anything else: shown as it is; its destination forgotten
        self.emit(off, "%s; %s %s" % (prefix, m, insn.op_str))
        if ops and ops[0].type == carm.ARM_OP_REG:
            self.regs[self.reg(insn, ops[0].reg)] = ("expr", "(%s %s)" % (m, insn.op_str))
        # Slots(GetArraySlotRef(closure, 3)): the literals
        return None

    def cond_text(self, cc):
        if self.flags is None:
            return CONDNAME.get(cc, "cc%d" % cc)
        a, b = self.flags
        return "%s %s %s" % (a, CONDS.get(cc, "?"), b)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("function", help="the holder (frame.slot or slot) or 0xref of a native function")
    ap.add_argument("--rom", help="a build directory: name the ROM functions NTK glue calls reach")
    ap.add_argument("--raw", action="store_true", help="print every instruction the tracker does not print")
    args = ap.parse_args(argv)
    if args.rom:
        pkgdisasm.load_rom(args.rom)
    pkg = pkgns.PackageImage(args.package)
    held = pkgns.holders(pkg)
    ref = pkgns.find(pkg, held, args.function)
    rows = [r for r in pkgns.natives(pkg) if r[0] == ref]
    if not rows:
        print("%s: not a native function" % args.function)
        return 1
    _, binary, n, off, length = rows[0]
    lits = []
    closure = pkg.slots(ref)[3]
    if pkg.is_ptr(closure) and pkg.flags(closure) & 3 == 3:
        for tag, value in pkg.frame_slots(closure):
            if tag == "_literals" and pkg.is_ptr(value):
                lits = pkg.slots(value)
    print("%s (%#x): %d args, code +%#x, %d bytes" % (held.get(ref, args.function), ref, n, off, length))
    for k, lit in enumerate(lits):
        print("  lit[%d] %s" % (k, pkg.describe(lit)))
    lister = Lister(pkg, pkg.data(binary), off, off + length, lits)
    # the literals pointer: Slots(...) is the first glue call after the
    # handles; its result register is recognised by name below
    orig_step = lister.step

    def step(insn, insns, i, labels):
        if insn.mnemonic == "bl" and insn.operands and insn.operands[0].type == carm.ARM_OP_IMM:
            name, _ = lister.call_name(insn.operands[0].imm)
            if name.startswith("Slots__"):
                lister.regs["r0"] = ("lits", 0)
                return None
        if insn.mnemonic == "mov" and len(insn.operands) == 2 and insn.operands[1].type == carm.ARM_OP_REG:
            src = lister.regs.get(lister.reg(insn, insn.operands[1].reg))
            if src is not None and src[0] == "lits":
                lister.regs[lister.reg(insn, insn.operands[0].reg)] = src
                return None
        return orig_step(insn, insns, i, labels)
    lister.step = step
    for line in lister.run(args.raw):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
