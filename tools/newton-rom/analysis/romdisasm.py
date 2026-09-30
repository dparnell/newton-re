"""romdisasm.py - a range of the ROM disassembled straight from rom.bin, without Ghidra.

For when the Ghidra project is locked (another process has it open) or has not
been built yet: capstone over the big-endian ARM image, each `bl`/`b` target
and each pc-relative literal named from symbols.txt.  It does not follow the
flow, so a literal pool in the range decodes as nonsense (start again after it).

Inputs:  <build>/rom.bin and <build>/symbols.txt (extract_rom.py, dump_symbols.py)
Output:  one line per instruction on stdout
Usage:   build/venv/Scripts/python tools/newton-rom/analysis/romdisasm.py build/MP2x00US 0x268c24 0x268d30
Needs capstone (tools/newton-rom/requirements.txt).
"""
import argparse
import os

import capstone


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('build')
    ap.add_argument('start')
    ap.add_argument('end')
    a = ap.parse_args()
    rom = open(os.path.join(a.build, 'rom.bin'), 'rb').read()
    syms = {}
    with open(os.path.join(a.build, 'symbols.txt'), encoding='utf-8', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 3:
                try:
                    syms.setdefault(int(p[0], 16), p[2])
                except ValueError:
                    pass
    start, end = int(a.start, 16), int(a.end, 16)
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
    for i in md.disasm(rom[start:end], start):
        extra = ''
        if i.mnemonic.startswith('bl') or i.mnemonic == 'b':
            try:
                extra = syms.get(int(i.op_str.lstrip('#'), 16), '')
            except ValueError:
                pass
        elif i.mnemonic.startswith('ldr') and '[pc' in i.op_str:
            off = int(i.op_str.split('#')[-1].rstrip(']'), 16) if '#' in i.op_str else 0
            at = i.address + 8 + off
            if at + 4 <= len(rom):
                v = int.from_bytes(rom[at:at + 4], 'big')
                extra = '=0x%x %s' % (v, syms.get(v, ''))
        print('%08x  %-7s %s  %s' % (i.address, i.mnemonic, i.op_str, extra))


if __name__ == '__main__':
    main()
