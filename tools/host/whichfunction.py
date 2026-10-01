#!/usr/bin/env python3
"""Name the function an address in a host build belongs to.

Usage:
    python tools/host/whichfunction.py <exe> <offset> [--objs DIR]
    python tools/host/whichfunction.py build/host/host/newton.exe 0xb9a1

When the host falls over it prints where, as an address and as an offset
into its own image:

    [host] the machine fell over: an exception (0xc0000005) at 00007FF7F34CB9A1 (image + 0xb9a1)

The address changes from run to run (Windows loads the image wherever it
likes), but the offset does not.  This turns that offset into a function
name, which is the thing worth knowing.

There is no symbol table in the linked executable and the PDB is not
something to parse here, so the name comes from the object files instead:

  * `.pdata` (the table Windows unwinds through) gives the bounds of the
    function the offset falls in;
  * the object files under the build directory still have their COFF
    symbol tables, so every function in them has a name and a place;
  * the bytes of a function in the executable are the bytes in its object
    file, except where the linker patched a relocation - and each object
    says where its own relocations are.

So: take the function's bytes out of the executable, and look for an
object-file symbol whose bytes match everywhere the object has no
relocation.  A whole function matching to the byte is not a coincidence,
and the search answers one name.

It needs nothing but the Python standard library, and works on any of the
host executables (`newton.exe`, `newtonscript.exe`, a test).

On a Linux host the executable is ELF and keeps its own symbol table, so
the name is simply looked up there (`.symtab`, the function whose
[value, value + size) holds the offset - a position-independent
executable's offsets are its virtual addresses), and demangled through
`c++filt` when there is one.  The crash line is the same:

    [host] the machine fell over: a signal (0xb) at 0x55d3c4a01234 (image + 0x1a1234)
    python3 tools/host/whichfunction.py build/host/host/newton 0x1a1234
"""

import argparse
import glob
import os
import struct
import sys


def pe_sections(data):
    """(name, rva, vsize, rawptr, rawsize) of each section of a PE image."""
    e_lfanew, = struct.unpack_from("<I", data, 0x3C)
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    coff = e_lfanew + 4
    nsec, = struct.unpack_from("<H", data, coff + 2)
    optsize, = struct.unpack_from("<H", data, coff + 16)
    base = coff + 20 + optsize
    out = []
    for i in range(nsec):
        off = base + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("latin-1")
        vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        out.append((name, rva, vsize, rawptr, rawsize))
    return out


def rva_to_offset(sections, rva):
    for name, va, vsize, rawptr, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            return rawptr + (rva - va)
    return None


def function_bounds(data, sections, rva):
    """The [begin, end) of the .pdata entry the rva falls in."""
    pdata = next((s for s in sections if s[0] == ".pdata"), None)
    if pdata is None:
        raise ValueError("the image has no .pdata to unwind through")
    off, size = pdata[3], pdata[4]
    for i in range(size // 12):
        begin, end, unwind = struct.unpack_from("<III", data, off + i * 12)
        if begin <= rva < end:
            return begin, end
    return None


def coff_sections(data):
    """(name, rawptr, rawsize, relptr, nreloc) of each section of an object."""
    nsec, = struct.unpack_from("<H", data, 2)
    optsize, = struct.unpack_from("<H", data, 16)
    base = 20 + optsize
    out = []
    for i in range(nsec):
        off = base + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        relptr, = struct.unpack_from("<I", data, off + 24)
        nreloc, = struct.unpack_from("<H", data, off + 32)
        out.append((name, rawptr, rawsize, relptr, nreloc))
    return out


def coff_symbols(data):
    """(name, value, section number) of each symbol of an object."""
    machine, nsec, stamp, symptr, nsym, optsize, chars = struct.unpack_from("<HHIIIHH", data, 0)
    if symptr == 0:
        return []
    strtab = symptr + nsym * 18
    out = []
    i = 0
    while i < nsym:
        off = symptr + i * 18
        raw = data[off:off + 8]
        if raw[:4] == b"\0\0\0\0":
            stroff, = struct.unpack_from("<I", raw, 4)
            end = data.index(b"\0", strtab + stroff)
            name = data[strtab + stroff:end].decode("latin-1")
        else:
            name = raw.rstrip(b"\0").decode("latin-1")
        value, secnum, typ, cls, naux = struct.unpack_from("<IhHBB", data, off + 8)
        out.append((name, value, secnum))
        i += 1 + naux
    return out


def relocated_bytes(data, section):
    """The byte offsets a section's relocations write over."""
    name, rawptr, rawsize, relptr, nreloc = section
    covered = set()
    for r in range(nreloc):
        va, symidx, typ = struct.unpack_from("<IIH", data, relptr + r * 10)
        covered.update(range(va, va + 4))		# every relocation here is four bytes wide
    return covered


def find(body, objs, slack):
    """Object symbols whose bytes are the function's, relocations apart."""
    found = []
    for path in objs:
        with open(path, "rb") as f:
            data = f.read()
        try:
            sections = coff_sections(data)
            symbols = coff_symbols(data)
        except Exception:
            continue
        text = {}
        for i, section in enumerate(sections):
            if section[0].startswith(".text"):
                text[i + 1] = (section, relocated_bytes(data, section))
        if not text:
            continue
        for name, value, secnum in symbols:
            if secnum not in text:
                continue
            (sname, rawptr, rawsize, relptr, nreloc), covered = text[secnum]
            at = rawptr + value
            if at + len(body) > rawptr + rawsize:
                continue
            if data[at:at + 3] != body[:3]:
                continue					# the prologue, cheaply
            differ = 0
            for k in range(len(body)):
                if (value + k) in covered:
                    continue
                if data[at + k] != body[k]:
                    differ += 1
                    if differ > slack:
                        break
            if differ <= slack:
                found.append((differ, path, name))
    found.sort()
    return found


class ElfSymbols:
    """The function symbols of an ELF executable: name_of(offset)."""

    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"\x7fELF" or data[4] != 2:
            raise ValueError("not a 64-bit ELF image")
        end = "<" if data[5] == 1 else ">"
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from(end + "HHH", data, 0x3A)
        sections = []
        for i in range(shnum):
            name, kind, flags, addr, offset, size, link, info, align, entsize = \
                struct.unpack_from(end + "IIQQQQIIQQ", data, shoff + i * shentsize)
            sections.append((name, kind, offset, size, link, entsize))
        funcs = []
        for name, kind, offset, size, link, entsize in sections:
            if kind != 2:					# SHT_SYMTAB
                continue
            strings = sections[link]
            stroff = strings[2]
            for k in range(size // entsize):
                st_name, st_info, st_other, st_shndx, st_value, st_size = \
                    struct.unpack_from(end + "IBBHQQ", data, offset + k * entsize)
                if st_info & 0xF != 2 or st_value == 0:	# STT_FUNC
                    continue
                stop = data.index(b"\0", stroff + st_name)
                funcs.append((st_value, max(st_size, 1), data[stroff + st_name:stop].decode("latin-1")))
        if not funcs:
            raise ValueError("no symbol table (a stripped executable)")
        funcs.sort()
        self.starts = [f[0] for f in funcs]
        self.funcs = funcs
        self.cache = {}

    def lookup(self, offset):
        """(start, size, mangled name) of the function holding offset, or None."""
        import bisect
        i = bisect.bisect_right(self.starts, offset) - 1
        if i < 0:
            return None
        start, size, name = self.funcs[i]
        if offset >= start + size:
            return None
        return start, size, name

    def name_of(self, offset):
        hit = self.lookup(offset)
        if hit is None:
            return None
        return demangle(hit[2])


def demangle(names):
    """The C++ names demangled by c++filt when it is there (one or a list)."""
    one = isinstance(names, str)
    names = [names] if one else list(names)
    try:
        import subprocess
        out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True,
                             text=True, check=True).stdout.splitlines()
        if len(out) == len(names):
            names = out
    except (OSError, subprocess.CalledProcessError):
        pass
    return names[0] if one else names


def is_elf(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


def main_elf(exe, offset):
    symbols = ElfSymbols(exe)
    hit = symbols.lookup(offset)
    if hit is None:
        print(f"{offset:#x} is not in any function of {exe}")
        return 1
    start, size, name = hit
    print(f"the function runs {start:#x}-{start + size:#x} ({size} bytes); "
          f"the fault is {offset - start:#x} into it")
    print(f"{demangle(name)}\n    {name}")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", help="the host executable that fell over")
    ap.add_argument("offset", help="the offset into the image the crash reported (0x...)")
    ap.add_argument("--objs", default=None,
                    help="where the object files are (default: the exe's build directory)")
    ap.add_argument("--slack", type=int, default=8,
                    help="bytes that may differ outside the relocations (default 8)")
    args = ap.parse_args(argv[1:])

    if is_elf(args.exe):
        return main_elf(args.exe, int(args.offset, 0))
    with open(args.exe, "rb") as f:
        image = f.read()
    rva = int(args.offset, 0)
    sections = pe_sections(image)
    bounds = function_bounds(image, sections, rva)
    if bounds is None:
        print(f"{rva:#x} is not in any function of {args.exe}")
        return 1
    begin, end = bounds
    offset = rva_to_offset(sections, begin)
    body = image[offset:offset + (end - begin)]
    print(f"the function runs {begin:#x}-{end:#x} ({end - begin} bytes); "
          f"the fault is {rva - begin:#x} into it")

    root = args.objs
    if root is None:
        # build/host/host/newton.exe -> build/host
        root = os.path.dirname(os.path.dirname(os.path.abspath(args.exe)))
    objs = glob.glob(os.path.join(root, "**", "*.obj"), recursive=True)
    if not objs:
        print(f"no object files under {root}")
        return 1
    found = find(body, objs, args.slack)
    if not found:
        print(f"no object file has it (looked in {len(objs)} of them)")
        return 1
    for differ, path, name in found:
        note = "" if differ == 0 else f"  ({differ} bytes differ)"
        print(f"{name}\n    {os.path.relpath(path)}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
