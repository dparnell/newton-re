"""The Newton OS patchable jump table.

Nearly every cross-module call in the ROM goes through a table of ARM `B`
(branch) instructions so that individual functions can be patched by a ROM
extension.  Facts established from the MP2100 D and MP2x00 US images:

* The table lives physically in the read-only area at ROM address 0x2000 and is
  one `B target` instruction per entry (16,723 entries in the German ROM).
* The MMU remaps it to virtual address 0x01A00000 in a *sparse* layout.
  Virtual page `p` (4 KB, p = 0, 1, 2, ...) is a plain alias of the physical
  ROM page `0x2000 + (p // 32) * 0x1000`, so 32 consecutive virtual pages show
  the same 4 KB of ROM.  Each of them "owns" a different 128-byte slice of that
  page: slot `i` lives in virtual page `i // 32` at page offset
  `((i // 32) % 32) * 0x80 + (i % 32) * 4`.  Every function therefore has one
  unique virtual slot address even though the bytes are shared; the REx
  "ptpt"/"glpt" page tables implement this mapping and let a ROM patch replace
  the table one 4 KB page at a time.
* The branch offsets are encoded relative to the *virtual* address of the entry,
  not its physical ROM address.  Decoding a raw ROM dump at 0x2000 therefore
  gives nonsense targets unless the virtual address is used.
* The debug symbol table contains each exported function twice: once at its
  real address in the ROM, and once (same name) at its jump-table slot.  The
  branch target of every slot is the identically named low symbol, which is
  how `verify()` checks the decoding.

The one exception is the slot `_DebugStr`, whose body carries the plain C name
`DebugStr` - an assembler alias rather than a mismatch.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

from .aif import AIFImage
from .symbols import Symbol

VIRTUAL_BASE = 0x01A00000
ENTRIES_PER_PAGE = 32
PAGE_SIZE = 0x1000
PAGE_USED_BYTES = ENTRIES_PER_PAGE * 4
DEFAULT_ROM_ADDRESS = 0x2000

_B_OPCODE = 0xEA  # unconditional B: cond=1110, 101, L=0


def is_branch(word: int) -> bool:
    return (word >> 24) == _B_OPCODE


def branch_target(word: int, pc: int) -> int:
    """Target of an unconditional `B` located at `pc`."""
    imm = word & 0x00FFFFFF
    if imm & 0x00800000:
        imm -= 0x01000000
    return (pc + 8 + imm * 4) & 0xFFFFFFFF


def page_slice_offset(page: int) -> int:
    """Offset within virtual page `page` of the 128 bytes of slots it owns."""
    return (page % (PAGE_SIZE // PAGE_USED_BYTES)) * PAGE_USED_BYTES


def virtual_to_index(vaddr: int) -> int:
    page, off = divmod(vaddr - VIRTUAL_BASE, PAGE_SIZE)
    off -= page_slice_offset(page)
    if not 0 <= off < PAGE_USED_BYTES or off & 3:
        raise ValueError(f"{vaddr:#x} is not a jump table slot")
    return page * ENTRIES_PER_PAGE + off // 4


def index_to_virtual(index: int) -> int:
    page, slot = divmod(index, ENTRIES_PER_PAGE)
    return VIRTUAL_BASE + page * PAGE_SIZE + page_slice_offset(page) + slot * 4


def page_count(count: int) -> int:
    return (count + ENTRIES_PER_PAGE - 1) // ENTRIES_PER_PAGE


def virtual_page_source(page: int, rom_address: int = DEFAULT_ROM_ADDRESS) -> int:
    """Physical ROM address that virtual page `page` of the table aliases."""
    return (rom_address + page * PAGE_USED_BYTES) & ~(PAGE_SIZE - 1)


def is_virtual_slot(addr: int, count: int) -> bool:
    if addr < VIRTUAL_BASE:
        return False
    try:
        return virtual_to_index(addr) < count
    except ValueError:
        return False


@dataclass(frozen=True)
class JumpEntry:
    index: int
    virtual: int      # address of the slot in the remapped virtual table
    rom_address: int  # address of the same word in the physical ROM
    target: int       # real function address the slot branches to


class JumpTable:
    def __init__(self, image: AIFImage, rom_address: int = DEFAULT_ROM_ADDRESS,
                 count: Optional[int] = None):
        self.image = image
        self.rom_address = rom_address
        if count is None:
            count = self._measure(image, rom_address)
        self.count = count
        self.entries: List[JumpEntry] = []
        for i in range(count):
            rom = rom_address + 4 * i
            v = index_to_virtual(i)
            word = image.ro_word(rom)
            if not is_branch(word):
                raise ValueError(f"jump table entry {i} at {rom:#x} is not a B instruction")
            self.entries.append(JumpEntry(i, v, rom, branch_target(word, v)))
        self.by_virtual: Dict[int, JumpEntry] = {e.virtual: e for e in self.entries}
        self.by_target: Dict[int, List[JumpEntry]] = {}
        for e in self.entries:
            self.by_target.setdefault(e.target, []).append(e)

    @staticmethod
    def _measure(image: AIFImage, rom_address: int) -> int:
        """Count consecutive B instructions starting at rom_address."""
        n = 0
        limit = image.header.image_base + image.header.ro_area_size
        addr = rom_address
        while addr + 4 <= limit and is_branch(image.ro_word(addr)):
            n += 1
            addr += 4
        return n

    @staticmethod
    def locate(image: AIFImage, min_entries: int = 1000) -> int:
        """Find the ROM address of the longest run of B instructions."""
        ro = image.ro
        best_start, best_len, run, start = 0, 0, 0, 0
        for i in range(0, len(ro) - 3, 4):
            b = ro[i] if image.big_endian else ro[i + 3]
            if b == _B_OPCODE:
                if run == 0:
                    start = i
                run += 1
                if run > best_len:
                    best_start, best_len = start, run
            else:
                run = 0
        if best_len < min_entries:
            raise ValueError("no jump table found")
        return image.header.image_base + best_start

    def verify(self, symbols: Iterable[Symbol]) -> Tuple[int, List[Tuple[JumpEntry, str]]]:
        """Check every slot symbol branches to a same-named low symbol.

        Returns (matched, mismatches) where mismatches is a list of
        (entry, name) for slots whose target has no symbol of that name.
        """
        low: Dict[str, set] = {}
        slots: List[Tuple[int, str]] = []
        for s in symbols:
            if is_virtual_slot(s.value, self.count):
                slots.append((s.value, s.name))
            elif s.value < VIRTUAL_BASE:
                low.setdefault(s.name, set()).add(s.value)
        matched, mismatches = 0, []
        for vaddr, name in slots:
            entry = self.by_virtual[vaddr]
            if entry.target in low.get(name, ()):
                matched += 1
            else:
                mismatches.append((entry, name))
        return matched, mismatches
