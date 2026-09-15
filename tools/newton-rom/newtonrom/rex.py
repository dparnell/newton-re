"""Parser for Newton ROM Extension (REx) blocks.

The "... high" files in DebugRom/ are REx blocks: a header with a 'RExBlock'
signature followed by a table of tagged config entries (see
headers/OS600/ROMExtension.h for the C declarations and tag meanings).
`start` is the virtual address the block is placed at; for the shipped ROMs it
is exactly ROM$$Size of the base image, i.e. the REx sits immediately after the
base ROM (RO area + RW initialisers) in the 8 MB ROM address space.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import List, Optional

SIGNATURE = b"RExBlock"

# Well-known config entry tags (kXxxTag constants in ROMExtension.h)
TAG_DESCRIPTIONS = {
    "ram ": "RAM configuration",
    "ralc": "RAM allocation",
    "diag": "diagnostics",
    "jump": "C patch table (jump table overrides)",
    "fexp": "frame export table",
    "fimp": "frame import table",
    "pkgl": "package list (built-in packages)",
    "pad ": "padding",
    "ptpt": "patch table page table",
    "glpt": "Gelato page table",
    "romt": "ROM timing config",
    "fsht": "flash timing config",
    "fshb": "flash timing bank",
    "fshd": "flash driver config",
    "fdrv": "flash driver entry",
    "flsa": "flash address",
    "dio ": "DIO config",
    "gpio": "GPIO config",
}


@dataclass(frozen=True)
class ConfigEntry:
    tag: str
    offset: int
    length: int


@dataclass(frozen=True)
class RExBlock:
    checksum: int
    header_version: int
    manufacturer: int
    version: int
    length: int
    id: int
    start: int
    entries: List[ConfigEntry]
    data: bytes

    @classmethod
    def parse(cls, data: bytes) -> "RExBlock":
        if data[:8] != SIGNATURE:
            raise ValueError("not a REx block (missing RExBlock signature)")
        checksum, hver, manuf, ver, length, id_, start, count = struct.unpack(">8I", data[8:40])
        entries = []
        for i in range(count):
            tag, off, ln = struct.unpack(">4sII", data[40 + 12 * i:52 + 12 * i])
            entries.append(ConfigEntry(tag.decode("latin-1"), off, ln))
        return cls(checksum, hver, manuf, ver, length, id_, start, entries, data)

    @classmethod
    def from_file(cls, path) -> "RExBlock":
        with open(path, "rb") as f:
            return cls.parse(f.read())

    def entry(self, tag: str) -> Optional[ConfigEntry]:
        for e in self.entries:
            if e.tag == tag:
                return e
        return None

    def entry_bytes(self, entry: ConfigEntry) -> bytes:
        return self.data[entry.offset:entry.offset + entry.length]
