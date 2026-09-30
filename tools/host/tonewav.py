#!/usr/bin/env python3
"""A WAV file of one sine tone - a sound for a host Newton to download.

Usage:
    python tools/host/tonewav.py OUT.wav [--hz 880] [--ms 200] [--rate 11025] [--bits 8]

Writes a mono PCM WAV of a sine at --hz, --ms milliseconds long, at --rate
samples a second, 8-bit (unsigned, as WAV has it) or 16-bit, at half full
scale.  src/host/demo/www/beep.wav is `tonewav.py beep.wav` with the
defaults - the file Newt's Cape's audio helper (audx10c2.pkg) takes into
the In Box in src/host/demo/apps-newtscape-helpers.ns.

Standard library only.
"""

import argparse
import math
import struct
import wave


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--hz", type=float, default=880.0)
    ap.add_argument("--ms", type=int, default=200)
    ap.add_argument("--rate", type=int, default=11025)
    ap.add_argument("--bits", type=int, choices=(8, 16), default=8)
    args = ap.parse_args(argv)
    frames = args.rate * args.ms // 1000
    out = bytearray()
    for n in range(frames):
        v = 0.5 * math.sin(2 * math.pi * args.hz * n / args.rate)
        if args.bits == 8:
            out.append(int(round(128 + 127 * v)))
        else:
            out += struct.pack("<h", int(round(32767 * v)))
    with wave.open(args.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(args.bits // 8)
        w.setframerate(args.rate)
        w.writeframes(bytes(out))
    print("%s: %d frames of %g Hz at %d Hz, %d-bit" % (args.out, frames, args.hz, args.rate, args.bits))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
