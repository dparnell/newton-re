#!/usr/bin/env python3
"""List the x86-64 instruction-set extensions a Windows executable uses.

Purpose: a release must run on any x86-64 CPU.  A zig build without a target
compiles for the build machine's CPU, and the binary then dies at start with
an illegal instruction (0xC000001D) on a CPU without AVX - a user's Celeron
laptop ran none of the Windows releases.  This disassembles every executable
section of a PE file with capstone and counts the instructions that need more
than the x86-64 baseline (SSE2), with the first of each kind.

Input:  a 64-bit PE executable (newton.exe).
Output: one line per extension found: name, count, first instruction.
        Exit status 1 if any is found, so CI can fail a non-portable build.
Needs:  capstone (`pip install capstone`; build/venv has it).

    python tools/ci/cpuscan.py build/host/host/newton.exe
"""
import collections
import struct
import sys

import capstone
from capstone import x86

# capstone's group names for everything past SSE2
BEYOND_BASELINE = {
    'SSE3', 'SSSE3', 'SSE41', 'SSE42', 'SSE4A', 'AVX', 'AVX2', 'AVX512',
    'FMA', 'FMA4', 'F16C', 'BMI', 'BMI2', 'ADX', 'XOP', 'SHA', 'AES',
    'PCLMUL', 'TBM',
}
# BMI's tzcnt is rep bsf, which a CPU without BMI runs as bsf; LLVM emits it
# for baseline x86-64 where the two agree
RUNS_ANYWHERE = {'tzcnt'}


def sections(data):
    """(virtual address, bytes) of each executable section."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        sys.exit('not a PE file')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(count):
        entry = pe + 24 + optional + 40 * i
        vsize, va, rsize, roff = struct.unpack_from('<IIII', data, entry + 8)
        flags = struct.unpack_from('<I', data, entry + 36)[0]
        if flags & 0x20000000:          # IMAGE_SCN_MEM_EXECUTE
            yield va, data[roff:roff + min(vsize, rsize)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    names = {getattr(x86, n): n[len('X86_GRP_'):] for n in dir(x86) if n.startswith('X86_GRP_')}
    counts = collections.Counter()
    first = {}
    for va, code in sections(data):
        offset = 0
        while offset < len(code):
            decoded = False
            for ins in md.disasm(code[offset:], va + offset):
                decoded = True
                offset = ins.address - va + ins.size
                if ins.mnemonic in RUNS_ANYWHERE:
                    continue
                for group in ins.groups:
                    name = names.get(group)
                    if name in BEYOND_BASELINE:
                        counts[name] += 1
                        first.setdefault(name, f'{ins.address:#x} {ins.mnemonic} {ins.op_str}')
            if not decoded:             # padding or data: step over a byte
                offset += 1
    for name, n in counts.most_common():
        print(f'{name:8} {n:7}  first {first[name]}')
    if not counts:
        print('baseline x86-64 only')
    return 1 if counts else 0


if __name__ == '__main__':
    sys.exit(main())
