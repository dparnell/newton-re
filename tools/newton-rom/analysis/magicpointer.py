#!/usr/bin/env python3
"""Find the magic-pointer numbers (@n) of ROM objects by name, for writing
NewtonScript that uses the ROM's protos the way a package does
(`{_proto: @164, ...}` is protoCheckbox) - the host's own panels
(host/HostSettings.ns, print/host/HostPrinters.ns) are written so.

    python tools/newton-rom/analysis/magicpointer.py protoCheckbox protoTextButton ...
        [--newtonscript build/host/host/newtonscript] [--limit 1600]

Prints "NAME @N" for each name found (a name not printed is not a magic
pointer below the limit).  How: a script for the host's newtonscript that
compares each of @0 .. @limit-1 with ROMConstant("NAME") - the ROM object
of that name - and prints the numbers that match; newtonscript boots from
the reconstructed objects, so no ROM image is needed.
"""
import argparse
import os
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="+")
    parser.add_argument("--newtonscript", default=os.path.join("build", "host", "host", "newtonscript"))
    parser.add_argument("--limit", type=int, default=1600)
    args = parser.parse_args()
    lines = ["mpWanted := [" + ", ".join('ROMConstant("%s")' % n for n in args.names) + "];",
             "mpNames := [" + ", ".join('"%s"' % n for n in args.names) + "];",
             'mpCheck := func(i, obj) foreach k, w in mpWanted do if w and w = obj then Print(mpNames[k] & " @" & NumberStr(i));']
    for i in range(args.limit):
        lines.append("try call mpCheck with (%d, @%d) onexception |evt.ex| do nil;" % (i, i))
    with tempfile.NamedTemporaryFile("w", suffix=".ns", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        script = f.name
    try:
        out = subprocess.run([args.newtonscript, script], capture_output=True, text=True).stdout
    finally:
        os.unlink(script)
    for line in out.splitlines():
        line = line.strip().strip('"')
        if " @" in line:
            print(line)


if __name__ == "__main__":
    sys.exit(main())
