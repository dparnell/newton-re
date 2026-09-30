# tools/fonts - the Newton's fonts as editable text

The MessagePad ROM's 13 fonts are `'sfnt` binaries in the font family
frames: sfnt containers (version 0x00010000) with **no outlines**, so not
TrueType fonts. Five are bitmap fonts (Apple's `bloc`/`bdat` strikes, the
forerunners of OpenType's EBLC/EBDT) and eight are metric-only fonts for
the printer faces (Helvetica and Times Roman). What every table holds, and
how the ROM reads it, is [docs/qd/fonts-sfnt.md](../../docs/qd/fonts-sfnt.md).

`newtonsfnt.py` (Python 3.9+, standard library only) turns a font into a
directory of text files and back, byte for byte. The ROM source tree
(`romsrc/`) keeps its fonts that way: `sfnt('sfnt, "resources/sfnt/<addr>")`
in `romsrc/objects/*.ns` names the directory, and the builder
(`tools/newton-rom/analysis/romsrc.py build`) packs it.

## Commands

    python tools/fonts/newtonsfnt.py unpack FONT.sfnt DIR
    python tools/fonts/newtonsfnt.py pack DIR FONT.sfnt
    python tools/fonts/newtonsfnt.py check FONT.sfnt...

`unpack` writes `DIR` afresh and then packs it again, failing (exit 1)
unless that gives back the font's bytes exactly. `pack` builds a font from
a directory, edited or not. `check` says whether each font round-trips and
which tables (if any) had to be kept as hex.

## The directory

| File | Table(s) | Form |
|---|---|---|
| `sfnt.txt` | the table directory | `version`, then `table TAG FILE CHECKSUM` per table in directory order. The builder lays the tables out in that order, each padded to four bytes with nought; `auto` checksums are worked out (the `head` one with `checkSumAdjustment` taken as nought). |
| `head.txt`, `hhea.txt`, `maxp.txt`, `post.txt`, `hsty.txt` | fixed-size tables | `field value` lines, a comment after each saying what it is and whether the ROM reads it. `head`'s `checkSumAdjustment` is `auto` (0xB1B0AFBA less the whole font's checksum). |
| `hmtx.tsv` | `hmtx` | `glyph advance lsb` per glyph, in design units; `-` for the advance of a glyph past `hhea`'s `numberOfHMetrics`. |
| `cmap.tsv` | `cmap` | `version`, then per subtable a `subtable PLATFORM ENCODING FORMAT LANGUAGE` line and a `0xCODE glyph` line per character (a comment names the character). Format 4 only; the builder makes one segment per run of consecutive codes with the same glyph delta, as the ROM's fonts have it. |
| `name.tsv` | `name` | `platform encoding language nameID string`, tab-separated, strings in the order they are stored. |
| `bloc.txt` + `strikeN-PPEM.bdf` | `bloc` and `bdat` | `bloc.txt` has the two tables' versions and the strikes' files in order; each strike is a BDF file (below). |
| `TAG.hex` | anything else | the table's bytes in hex, 16 to a line - what a table the decoder would not reproduce exactly falls back to (none of the ROM's do). |

## A strike as BDF

Each strike is a [Glyph Bitmap Distribution Format](https://en.wikipedia.org/wiki/Glyph_Bitmap_Distribution_Format)
2.1 file, which FontForge, gbdfed or a text editor can edit. The builder
reads from it:

- the `BLOC_*` properties: every field of the strike's bitmapSizeTable -
  the horizontal and vertical line metrics (`BLOC_HORI_ASCENDER`, ...), the
  glyph range, `BLOC_PPEM_X`/`_Y`, `BLOC_BIT_DEPTH`, `BLOC_FLAGS`,
  `BLOC_COLOR_REF` - and `BLOC_INDEX_SUBTABLE_n`, `"first last
  indexFormat imageFormat endWritten"` for each index subtable (index
  formats 1 and 3, image format 1; `endWritten` 0 means the offset after
  the last glyph is written as nought, as in the ROM's fonts);
- each glyph: `STARTCHAR gN` (N is the glyph id; a glyph renamed by an
  editor is found through its `ENCODING` and `cmap.tsv` instead), `DWIDTH`
  (the advance, the small metrics' fifth byte), `BBX width height x y`
  (x is the bearing; y is the bottom row, the bearing y less the height)
  and the `BITMAP` rows, one hex byte per eight pixels.

`FONT`, `SIZE`, `FONTBOUNDINGBOX`, `FONT_ASCENT`, `FONT_DESCENT`,
`PIXEL_SIZE`, `FAMILY_NAME` and `SWIDTH` are written for other programs
and not read back. The ROM draws text by the `BLOC_HORI_*` line metrics
(`FindSFNT`), so an edit that makes a glyph stand taller or wider than
the rest should change those too. A glyph id inside a subtable's range
with no glyph in the file gets offset nought - the first glyph's image,
which is what the ROM's fonts do for the ids past `maxp`'s `numGlyphs`.

## Tests

- `python tools/fonts/test_newtonsfnt.py [romsrc/resources/sfnt]` (ctest
  `tools.NewtonFonts`): the 13 fonts packed, unpacked and packed again
  byte for byte with no table as hex, the committed directories exactly
  what the unpacker writes, and edits (a pixel, a glyph made wider with a
  new advance, a `cmap` mapping, an `hsty` value) change only what they
  should, with the checksums right.
- `python tools/fonts/glyph_edit_test.py --tree romsrc -o <dir> --newton
  <newton> --newtonscript <newtonscript> --script src/host/demo/fontedit.ns
  [--original <objects file>]` (ctest `host.ROMSourceFontEdit`): one pixel
  of the System font's 9-point A flipped in its BDF file, the tree built,
  the OS booted on it and on the unedited tree, and exactly that pixel
  differs on the screen.
