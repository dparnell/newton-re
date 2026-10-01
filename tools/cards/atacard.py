#!/usr/bin/env python3
"""Make and describe ATA (PC Card / CompactFlash fixed disk) card images for
the reconstruction's host card socket.

Purpose
    The MessagePad 2x00's ROM takes an ATA card on by reading the card's
    own partition map (pcmcia/CardATALoader.h, ROM 0x0004a2d0): the
    packages the card carries for itself - its driver in an
    Apple_Newton_Driver partition, and one in the Apple_Newton partition
    the card's store lives in - are loaded when it goes in.  This makes a
    card that can be tried on that path, and says what is on one.

    The image is Einstein's TLinearCard container, as the memory cards are
    (tools/cards/linearcard.py; hal/host/HostCard.h): the data section is
    the disk, 512-byte sectors one after another, and the footer's type is
    0xD (a function-specific card), which tells the host to stand its model
    of an ATA drive (hal/host/HostATA.cpp) behind the card's register
    window instead of mapping the data as common memory.

    The CIS says what a CompactFlash card in memory mode says: CISTPL_DEVICE
    (function specific), CISTPL_VERS_1, CISTPL_FUNCID fixed disk,
    CISTPL_FUNCE disk interface ATA, CISTPL_CONFIG (registers at attribute
    0x200) and one CISTPL_CFTABLE_ENTRY: the memory-mapped interface, Vcc,
    a 2 KB window at card address 0 - the task file at 0-0xF and the data
    register again at 0x400.

    The disk is laid out as the ROM reads it:
      block 0       the driver descriptor block ('ER', 512-byte blocks);
                    with --mbr, a PC master boot record instead, its first
                    partition of type 0x83 (bootable) holding the rest,
                    whose block numbers are then the disk's own (the ROM
                    reads a partition's package from pmPyPartStart as a
                    block on the disk)
      block 1...    Apple partition map entries ('PM'): the map itself,
                    Apple_Newton_Driver (with --driver), Apple_Newton
      then          each partition's blocks, a package at the start of a
                    Newton partition when one is given
    A Newton entry's pmPad is 'newt', the word after it has bit 8 set
    (0x100), and the word at entry offset 0x94 is the number of blocks of
    package at the partition's start (0 for none).  The Apple_Newton
    partition has no boot code (pmBootSize 0): the ROM would jump into
    ARM610 code there, which the host cannot run.

Usage
    python tools/cards/atacard.py make FILE --size MB [--driver PKG] [--package PKG]
                                           [--newton-blocks N] [--name NAME] [--mbr]
    python tools/cards/atacard.py info FILE

Inputs / outputs
    make writes FILE: a disk of MB megabytes (1-512) in the container, the
    Apple_Newton partition taking what the map and the driver leave (or N
    blocks).  info prints the footer, the CIS tuples and the partition map:
    each entry's name, type, place and size, and whether it is the
    Newton's and how many blocks of package it carries.

    Standard library only.  Written from the formats (Inside Macintosh:
    Devices, "The partition map"; PC Card Standard; CF+ specification), not
    copied from anything.
"""

import argparse
import os
import struct
import sys

FOOTER = 52
MAGIC = b"TLinearCard\0"
TYPE_ATA = 0xD
BLOCK = 512
MAP_ENTRIES = 3          # the map, the driver, the Newton partition


def cis_bytes(name):
    vendor = b"Newton host\0"
    product = name.encode("latin-1") + b"\0"
    vers1 = bytes([4, 1]) + vendor + product + b"\xff"
    tuples = [
        bytes([0x01, 3, 0xD9, 0x01, 0xFF]),            # CISTPL_DEVICE: function specific, 250 ns, 2 KB
        bytes([0x15, len(vers1)]) + vers1,              # CISTPL_VERS_1
        bytes([0x21, 2, 0x04, 0x01]),                   # CISTPL_FUNCID: fixed disk, POST
        bytes([0x22, 2, 0x01, 0x01]),                   # CISTPL_FUNCE: disk interface ATA
        bytes([0x1A, 5, 0x01, 0x00, 0x00, 0x02, 0x0F]), # CISTPL_CONFIG: 2-byte address 0x200, last index 0, mask 0x0F
        bytes([0x1B, 7, 0xC0, 0x00, 0x21, 0x01, 0x55, 0x08, 0x00]),  # CISTPL_CFTABLE_ENTRY: index 0 default, memory, Vcc 5 V, 2 KB at 0
        bytes([0x14, 0]),                               # CISTPL_NO_LINK
        bytes([0xFF]),                                  # CISTPL_END
    ]
    return b"".join(tuples)


def map_entry(map_count, start, count, name, part_type, package_blocks=0, newton=False):
    e = bytearray(BLOCK)
    struct.pack_into(">2sHIII", e, 0, b"PM", 0, map_count, start, count)
    e[0x10:0x10 + len(name)] = name.encode("latin-1")
    e[0x30:0x30 + len(part_type)] = part_type.encode("latin-1")
    struct.pack_into(">III", e, 0x50, 0, count, 0x37)       # data area, status: valid, allocated, in use, readable, writable
    if newton:
        e[0x88:0x8C] = b"newt"
        struct.pack_into(">I", e, 0x8C, 0x100)
        struct.pack_into(">I", e, 0x94, package_blocks)
    return bytes(e)


def blocks_of(data):
    return (len(data) + BLOCK - 1) // BLOCK


def make(path, size_mb, driver=None, package=None, newton_blocks=None, name="ATA card", mbr=False):
    total = size_mb * 1024 * 1024 // BLOCK
    disk = bytearray(total * BLOCK)
    driver_data = open(driver, "rb").read() if driver else b""
    package_data = open(package, "rb").read() if package else b""
    base = 1 if mbr else 0           # where the map's own block 0 lies
    if mbr:
        rest = total - 1
        disk[0x1BE:0x1BE + 16] = struct.pack("<B3sB3sII", 0x80, b"\0\0\0", 0x83, b"\0\0\0", 1, rest)
        disk[0x1FE:0x200] = b"\x55\xaa"
    # the driver descriptor block
    struct.pack_into(">2sHI", disk, base * BLOCK, b"ER", BLOCK, total)
    first = base + 1 + MAP_ENTRIES
    driver_blocks = max(blocks_of(driver_data), 1) if driver else 0
    driver_start = first
    newton_start = driver_start + driver_blocks
    newton_count = newton_blocks if newton_blocks else total - newton_start
    if newton_start + newton_count > total or newton_count < blocks_of(package_data) + 1:
        raise SystemExit("atacard: the disk is too small for what goes on it")
    entries = [map_entry(MAP_ENTRIES, base + 1, MAP_ENTRIES, "Apple", "Apple_partition_map")]
    if driver:
        entries.append(map_entry(MAP_ENTRIES, driver_start, driver_blocks, "Newton driver", "Apple_Newton_Driver",
                                 blocks_of(driver_data), True))
    else:
        entries.append(map_entry(MAP_ENTRIES, 0, 0, "Extra", "Apple_Free"))
    entries.append(map_entry(MAP_ENTRIES, newton_start, newton_count, name, "Apple_Newton",
                             blocks_of(package_data), True))
    for i, e in enumerate(entries):
        disk[(base + 1 + i) * BLOCK:(base + 2 + i) * BLOCK] = e
    disk[driver_start * BLOCK:driver_start * BLOCK + len(driver_data)] = driver_data
    disk[newton_start * BLOCK:newton_start * BLOCK + len(package_data)] = package_data

    cis = cis_bytes(name)
    name_bytes = name.encode("utf-8") + b"\0"
    cis_start = len(disk)
    name_start = cis_start + len(cis)
    footer = struct.pack(">10I", len(name_bytes), name_start, 0, 0, len(cis), cis_start,
                         len(disk), 0, TYPE_ATA, 1) + MAGIC
    with open(path, "wb") as f:
        f.write(disk + cis + name_bytes + footer)
    print("%s: an ATA card of %d MB (%d blocks); the map at block %d, Apple_Newton at %d (%d blocks)%s%s"
          % (path, size_mb, total, base, newton_start, newton_count,
             ", a %d-block driver at %d" % (driver_blocks, driver_start) if driver else "",
             ", a %d-block package in it" % blocks_of(package_data) if package else ""))


def info(path):
    data = open(path, "rb").read()
    if len(data) < FOOTER or data[-12:] != MAGIC:
        raise SystemExit("%s: not a TLinearCard image" % path)
    words = struct.unpack(">10I", data[-FOOTER:-12])
    keys = ("nameSize", "nameStart", "iconSize", "iconStart", "cisSize", "cisStart",
            "dataSize", "dataStart", "type", "version")
    footer = dict(zip(keys, words))
    for k in keys:
        print("%-10s 0x%x" % (k, footer[k]))
    if footer["type"] != TYPE_ATA:
        print("not an ATA card (type 0x%x)" % footer["type"])
        return
    cis = data[footer["cisStart"]:footer["cisStart"] + footer["cisSize"]]
    i = 0
    while i < len(cis) and cis[i] != 0xFF:
        code, size = cis[i], cis[i + 1]
        print("  CIS +%03x %02x %s" % (i, code, cis[i + 2:i + 2 + size].hex(" ")))
        i += 2 + size
    disk = data[footer["dataStart"]:footer["dataStart"] + footer["dataSize"]]
    block = lambda n: disk[n * BLOCK:(n + 1) * BLOCK]
    base = 0
    b0 = block(0)
    if b0[0x1FE:0x200] == b"\x55\xaa":
        for k in range(4):
            e = b0[0x1BE + 16 * k:0x1BE + 16 * k + 16]
            if e[4] == 0x83:
                base = struct.unpack("<I", e[8:12])[0]
                print("MBR: partition %d of type 0x83 at block %d" % (k, base))
                break
    if block(base)[:2] != b"ER":
        print("no driver descriptor block at %d" % base)
        return
    print("driver descriptor block at %d: %d blocks of %d" % (base, struct.unpack(">I", block(base)[4:8])[0],
                                                              struct.unpack(">H", block(base)[2:4])[0]))
    n, count = base + 1, None
    while count is None or n <= base + count:
        e = block(n)
        if e[:2] != b"PM":
            break
        count = count or struct.unpack(">I", e[4:8])[0]
        start, blocks = struct.unpack(">II", e[8:16])
        pname = e[0x10:0x30].split(b"\0")[0].decode("latin-1")
        ptype = e[0x30:0x50].split(b"\0")[0].decode("latin-1")
        newton = e[0x88:0x8C] == b"newt" and struct.unpack(">I", e[0x8C:0x90])[0] & 0x100
        line = "  entry %d: %-20s %-22s at %d, %d blocks" % (n, pname, ptype, start, blocks)
        if newton:
            pkg = struct.unpack(">I", e[0x94:0x98])[0]
            line += ", the Newton's"
            if pkg:
                sig = disk[start * BLOCK:start * BLOCK + 8]
                line += ", %d blocks of package (%s)" % (pkg, sig.decode("latin-1", "replace"))
        print(line)
        n += 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    m = sub.add_parser("make")
    m.add_argument("file")
    m.add_argument("--size", type=int, required=True, help="the disk in MB (1-512)")
    m.add_argument("--driver", help="a package for the Apple_Newton_Driver partition")
    m.add_argument("--package", help="a package at the start of the Apple_Newton partition")
    m.add_argument("--newton-blocks", type=int, help="the Apple_Newton partition's size in blocks")
    m.add_argument("--name", default="ATA card")
    m.add_argument("--mbr", action="store_true", help="put the map inside a PC partition of type 0x83")
    i = sub.add_parser("info")
    i.add_argument("file")
    args = ap.parse_args(argv)
    if args.command == "make":
        if not 1 <= args.size <= 512:
            raise SystemExit("atacard: --size is 1 to 512")
        make(args.file, args.size, args.driver, args.package, args.newton_blocks, args.name, args.mbr)
    else:
        info(args.file)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
