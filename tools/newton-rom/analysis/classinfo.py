#!/usr/bin/env python3
"""Decode the ROM's protocol class-info tables (TClassInfo).

Usage:
    python classinfo.py <build_dir> <address>            one table, at the address
    python classinfo.py <build_dir> --name TFooImpl      the table of an implementation
    python classinfo.py <build_dir> --all [-o file.md]   every implementation in the ROM

A Newton "protocol" implementation (ProtocolGen output) describes itself
with a TClassInfo (headers/OS600/Protocols.h): a relocatable table of
self-relative offsets to its names and dispatch table and of ARM `B`
instructions to its Sizeof/alloc/free/New/Delete code, followed by the
dispatch table (one `B` per method: slot 0 unused, 1 ClassInfo, 2 New,
3 Delete, then the protocol's methods in declaration order) and the
monitor entry procedure (generated for every implementation, used when
the instance is started as a monitor) with its selector table (pairs of
`B`s: an argument-unpacking stub and the method; selector n is dispatch
slot n + 2).  Every implementation's
static `ClassInfo()` is `sub r0,pc,#imm; mov pc,lr`, which is how --name
and --all find the tables from the `ClassInfo__<n><name>SFv` symbols.

Reads rom.bin and symbols.json from the build directory; no Ghidra needed.
--all -o writes the table docs/protocols/classinfos.md (regenerate it after
a re-import; docs/protocols/README.md explains the columns).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys


def rotate_imm(word: int) -> int:
    imm = word & 0xFF
    rot = ((word >> 8) & 0xF) * 2
    return ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF


def branch_target(at: int, word: int):
    """Target of an unconditional `B` at `at`, or None."""
    if (word >> 24) != 0xEA:
        return None
    off = word & 0xFFFFFF
    if off & 0x800000:
        off -= 0x1000000
    return at + 8 + off * 4


class Rom:
    def __init__(self, build_dir: str):
        with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
            self.rom = f.read()
        with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
            data = json.load(f)
        items = data["symbols"]
        # the patchable jump table (virtual slot address -> body): branches
        # into it are resolved to the body they reach
        self.slots = {virtual: target for virtual, target in data["jumptable"]["entries"]}
        self.by_addr = {}
        self.classinfo_fns = {}  # implementation name -> ClassInfo() address
        for s in items:
            if "jt_index" in s:
                continue
            self.by_addr.setdefault(s["address"], s["name"])
            m = re.match(r"ClassInfo__\d+(\w+?)SFv$", s["name"])
            if m:
                self.classinfo_fns[m.group(1)] = s["address"]

    def word(self, at: int) -> int:
        return struct.unpack(">I", self.rom[at:at + 4])[0]

    def cstring(self, at: int) -> str:
        end = self.rom.index(b"\0", at)
        return self.rom[at:end].decode("latin-1")

    def name(self, addr) -> str:
        if addr is None:
            return "(not a branch)"
        return self.by_addr.get(addr, f"0x{addr:08x}")

    def resolve(self, at: int, word: int):
        """Target of a `B` at `at`, followed through a jump-table slot."""
        t = branch_target(at, word)
        return self.slots.get(t, t)


FIELDS = ["fReserved1", "fNameDelta", "fInterfaceDelta", "fSignatureDelta", "fBTableDelta",
          "fEntryProcDelta", "fSizeofBranch", "fAllocBranch", "fFreeBranch", "fDefaultNewBranch",
          "fDefaultDeleteBranch", "fVersion", "fFlags", "fSelectorBranch", "fReserved2"]
DELTAS = {"fNameDelta", "fInterfaceDelta", "fSignatureDelta", "fBTableDelta", "fEntryProcDelta"}
BRANCHES = {"fSizeofBranch", "fAllocBranch", "fFreeBranch", "fDefaultNewBranch", "fDefaultDeleteBranch", "fSelectorBranch"}


def decode(rom: Rom, at: int) -> dict:
    info = {"address": at}
    for i, f in enumerate(FIELDS):
        w = rom.word(at + 4 * i)
        if f in DELTAS:
            delta = w - 0x100000000 if w & 0x80000000 else w
            info[f] = (at + 4 * i + delta) if w else 0
        elif f in BRANCHES:
            info[f] = rom.resolve(at + 4 * i, w) if w else 0
        else:
            info[f] = w
    info["implementation"] = rom.cstring(info["fNameDelta"])
    info["interface"] = rom.cstring(info["fInterfaceDelta"])
    # the signature is a capability list: name, value string pairs ended by
    # an empty name (TClassInfo::GetCapability)
    capabilities = []
    at_sig = info["fSignatureDelta"]
    while True:
        name = rom.cstring(at_sig)
        if not name:
            break
        value = rom.cstring(at_sig + len(name) + 1)
        capabilities.append((name, value))
        at_sig += len(name) + 1 + len(value) + 1
    info["capabilities"] = capabilities
    def show(text):
        return "".join(c if " " <= c <= "~" else "<%02x>" % ord(c) for c in text)
    info["signature"] = "; ".join(f"{show(n)}={show(v)}" if v else show(n) for n, v in capabilities)
    # instance size: Sizeof() is `mov r0,#imm; mov pc,lr`
    info["size"] = None
    if info["fSizeofBranch"]:
        w = rom.word(info["fSizeofBranch"])
        if (w & 0xFFFFF000) == 0xE3A00000:
            info["size"] = rotate_imm(w)
    # the dispatch table: B instructions until something that is not one
    btable = []
    bt = info["fBTableDelta"]
    if bt:
        i = 0
        while i < 128:
            w = rom.word(bt + 4 * i)
            if i == 0 and w == 0:
                btable.append(None)
            else:
                t = rom.resolve(bt + 4 * i, w)
                if t is None:
                    break
                btable.append(t)
            i += 1
    info["btable"] = btable
    # a monitor's entry: `tst r1,r1; bmi; cmp r1,#N; bge; stmdb; ldr; add r12,pc,#imm; ...`
    selectors = []
    ep = info["fEntryProcDelta"]
    if ep:
        cmp = rom.word(ep + 8)
        add = rom.word(ep + 0x18)
        if (cmp & 0xFFFFF000) == 0xE3510000 and (add & 0xFFFFF000) == 0xE28FC000:
            # the check is against the dispatch slot count, which is 4 more
            # than the number of selector pairs in the table
            count = rotate_imm(cmp) - 4
            table = ep + 0x18 + 8 + rotate_imm(add)
            for n in range(count):
                stub = rom.resolve(table + 8 * n, rom.word(table + 8 * n))
                method = rom.resolve(table + 8 * n + 4, rom.word(table + 8 * n + 4))
                selectors.append((stub, method))
    info["selectors"] = selectors
    return info


def table_address(rom: Rom, classinfo_fn: int):
    """The table a `sub r0,pc,#imm; mov pc,lr` ClassInfo() returns."""
    w = rom.word(classinfo_fn)
    if (w & 0xFFFFF000) != 0xE24F0000:
        return None
    return classinfo_fn + 8 - rotate_imm(w)


def print_one(rom: Rom, info: dict) -> None:
    print(f"TClassInfo at 0x{info['address']:08x}: {info['implementation']} implements {info['interface']}")
    print(f"  signature \"{info['signature']}\"  version {info['fVersion']}  flags 0x{info['fFlags']:x}"
          f"  size {info['size'] if info['size'] is not None else '?'}")
    for f in BRANCHES:
        if info[f]:
            print(f"  {f:22s} 0x{info[f]:08x}  {rom.name(info[f])}")
    for f in ("fReserved1", "fReserved2"):
        if info[f]:
            print(f"  {f:22s} 0x{info[f]:08x}")
    print(f"  dispatch table at 0x{info['fBTableDelta']:08x}:")
    for i, t in enumerate(info["btable"]):
        print(f"    slot {i:2d} +0x{4 * i:02x}  {'-' if t is None else f'0x{t:08x}  ' + rom.name(t)}")
    if info["fEntryProcDelta"]:
        print(f"  monitor entry at 0x{info['fEntryProcDelta']:08x}, selectors:")
        for n, (stub, method) in enumerate(info["selectors"]):
            print(f"    {n:2d}  {rom.name(method)}")


def markdown(rom: Rom, infos: list) -> str:
    out = ["<!-- generated by tools/newton-rom/analysis/classinfo.py --all; do not edit -->", "",
           "| Implementation | Interface | Size | Version | Flags | Methods | Capabilities | Table |",
           "|---|---|---|---|---|---|---|---|"]
    for info in sorted(infos, key=lambda i: (i["interface"], i["implementation"])):
        sig = info["signature"].replace("|", "\\|")
        out.append(f"| `{info['implementation']}` | `{info['interface']}` | "
                   f"{info['size'] if info['size'] is not None else '?'} | {info['fVersion']} | "
                   f"0x{info['fFlags']:x} | {max(len(info['btable']) - 4, 0)} | "
                   f"{('`' + sig + '`') if sig else ''} | 0x{info['address']:08x} |")
    return "\n".join(out) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("address", nargs="?", type=lambda s: int(s, 0))
    ap.add_argument("--name", help="implementation class name (its ClassInfo__...SFv symbol is used)")
    ap.add_argument("--all", action="store_true", help="summarise every class info in the ROM")
    ap.add_argument("-o", "--output", help="with --all: write the summary as markdown")
    args = ap.parse_args(argv)
    rom = Rom(args.build_dir)
    if args.all:
        infos = []
        for name, fn in sorted(rom.classinfo_fns.items()):
            at = table_address(rom, fn)
            if at is None:
                print(f"warning: {name}::ClassInfo at 0x{fn:08x} is not of the expected form", file=sys.stderr)
                continue
            infos.append(decode(rom, at))
        if args.output:
            with open(args.output, "w", encoding="utf-8", newline="\n") as f:
                f.write(markdown(rom, infos))
            print(f"{len(infos)} class infos -> {args.output}")
        else:
            for info in sorted(infos, key=lambda i: (i["interface"], i["implementation"])):
                print(f"0x{info['address']:08x}  {info['implementation']:34s} {info['interface']:28s} "
                      f"size {info['size'] if info['size'] is not None else '?':>4}  v{info['fVersion']}"
                      f"  {max(len(info['btable']) - 4, 0)} methods")
        return 0
    if args.name:
        fn = rom.classinfo_fns.get(args.name)
        if fn is None:
            print(f"no ClassInfo__...{args.name}SFv symbol", file=sys.stderr)
            return 1
        at = table_address(rom, fn)
        if at is None:
            print(f"{args.name}::ClassInfo at 0x{fn:08x} is not of the expected form", file=sys.stderr)
            return 1
    elif args.address is not None:
        at = args.address
    else:
        ap.error("give an address, --name or --all")
    print_one(rom, decode(rom, at))
    return 0


if __name__ == "__main__":
    sys.exit(main())
