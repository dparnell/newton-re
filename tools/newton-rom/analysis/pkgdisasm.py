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


def disassemble(code, start, end):
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
    off = start & ~3
    while off < end and off + 4 <= len(code):
        word = struct.unpack_from('>I', code, off)[0]
        insns = list(md.disasm(code[off:off + 4], off))
        text = (insns[0].mnemonic + ' ' + insns[0].op_str) if insns else '.word 0x%08x' % word
        note = fourcc(word)
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
    args = ap.parse_args()

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
