# tools/tables: the Unicode, collation and locale tables as text

The ROM keeps its character tables as binaries in NewtonScript objects:
the 'unicode frame's (Runicode, installed by `frames/UnicodeTables.cpp`'s
`InitUnicode`: the three encodings' maps to and from Unicode, the
character classes, their types and the four case tables), the sorting
tables (`Rsorttables`, `frames/SortTables.h`) and each locale's
`wordBreakTable` and `lineBreakTable` (`qd/Text.cpp`'s `FindWordBreaks`).
`romsrc/` keeps all sixteen as text - `texttable(class, "path.txt")` in
the object files - and `newtontables.py` makes the ROM's bytes out of
them, exactly the ROM's for the committed files.

Each file says what it is on its first line, `format <kind>`; `#` starts
a comment, and the unpacker puts the character a line is about in one.

| format | what | a line |
|---|---|---|
| `to-unicode` | an encoding's `mapToUnicode`: the character each byte stands for; the header's four halfwords (kind 0, 256 entries, flags, segments) | `0x80 U+00C4` |
| `from-unicode` | its `mapFromUnicode`: `segment U+first U+last`, then each character of it and its byte; the segments' bytes lie one after another, and a character in none becomes 0x1a | `U+00C4 0x80` |
| `char-classes` | `charClass`: the class of each Mac Roman character | `0x41 2` |
| `class-types` | `typelist`: each class's type flags | `2 0x01` |
| `class-deltas` | `upperList`, `lowerList`, `upperNoMarkList`, `noMarkList`: what is added to a character of each class | `3 -32` |
| `sort-table` | a sorting table: `id`, each `range`'s characters with their projection (primary weight; 0 ignored, 0xffff a ligature) and second-order weight, the `singles` (any order: they are sorted), the `ligatures` (the character, the two it sorts as, the least character sorting the same) and the `lowest` table (the least character of each primary weight). The counts and offsets in the header are worked out. | `U+0061 0x0041 0x0007` |
| `break-table` | a break table: `flags`, `backup` (how near the start the backward scan is skipped), the `classes` of the 256 characters, the `aux` bytes, then the `backward` and `forward` state machines - `states` (each entry's row; a state is named by its offset into that list, two to an entry) and the `row`s (the next state for each class, `*` where a word may begin or end) | `row 0 20* 4* 6* 8 8 0 0 0 8 0 0` |

## Use

    python tools/tables/newtontables.py unpack sort-table X.bin X.txt
    python tools/tables/newtontables.py pack X.txt X.bin
    python tools/tables/newtontables.py check sort-table X.bin...

`romsrc.py extract` writes a binary of class 'UniC (by its slot),
'Sort, or 'Intl (a `wordBreakTable` or `lineBreakTable` slot) this way when
it comes back the same, and `romsrc.py build` packs `texttable(...)`.

Tests: `test_newtontables.py [--rom build/MP2x00US]` (ctests
`tools.NewtonTables`, `tools.NewtonTablesROM`: the sixteen committed
files pack to the ROM's bytes and unpack to their committed text, and
edits read back); `table_edit_test.py` (ctest `host.ROMSourceTableEdit`:
a and z given each other's weights in the default sorting table, and the
booted machine's `StrCompare("apple", "zebra")` changes sign).
