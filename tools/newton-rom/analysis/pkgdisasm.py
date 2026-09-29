"""Disassemble the native code of a package's protocol part.

A package's protocol part (NTK's native code: a driver, a comms tool, a
service) is big-endian ARM (v4) that the ROM runs where it lies; the
reconstruction never runs it, but reading it is how the host's
replacements learn what the part does - the layout of an option it
builds, the value it puts in a field.

    python pkgdisasm.py <file.pkg> --list
    python pkgdisasm.py <file.pkg> --part N [--start OFF] [--end OFF]
    python pkgdisasm.py <file.pkg> --find itrs [--context 0x60]

--list prints the parts (index, kind, offset in the file, size);
--part disassembles a part, OFF being offsets within the part (default:
the whole part); --find locates every occurrence of a four-character
literal (as ASCII, how native code holds an option label) and
disassembles the code before it, where the function that loads it from
its literal pool lies.  A word that reads as four printable characters
is shown as such beside the instruction.

With --rom BUILD a call through NTK's glue (a stub `ldr pc,[pc,#-4]` and
an address 0x018xxxxx) is named: the ROM maps 0x01800000 onto a table at
physical 0x13000, a B per word into the jump table, so 0x01800000 + 4k is
the function the word at ROM 0x13000 + 4k branches to.

Needs capstone (tools/newton-rom/requirements.txt).  Offsets are relative
to the part (a part is loaded at an arbitrary address and relocated, so
branch targets are part offsets too).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import packages  # noqa: E402

import capstone  # noqa: E402


def parts(data):
    p = packages.parse_package(data, 0)
    reloc = 0
    if p['flags'] & 0x04000000:		# kRelocationFlag: a relocation chunk follows the directory
        reloc = struct.unpack_from('>I', data, p['directory_size'] + 4)[0]
    out = []
    for i, part in enumerate(p['parts']):
        start = p['directory_size'] + reloc + part['offset']
        out.append((i, part['flags'] & 3, start, part['size']))
    return out


def fourcc(word):
    b = struct.pack('>I', word)
    if all(0x20 <= c < 0x7f for c in b):
        return "'" + b.decode('ascii') + "'"
    return ''


# NTK's glue: native code reaches the ROM through stubs `ldr pc,[pc,#-4]`
# followed by an address 0x018xxxxx.  The ROM maps that page range onto a
# table at physical 0x13000 (just after the patchable jump table), one B
# instruction per word, each to a slot of the 2.x jump table: so
# 0x01800000 + 4k is the function the word at ROM 0x13000 + 4k branches to.
GLUE_BASE = 0x01800000
GLUE_TABLE = 0x13000
GLUE_SIZE = 0x3000

_rom = None
_names = None


def load_rom(build_dir):
    """The ROM image and its code symbols, for naming glue calls."""
    global _rom, _names
    import re
    with open(os.path.join(build_dir, 'rom.bin'), 'rb') as f:
        _rom = f.read()
    _names = {}
    with open(os.path.join(build_dir, 'symbols.txt'), encoding='utf-8') as f:
        for line in f:
            m = re.match(r'^(\S+) code\s+(\S+)', line)
            if m:
                _names.setdefault(int(m.group(1), 16), m.group(2))


def glue_name(address):
    """The ROM function an NTK glue address 0x018xxxxx reaches (None when
    no ROM is loaded or the address is not glue)."""
    if _rom is None or not (GLUE_BASE <= address < GLUE_BASE + GLUE_SIZE):
        return None
    word = struct.unpack_from('>I', _rom, GLUE_TABLE + address - GLUE_BASE)[0]
    if word >> 24 != 0xea:
        return None
    off = word & 0xffffff
    if off & 0x800000:
        off -= 0x1000000
    target = address + 8 + off * 4
    return _names.get(target, '%#x' % target)


def glue_stubs(code):
    """offset -> ROM function name of each glue stub in the code."""
    stubs = {}
    for off in range(0, len(code) - 7, 4):
        if struct.unpack_from('>I', code, off)[0] == 0xe51ff004:
            name = glue_name(struct.unpack_from('>I', code, off + 4)[0])
            if name:
                stubs[off] = name
    return stubs


# NTK's native-compiled NewtonScript also calls routines of its own at the
# front of the code binary, most of which check the ROM's version word
# (0x13dc: 0x20002 on the MP2x00 US 2.1) and, on a 2.x ROM, go straight to
# a glue stub (the ROM has the runtime support NTK's native code needs:
# GetGInterpreter, TInterpreter::GetReceiver, IsSend, SetSendEnv...).
ROM_VERSION = 0x20002


def trampoline_target(code, stubs, at, depth=0):
    """What a call to `at` reaches on this ROM: a glue stub's function, or
    None for a routine of the binary's own."""
    if at in stubs:
        return stubs[at]
    if depth > 4:
        return None
    import re
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
    for k in range(16):
        off = at + 4 * k
        if off + 4 > len(code):
            return None
        insns = list(md.disasm(code[off:off + 4], off))
        if not insns:
            return None
        m, o = insns[0].mnemonic, insns[0].op_str
        if m in ('bge', 'bgt') and o.startswith('#'):
            # (the version test's branch taken on a 2.x ROM)
            return trampoline_target(code, stubs, int(o[1:], 16), depth + 1)
        if m == 'b' and o.startswith('#') and k > 0:
            return trampoline_target(code, stubs, int(o[1:], 16), depth + 1)
        if m == 'ldrgt' and o.startswith('pc'):
            found = re.search(r'#(0x[0-9a-f]+|\d+)', o)
            word = struct.unpack_from('>I', code, off + 8 + int(found.group(1), 0))[0]
            return glue_name(word)
        if m.startswith('ldm') or m.startswith('pop') or (m == 'mov' and o.startswith('pc')):
            return None
    return None


def disassemble(code, start, end):
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
    stubs = glue_stubs(code) if _rom is not None else {}
    off = start & ~3
    while off < end and off + 4 <= len(code):
        word = struct.unpack_from('>I', code, off)[0]
        insns = list(md.disasm(code[off:off + 4], off))
        text = (insns[0].mnemonic + ' ' + insns[0].op_str) if insns else '.word 0x%08x' % word
        note = fourcc(word)
        if insns and insns[0].mnemonic in ('bl', 'b') and insns[0].op_str.startswith('#'):
            target = int(insns[0].op_str[1:], 16)
            if target in stubs:
                note = stubs[target]
            elif _rom is not None:
                reached = trampoline_target(code, stubs, target)
                if reached:
                    note = '(2.x) ' + reached
        elif _rom is not None and glue_name(word):
            note = glue_name(word)
        print('  %06x  %08x  %-40s %s' % (off, word, text, note))
        off += 4


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('package')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--part', type=int)
    ap.add_argument('--start', type=lambda s: int(s, 0), default=0)
    ap.add_argument('--end', type=lambda s: int(s, 0))
    ap.add_argument('--find', action='append', default=[])
    ap.add_argument('--context', type=lambda s: int(s, 0), default=0x60)
    ap.add_argument('--rom', help='a build directory (rom.bin, symbols.txt): name the ROM functions NTK glue calls reach')
    args = ap.parse_args()
    if args.rom:
        load_rom(args.rom)

    data = open(args.package, 'rb').read()
    ps = parts(data)
    if args.list:
        for i, kind, start, size in ps:
            print('part %d  kind %d  at 0x%x  size 0x%x' % (i, kind, start, size))
    if args.part is not None:
        i, kind, start, size = ps[args.part]
        code = data[start:start + size]
        disassemble(code, args.start, args.end if args.end is not None else size)
    for label in args.find:
        key = label.encode('ascii')
        for i, kind, start, size in ps:
            if kind != 0:
                continue
            code = data[start:start + size]
            at = code.find(key)
            while at >= 0:
                if at % 4 == 0:
                    print('part %d: %r at 0x%x' % (i, label, at))
                    disassemble(code, max(0, at - args.context), at + 4)
                    print()
                at = code.find(key, at + 1)


if __name__ == '__main__':
    main()
