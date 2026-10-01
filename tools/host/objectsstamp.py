#!/usr/bin/env python3
"""objectsstamp.py - check that newton refuses an object file it was not built for.

An object file (romsrc.py build -o; the build's romsrc-objects.bin) carries
the stamp of the builder that wrote it, and newton and newtonscript are
compiled with the stamp of the builder their build ran
(host/HostObjectsFile.h's HostObjectsFileMatches).  This makes two copies of
an object file that a newton must refuse - one whose stamp is another
builder's, and one written before there were stamps (version 3) - and runs
the program on each, expecting it to stop at once with

    <program>: <file> was built for a different <program> (rebuild with cmake --build <dir>)

and a non-zero exit, rather than boot.  The object file itself is left
alone.

    python tools/host/objectsstamp.py --objects build/host/romsrc-objects.bin \\
        --program build/host/host/newton --program build/host/host/newtonscript \\
        --work build/host/objectsstamp

Prints `objectsstamp: done` when every program refused both copies
(ctest host.NewtonObjectsStamp).
"""

import argparse
import os
import struct
import subprocess
import sys


def walk_to_stamp(data):
    """The offset of the version-4 stamp block (its length word)."""
    area_size, mp_count = struct.unpack_from(">I", data, 16)[0], struct.unpack_from(">I", data, 24)[0]
    at = 28 + area_size + 4 * mp_count
    count = struct.unpack_from(">I", data, at)[0]
    at += 4
    for _ in range(count):
        length = struct.unpack_from(">I", data, at + 4)[0]
        at += 8 + ((length + 3) & ~3)
    moved = struct.unpack_from(">I", data, at)[0]
    return at + 4 + 8 * moved


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--objects", required=True, help="an object file the programs were built for")
    ap.add_argument("--program", action="append", required=True, help="newton or newtonscript (repeatable)")
    ap.add_argument("--work", required=True, help="a directory for the two copies")
    a = ap.parse_args(argv)
    with open(a.objects, "rb") as f:
        data = bytearray(f.read())
    if data[:8] != b"NewtObjs" or struct.unpack_from(">I", data, 8)[0] != 4:
        print("objectsstamp: %s is not a version 4 object file" % a.objects)
        return 1
    at = walk_to_stamp(data)
    length = struct.unpack_from(">I", data, at)[0]
    os.makedirs(a.work, exist_ok=True)
    # another builder's stamp: the same length, the last character changed
    other = bytearray(data)
    last = at + 4 + length - 1
    other[last] = ord("0") if other[last] != ord("0") else ord("1")
    # a file from before the stamps: version 3, the stamp block left off
    old = bytearray(data[:at])
    struct.pack_into(">I", old, 8, 3)
    copies = {"another builder's": os.path.join(a.work, "other-stamp.bin"),
              "a version 3": os.path.join(a.work, "version3.bin")}
    for (what, path), body in zip(copies.items(), (other, old)):
        with open(path, "wb") as f:
            f.write(body)
    failed = 0
    for program in a.program:
        program = os.path.abspath(program)
        name = os.path.splitext(os.path.basename(program))[0]
        for what, path in copies.items():
            args = [program, "--objects", path]
            args += ["--headless", "5"] if name == "newton" else ["-e", "Print(1)"]
            try:
                r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            except subprocess.TimeoutExpired:
                print("objectsstamp: %s booted %s object file and ran on" % (name, what))
                failed += 1
                continue
            said = r.stderr.decode("utf-8", "replace") + r.stdout.decode("utf-8", "replace")
            expected = "%s: %s was built for a different %s (rebuild with cmake --build" % (name, path, name)
            if r.returncode == 0 or expected not in said:
                print("objectsstamp: %s did not refuse %s object file (exit %d):\n%s" % (name, what, r.returncode, said[-2000:]))
                failed += 1
            else:
                print("objectsstamp: %s refused %s object file" % (name, what))
    if failed:
        return 1
    print("objectsstamp: done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
