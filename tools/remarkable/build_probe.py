#!/usr/bin/env python3
"""Build rmprobe (tools/remarkable/rmprobe.c) for a reMarkable Paper Pro:
aarch64 Linux, against the same glibc floor as newton's cross build
(src/cmake/zig-aarch64-linux.cmake), with zig as the cross compiler.

    python tools/remarkable/build_probe.py [-o tmp/rmpp-app] [--glibc 2.31]

writes OUT/rmprobe and OUT/rmprobe-app/ (an AppLoad application that runs
`rmprobe --qtfb 30` and leaves its output in /home/root/rmprobe-qtfb.log),
and OUT/rmsample (tools/remarkable/rmsample.c: the sampling profiler's
trigger, tools/remarkable/README.md "Profiling on the tablet").
The steps to run it on the tablet are docs/host-remarkable.md's
"On the device".
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "imaging"))
import png  # noqa: E402


def icon(path, letter_rows):
    """A 64x64 gray icon: a rounded frame round a few rows of pixels."""
    size = 64
    rows = [[255] * size for _ in range(size)]
    for i in range(size):
        for t in range(3):
            rows[t][i] = rows[size - 1 - t][i] = rows[i][t] = rows[i][size - 1 - t] = 0
    top = (size - len(letter_rows) * 4) // 2
    for y, line in enumerate(letter_rows):
        left = (size - len(line) * 4) // 2
        for x, c in enumerate(line):
            if c != " ":
                for dy in range(4):
                    for dx in range(4):
                        rows[top + y * 4 + dy][left + x * 4 + dx] = 0
    png.write_gray(path, size, size, rows, 8)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", default=os.path.join("tmp", "rmpp-app"))
    parser.add_argument("--glibc", default="2.31")
    parser.add_argument("--zig", default="zig")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    exe = os.path.join(args.out, "rmprobe")
    command = [args.zig, "cc", "-target", "aarch64-linux-gnu." + args.glibc, "-O2", "-Wall",
               os.path.join(HERE, "rmprobe.c"), "-o", exe, "-lrt"]
    print(" ".join(command))
    subprocess.check_call(command)
    sampler = os.path.join(args.out, "rmsample")
    command = [args.zig, "cc", "-target", "aarch64-linux-gnu." + args.glibc, "-O2", "-Wall",
               os.path.join(HERE, "rmsample.c"), "-o", sampler]
    print(" ".join(command))
    subprocess.check_call(command)
    app = os.path.join(args.out, "rmprobe-app")
    os.makedirs(app, exist_ok=True)
    shutil.copy(exe, os.path.join(app, "rmprobe"))
    with open(os.path.join(app, "run.sh"), "w", newline="\n") as f:
        f.write("#!/bin/sh\n# AppLoad starts this with QTFB_KEY set; the output is for the owner to send back\n"
                "cd \"$(dirname \"$0\")\"\nexec ./rmprobe --qtfb 30 > /home/root/rmprobe-qtfb.log 2>&1\n")
    with open(os.path.join(app, "external.manifest.json"), "w", newline="\n") as f:
        json.dump({"name": "rmprobe", "application": "run.sh", "qtfb": True, "disablesWindowedMode": True}, f, indent=2)
        f.write("\n")
    icon(os.path.join(app, "icon.png"), ["X  X", " XX ", " XX ", "X  X"])
    print("built %s, %s and %s/" % (exe, sampler, app))


if __name__ == "__main__":
    main()
