"""framewalk.py - a ROM function's stack slots followed along one path, as frame offsets.

Apple's ARM C++ compiler moves sp up and down all through a function -
`sub sp`/`add sp`, pushes and pops, post- and pre-indexed loads and stores
on sp - so the same `[sp, #x]` names a different slot from one instruction
to the next, and a local is often copied to a fresh slot a few
instructions later.  Reading which variable an instruction touches means
keeping count of sp; this does it.  Starting from an address with sp at
a given offset (0 at the function's entry: the caller's sp, the `ip` of
`mov ip, sp`), it walks the instructions in order, following
unconditional branches and taking or not taking each conditional one as
told, and prints each with every `[sp, #x]` rewritten as `[F<offset>]` -
the slot relative to the entry's sp, the same for the whole function.

Two things it gets right that a disassembler's listing hides:

  * capstone prints `ldr rX, [sp], #-4` (0xe41dX004) as `pop {rX}`; the
    instruction lowers sp, it does not raise it.  The word is decoded
    for the real direction.
  * a halfword pair is often read with `ldr rX, [sp, #odd]` and no `asr
    #16` after it: that loads the whole word rotated by sixteen, and the
    arithmetic that follows is a packed 32-bit operation whose low half is
    what is kept.  (Not decoded - but the frame offsets show which two
    halfwords are involved.)

Inputs:  <build>/rom.bin and <build>/symbols.txt (extract_rom.py, dump_symbols.py)
Output:  one line per instruction walked: address, sp (frame offset), the
         instruction with its sp slots as frame offsets; `?? not taken` at
         each conditional branch nobody chose for
Usage:   build/venv/Scripts/python tools/newton-rom/analysis/framewalk.py build/MP2x00US START STOP [--sp N]
             [--take ADDR ...] [--skip ADDR ...]
         e.g. framewalk.py build/MP2x00US 0xa1b2c 0xa1fb0 --take 0xa1eb8
             (TEditView::AddNewParagraph from its entry to the remote-writing
             test, a word not from the recogniser)
Needs capstone (tools/newton-rom/requirements.txt).
"""
import argparse
import os
import re

import capstone


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('build')
    ap.add_argument('start')
    ap.add_argument('stop')
    ap.add_argument('--sp', type=int, default=0, help='sp at START, as a frame offset (0 at a function entry)')
    ap.add_argument('--take', nargs='*', default=[], help='conditional branches to take')
    ap.add_argument('--skip', nargs='*', default=[], help='conditional branches not to take (the default; quiets the ??)')
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
    take = {int(x, 16) for x in a.take}
    skip = {int(x, 16) for x in a.skip}
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
    md.skipdata = True

    def decode(addr):
        for i in md.disasm(rom[addr:addr + 4], addr):
            return i.mnemonic, i.op_str
        return '.word', ''

    def word(addr):
        return int.from_bytes(rom[addr:addr + 4], 'big')

    def count(regs):
        return len(regs.split(','))

    addr, stop, sp = int(a.start, 16), int(a.stop, 16), a.sp
    for _ in range(20000):
        if addr == stop:
            break
        mn, ops = decode(addr)
        w = word(addr)
        text = '%-7s %s' % (mn, ops)
        shown = re.sub(r'\[sp, #(-?(?:0x)?[0-9a-f]+)\]', lambda m: '[F%+d]' % (sp + int(m.group(1), 0)), text)
        shown = shown.replace('[sp]', '[F%+d]' % sp)
        single = (w >> 25) & 7 == 2 and (w >> 16) & 0xf == 13		# ldr/str with sp as the base
        if single and (w >> 24) & 1 == 0:							# post-indexed: the real direction
            off = w & 0xfff
            shown = '%s r%d, [F%+d], #%s%#x' % ('ldr' if (w >> 20) & 1 else 'str', (w >> 12) & 0xf, sp, '' if (w >> 23) & 1 else '-', off)
        extra = ''
        if mn.startswith('bl'):
            try:
                extra = syms.get(int(ops.lstrip('#'), 16), '')
            except ValueError:
                pass
        print('%08x  F%+-5d %s  %s' % (addr, sp, shown, extra))
        # sp after the instruction
        m = re.match(r'(sub|add)\s*$', mn) and re.match(r'sp, sp, #(\S+)$', ops)
        if m:
            sp += (-1 if mn == 'sub' else 1) * int(m.group(1), 0)
        elif single and (w >> 21) & 1 | ((w >> 24) & 1 == 0):
            off = w & 0xfff
            sp += off if (w >> 23) & 1 else -off
        elif (w >> 25) & 7 == 4 and (w >> 16) & 0xf == 13 and (w >> 21) & 1:	# ldm/stm sp!
            n = bin(w & 0xffff).count('1')
            sp += 4 * n if (w >> 23) & 1 else -4 * n
        # the flow
        b = re.match(r'b(eq|ne|lt|le|gt|ge|hi|ls|lo|hs|mi|pl|cc|cs|vs|vc)?$', mn)
        if b and ops.startswith('#'):
            target = int(ops.lstrip('#'), 16)
            if b.group(1) is None or addr in take:
                addr = target
                continue
            if addr not in skip:
                print('          ?? not taken')
        if mn in ('ldmdb', 'ldmia') and 'pc' in ops:
            break
        addr += 4


if __name__ == '__main__':
    main()
