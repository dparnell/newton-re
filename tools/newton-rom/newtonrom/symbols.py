"""Reader for the low-level (LANG_NONE) symbol table in an AIF debug area.

A LANG_NONE section is laid out as:

    DebugSection item (36 bytes)
    Symbol[nsyms]            each 8 bytes: flags:8 | string offset:24, value:32
    StringTable              length:32 followed by length-prefixed strings

Symbol flags (SymbolFlag in mpdumper.h): bit 0 = global, and the upper bits
select the class: 0x02 code, 0x04 data, 0x06 zero-initialised data.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional

from .aif import LANG_NONE, AIFImage, DebugSection

FLAG_GLOBAL = 0x01
CLASS_MASK = 0x06
CLASS_ABS = 0x00
CLASS_CODE = 0x02
CLASS_DATA = 0x04
CLASS_ZINIT = 0x06

CLASS_NAMES = {CLASS_ABS: "abs", CLASS_CODE: "code", CLASS_DATA: "data", CLASS_ZINIT: "zinit"}


@dataclass(frozen=True)
class Symbol:
    name: str
    value: int
    flags: int

    @property
    def is_global(self) -> bool:
        return bool(self.flags & FLAG_GLOBAL)

    @property
    def sym_class(self) -> str:
        return CLASS_NAMES[self.flags & CLASS_MASK]

    @property
    def is_code(self) -> bool:
        return (self.flags & CLASS_MASK) == CLASS_CODE


def read_symbols(image: AIFImage, section: Optional[DebugSection] = None) -> List[Symbol]:
    """Return all symbols of the first LANG_NONE section (or the given one)."""
    if section is None:
        section = image.first_section(LANG_NONE)
    if section.language != LANG_NONE:
        raise ValueError("symbol tables only exist in LANG_NONE sections")

    e = ">" if image.big_endian else "<"
    dbg = image.debug
    nsyms = section.name_nsyms
    sym_off = section.offset + DebugSection.SIZE
    str_off = sym_off + nsyms * 8
    (str_len,) = struct.unpack(e + "I", dbg[str_off:str_off + 4])
    strings = dbg[str_off:str_off + str_len]

    def name_at(offset: int) -> str:
        length = strings[offset]
        return strings[offset + 1:offset + 1 + length].decode("latin-1")

    out: List[Symbol] = []
    for i in range(nsyms):
        word, value = struct.unpack(e + "II", dbg[sym_off + 8 * i:sym_off + 8 * i + 8])
        out.append(Symbol(name_at(word & 0xFFFFFF), value, word >> 24))
    return out


def by_name(symbols: Iterable[Symbol]) -> Dict[str, List[Symbol]]:
    d: Dict[str, List[Symbol]] = {}
    for s in symbols:
        d.setdefault(s.name, []).append(s)
    return d


def by_value(symbols: Iterable[Symbol]) -> Dict[int, List[Symbol]]:
    d: Dict[int, List[Symbol]] = {}
    for s in symbols:
        d.setdefault(s.value, []).append(s)
    return d
