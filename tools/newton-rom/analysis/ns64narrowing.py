#!/usr/bin/env python3
"""
ns64narrowing.py - where a NewtonScript integer is silently narrowed in the
64-bit flavour (docs/frames/64bit.md).

Under NEWTON_NS64 an integer is 62 bits, and RINT/RVALUE/CoerceToInt answer
a Long.  On Windows (LLP64) a `long` is 32 bits, so `long x = RINT(r)`
quietly keeps the low half - harmless for a coordinate or an index, wrong
for a time, an id or a hash.  Nothing warns by default; clang's
-Wshorten-64-to-32 does, along with every other 64-to-32 conversion.  This
reads the log of such a build and keeps the warnings whose source line
takes an integer out of a Ref, grouped by file and by area.

Inputs: the output of building a NEWTON_NS64 tree with -Wshorten-64-to-32
with the Windows (zig) toolchain:

    cmake -G Ninja -S src -B build/host-ns64a -DCMAKE_TOOLCHAIN_FILE=src/cmake/zig-toolchain.cmake \
          -DNEWTON_NS64=ON "-DCMAKE_CXX_FLAGS=-Wshorten-64-to-32"
    cmake --build build/host-ns64a -- -k 0 > build-a.log 2>&1

Usage:

    python tools/newton-rom/analysis/ns64narrowing.py build-a.log [--sites] [--all]

Output: counts per src/ area and per file; --sites lists each site with its
source line; --all keeps every 64-to-32 warning, not only the Ref ones.
"""

import argparse
import collections
import re
import sys

WARNING = re.compile(r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+): warning: (?P<text>.*)\[-Wshorten-64-to-32\]")
REFISH = re.compile(r"\b(RINT|RVALUE|CoerceToInt|NarrowInteger)\s*\(")


def area_of(path):
    p = path.replace("\\", "/")
    i = p.find("/src/")
    if i < 0:
        return "?", p
    rel = p[i + 5:]
    return rel.split("/")[0], rel


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("log")
    ap.add_argument("--sites", action="store_true", help="list every site")
    ap.add_argument("--all", action="store_true", help="every 64-to-32 warning, not only the Ref ones")
    args = ap.parse_args()

    lines = open(args.log, encoding="utf-8", errors="replace").read().splitlines()
    sites = {}
    for i, l in enumerate(lines):
        m = WARNING.match(l)
        if not m:
            continue
        source = lines[i + 1] if i + 1 < len(lines) else ""
        if not args.all and not REFISH.search(source):
            continue
        area, rel = area_of(m.group("path"))
        key = (rel, int(m.group("line")))
        if key not in sites:
            sites[key] = (area, source.strip(), m.group("text").strip())

    by_area = collections.Counter(a for a, _, _ in sites.values())
    by_file = collections.Counter(k[0] for k in sites)
    tests = sum(1 for k in sites if "/tests/" in k[0])
    print("%d sites (%d in tests)" % (len(sites), tests))
    for area, n in by_area.most_common():
        print("  %-14s %d" % (area, n))
    print()
    for f, n in by_file.most_common():
        print("  %4d  %s" % (n, f))
    if args.sites:
        print()
        for (f, ln), (_, src, text) in sorted(sites.items()):
            print("%s:%d: %s" % (f, ln, src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
