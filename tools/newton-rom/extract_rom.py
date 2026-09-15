#!/usr/bin/env python3
"""Extract a loadable ROM image and memory layout from a Newton debug ROM.

Usage:
    python extract_rom.py "<image file>" [--rex "<high file>"] -o <output dir>

Reads the AIF debug ROM image ("Senior ... image") and optionally the matching
ROM extension ("Senior ... high") and writes:

    <out>/rom.bin       the ROM exactly as the CPU sees it from address 0:
                        read-only area, read-write initialisers, then the REx
    <out>/layout.json   every memory region with its load address, size and
                        origin (file + offset), for the Ghidra import script

The layout of the physical 8 MB ROM is  RO | RW-init | REx ; the REx header's
`start` field is checked against ROM$$Size (= RO + RW sizes) to prove that.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from newtonrom.aif import AIFImage  # noqa: E402
from newtonrom.rex import RExBlock, TAG_DESCRIPTIONS  # noqa: E402


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="AIF debug ROM image ('Senior ... image')")
    ap.add_argument("--rex", help="ROM extension block to append ('Senior ... high')")
    ap.add_argument("-o", "--out", required=True, help="output directory")
    args = ap.parse_args(argv)

    img = AIFImage.from_file(args.image)
    h = img.header
    os.makedirs(args.out, exist_ok=True)

    regions = []
    rom = bytearray()

    regions.append({
        "name": "ROM_RO", "kind": "rom", "address": h.image_base, "size": h.ro_area_size,
        "rom_offset": len(rom), "source": {"file": os.path.basename(args.image), "offset": h.ro_offset},
        "read": True, "write": False, "execute": True,
        "comment": "read-only area: code and constant data",
    })
    rom += img.ro

    # The RW initialisers are stored in ROM right after the RO area and copied
    # to RAM (rw_base) by the boot code.  Both views are described.
    regions.append({
        "name": "ROM_RWINIT", "kind": "rom", "address": h.image_base + h.ro_area_size,
        "size": h.rw_area_size, "rom_offset": len(rom),
        "source": {"file": os.path.basename(args.image), "offset": h.rw_offset},
        "read": True, "write": False, "execute": False,
        "comment": "ROM copy of the initialised read-write data",
    })
    rom += img.rw
    regions.append({
        "name": "RAM_RW", "kind": "ram_init", "address": h.rw_base, "size": h.rw_area_size,
        "rom_offset": len(rom) - h.rw_area_size,
        "source": {"file": os.path.basename(args.image), "offset": h.rw_offset},
        "read": True, "write": True, "execute": False,
        "comment": "initialised read-write data (copied from ROM_RWINIT at boot)",
    })
    regions.append({
        "name": "RAM_ZI", "kind": "ram_zero", "address": h.zero_init_base, "size": h.zero_init_area_size,
        "read": True, "write": True, "execute": False,
        "comment": "zero-initialised data",
    })

    rex_info = None
    if args.rex:
        rex = RExBlock.from_file(args.rex)
        rom_size = h.ro_area_size + h.rw_area_size
        if rex.start != h.image_base + rom_size:
            print(f"warning: REx start {rex.start:#x} != end of base ROM {h.image_base + rom_size:#x}; "
                  f"padding rom.bin accordingly", file=sys.stderr)
            if rex.start < h.image_base + rom_size:
                print("error: REx overlaps the base ROM", file=sys.stderr)
                return 1
            rom += b"\xff" * (rex.start - (h.image_base + rom_size))
        regions.append({
            "name": "REX", "kind": "rom", "address": rex.start, "size": len(rex.data),
            "rom_offset": len(rom), "source": {"file": os.path.basename(args.rex), "offset": 0},
            "read": True, "write": False, "execute": True,
            "comment": "ROM extension block (RExBlock)",
        })
        rom += rex.data
        rex_info = {
            "start": rex.start, "length": rex.length, "id": rex.id,
            "manufacturer": rex.manufacturer, "version": rex.version,
            "entries": [{"tag": e.tag, "address": rex.start + e.offset, "size": e.length,
                         "description": TAG_DESCRIPTIONS.get(e.tag, "")} for e in rex.entries],
        }

    with open(os.path.join(args.out, "rom.bin"), "wb") as f:
        f.write(rom)
    layout = {
        "image": os.path.basename(args.image),
        "big_endian": img.big_endian,
        "entry_point": h.image_base + h.entry_point,
        "aif_header": {k: getattr(h, k) for k in (
            "ro_area_size", "rw_area_size", "debug_area_size", "zero_init_area_size",
            "image_base", "data_base", "addressing_type")},
        "rom_size": len(rom),
        "regions": regions,
        "rex": rex_info,
    }
    with open(os.path.join(args.out, "layout.json"), "w") as f:
        json.dump(layout, f, indent=2)

    print(f"wrote {os.path.join(args.out, 'rom.bin')} ({len(rom):#x} bytes)")
    for r in regions:
        print(f"  {r['name']:10s} {r['address']:#010x} +{r['size']:#08x}  {r['comment']}")
    if rex_info:
        for e in rex_info["entries"]:
            print(f"    REx {e['tag']!r} {e['address']:#010x} +{e['size']:#08x}  {e['description']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
