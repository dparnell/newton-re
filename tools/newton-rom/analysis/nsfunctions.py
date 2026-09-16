#!/usr/bin/env python3
"""List the ROM's built-in NewtonScript functions, and disassemble their bytecode.

Usage:
    python nsfunctions.py <build_dir> [--list] [--natives] [--disasm NAME]... [-o file]
    python nsfunctions.py build/MP2100D --list
    python nsfunctions.py build/MP2100D --natives -o src/frames/ROMNatives.cpp
    python nsfunctions.py build/MP2100D --disasm Max --disasm ArrayInsert

The built-in functions frame (Rbuiltinfunctions, the ROM's object
0x0062418d) maps function names to function objects of two kinds: native
functions (class 0x132: {funcPtr, numArgs}, funcPtr the jump-table address
of a C function) and NewtonScript functions (class 0x32: [instructions,
literals, argFrame, numArgs | numLocals << 16]).  --list prints them all
with their kind and argument count; --natives emits a C++ table of the
native ones (name, jump-table address, the ROM function it reaches and its
symbol) for frames/NativeFunctions.cpp to bind host implementations to;
--disasm prints a NewtonScript function's bytecode (Newton Formats, the
instruction set of the NewtonScript interpreter, as TInterpreter::SlowRun
0x002cc66c executes it).

The ROM's objects are read as they are in the image (big-endian, the ARM
layout); a NewtonScript function in the ROM is an array whose first slot
is its class.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


BUILTIN_FUNCTIONS = 0x62418d
SYMBOL_CLASS = 0x55552

SIMPLE_OPS = ["pop", "dup", "return", "push-self", "set-lex-scope", "iter-next", "iter-done", "pop-handlers"]
OPS = {
    3: "push", 4: "push-constant", 5: "call", 6: "invoke", 7: "send", 8: "send-if-defined",
    9: "resend", 10: "resend-if-defined", 11: "branch", 12: "branch-if-true", 13: "branch-if-false",
    14: "find-var", 15: "get-var", 16: "make-frame", 17: "make-array", 18: "get-path",
    19: "set-path", 20: "set-var", 21: "find-and-set-var", 22: "incr-var",
    23: "branch-if-loop-not-done", 24: "freq-func", 25: "new-handlers",
}
FREQ_FUNCS = ["+", "-", "aref", "setAref", "=", "not", "<>", "*", "/", "div", "<", ">", ">=", "<=",
              "band", "bor", "bnot", "newiterator", "length", "clone", "setClass", "addArraySlot",
              "stringer", "hasPath", "ClassOf"]


class ROM:
    def __init__(self, build_dir: str):
        with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
            self.rom = f.read()
        with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
            data = json.load(f)
        self.symbols = {}
        for s in data["symbols"]:
            if "jt_index" not in s:
                self.symbols.setdefault(s["address"], s["name"])
        self.jump = {int(v): int(t) for v, t in data["jumptable"]["entries"]} if "jumptable" in data else {}

    def word(self, a: int) -> int:
        return struct.unpack(">I", self.rom[a:a + 4])[0]

    def is_ptr(self, ref: int) -> bool:
        return ref & 3 == 1

    def flags(self, ref: int) -> int:
        return self.word(ref - 1) & 0xff

    def size(self, ref: int) -> int:
        return self.word(ref - 1) >> 8

    def slots(self, ref: int):
        a = ref - 1
        n = (self.size(ref) - 12) // 4
        return [self.word(a + 12 + 4 * i) for i in range(n)]

    def cls(self, ref: int) -> int:
        return self.word(ref - 1 + 8)

    def data(self, ref: int) -> bytes:
        a = ref - 1
        return self.rom[a + 12:a + self.size(ref)]

    def symname(self, ref: int) -> str:
        if not self.is_ptr(ref) or self.cls(ref) != SYMBOL_CLASS:
            return None
        d = self.data(ref)
        return d[4:d.index(b"\0", 4)].decode("latin-1")

    def map_tags(self, mapref: int):
        s = self.slots(mapref)
        t = self.map_tags(s[0]) if s[0] != 2 else []
        return t + s[1:]

    def frame_slots(self, ref: int):
        """(tag name, value) pairs of a frame."""
        tags = self.map_tags(self.cls(ref))
        return list(zip((self.symname(t) for t in tags), self.slots(ref)))

    def frame_get(self, ref: int, name: str):
        for tag, value in self.frame_slots(ref):
            if tag.lower() == name.lower():
                return value
        return None

    def describe(self, ref: int) -> str:
        if ref & 3 == 0:
            return str(struct.unpack(">i", struct.pack(">I", ref))[0] >> 2)
        if ref == 2:
            return "nil"
        if ref == 0x1a:
            return "true"
        if ref & 3 == 2:
            if (ref >> 2) & 3 == 1:
                return "$%s" % repr(chr(ref >> 4))
            return "imm %#x" % ref
        if ref & 3 == 3:
            return "@%d" % (ref >> 2)
        name = self.symname(ref)
        if name is not None:
            return "'" + name
        c = self.cls(ref)
        cname = self.symname(c) if self.is_ptr(c) else None
        if cname == "string":
            d = self.data(ref)
            text = d.decode("utf-16-be").rstrip("\0")
            return '"%s"' % text
        f = self.flags(ref)
        if f & 3 == 3:
            return "{frame %#x}" % ref
        if f & 1:
            return "[array %#x of %s]" % (ref, cname or self.describe(c))
        return "<%s %#x>" % (cname or "binary", ref)


def builtins(rom: ROM):
    """(name, function ref) pairs of the built-in functions frame, in slot order."""
    return rom.frame_slots(BUILTIN_FUNCTIONS)


def function_kind(rom: ROM, fn: int):
    """('native', funcPtr, numArgs) or ('script', numArgs, numLocals) or ('other', class)."""
    if not rom.is_ptr(fn):
        return ("other", fn, 0)
    s = rom.slots(fn)
    if s[0] == 0x132:
        return ("native", s[1], s[2] >> 2)
    if s[0] == 0x32:
        n = s[4] >> 2
        return ("script", n & 0xffff, n >> 16)
    return ("other", s[0], 0)


def disassemble(rom: ROM, fn: int, out):
    s = rom.slots(fn)
    code = rom.data(s[1])
    literals = rom.slots(s[2]) if rom.is_ptr(s[2]) else []
    n = s[4] >> 2
    out.append("  args %d, locals %d, argFrame %s, %d bytes" % (n & 0xffff, n >> 16, rom.describe(s[3]), len(code)))
    if rom.is_ptr(s[3]):
        out.append("  argFrame slots: " + ", ".join(t for t, v in rom.frame_slots(s[3])))
    pc = 0
    while pc < len(code):
        op = code[pc]
        a = op >> 3
        b = op & 7
        text = ""
        length = 1
        if b == 7:
            b = struct.unpack(">H", code[pc + 1:pc + 3])[0]
            length = 3
        if a == 0:
            text = SIMPLE_OPS[b] if b < 8 else "simple %d" % b
        else:
            name = OPS.get(a, "op%d" % a)
            text = "%s %d" % (name, b)
            if a == 3 and b < len(literals):
                text += "  ; " + rom.describe(literals[b])
            elif a in (5, 6) and b < len(literals):
                pass
            elif a == 4:
                text = "push-constant %s" % rom.describe(b if length == 1 else struct.unpack(">h", code[pc + 1:pc + 3])[0] & 0xffffffff)
            elif a in (14, 21, 22) and b < len(literals):
                text += "  ; " + rom.describe(literals[b])
            elif a in (15, 20):
                text += "  ; local %d" % b
            elif a == 24 and b < len(FREQ_FUNCS):
                text += "  ; " + FREQ_FUNCS[b]
        out.append("  %4d: %s" % (pc, text))
        pc += length


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--list", action="store_true", help="list every built-in function")
    ap.add_argument("--natives", action="store_true", help="emit the native function table as C++")
    ap.add_argument("--disasm", action="append", default=[], help="disassemble this NewtonScript function")
    ap.add_argument("-o", "--output")
    args = ap.parse_args(argv)

    rom = ROM(args.build_dir)
    out = []
    fns = builtins(rom)
    if args.list:
        for name, fn in sorted(fns, key=lambda p: p[0].lower()):
            kind, x, y = function_kind(rom, fn)
            if kind == "native":
                target = rom.jump.get(x, x)
                sym = rom.symbols.get(target, "?")
                out.append("%-40s native  %d args  %#x -> %#x %s" % (name, y, x, target, sym))
            elif kind == "script":
                out.append("%-40s script  %d args, %d locals" % (name, x, y))
            else:
                out.append("%-40s %s" % (name, rom.describe(fn)))
    if args.natives:
        out += [
            "// Generated by tools/newton-rom/analysis/nsfunctions.py; do not edit.",
            "//   python tools/newton-rom/analysis/nsfunctions.py <build_dir> --natives -o src/frames/ROMNatives.cpp",
            "// The ROM's native built-in functions: the name each has in the built-in",
            "// functions frame, the jump-table address its funcPtr holds, the C function",
            "// that reaches, and its argument count.  NativeFunctions.cpp binds host",
            "// implementations to these addresses.",
            "",
            '#include "NativeFunctions.h"',
            "",
            "const ROMNativeEntry gROMNativeEntries[] = {",
        ]
        count = 0
        for name, fn in sorted(fns, key=lambda p: p[0].lower()):
            kind, x, y = function_kind(rom, fn)
            if kind != "native":
                continue
            target = rom.jump.get(x, x)
            sym = rom.symbols.get(target, "")
            out.append('\t{ "%s", 0x%08x, 0x%08x, %d, "%s" },' % (name, x, target, y, sym))
            count += 1
        out += ["};", "", "const long gROMNativeCount = %d;" % count, ""]
    for name in args.disasm:
        matches = [(n, f) for n, f in fns if n.lower() == name.lower()]
        if not matches:
            print("no built-in function %s" % name, file=sys.stderr)
            return 1
        n, f = matches[0]
        kind, x, y = function_kind(rom, f)
        if kind != "script":
            print("%s is not a NewtonScript function" % name, file=sys.stderr)
            return 1
        out.append("%s (%#x):" % (n, f))
        disassemble(rom, f, out)
    text = "\n".join(out) + "\n"
    if args.output:
        with open(args.output, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
