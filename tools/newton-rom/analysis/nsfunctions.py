#!/usr/bin/env python3
"""List the ROM's built-in NewtonScript functions, and disassemble their bytecode.

Usage:
    python nsfunctions.py <build_dir> [--list] [--natives] [--disasm NAME]... [-o file]
    python nsfunctions.py build/MP2100D --list
    python nsfunctions.py build/MP2100D --natives -o src/frames/ROMNatives.cpp
    python nsfunctions.py build/MP2100D --disasm Max --disasm ArrayInsert
    python nsfunctions.py build/MP2100D --object unionsoupprototype --disasm unionsoupprototype.Add

The built-in functions frame (Rbuiltinfunctions, the ROM's object
0x0062418d) maps function names to function objects of two kinds: native
functions (class 0x132: {funcPtr, numArgs}, funcPtr the jump-table address
of a C function) and NewtonScript functions (class 0x32: [instructions,
literals, argFrame, numArgs | numLocals << 16]).  --list prints them all
with their kind and argument count; --natives emits a C++ table of the
native ones (name, jump-table address, the ROM function it reaches and its
symbol) for frames/NativeFunctions.cpp to bind host implementations to,
and a second table of every other native function object in the ROM's
object area (the methods of the store, soup, cursor and entry prototype
frames and the like), each named by the frame slot that holds it;
--disasm prints a NewtonScript function's bytecode (Newton Formats, the
instruction set of the NewtonScript interpreter, as TInterpreter::SlowRun
0x002cc66c executes it); --object prints the slots of a ROM frame or array
with each function's kind.  An object is named by the ROM's Ref symbol
(Rfoo or foo, whose word holds the ref), a built-in function's name, or
0x address of the ref (a magic pointer is resolved through
gROMMagicPointerTable); a function to disassemble may also be object.slot,
the slot found through _proto and _parent as a method lookup would.

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


# the built-in functions frame is what the Rbuiltinfunctions constant points
# at, so this works on any of the ROMs (ROM.builtin_functions below)
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
        by_name = {}
        for s in data["symbols"]:
            if "jt_index" not in s:
                self.symbols.setdefault(s["address"], s["name"])
                by_name.setdefault(s["name"], s["address"])
        self.by_name = by_name
        self.mp_table = by_name["gROMMagicPointerTable"]
        self.soup = by_name["gROMSoupData"]
        self.soup_size = self.word(by_name["gROMSoupDataSize"])
        self.jump = {int(v): int(t) for v, t in data["jumptable"]["entries"]} if "jumptable" in data else {}

    # the frame magic pointer 1.2 resolves to, found through the constant
    # rather than the literal in ResolveMagicPtr, so any ROM works
    def builtin_functions(self) -> int:
        return self.word(self.by_name["Rbuiltinfunctions"])

    def word(self, a: int) -> int:
        return struct.unpack(">I", self.rom[a:a + 4])[0]

    def resolve_magic(self, ref: int) -> int:
        """A magic pointer (@n, table 0) resolved through gROMMagicPointerTable
        (the count, then the refs); other refs unchanged."""
        if ref & 3 != 3:
            return ref
        index = ref >> 2
        if index >= self.word(self.mp_table):
            return ref
        return self.word(self.mp_table + 4 + 4 * index)

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

    def frame_get(self, ref: int, name: str, inherited: bool = False):
        """A frame's slot by name; inherited: looked up through _proto and
        _parent as NewtonScript's method lookup does."""
        while ref is not None and self.is_ptr(ref) and self.flags(ref) & 3 == 3:
            proto = None
            parent = None
            for tag, value in self.frame_slots(ref):
                if tag is None:
                    continue
                if tag.lower() == name.lower():
                    return value
                if tag == "_proto":
                    proto = self.resolve_magic(value)
                elif tag == "_parent":
                    parent = self.resolve_magic(value)
            if not inherited:
                return None
            if proto is not None and proto != 2 and self.is_ptr(proto):
                found = self.frame_get(proto, name, True)
                if found is not None:
                    return found
            ref = parent
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
    return rom.frame_slots(rom.builtin_functions())


def objects(rom: ROM):
    """Every object ref in the ROM's object area, in address order."""
    a = rom.soup
    end = rom.soup + rom.soup_size
    while a < end:
        yield a + 1
        a += (rom.size(a + 1) + 3) & ~3


def other_natives(rom: ROM):
    """(slot name, function ref) pairs of the native function objects that are
    not in the built-in functions frame, named by the (first) frame slot that
    holds each; unreferenced ones are named ""."""
    in_builtins = {fn for _, fn in builtins(rom)}
    natives = []
    frames = []
    for ref in objects(rom):
        f = rom.flags(ref)
        if f & 3 != 0 and rom.size(ref) == 24 and rom.slots(ref)[0] == 0x132:
            if ref not in in_builtins:
                natives.append(ref)
        elif f & 3 == 3 and rom.is_ptr(rom.cls(ref)):
            frames.append(ref)
    names = {}
    for frame in frames:
        for tag, value in rom.frame_slots(frame):
            if value in names or not rom.is_ptr(value) or tag is None:
                continue
            names[value] = tag
    return [(names.get(fn, ""), fn) for fn in natives]


def resolve(rom: ROM, name: str):
    """A ROM object ref from an address (0x...), the name of a ROM Ref (an
    R... symbol, whose word holds the ref) or a built-in function's name."""
    if name.lower().startswith("0x"):
        return rom.resolve_magic(int(name, 16))
    if name in rom.by_name:
        return rom.resolve_magic(rom.word(rom.by_name[name]))
    if "R" + name in rom.by_name:
        return rom.resolve_magic(rom.word(rom.by_name["R" + name]))
    # the R symbols are all lower case, but the objects they hold are
    # written the way NewtonScript writes them (unionSoupPrototype)
    if "R" + name.lower() in rom.by_name:
        return rom.resolve_magic(rom.word(rom.by_name["R" + name.lower()]))
    for n, fn in builtins(rom):
        if n.lower() == name.lower():
            return fn
    return None


def describe_slot(rom: ROM, value: int) -> str:
    kind, x, y = function_kind(rom, value)
    if kind == "native":
        target = rom.jump.get(x, x)
        return "native %d args  %#x -> %#x %s" % (y, x, target, rom.symbols.get(target, "?"))
    if kind == "script":
        return "script %d args, %d locals (%#x)" % (x, y, value)
    return rom.describe(value)


def function_kind(rom: ROM, fn: int):
    """('native', funcPtr, numArgs) or ('script', numArgs, numLocals) or ('other', class)."""
    if not rom.is_ptr(fn):
        return ("other", fn, 0)
    if rom.flags(fn) & 3 == 0:
        return ("other", rom.cls(fn), 0)
    s = rom.slots(fn)
    if len(s) < 3:
        return ("other", s[0] if s else 0, 0)
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
    ap.add_argument("--object", action="append", default=[], help="print the slots of this ROM frame or array (a name or 0x address)")
    ap.add_argument("--disasm", action="append", default=[], help="disassemble this NewtonScript function (a built-in's name, object.slot or 0x address)")
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
        def emit(pairs):
            count = 0
            seen = set()
            for name, fn in pairs:
                kind, x, y = function_kind(rom, fn)
                if kind != "native" or x in seen:
                    continue
                seen.add(x)
                target = rom.jump.get(x, x)
                sym = rom.symbols.get(target, "")
                out.append('\t{ "%s", 0x%08x, 0x%08x, %d, "%s" },' % (name, x, target, y, sym))
                count += 1
            return count
        count = emit(sorted(fns, key=lambda p: p[0].lower()))
        out += ["};", "", "const long gROMNativeCount = %d;" % count, ""]
        out += [
            "// The other native function objects in the ROM's object area (the methods",
            "// of the store, soup, cursor and entry prototype frames and the like),",
            "// each named by the frame slot that holds it; bound the same way.",
            "const ROMNativeEntry gROMMethodEntries[] = {",
        ]
        count = emit(sorted(other_natives(rom), key=lambda p: (p[0].lower(), p[1])))
        out += ["};", "", "const long gROMMethodCount = %d;" % count, ""]
    for name in args.object:
        ref = resolve(rom, name)
        if ref is None or not rom.is_ptr(ref):
            print("no ROM object %s" % name, file=sys.stderr)
            return 1
        flags = rom.flags(ref)
        if flags & 3 == 3:
            out.append("%s (%#x): frame" % (name, ref))
            for tag, value in rom.frame_slots(ref):
                out.append("  %-24s %s" % (tag, describe_slot(rom, value)))
        elif flags & 1:
            out.append("%s (%#x): array of %s" % (name, ref, rom.describe(rom.cls(ref))))
            for i, value in enumerate(rom.slots(ref)):
                out.append("  %-24d %s" % (i, describe_slot(rom, value)))
        else:
            out.append("%s (%#x): %s" % (name, ref, rom.describe(ref)))
    for name in args.disasm:
        n, f = name, None
        if "." in name:
            obj, slot = name.rsplit(".", 1)
            ref = resolve(rom, obj)
            if ref is not None and rom.is_ptr(ref) and rom.flags(ref) & 3 == 3:
                f = rom.frame_get(ref, slot, True)
        else:
            f = resolve(rom, name)
        if f is None:
            print("no function %s" % name, file=sys.stderr)
            return 1
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
