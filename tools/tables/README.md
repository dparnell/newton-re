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

## The ink code books, the trigrams and the letter pictures (`inktables.py`)

Three more kinds of the ROM's trained data, which `romsrc/` kept as raw
binaries, have a text form made and read by `inktables.py` - the same
conventions (`format <kind>` first, `#` comments), the ROM's own bytes
for every committed file:

| format | what | a line |
|---|---|---|
| `code-book` | an ink code book ('Comp: `codebook1` = `Rparagraphcodebook1`, the writing book, and `codebook2` = `Rparagraphcodebook2` = @145, the ink book; `ink/CICCodec.h`). Eight `table N`s, each its four header words - `base-down`, `base-up`, `escape-up`, `escape-down` (`CodeTableBaseDown` ...) - and its code words: the value, then its code as the bits come out of the stream, first bit first (`DecodeWord_OLD` reads `code \|= bit << n`). A table's size and count are worked out, and its entries put in order of code length (stably), which the decoder needs; a code that begins with another is refused, since the decoder would never get to it. | `27 100011` |
| `trigrams` | the 'Trigrams binary (`Rtrig`): a weight 1-3 for each three-symbol sequence that has one, the symbols `a`-`z`, `'` and `_` (the edge of a word); a sequence not listed is 0 | `the 3` |
| `letter-images` | the 'letterimages binary of `charsetInfoResources` (`recognition/LetterShapes.h`): each `letter` (its Mac Roman byte, the character in a comment), each of its variants as `variant group G` in order, and each variant's strokes as `x,y` points in the 0-255 box (the letter's body between y 0x55 and 0xaa). Every count and offset is worked out. | `stroke 127,22 127,58 127,89 127,127 127,145` |

What each format's bytes are, and how it was established:

- **code-book.** A table is six big-endian halfwords - size
  (12 + 8 x count), count, base-down, base-up, escape-up, escape-down -
  then eight bytes an entry: the value (signed), the code's length in
  bits, and the code as a word whose bit 0 is the first bit read
  (`ink/CICDecoder.cpp`'s `CodeTable*` accessors and `DecodeWord_OLD`).
  `DcdrSelectCodeBook` takes eight tables one after another by their
  sizes; the eight are read for, in order, the x and y of the step to
  where a stroke starts, of a segment's end (or a short stroke's step),
  and of a segment's two bending numbers (`ReadNewStroke`,
  `ReadSegmentNear`, `ReadShortStroke`; the unpacker says which in a
  comment).  Both books are exactly eight tables with nothing after, in
  order of code length, every code shorter than 2^length, and every
  table's codes **complete** - the sum of 2^-length over a table is
  exactly 1 (checked over both committed books), so any string of bits
  begins with some code.  A value is therefore added by splitting a code
  in two (`v 0101` becomes `v 01010` and `w 01011`), not by inventing a
  fresh one, which would begin with an existing code and be refused.
  Who reads the second book: `romsrc/magic.tsv` names @145 `paragraphCodebook2`;
  the ROM constant `Rparagraphcodebook2` (0x0068133c) is that magic
  pointer (0x247), the word at 0x003af248 - the magic-pointer table's
  entry - is the only other place the ROM holds 0x0063dedd, and the one
  function that reads the constant is `InitializeParagraphCompression`
  (0x001543a4, found with `analysis/xrefs.py ... Rparagraphcodebook2`),
  which makes it `globalCodeBookPtrInk`, the book `LockCodeBook(2)`
  answers - the ink book, used for ink formats 2 and 3.
- **trigrams.** 8192 bytes: 32 x 32 x 32 two-bit entries, entry
  (a x 32 + b) x 32 + c in byte i >> 2 at bits 2 x (i & 3) - the first
  entry of a byte in its lowest bits.  Established from the data: read
  that way, exactly the symbols 1-28 are used in every position (read
  most significant first, the third position uses 0 and 31 and not 3 or
  28, which no alphabet explains); 1-26 are a-z by their frequencies
  (`q` is followed only by `u`, `the`, `ing`, `ion` weigh 3); 27 is an
  apostrophe (the trigrams with it are `n't`, `'ll`, `'re`, `'ve`, `'s`,
  `'d`, `'m`) and 28 the edge of a word (it comes first in trigrams
  such as `_th` and last in ones such as `ng_`, and `x__` is set for
  nearly every letter x).  Symbols 0 and 29-31 are written
  `<n>` should one ever be used.  **Nothing in the ROM's code reads this
  table**: the only place its reference (0x005ba4b1) occurs in the ROM is
  `Rtrig` (0x00680eb4), whose only reference is `RStrig`, the constant's
  export for packages, which nothing references either (`analysis/
  xrefs.py ... Rtrig RStrig`, and a byte search of `rom.bin`).  The
  cursive recogniser's trigrams are a different table, `DTETrigrams`
  (`DTETrigrams`, 40 symbols, read by `Xrlv.cpp`).  So what the weights
  1-3 mean is not established.
- **letter-images.** As `recognition/LetterShapes.h` describes it (from
  `LILetterCount`, `GetNextLetter`, `LIGetVariantInfo`, `LIGetStrokeInfo`
  and `LIGetPoint`): a word, the letter count; each letter its byte, its
  variant count and a halfword offset per variant; a variant its group,
  its stroke count and a halfword offset per stroke; a stroke its point
  count and an x and a y byte per point.  The ROM lays out all the
  letters' headers, then every variant, then every stroke, each in order
  with no gaps and nothing shared (checked when unpacking), so packing
  lays them out the same way and no offset is kept in the text.  The
  order of the letters and of a letter's variants is kept: the slip
  numbers variants across the letters (`LIGetVariantIndex`).  89 letters,
  831 strokes.

Use:

    python tools/tables/inktables.py unpack code-book X.bin X.txt
    python tools/tables/inktables.py pack X.txt X.bin
    python tools/tables/inktables.py check code-book X.bin...

As a module: `FORMATS`, `unpack(data, format, path)`,
`pack_lines(format, lines, base)` (the text split on newlines, its format
line included; `base` is the text's directory, which none of these
formats needs) and `pack(path)`.  Tests: `test_inktables.py [--rom
build/MP2x00US]` - the four committed binaries unpack and pack back to
the same bytes and the same text (with `--rom`, the ROM's own bytes), and
edits read back: a code word's value, a code split to add a value (the
table's size and count and every later table move), a code that begins
with another refused, trigram weights, a point moved and a stroke added
to a picture (every later offset moves).

## The cursive recogniser's letter table, rules and trigrams (`hwrtables.py`)

ParaGraph's cursive ("xr") recogniser (`recognition/ParaGraph.h`) reads
four binaries of `charsetInfoResources` - `DTEHeader`, `DTEMain`,
`PPDMain` and `DTETrigrams` (`paragraphData`, `romsrc/objects/system/paragraphData.ns`).
`hwrtables.py` gives each a text form, with the same conventions
(`format <kind>` first, `#` comments) and the ROM's own bytes for every
committed file:

| format | what | a line |
|---|---|---|
| `dte-header` | `DTEHeader` ('DTEH, 216 bytes): the stale `handle` word, the names in `DTIHeader.fHeader` as `text OFFSET string` (the rest nought; `bytes OFFSET hex` for a run that is not plain text), and the 18 pointer words as `word fName value` | `text 0x84 DTI4EngM2.00` |
| `dte-main` | `DTEMain` ('DTEM): a `symbol` per engine character code with its descriptor's bytes 1-3; a `variant c.v` per variant - `vex`, `group`, `styles`, `rewcap`, `pos`, `size`; an `xrp c.v.p` per prototype xr - `xr`, `b1`, `flags`, `skip` and the `types`, `heights`, `shifts`, `links` and `dirs` nibble tables as hex digits. The offset table and every count are worked out. | `variant a.0 vex 0 group 1 styles 0xe rewcap 1 pos 0xbd size 0xce` |
| `ppd-main` | `PPDMain` ('PPDB): the post-processing rules - for each `letter`, `variant` and neighbour (`next c.v n`), `rule c.v own` or `rule c.v n.w`, and the rule's queues as stack-machine code with their weights; the `constant`s; `function` lines naming what `call` calls (`kPPFunctions`). Every header, bit set, offset, size, count and weight index is worked out. | `queue a.0 own -4 : 100 ; xr 1 top ; xr 6 top ; sub ; mul` |
| `dte-trigrams` | `DTETrigrams` ('Trigram): a weight 1-3 for each three-class sequence that has one, over the 40 classes `_` (space), `a`-`z`, `å ä ç é ï ö ü â è ê î û` and `'`; one not listed is 0 | `the 3` |

The engine's character codes (`symbol`, `letter`, a label `c.v`) are
`OSToRec`'s: Mac Roman below 0x7f, written as the character (`0x23` for
`#`, `0x20` for the space); 0x7f-0x83, 0xa0 and 0xa1 are the Swedish
letters and the pound sign, £ Ä ä Ö ö Å å (`os_rec_ctbl_intl`), which a
comment says.

What each format's bytes are, and how it was established (every structure
below is checked when unpacking - a binary that breaks one is refused
rather than mis-read - and the four committed files keep all of them):

- **Every binary's first word** is not the engine's: `ReadDteResource`
  steps over it (`dteMain + 4`, `ppdMain + 4`, `trigramData += 4`, and the
  header's bytes from 4).  By the data it is the build Macintosh's handle
  for the resource: `DTEHeader`'s stale `fRAMDTE` holds 0x0109b8d8,
  `DTEMain`'s first word, and `fLearnInfo` and `fPDF` hold neighbouring
  addresses; `PPDMain`'s first word is "HMEM".  Kept as `handle`.
- **dte-header.** Bytes 4-0x8f are `DTIHeader.fHeader`, copied and never
  read: four strings ("english.dti", "s:8/27/1996",
  "DTE:English:english.dti", "DTI4EngM2.00" - a file name, its date, the
  DTE's name, its version, by their contents), nought between them.
  0x90-0xd7 are `DTIHeader`'s pointer words (`ParaGraph.h`: `fRAMDTE` ...
  `fD0`, at `DTIHeader` offset + 4), which the engine sets on loading;
  kept as they are.
- **dte-main.** As `CountSym` (`XrMatrix.cpp`), `GetSymDescriptor`,
  `GetVarPosSize`, `GetVarGroup`, `CheckVarActive`, `SetDefVexes`
  (`ParaGraph.cpp`), `GetXrCorr` (`XrPostCalc.cpp`) and `GetVarOfChar`
  (`XrPostEval.cpp`) read it: 256 words of offsets from the table, then
  the descriptors.  A descriptor: byte 0 the variant count, bytes 1-3,
  sixteen bytes each of prototype counts (+4), vexes (+0x14), groups
  (+0x24: bit 0 not a capital, bits 1-3 the group, 4-7 the letter-set
  styles), positions (+0x34) and sizes (+0x44: nibbles, as `Xrlv.cpp`'s
  `GetSymBox` and `SizePenalty` read them), then the prototypes, 0x4c
  bytes each, variant after variant.  A prototype (`XrMatch`,
  `CountXrAsm`, `GetXrCorr`): byte 0 the xr written into the trace, byte
  1, byte 2 flags (bit 7: reads only an xr next to a break), byte 3 the
  skip penalty, then nibble tables, high nibble first: 64 types (+4), 16
  heights (+0x24), 16 shifts (+0x2c), 16 links (+0x34), 32 directions
  (+0x3c).  From the data: 91 descriptors (0x20-0x7f, 0x80-0x83, 0xa0,
  0xa1), in order of code, one after another with nothing between or
  after; every byte for a variant past the count is nought (so the
  packer writes nought there); 3,499 prototypes.  **Not known**: the
  descriptor's byte 1 (the top byte of `GetVarPosSize`'s answer, which
  no caller uses: 0x08 for capitals, 0x09 for b d h k l, 0x2a for most
  x-height letters, 0x2f for j p q y z, 0x0f for digits - a position
  class, by the data), byte 2 (the code itself, except 0x2d for `_` and
  0x76 for 0x80-0xa1) and byte 3 (always nought), and a prototype's byte
  1 (1-15) - nothing in the reconstructed engine reads them, so they are
  kept as numbers (`b1`, `b2`, `b3`).  A prototype's byte 0 is, in 3,074
  of the 3,204 prototypes where it is below 64, the type its own table
  rates highest; 295 have it above 63.
- **ppd-main.** As `CreatePDFHeader` (`ParaGraph.cpp`), `PDFGetRule` and
  its helpers (`XrRules.cpp`), `CalculateGroupResult` and
  `CalculateQueueResult` (`XrPostCalc.cpp`) read it: three words the
  engine writes the sections' addresses over (stale; `stale`), a word of
  two halfwords - section 1's and section 2's lengths in halfwords -
  then section 0, the rules' tree, from +0x10; section 1 (the constants
  `const` pushes) at the offset in the main header's fourth word, and
  section 2 (the queues' weights) after it, to the end.  The main
  header's first three words ("PDF\0", 1, 0) nothing reads (`main`).  A
  rule: its queue count, each queue's length (+2), each queue's weight's
  index in section 2 (+0x16), the queues from +0x20 - room for five
  queues (the weights' indices end at +0x20), and no rule has more.
  From the data (87 letters, 374 variants, 63 neighbours, 467 rules,
  1,090 queues): the tree lies depth first in order - main header,
  letter, its variants, each variant's own rule and then its neighbours'
  rules - with no gaps and nothing shared, and every header's first word
  is the size of all under it (a variant's +0x24 the size of its own
  rule), so packing lays it out again and no offset or size is kept;
  section 2 has exactly one weight per queue and the queues use them in
  order (index 0, 1, 2 ...), so a weight is written on its queue's line
  and the indices are worked out; a rule header's bytes past its queues
  are nought.  A queue is code up to a nought; only bytecodes 0x0a, 0x0b
  (numbers), 0x0c (arithmetic), 0x0e (a function), 0x0f (an xr's field)
  and 0x10 (a constant: six bytes, the last two nought) occur, and every
  number is in the shortest form that holds it (no 0x0a holds one below
  256), which is the form packing chooses.  The bytes after the nought
  are kept as `pad`: a queue's length is not always its code's plus one -
  347 of the 1,090 have one to three bytes more, mostly nought but 3, 6
  or 7 in a few (what the rule compiler left there).  Anything the unpacker
  could not write back the same would be written as `raw HEX` (nothing
  in the committed file is).  The function names come from
  `kPPFunctions` (`XrPostCalc.cpp`, in the ROM's `Functions` order) when
  unpacking; packing reads only the text's own `function` lines.
- **dte-trigrams.** As `XrlvGetNextSymbols` (`Xrlv.cpp`) reads it: 16,000
  bytes, the weight of class c after a, b in byte a x 400 + b x 10 +
  (c >> 2) at bits 2 x (c & 3) - the reading's state holds its last two
  classes, the older in its second byte, and starts at 0 (two spaces) -
  and the reader charges a letter 5 less the weight.  A character's
  class is `triads_mapping[c] - 1` (`XrReaderTables.cpp`): 0 the space,
  1-26 a-z (with their capitals and some accented forms), 27-38 the
  accented letters the text names by one of their characters (each with
  its capital; ä also à, ö also ô, ü also ù), 39 the apostrophe; '@' and
  '/' map past 39 and are read as 0.  5,612 trigrams have a weight.
  (This is not the 'Trigrams binary `inktables.py` reads, which nothing
  in the ROM reads.)

Use:

    python tools/tables/hwrtables.py unpack dte-main X.bin X.txt
    python tools/tables/hwrtables.py pack X.txt X.bin
    python tools/tables/hwrtables.py check dte-main X.bin...

As a module: `FORMATS`, `unpack(data, format, path)`,
`pack_lines(format, lines, base=None)` (the text split on newlines, its
format line included; `base` is not needed) and `pack(path)`.  Tests:
`test_hwrtables.py [--rom build/MP2x00US]` - the four committed binaries
unpack and pack back to the same bytes and the same text (with `--rom`,
the ROM's own bytes); the names the text gives the engine's codes and the
trigram classes match `os_rec_ctbl_intl` and `triads_mapping`; and edits
are read back through Python mirrors of the C++ readers
(`GetSymDescriptor`/`GetXrCorr`, `CreatePDFHeader`/`PDFGetRule`/
`CalculateGroupResult`, `XrlvGetNextSymbols`): a header string, a vex, a
prototype added (every later descriptor moves), a queue's number and
weight changed and a rule added for a new neighbour (every later offset,
size and weight index moves, the constants still found), and trigram
weights; a prototype out of order, an unknown function and a trigram
given twice are refused.
