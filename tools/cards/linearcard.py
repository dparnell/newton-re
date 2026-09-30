#!/usr/bin/env python3
"""Read and make PC card images in Einstein's TLinearCard layout.

The host's memory cards (src/hal/host/HostCard.h, `newton --card file`) are
files in the layout the Einstein emulator uses for its linear (flash) cards,
so a card can move between the two.  The layout, read from Einstein's
source and written down in docs/stores/README.md ("Einstein's files"):

    the card's common memory ("data") | its CIS, natural byte order |
    an optional PNG icon | the card's name (UTF-8, terminated) |
    a 52-byte footer: ten big-endian words - name size, name start, icon
    size, icon start, CIS size, CIS start, data size, data start, type
    (CISTPL_DEVICE's high nibble), version (1) - then "TLinearCard\\0".

Usage:
    python tools/cards/linearcard.py info FILE
        the footer, the name and the CIS tuple by tuple (stopping where the
        Newton's parser stops: at CISTPL_END, 0xff)
    python tools/cards/linearcard.py make FILE --size MB --cis HEX [--name NAME]
        a blank (0xff) card of MB megabytes with the given CIS bytes

`make` is how a card with a CIS of one's choosing is tried on the
reconstruction - for instance one like the default card Einstein makes
(docs/stores/README.md), whose CIS ends after CISTPL_DEVICE.

No third-party code: the layout is re-expressed from its description.
"""

import argparse
import struct
import sys

FOOTER = 52
MAGIC = b"TLinearCard\0"

TUPLE_NAMES = {
    0x01: "CISTPL_DEVICE", 0x13: "CISTPL_LINKTARGET", 0x14: "CISTPL_NO_LINK",
    0x15: "CISTPL_VERS_1", 0x17: "CISTPL_DEVICE_A", 0x18: "CISTPL_JEDEC_C",
    0x19: "CISTPL_JEDEC_A", 0x1a: "CISTPL_CONFIG", 0x1b: "CISTPL_CFTABLE_ENTRY",
    0x1e: "CISTPL_DEVICE_GEO", 0x20: "CISTPL_MANFID", 0x21: "CISTPL_FUNCID",
    0x40: "CISTPL_VERS_2", 0xff: "CISTPL_END",
}


def read_image(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < FOOTER or data[-12:] != MAGIC:
        raise SystemExit(f"{path}: not a TLinearCard image")
    words = struct.unpack(">10I", data[-FOOTER:-12])
    keys = ("nameSize", "nameStart", "iconSize", "iconStart", "cisSize",
            "cisStart", "dataSize", "dataStart", "type", "version")
    return data, dict(zip(keys, words))


def info(path):
    data, footer = read_image(path)
    for key, value in footer.items():
        print(f"{key:10} 0x{value:x}")
    name = data[footer["nameStart"]:footer["nameStart"] + footer["nameSize"]]
    print("name      ", name.split(b"\0")[0].decode("utf-8", "replace"))
    cis = data[footer["cisStart"]:footer["cisStart"] + footer["cisSize"]]
    i = 0
    while i < len(cis):
        code = cis[i]
        if code == 0xff:
            print(f"  +{i:03x} {code:02x} CISTPL_END - the parser stops here")
            if i + 1 < len(cis) and any(b != 0xff for b in cis[i + 1:]):
                print(f"  ({len(cis) - i - 1} bytes after it are never read)")
            break
        if i + 1 >= len(cis):
            break
        size = cis[i + 1]
        body = cis[i + 2:i + 2 + size]
        print(f"  +{i:03x} {code:02x} {TUPLE_NAMES.get(code, '?'):18} {body.hex(' ')}")
        i += 2 + size


def make(path, size_mb, cis_hex, name):
    cis = bytes.fromhex(cis_hex.replace(" ", ""))
    common = b"\xff" * (size_mb * 1024 * 1024)
    name_bytes = name.encode("utf-8") + b"\0"
    card_type = (cis[2] >> 4) if len(cis) > 2 and cis[0] == 0x01 else 0
    cis_start = len(common)
    name_start = cis_start + len(cis)
    footer = struct.pack(">10I", len(name_bytes), name_start, 0, 0, len(cis), cis_start,
                         len(common), 0, card_type, 1) + MAGIC
    with open(path, "wb") as f:
        f.write(common + cis + name_bytes + footer)
    print(f"{path}: {size_mb} MB, a CIS of {len(cis)} bytes")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("info")
    p.add_argument("file")
    p = sub.add_parser("make")
    p.add_argument("file")
    p.add_argument("--size", type=int, default=4)
    p.add_argument("--cis", required=True)
    p.add_argument("--name", default="Card")
    args = parser.parse_args(argv)
    if args.command == "info":
        info(args.file)
    else:
        make(args.file, args.size, args.cis, args.name)


if __name__ == "__main__":
    main(sys.argv[1:])
