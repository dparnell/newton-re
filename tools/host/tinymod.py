#!/usr/bin/env python3
"""A ProTracker module (.mod) of one short tune - a MOD file for a host
Newton to download.

Usage:
    python tools/host/tinymod.py OUT.mod [--title TITLE]

Writes a four-channel "M.K." module: the 20-byte title, 31 sample
headers (the first a 256-byte square wave, looped; the others empty), a
song of one position playing pattern 0, the pattern (64 rows of four
channels: a rising arpeggio on channel 1, a note every fourth row) and
the sample data.  src/host/demo/www/tune.mod is `tinymod.py tune.mod
--title "Host Tune"` - the file Newt's Cape's MOD helper (mdsv10a2.pkg)
saves as a package in src/host/demo/apps-newtscape-helpers.ns.

Standard library only.
"""

import argparse
import struct

# ProTracker periods of the notes C-2 .. B-2
PERIODS = [428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226]


def make(title: str) -> bytes:
    out = bytearray(title.encode("latin-1")[:20].ljust(20, b"\0"))
    sample_length = 256
    for n in range(31):
        if n == 0:
            out += b"square".ljust(22, b"\0")
            out += struct.pack(">HBBHH", sample_length // 2, 0, 48, 0, sample_length // 2)
        else:
            out += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([1, 127])                  # one position; the restart byte
    out += bytes([0] * 128)                 # the order: pattern 0
    out += b"M.K."
    for row in range(64):
        for channel in range(4):
            if channel == 0 and row % 4 == 0:
                period = PERIODS[(row // 4) % len(PERIODS)]
                out += struct.pack(">BBBB", (1 & 0xf0) | (period >> 8), period & 0xff, (1 & 0x0f) << 4, 0)
            else:
                out += bytes(4)
    out += bytes([0x40] * (sample_length // 2) + [0xc0] * (sample_length // 2))     # the square wave
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--title", default="Host Tune")
    args = ap.parse_args(argv)
    data = make(args.title)
    with open(args.out, "wb") as f:
        f.write(data)
    print("%s: \"%s\", %d bytes" % (args.out, args.title, len(data)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
