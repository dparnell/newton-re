# The ROM's fonts: sfnt containers of bitmap and metric tables

The font family frames (`docs/qd/README.md`, "Fonts") hold their faces as
`'sfnt` binaries - 13 in the US ROM. Each is an **sfnt container**
(version 0x00010000, the same wrapper TrueType uses) but none of them has
outlines (`glyf`, `loca`, `CFF `): they are not TrueType fonts. Five are
**bitmap fonts** - Apple's `bloc`/`bdat` strikes, the forerunners of
OpenType's `EBLC`/`EBDT` - and eight are **metric-only fonts** that give
the printer faces their widths.

The ROM source tree keeps them as text (`romsrc/resources/sfnt/<addr>/`:
a BDF file per strike, a text file per other table); `tools/fonts/
newtonsfnt.py` makes that form and packs it back byte for byte
(`tools/fonts/README.md`).

**How this was established.** The reading side is the reconstructed font
engine - `src/qd/Fonts.cpp` and the alert manager's own reader in
`src/alert/AlertDialog.cpp`, each function cited to its ROM address
below - which draws the ROM's text pixel for pixel (`host.NewtonNoROMSameScreen`
boots on fonts rebuilt from the text form and matches the boot on the ROM
image). The layout side is the fonts' own bytes: `newtonsfnt.py` decodes
every table of all 13 into fields, and packs them back identical to the
ROM's (`tools.NewtonFonts`, `host.ROMSourceCommitted`), so every layout
rule below - down to the padding and the checksums - is one the round
trip checks.

## The 13 fonts

| Font (`resources/sfnt/`) | Family (face) | Kind | Tables | Strikes (pixels per em) |
|---|---|---|---|---|
| 3db801 | System, `'espyFont` (plain) | bitmap | 10 | 9 10 11 12 14 16 |
| 3e21c1 | System (bold) | bitmap | 10 | 9 10 11 12 14 16 |
| 3e9319 | Fancy, `'newYorkFont` | bitmap | 10 | 9 10 12 14 18 24 36 |
| 3f5e81 | Simple, `'genevaFont` | bitmap | 10 | 9 10 12 14 18 24 36 48 |
| 408ee5 | Casual, `'handwritingFont` | bitmap | 10 | 10 12 18 36 |
| 412165 412991 4131bd 4139e9 | Helvetica (plain, bold, italic, bold italic) | metrics | 5 | - |
| 4142c9 414af5 415321 415b4d | Times Roman (plain, bold, italic, bold italic) | metrics | 5 | - |

A bitmap font has `bdat`, `bloc`, `cmap`, `head`, `hhea`, `hmtx`, `hsty`,
`maxp`, `name`, `post`; a metric-only font (2080 bytes) has `cmap`,
`head`, `hhea`, `hmtx`, `hsty`. The families' `userSizes` slots (what the
Styles picker offers: 9 10 12 18 for System and Simple) are not the strikes;
a size between strikes is drawn from the nearest, scaled.

## The container

The offset table and a 16-byte directory entry per table (tag, checksum,
offset, length), as in any sfnt; `searchRange`/`entrySelector`/
`rangeShift` are the standard ones. In all 13 the entries are in tag
order, the tables lie in the file in the same order, each padded to four
bytes with nought (the last one too: a metric-only font ends in two bytes
of padding after `hsty`), every checksum is the standard one (`head`'s
taken with `checkSumAdjustment` nought) and `checkSumAdjustment` is right.

The ROM finds a table by a linear search of the directory for its tag
(`FindFontTable`, ROM 0x000aeba0) and never looks at a checksum.

## What the ROM reads

`OpenFont` → `SFNTOpenFont` → `FindSFNT` (ROM 0x000aec68): the family's
data for the face (`ChooseStrike`, ROM 0x000ae274 - bold italic, italic,
bold or plain, whichever the family has; the faces the data has are then
not synthesised), then:

1. **`cmap`**: the subtable whose *platform id* equals the family's
   `encoding` slot (0 in every family); its format picks the mapping -
   `MapFormat0/4/6` (ROM 0x000ae5b4, 0x000ae5d4, 0x000ae7c0; format 2 maps
   everything to glyph 0). A family with a `badFontMap` slot gets
   `MapFormat4Patched` (ROM 0x000ae7f0), which maps the same.
2. **`bloc`**: absent → a metric-only font (`SetupWidthsFont`, below).
   Present → `LocateEntry` (ROM 0x000aebec) picks the strike, and the
   strike's line metrics become the open font's.
3. **`bdat`**: the glyphs, reached through the strike's index subtables
   (`SFNTGetGlyphInfo`, `SFNTGetGlyph`, ROM 0x000ae958, 0x000aea58).

`head`, `hhea`, `hmtx` and `hsty` are read only for a metric-only font.
Nothing in the ROM reads `maxp`, `name` or `post`, nor a bitmap font's
`hhea`/`hmtx`/`hsty`.

The alert manager has a reader of its own, below QuickDraw
(`TAlertGlyph::InitGlyph`/`GetAlertGlyphWidth`, ROM 0x000303c0,
0x000304d8): it takes the *first* `cmap` subtable whatever its platform,
the same `LocateEntry`, and the strike's `maxBeforeBL`/`minAfterBL` as its
ascender and descender (see `docs/alert/README.md`).

## `bloc`: the strikes

    +0   version        0x00020000
    +4   numSizes       the strikes
    +8   bitmapSizeTable[numSizes], 0x30 bytes each:
           +0x00 indexSubTableArrayOffset   (from the start of 'bloc')
           +0x04 indexTablesSize            (the array and its subtables)
           +0x08 numberOfIndexSubTables
           +0x0c colorRef
           +0x10 hori: sbitLineMetrics (12 signed bytes)
                 ascender, descender, widthMax, caretSlopeNumerator,
                 caretSlopeDenominator, caretOffset, minOriginSB,
                 minAdvanceSB, maxBeforeBL, minAfterBL, pad1, pad2
           +0x1c vert: sbitLineMetrics
           +0x28 startGlyphIndex, +0x2a endGlyphIndex   (halfwords)
           +0x2c ppemX, +0x2d ppemY, +0x2e bitDepth, +0x2f flags

then, strike after strike, each strike's index subtable array (8 bytes an
entry: first glyph, last glyph, the subtable's offset from the array)
followed by its subtables. A subtable starts `indexFormat`, `imageFormat`,
`imageDataOffset` (into `bdat`), then its offsets.

What the ROM reads of a strike:

- `LocateEntry` compares the wanted size (rounded) with **`ppemX`**
  (+0x2c) and considers only strikes whose `bitDepth` (+0x2e) is 1,
  taking the strikes to be in order of size: it stops at the first one
  further from the size than the one before and answers that one. (ROM
  quirk, kept: its entry pointer moves on only from a one-bit strike, so
  after a deeper strike it looks at the same entry for the rest of the
  count. All the ROM's strikes are one bit deep.) `IsSizeAvailable`
  (ROM 0x000aef10) asks whether a strike's `ppemX` is exactly the size.
- `FindSFNT` takes `hori.ascender` (+0x10) as the ascent, minus
  `hori.descender` (+0x11) as the descent, `hori.widthMax` (+0x12), and
  `minOriginSB`, `minAdvanceSB`, `maxBeforeBL`, `minAfterBL` (+0x16..+0x19);
  it answers `ppemX` as the strike's size (which `SFNTOpenFont` compares
  with the size wanted to decide whether to scale).
- `SFNTGetGlyphInfo` checks the glyph against `startGlyphIndex`/
  `endGlyphIndex` (+0x28/+0x2a) and walks the subtable array while an
  entry's first glyph is at or below the glyph (no count: the range check
  is what keeps it inside), then reads the subtable by `indexFormat`:
  1 (32-bit offsets), 2 (a constant image size times the index) or 3
  (16-bit offsets). A glyph outside the strike is drawn as glyph 0.

In the ROM's fonts **every strike is alike in its shape**: one index
subtable covering glyphs 0 to 257, **index format 3, image format 1**,
`bitDepth` 1, `flags` 1 (horizontal), `colorRef` 0, `ppemX` = `ppemY`.
Nothing else occurs - no index format 1 or 2, no image format 6.

Two things about those subtables are not what the format's description
would lead one to expect, and the text form keeps both:

- `maxp` says 244 glyphs, and glyphs 244 to 257 have **offset nought** -
  so they point at glyph 0's image (the missing-glyph box) rather than
  being empty. `cmap` never maps a character to them.
- The offset after the last glyph - the end of glyph 257's image, which
  nothing reads - is **nought** too, not the end of the images. (A
  subtable's lengths therefore cannot be worked out from its offsets;
  each image's length is what its metrics say, which is how the ROM reads
  it anyway.)

The **vertical** line metrics are never read, and hold what look like
leftovers of the tool that made the fonts: `minOriginSB` 119 or 100 and
`minAdvanceSB` anything from -128 to 124, varying strike by strike.

## `bdat`: the glyph images

    +0   version        0x00020000
    +4   the images, strike after strike, each strike's in glyph id order,
         one after the other with no padding

**Every image is format 1**: small metrics - five bytes: height, width,
bearing x (signed), bearing y (signed), advance - then `height` rows of
`(width + 7) / 8` bytes, each row starting on a byte (unused bits at the
end of a row are nought in the ROM's fonts, but the text form keeps
whatever is there). The ROM also reads format 6 (big metrics, eight bytes,
of which it uses the first five as it does format 1's) but no font has
it. The advance the ROM draws by is the image's own fifth byte, plus the
synthesised faces' extra width (the style table, `docs/qd/README.md`).
`SFNTGetGlyph` answers the bearing y raised by a superscript's or
subscript's baseline shift; the alert reader clamps a negative bearing x
to nought and cuts the rows that would fall below its descender.

Every strike has 244 images (glyphs 0 to 243). Glyph 1 is an empty
image (0 x 0, advance 0), which character code 0x00 maps to.

## `cmap`

`version` 0 and one subtable: platform 0 (Unicode), encoding 0, **format
4**, language 0. Every segment has `idRangeOffset` nought (no glyph id
array): the codes are cut into maximal runs of consecutive codes whose
glyph id is the code plus the same delta, one segment a run, then the
0xFFFF segment (delta 1). The bitmap fonts have 47 segments over Mac
Roman's repertoire in Unicode (0x20-0x7F, the Latin-1 letters, the
typographic punctuation, the maths signs, fi/fl at 0xFB01/2, Apple's
private-use characters at 0xF714-6, 0xF7FF and 0xFC00-0xFC0F); the metric
fonts have 116, their glyphs being in another order. Control codes are
mapped too: in the bitmap fonts 0x00 to 0x0C go to glyphs 1 to 13 and
0x0D to 0x0F to glyphs 2 to 4 - the space, `!` and `"` - which the text
drawing code never asks for.

`MapFormat4` (ROM 0x000ae5d4) finds the first segment whose end code is at
or past the character and answers nought when its start code is past it;
it honours `idRangeOffset` (glyph array) segments though no font has one.

## `head`, `hhea`, `hmtx`: the metric-only fonts

`SetupWidthsFont` (ROM 0x000ae388) and `SFNTGetWidthsInfo` (ROM 0x000ae52c):

- `head` `unitsPerEm` (+0x12): the scale, size / unitsPerEm - 2048 in all
  13 fonts;
- `head` `macStyle` (+0x2c): the faces the data already has, taken off
  the ones to synthesise - 0, 1, 2 and 3 for each family's plain, bold,
  italic and bold italic (the bitmap bold font 3e21c1 says 0, but a
  bitmap font's `head` is not read);
- `hhea` `ascender`, `descender`, `lineGap`, `advanceWidthMax` (+4, +6,
  +8, +10), scaled: the ascent, minus the descent, the leading, the widest;
  `numberOfHMetrics` (+0x22): 229 in the metric fonts;
- `hmtx`: a glyph's advance is its entry's advance width (the last entry's
  for a glyph past `numberOfHMetrics`), plus the `hsty` extra, times the
  scale. The metric fonts' widths are the real ones (Helvetica's space is
  569 units, 0.278 em).

`head`'s dates are one 32-bit Macintosh date written into both halves of
the 64-bit field: 1996-11-01 for the System fonts, 1995-07-06 for Fancy
and Simple, 1993-06-11 for the printer faces. In the bitmap fonts `hhea`
(ascender 2048, descender -512, 244 metrics) and `hmtx` are placeholders,
and `hmtx` in particular holds nothing like widths (advances such as
14336 and 55748, repeating every fourteen glyphs) - harmless, since only
a metric-only font's is read.

## `hsty`: Apple's horizontal style table

22 bytes: a 16.16 version (1.0) then nine signed halfwords in design
units - the extra advance a synthesised face adds to every glyph. It is
read only by `SetupWidthsFont`, with the faces the open font still has to
synthesise once `FindSFNT` has kept only face bits 0-3, 7 and 8 and
`macStyle`'s have been taken off:

| Offset | Face | Bitmap fonts | Metric fonts |
|---|---|---|---|
| +4 | none left (plain) | 0 | 0 |
| +6 | bold (bit 0) | 169 | 170 |
| +8 | italic (bit 1) | 0 | 0 |
| +10 | underline (bit 2) | 0 | 0 |
| +12 | outline (bit 3) | 129 | 128 |
| +14 | shadow (bit 4) | 169 | 170 |
| +16 | condense (bit 5) | -170 | -170 |
| +18 | extend (bit 6) | 169 | 170 |
| +20 | superscript (bit 7) | 0 | 0 |

The plain entry is used when no face is left; otherwise the entries of
the faces left are summed (one halfword per face bit, starting at +6).
Shadow, condense and extend are never reached - `FindSFNT` masks those
bits off first - and a **subscript** (bit 8) reads the halfword at +22,
**past the end of the table**: in a metric-only font that is the padding
after it (the table is the file's last), nought. (ROM quirk, kept by the
reconstruction.) 170 units is a twelfth of an em: bold printer text is
a twelfth of an em wider per character.

## `maxp`, `name`, `post` (bitmap fonts only, never read)

`maxp` version 1.0: 244 glyphs, `maxZones` 2, the rest nought. `post`
version 3.0 (no glyph names), italic angle -13, underline at 200, 50
thick. `name` (format 0): seven Macintosh Roman records, the same in all
five fonts - "Copyright Apple Computer, Inc. 1991-1992", "Roman",
"Regular", "Apple Roman Regular", "Roman Regular", "Version 1.0",
"Roman-Regular" - so the fonts do not name their families; the family
frames do.
