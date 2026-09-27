#!/usr/bin/env python3
"""List and extract the packages built into the ROM extension.

Usage:
    python packages.py <build_dir> [--parts] [--extract DIR [--relocatable] [--rename OLD=NEW]...] [--doc FILE]
    python packages.py build/MP2100D --parts
    python packages.py build/MP2100D --extract build/packages
    python packages.py build/MP2100D --doc docs/packages/rex-packages.md

The ROM extension's `pkgl` entry (layout.json, "package list") holds the
packages the MessagePad ships with, one after another; each is a complete
Newton package as NTK writes them, which the package manager loads at boot
from a memory source.  This reads the package directories and lists them
(the name, type, version, size, flags, and with --parts each part's type,
flags, size and info), --extract writes each package to DIR as a .pkg
file (a test source for the package loader), and --doc writes the listing
as a markdown table with a header naming this script.

A package built into the ROM is not a package as one arrives from outside:
its frames parts' pointer refs are the objects' addresses in the ROM image,
where a package that is loaded into memory holds offsets from its own
start (FramePartHandler.cpp's ImportPart: a part in the ROM is imported at
its address, any other at its offset in the package).  --relocatable
rebases every pointer ref that points into the package so the extracted
file loads from anywhere (`newton --package`), and --rename OLD=NEW gives
the package named OLD a new name (appended to the directory data, the
parts moved along and their refs with them) so a copy of a built-in
package can be installed beside the one the ROM already has - the package
manager refuses a second package of the same name
(kError_Package_Already_Exists).

The directory format (Newton Formats, and TPrivatePackageIterator
0x001964fc): the 8-byte signature "package0" or "package1" (the newer
directory format; a relocation chunk follows the directory only when the
flags say so), the package type
(4 characters), the flags word (bit 31 kAutoRemoveFlag, 30 kCopyProtectFlag,
28 kNoCompressionFlag, 26 kRelocationFlag, 25 kUseFasterCompressionFlag),
the version, the copyright and the name as InfoRefs (a 16-bit offset into
the directory data and a 16-bit length; the name in UniChars with a
terminator), the package size, the creation date and two reserved words
(the modify date and the directory data's checksum in newer packages), the
directory size and the number of parts; then one 32-byte part entry per
part (the part's offset from the end of the directory, its size, the size
again, the type - 'form', 'book', 'auto', 'soup', 0 for raw code - a
reserved word, the flags: bits 0-1 the kind - 0 a protocol, 1 a NewtonScript
part, 2 raw data - and bit 4 kAutoLoadPartFlag, 5 kAutoRemovePartFlag, 6
kCompressedFlag, 7 kNotifyFlag, 8 kAutoCopyFlag; the info and the
compressor's name as InfoRefs), then the directory data the InfoRefs point
into.  All words are big-endian.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romid			# the ROM names itself in what these write

PART_KINDS = {0: "protocol", 1: "frames", 2: "raw"}


def fourcc(word: int) -> str:
    text = struct.pack(">I", word).decode("latin-1")
    return repr(text) if all(32 <= ord(c) < 127 for c in text) else "0x%08x" % word


def parse_package(data: bytes, base: int) -> dict:
    """The directory of the package at base in data."""
    d = base
    sig = data[d:d + 8]
    if sig[:7] != b"package" or sig[7:8] not in (b"0", b"1"):
        raise ValueError("no package signature at %#x" % base)
    (ptype, flags, version, copy_off, copy_len, name_off, name_len,
     size, creation, reserved2, reserved3, dir_size, num_parts) = struct.unpack(">4sIIHHHHIIIIII", data[d + 8:d + 52])
    parts = []
    for i in range(num_parts):
        e = d + 52 + i * 32
        (offset, psize, size2, part_type, reserved, pflags,
         info_off, info_len, comp_off, comp_len) = struct.unpack(">IIIIIIHHHH", data[e:e + 32])
        parts.append({
            "offset": offset, "size": psize, "size2": size2, "type": part_type, "flags": pflags,
            "info_off": info_off, "info_len": info_len, "compressor_off": comp_off, "compressor_len": comp_len,
        })
    data_start = d + 52 + num_parts * 32
    def info(off, length, unicode=False):
        raw = data[data_start + off:data_start + off + length]
        if unicode:
            return raw.decode("utf-16-be", "replace").rstrip("\0")
        return raw
    for p in parts:
        p["info"] = info(p["info_off"], p["info_len"])
        p["compressor"] = info(p["compressor_off"], p["compressor_len"]).decode("latin-1").rstrip("\0")
    return {
        "base": base, "signature": sig.decode("latin-1"), "type": ptype.decode("latin-1"), "flags": flags,
        "version": version, "copyright": info(copy_off, copy_len, True), "name": info(name_off, name_len, True),
        "size": size, "creation_date": creation, "reserved2": reserved2, "reserved3": reserved3,
        "directory_size": dir_size, "parts": parts,
    }


KOBJ_SLOTTED = 1			# objects.h: the flags in a header word's low byte
KOBJ_HEADER = 8				# the size-and-flags word and the GC word; then the class (or slot 0)


def rebase_frames_part(part: bytearray, lo: int, hi: int, delta: int, align: int) -> int:
    """Every pointer ref in the part's objects that points into [lo, hi)
    moved by delta.  ==> how many were moved."""
    moved = 0
    a = 0
    while a < len(part):
        header = struct.unpack_from(">I", part, a)[0]
        size = header >> 8
        if size < KOBJ_HEADER + 4 or a + size > len(part):
            raise ValueError("not a run of objects at part offset %#x" % a)
        refs = (size - KOBJ_HEADER) // 4 if header & KOBJ_SLOTTED else 1	# the slots, or a binary's class
        for j in range(refs):
            o = a + KOBJ_HEADER + 4 * j
            ref = struct.unpack_from(">I", part, o)[0]
            if ref & 3 == 1 and lo <= ref - 1 < hi:
                struct.pack_into(">I", part, o, (ref + delta) & 0xFFFFFFFF)
                moved += 1
        a += (size + align - 1) & ~(align - 1)
    return moved


def loadable_package(rom: bytes, pkg: dict, new_name: str | None = None) -> bytes:
    """The package as a file that loads from anywhere: its frames parts'
    refs made offsets from the package's start (and, given new_name, the
    package renamed)."""
    base, size = pkg["base"], pkg["size"]
    data = bytearray(rom[base:base + size])
    dir_size = pkg["directory_size"]
    align = 8 if pkg["signature"] == "package0" else 4
    directory = bytearray(data[:dir_size])
    grow = 0
    if new_name is not None:
        name = (new_name + "\0").encode("utf-16-be")
        data_start = 52 + len(pkg["parts"]) * 32
        struct.pack_into(">HH", directory, 24, dir_size - data_start, len(name))
        directory += name
        grow = (len(name) + 7) & ~7			# the parts kept on an eight-byte boundary
        directory += b"\0" * (grow - len(name))
        struct.pack_into(">I", directory, 44, dir_size + grow)			# the directory size
    body = bytearray(data[dir_size:])
    for p in pkg["parts"]:
        if p["flags"] & 3 != 1 or p["flags"] & 0x40:		# frames parts only, and not compressed ones
            continue
        start = p["offset"]
        part = bytearray(body[start:start + p["size"]])
        # a ref at the image address base + x becomes the offset x (+ grow when x is past the directory)
        rebase_frames_part(part, base + dir_size, base + size, -base + grow, align)
        body[start:start + p["size"]] = part
    out = directory + body
    struct.pack_into(">I", out, 28, len(out))		# the package size
    return bytes(out)


def rex_packages(build_dir: str):
    with open(os.path.join(build_dir, "layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    entry = next(e for e in layout["rex"]["entries"] if e["tag"] == "pkgl")
    start, end = entry["address"], entry["address"] + entry["size"]
    packages = []
    a = start
    while a + 52 <= end:
        if rom[a:a + 7] != b"package":
            a += 4
            continue
        pkg = parse_package(rom, a)
        packages.append(pkg)
        a += max(pkg["size"], 52)
        a = (a + 3) & ~3
    return rom, packages


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--parts", action="store_true", help="list each package's parts")
    ap.add_argument("--extract", metavar="DIR", help="write each package to DIR as <name>.pkg")
    ap.add_argument("--relocatable", action="store_true",
                    help="with --extract: the frames parts' refs made offsets from the package, so it loads from memory")
    ap.add_argument("--rename", metavar="OLD=NEW", action="append", default=[],
                    help="with --extract: the package named OLD written under the name NEW (implies --relocatable)")
    ap.add_argument("--doc", metavar="FILE", help="write the listing as markdown")
    args = ap.parse_args(argv)

    rom, packages = rex_packages(args.build_dir)
    lines = []
    for pkg in packages:
        lines.append("%#x  %-8s %-6s flags %#010x version %d size %7d dir %4d parts %d  %s"
                     % (pkg["base"], pkg["signature"], repr(pkg["type"]), pkg["flags"], pkg["version"],
                        pkg["size"], pkg["directory_size"], len(pkg["parts"]), pkg["name"]))
        if args.parts:
            for i, p in enumerate(pkg["parts"]):
                lines.append("    part %d: type %-8s kind %-8s flags %#06x offset %7d size %7d info %r compressor %r"
                             % (i, fourcc(p["type"]), PART_KINDS.get(p["flags"] & 3, "?"), p["flags"],
                                p["offset"], p["size"], p["info"], p["compressor"]))
    print("\n".join(lines))
    if args.extract:
        os.makedirs(args.extract, exist_ok=True)
        renames = dict(r.split("=", 1) for r in args.rename)
        for pkg in packages:
            new_name = renames.get(pkg["name"])
            pkg_name = new_name if new_name is not None else pkg["name"]
            name = "".join(c if c.isalnum() else "_" for c in pkg_name) or "package_%x" % pkg["base"]
            path = os.path.join(args.extract, name + ".pkg")
            if args.relocatable or new_name is not None:
                data = loadable_package(rom, pkg, new_name)
            else:
                data = rom[pkg["base"]:pkg["base"] + pkg["size"]]
            with open(path, "wb") as f:
                f.write(data)
            print("wrote", path)
    if args.doc:
        out = [
            "# The packages built into the ROM extension",
            "",
            "Generated by `tools/newton-rom/analysis/packages.py <build_dir> --doc docs/packages/rex-packages.md`",
            f"from the {romid.rom_version(args.build_dir)} ROM; do not edit.  The REx's `pkgl` entry,",
            "read as `TPrivatePackageIterator` reads a",
            "package directory (the format is described in the script).",
            "",
            "| ROM address | signature | type | flags | version | size | parts | name |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for pkg in packages:
            out.append("| %#x | %s | `%s` | %#010x | %d | %d | %d | %s |"
                       % (pkg["base"], pkg["signature"], pkg["type"], pkg["flags"], pkg["version"],
                          pkg["size"], len(pkg["parts"]), pkg["name"]))
        out += ["", "## Parts", "", "| package | part | type | kind | flags | offset | size | info | compressor |", "|---|---|---|---|---|---|---|---|---|"]
        for pkg in packages:
            for i, p in enumerate(pkg["parts"]):
                out.append("| %s | %d | %s | %s | %#06x | %d | %d | `%r` | `%s` |"
                           % (pkg["name"], i, fourcc(p["type"]), PART_KINDS.get(p["flags"] & 3, "?"), p["flags"],
                              p["offset"], p["size"], p["info"], p["compressor"]))
        with open(args.doc, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
        print("wrote", args.doc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
