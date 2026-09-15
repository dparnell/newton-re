#!/usr/bin/env python3
"""Print the initial values of kernel globals from the ROM's read-write init area.

Usage:
    python globals.py <build_dir> <name-or-address>...

The initialised data (RAM 0x0C100800-) is copied out of the ROM at boot; this
reads each named symbol's (or address's) 32-bit word from that copy, so the
reconstructed source can initialise the global the way the ROM does.  Symbols
in the zero-init area report 0; anything outside both reports "not in RW".
"""

from __future__ import annotations

import json
import os
import struct
import sys


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 2:
        print(__doc__)
        return 2
    build_dir, names = argv[0], argv[1:]
    with open(os.path.join(build_dir, "layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    items = data["symbols"] if isinstance(data, dict) and "symbols" in data else data
    by_name = {s["name"]: s["address"] for s in items}
    rw = next(r for r in layout["regions"] if r["name"] == "ROM_RWINIT")
    hdr = layout["aif_header"]
    data_base, rw_size, zi_size = hdr["data_base"], hdr["rw_area_size"], hdr["zero_init_area_size"]
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        f.seek(rw["rom_offset"])
        rwinit = f.read(rw["size"])

    for name in names:
        addr = int(name, 0) if name[:2].lower() == "0x" or name.isdigit() else by_name.get(name)
        if addr is None:
            print(f"{name}: unknown symbol")
            continue
        off = addr - data_base
        if 0 <= off < rw_size:
            value = struct.unpack(">I", rwinit[off:off + 4])[0]
            print(f"{name} @ 0x{addr:08x} = 0x{value:08x} ({value})")
        elif rw_size <= off < rw_size + zi_size:
            print(f"{name} @ 0x{addr:08x} = 0 (zero-init area)")
        else:
            print(f"{name} @ 0x{addr:08x}: not in RW")
    return 0


if __name__ == "__main__":
    sys.exit(main())
