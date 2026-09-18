#!/usr/bin/env python3
"""Which ROM a build directory holds, for the generated files to say.

The generated tables and documents name the ROM they came out of, and the
reconstruction can be pointed at a different image (see recite.py), so the
name has to come from the ROM rather than be written into the scripts.

The ROM says what it is in one place: the literal string VersionString
returns, built into the code as `D-2.1 (747129)` (the MP2100 D) or
`2.1 (717006)` (the MP2x00 US).  There is no symbol on it, so it is found
by its shape - a NUL-terminated ASCII string of an optional localisation
prefix, a dotted version and a parenthesised build number - which is
distinctive enough that only the one string in either image matches.
`gROMVersion`/`gROMStage` (ROM 0x13dc and 0x13e0) go with it as numbers.
"""

from __future__ import annotations

import os
import re
import struct

VERSION_STRING = re.compile(rb"(?:[A-Za-z]+-)?\d+\.\d+ \(\d+\)\x00")

kROMVersion = 0x13dc
kROMStage = 0x13e0


def rom_bytes(build_dir: str) -> bytes:
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        return f.read()


def rom_version(build_dir: str, rom: bytes = None) -> str:
    """The ROM's own version string, e.g. `D-2.1 (747129)`."""
    if rom is None:
        rom = rom_bytes(build_dir)
    found = set(m.group()[:-1].decode("ascii") for m in VERSION_STRING.finditer(rom))
    if len(found) != 1:
        raise ValueError("%s: %d version strings, expected one%s"
                         % (build_dir, len(found), (": " + ", ".join(sorted(found))) if found else ""))
    return found.pop()


def rom_numbers(build_dir: str, rom: bytes = None):
    """(gROMVersion, gROMStage) as the ROM's header holds them."""
    if rom is None:
        rom = rom_bytes(build_dir)
    return struct.unpack_from(">II", rom, kROMVersion)


if __name__ == "__main__":
    import sys

    for d in sys.argv[1:] or ["build/MP2100D"]:
        data = rom_bytes(d)
        version, stage = rom_numbers(d, data)
        print("%s: %s (gROMVersion %#x, gROMStage %#x)" % (d, rom_version(d, data), version, stage))
