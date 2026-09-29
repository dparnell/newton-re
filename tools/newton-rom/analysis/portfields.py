#!/usr/bin/env python3
"""Check the reconstruction's use of a port's visRgn and clipRgn against the ROM.

Usage:
    build/venv/Scripts/python tools/newton-rom/analysis/portfields.py build/MP2x00US \
        --project build/ghidra --name MP2x00US --ghidra D:/apps/ghidra_12.1.3_PUBLIC \
        [--src src] [--exclude comms --exclude host] [--show 0x001a678c]

Why: the ROM is built with QD_Gray, so its PixelMap has a gray-table word more
than the DDK's plain one and every GrafPort field after portBits sits four
bytes further on - visRgn at +0x24, clipRgn at +0x28 (SetupScalingRegions
reads the clip with `ldr r0,[r0,#0x28]` at 0x00196368).  The Ghidra project's
GrafPort type is the plain one, so its decompiler names the ROM's words
wrongly:

    decompiler says    the ROM's word is
    visRgn  (+0x20)    portRect's bottom-right
    clipRgn (+0x24)    visRgn
    fgPat   (+0x28)    clipRgn

A reconstruction transcribed from the decompile therefore swaps the two
regions.  This scans the reconstructed source for functions that read or
write a port's visRgn or clipRgn directly (`->visRgn`, `.clipRgn`, ...), takes
each one's `// ROM 0x...` citation, decompiles those ROM functions in one
Ghidra run (decompile.py), translates the decompiler's GrafPort field names
through the table above, and prints for each function which regions the
reconstruction uses and which the ROM does:

    OK        the same regions
    DIFFERS   the reconstruction uses a region the ROM does not (or not the
              one it does) - look at the disassembly (disasm.py) to confirm
    NO-PORT   the ROM's decompile names no GrafPort region (it reaches the
              port through another type or an offset): check by hand
    HOST      the function is the host's own (no ROM citation)

Inputs: the build directory (symbols), the Ghidra project, the source tree.
Output: the table on stdout; --show ADDR prints the decompile lines that
name port regions for one function.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

CITE = re.compile(r"^//\s*ROM\s+(0x[0-9a-fA-F]+)\s*(\S*)")
HOST_FIELD = re.compile(r"(?:->|\.)\s*(visRgn|clipRgn)\b")
# the decompiler's names, and what they are in the ROM
ROM_FIELD = re.compile(r"(?:->|\.)(visRgn|clipRgn|fgPat)\b")
REAL = {"visRgn": "portRect+4", "clipRgn": "visRgn", "fgPat": "clipRgn"}


def scan(src_dir, excludes):
    """[(file, line, address, name, {fields})] for every cited function using a region."""
    found = []
    for root, dirs, files in os.walk(src_dir):
        rel = os.path.relpath(root, src_dir).replace("\\", "/")
        if any(rel == e or rel.startswith(e + "/") for e in excludes):
            continue
        for f in files:
            if not f.endswith((".cpp", ".c")):
                continue
            path = os.path.join(root, f)
            with open(path, encoding="latin-1") as fh:
                lines = fh.read().splitlines()
            current = None          # (address, name, line)
            fields = set()
            host_fields = set()

            def flush():
                if current and fields:
                    found.append((path, current[2], current[0], current[1], set(fields)))
                elif not current and host_fields:
                    pass

            for i, line in enumerate(lines, 1):
                m = CITE.match(line.strip())
                if m:
                    flush()
                    current = (m.group(1).lower(), m.group(2), i)
                    fields = set()
                    continue
                if line.lstrip().startswith("//"):
                    continue
                for fm in HOST_FIELD.finditer(line):
                    fields.add(fm.group(1))
            flush()
    return found


def decompile(addresses, args):
    cmd = [sys.executable, os.path.join(HERE, "decompile.py"), "--project", args.project,
           "--name", args.name]
    if args.ghidra:
        cmd += ["--ghidra", args.ghidra]
    for a in addresses:
        cmd += ["--address", a]
    out = subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout
    out = out.replace("\r", "")
    blocks = {}
    current = None
    for line in out.splitlines():
        m = re.match(r"^// .*@ ([0-9a-fA-F]{8})", line)
        if m:
            current = "0x" + m.group(1).lower()
            blocks[current] = []
            continue
        if current:
            blocks[current].append(line)
    return blocks


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--project", required=True)
    ap.add_argument("--name", default="MP2x00US")
    ap.add_argument("--ghidra", default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("--src", default="src")
    ap.add_argument("--exclude", action="append", default=["ddk"])
    ap.add_argument("--show", action="append", default=[])
    args = ap.parse_args(argv)

    found = scan(args.src, args.exclude)
    addrs = sorted({"0x%08x" % int(a, 16) for _, _, a, _, _ in found})
    blocks = decompile(addrs, args)
    for path, line, addr, name, fields in sorted(found, key=lambda t: (t[0], t[1])):
        key = "0x%08x" % int(addr, 16)
        text = blocks.get(key)
        if text is None:
            verdict, rom = "NO-DECOMP", set()
        else:
            rom = {REAL[m.group(1)] for l in text for m in ROM_FIELD.finditer(l)
                   if not re.search(r"PenState|Pen\w*\.fgPat|\bpen\w*\.fgPat|PStack\w*\.fgPat", l)}
            rom.discard("portRect+4")
            if not rom:
                verdict = "NO-PORT"
            elif fields <= rom:
                verdict = "OK"
            else:
                verdict = "DIFFERS"
        print("%-9s %s:%d  %s %s  host=%s rom=%s" % (verdict, os.path.relpath(path).replace("\\", "/"), line,
              key, name, ",".join(sorted(fields)), ",".join(sorted(rom)) or "-"))
    for addr in args.show:
        key = "0x%08x" % int(addr, 16)
        print("--- %s" % key)
        for l in blocks.get(key, []):
            if ROM_FIELD.search(l):
                print(l)
    return 0


if __name__ == "__main__":
    sys.exit(main())
