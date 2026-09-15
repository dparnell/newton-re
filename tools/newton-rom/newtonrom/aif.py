"""Parser for ARM Image Format (AIF) files.

The Newton debug ROM images ("Senior ... image") are AIF executables produced by
the ARM SDT linker.  Layout of the file:

    offset 0x00   AIF header (128 bytes, see AIFHeader)
    offset 0x80   read-only area  (code + constant data), loaded at `image_base`
    ...           read-write area (initialised data), loaded at `data_base`
    ...           debug area      (one or more debug sections, see DebugSection)

The Newton ROMs are big-endian (the CPU runs in big-endian mode); the header's
`program_exit` word (SWI 0x11) is used to auto-detect byte order, exactly as the
original mpdumper tool does.

Reference for the structures: tools/mpdumper/mpdumper.h and the ARM SDT
"AIF" / "ASD debug table format" documentation.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Iterator, Optional

AIF_HEADER_SIZE = 0x80
_SWI_0x11_BE = b"\xef\x00\x00\x11"
_SWI_0x11_LE = b"\x11\x00\x00\xef"

# Debug section language codes (SectionLangType in mpdumper.h)
LANG_NONE, LANG_C, LANG_PASCAL, LANG_FORTRAN, LANG_ASM = range(5)
ITEM_SECTION = 1

_HEADER_FIELDS = (
    "bl_decompress", "bl_self_reloc", "bl_zero_init", "entry_point",
    "program_exit", "ro_area_size", "rw_area_size", "debug_area_size",
    "zero_init_area_size", "debug_type", "image_base", "min_workspace",
    "addressing_type", "data_base", "reserved1", "reserved2", "debug_init",
)


@dataclass(frozen=True)
class AIFHeader:
    bl_decompress: int
    bl_self_reloc: int
    bl_zero_init: int
    entry_point: int
    program_exit: int
    ro_area_size: int
    rw_area_size: int
    debug_area_size: int
    zero_init_area_size: int
    debug_type: int
    image_base: int
    min_workspace: int
    addressing_type: int
    data_base: int
    reserved1: int
    reserved2: int
    debug_init: int

    @classmethod
    def parse(cls, data: bytes, big_endian: bool) -> "AIFHeader":
        fmt = (">" if big_endian else "<") + "17I"
        return cls(**dict(zip(_HEADER_FIELDS, struct.unpack(fmt, data[:68]))))

    # Derived file offsets -------------------------------------------------
    @property
    def ro_offset(self) -> int:
        return AIF_HEADER_SIZE

    @property
    def rw_offset(self) -> int:
        return self.ro_offset + self.ro_area_size

    @property
    def debug_offset(self) -> int:
        return self.rw_offset + self.rw_area_size

    @property
    def rw_base(self) -> int:
        """Load address of the read-write area.

        AIF images built with a separate data segment (addressing_type bit 8)
        record it in `data_base`; otherwise RW follows RO directly.
        """
        if self.addressing_type & 0x100:
            return self.data_base
        return self.image_base + self.ro_area_size

    @property
    def zero_init_base(self) -> int:
        return self.rw_base + self.rw_area_size


@dataclass(frozen=True)
class DebugSection:
    """One item of type 1 (section) at the start of a debug-area chunk."""

    offset: int          # offset of this section item within the debug area
    length: int          # length of the section item structure itself
    language: int        # LANG_* code
    flags: int
    debug_version: int
    codestart: int
    datastart: int
    codesize: int
    datasize: int
    fileinfo: int
    debugsize: int       # total bytes of debug data belonging to this section
    name_nsyms: int      # LANG_NONE: number of symbols; otherwise string offset

    SIZE = 36

    @classmethod
    def parse(cls, data: bytes, offset: int, big_endian: bool) -> "DebugSection":
        e = ">" if big_endian else "<"
        (length_type,) = struct.unpack(e + "I", data[offset:offset + 4])
        item_type = length_type & 0xFFFF
        if item_type != ITEM_SECTION:
            raise ValueError(
                f"expected section item (1) at debug offset {offset:#x}, got {item_type}")
        language, flags, _unused, version = data[offset + 4:offset + 8]
        rest = struct.unpack(e + "7I", data[offset + 8:offset + 36])
        return cls(offset, length_type >> 16, language, flags, version, *rest)


class AIFImage:
    """An AIF file loaded into memory."""

    def __init__(self, data: bytes):
        if len(data) < AIF_HEADER_SIZE:
            raise ValueError("file too small to be an AIF image")
        exit_word = data[16:20]
        if exit_word == _SWI_0x11_BE:
            self.big_endian = True
        elif exit_word == _SWI_0x11_LE:
            self.big_endian = False
        else:
            raise ValueError("cannot determine endianness: program_exit is not SWI 0x11")
        self.data = data
        self.header = AIFHeader.parse(data, self.big_endian)
        h = self.header
        if h.debug_offset + h.debug_area_size > len(data):
            raise ValueError("AIF header describes more data than the file contains")

    @classmethod
    def from_file(cls, path) -> "AIFImage":
        with open(path, "rb") as f:
            return cls(f.read())

    @property
    def ro(self) -> bytes:
        h = self.header
        return self.data[h.ro_offset:h.ro_offset + h.ro_area_size]

    @property
    def rw(self) -> bytes:
        h = self.header
        return self.data[h.rw_offset:h.rw_offset + h.rw_area_size]

    @property
    def debug(self) -> bytes:
        h = self.header
        return self.data[h.debug_offset:h.debug_offset + h.debug_area_size]

    def sections(self) -> Iterator[DebugSection]:
        """Iterate over the debug sections in the debug area."""
        dbg = self.debug
        off = 0
        while off + DebugSection.SIZE <= len(dbg):
            sec = DebugSection.parse(dbg, off, self.big_endian)
            yield sec
            if sec.debugsize == 0:
                break
            off += sec.debugsize

    def first_section(self, language: Optional[int] = None) -> DebugSection:
        for sec in self.sections():
            if language is None or sec.language == language:
                return sec
        raise ValueError("no matching debug section")

    def word(self, file_offset: int) -> int:
        """Read a 32-bit word at a file offset."""
        e = ">" if self.big_endian else "<"
        return struct.unpack(e + "I", self.data[file_offset:file_offset + 4])[0]

    def ro_word(self, address: int) -> int:
        """Read a 32-bit word at a read-only area address (image_base relative)."""
        h = self.header
        return self.word(h.ro_offset + (address - h.image_base))
