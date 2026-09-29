# QuickDraw

Reverse-engineering notes on Newton's graphics library.  It is Apple's
QuickDraw rewritten in C for the ARM: the same data structures (Point,
Rect, Region, PixelMap, GrafPort - `headers/QD/NewtQD.h`, now `src/ddk/`)
and the same call names (`SetRect`, `UnionRgn`, `FrameRect`, ...), with
the port's `QDProcs` bottlenecks (`StdRect`, `StdRgn`, ...) and a
`TQDLibraryDriver` protocol that exports the library to drivers.  The
ROM's QuickDraw lives at 0x0030f000-0x00318000 (regions, rectangles,
polygons, pictures, the standard procs) with the blitting and text below.
Reconstructed source: `src/qd/`.  How each fact was established is
stated with it.

## Layout: APCS word alignment

The ARM Procedure Call Standard aligns every structure to a word, so a
`Region` (`StructSizeType rgnSize; Rect rgnBBox;`) has its `Rect` at
offset 4, not 2: `NewRgn` 0x003150b0 allocates 12 bytes, stores `rgnSize
= 12` and clears the Rect at +4, and every row-walking function starts
at +12.  `Picture` and `Polygon` are laid out the same way.  A host
compiler packs the shorts, so `tools/newton-rom/sync_ddk_headers.py`
patches the three structures with an explicit `short filler`
(`src/ddk/NewtQD.h`).  Ghidra's decompiler, given the unpadded DDK
struct, labels the fields two bytes off (`rgnBBox.left` for the ROM's
`top`); the ROM's offsets are what count.

## Rectangles (`src/qd/Rects.h`)

`SetRect(r, left, top, right, bottom)` 0x00314068 takes the Macintosh
argument order; `OffsetRect`/`InsetRect` 0x003140a0/0x003147d0 take
(dh, dv).  `SectRect` 0x00314e28 is `RSect` 0x00314988 with two
rectangles - `RSect(result, count, ...)` intersects any number of them,
answering false and an empty result when they do not all overlap.
`UnionRect` 0x00314e40 ignores an empty operand; `JoinRect` 0x00314f18 is
the same spelt for two non-empty ones.  `PtInRect` 0x003142b4 tests the
pixel below and to the right of the point (`top <= v < bottom`).
`MapCoord` 0x0030fedc (`dstStart + (x - srcStart) * dstSize / srcSize`,
rounded half away from zero) is behind `MapPt` 0x0030fd70, `MapRect`
0x0031431c and `ScalePt` 0x0030fe1c.

## Regions (`src/qd/Regions.h`)

A region is a handle to `rgnSize`, the bounding box and, unless the
region *is* its box (`rgnSize` 12), rows of shorts: a y, the x positions
at which membership flips from that row down, `0x7fff` ending the row,
and a final `0x7fff` ending the region.  Each row is a *change list*
against the rows above it - the Macintosh region format.  The union of
(0,0,10,10) and (5,5,15,15) is 46 bytes: rows `0: 0 10 | 5: 10 15 |
10: 0 5 | 15: 5 15 |` then the end marker (`test_Regions` checks these
bytes).

The operations (`SectRgn` 0x00315270, `UnionRgn` 0x00315284, `DiffRgn`
0x00315298, `XorRgn` 0x003152ac, `InsetRgn` 0x00315144, and `DoRgnOp`
0x00315f80 for the library driver) shortcut the easy cases (equal
regions, an empty operand, boxes that do not meet, two rectangles) and
otherwise run `RgnOp` 0x003162cc:

1. `Expand` 0x00315b68 gives a rectangular operand rows in a stack buffer
   (`top: left right | bottom: left right |`), so that one form is walked.
2. The rows of both regions are walked in y order.  Each region's
   current scanline (its x transitions) is kept by XOR-ing its rows in
   turn (`XorScan` 0x00315c40 merges two sorted transition lists,
   dropping the pairs that cancel); at every y where either region
   changes, the two scanlines are combined by the operation's scan
   procedure - `ShareScan` 0x00315e10 with flags (-1,-1) for union,
   (0,0) for intersection, (0,-1) for difference, `XorScan` for xor,
   `InsetScan` 0x00315cd8 for an inset (each span narrowed or widened,
   widened spans that touch merging).
3. The result scanline is XOR-ed with the previous result (the change
   between them) and each change span is written out as two points
   (y, x1), (y, x2) into a handle that grows by 0x100 bytes as needed
   (or stops, for `TrimRect`).  The number of points is answered.
4. `SortPoints` 0x00316a2c (a quicksort by y then x) and `PackRgn`
   0x0031677c turn the points back into the format: four points make a
   rectangular region, more make rows, none an empty one; `CullPoints`
   0x00316a40 first drops pairs of equal points (a transition twice)
   where mapping can produce them.

The scratch scanline buffers come from `QDNewTempPtr` 0x0031360c (a 1 KB
scratch area in the Newt globals before the heap; NOT YET) and are
halved on failure down to 200 bytes.  `InsetRgn` runs `RgnOp` twice: once
with dh, then, after transposing every point (v and h swapped) and
re-packing, with dv - the vertical inset done as a horizontal one on the
transposed region, which the second transposition undoes.  `MapRgn`
0x00315728 writes the region out as points (`PutRgn` 0x003161ac,
`PutRect` 0x00314884), maps each with `MapPt`, sorts, culls and packs.
`TrimRect` 0x0031586c intersects a region with a rectangle into a
24-byte point buffer: four points mean the intersection is a rectangle
(answer 0, the rectangle replaced), none an empty one (negative), more
a complex shape (positive).

Membership and drawing scan-convert a region into a bit mask one pixel
row at a time: `RgnState` (0x2c bytes: the region, the next row, the mask
and its word count, the rows the mask holds between, the x range and
origin, the pixel shift and depth) is set up by `InitRgnRec` 0x003169a8
/`InitRgn` 0x00315978 in the *current port's pixel depth* (the tables at
0x00376fbc and 0x00377000 give the pixels per word as a shift and a mask
for depths 1, 2, 4, 8, 16, 32) and `SeekRgn` 0x003159c0 applies the rows
down to a pixel row, inverting the mask over each span, restarting from
the top when asked for a row above the current one.  `RectInRgn`
0x003152c0 scans the rectangle's rows until a mask has a bit.  The host
masks at one bit per pixel whatever the port (the depth only sizes the
mask for drawing, which the host blitter does not use);
`test_Regions` rasterises every result through `SeekRgn` and compares it
pixel by pixel with set arithmetic on the rectangles the regions were
built from (twenty rounds of random rectangles included).

`PtInRgn` 0x003155bc walks the rows too, flipping membership at each x at
or left of the point - but applies a row only to points *strictly below*
its y (`cmp v, y; ble return 0` at 0x00315618, `bgt` at 0x00315674), so
for a non-rectangular region it answers for the pixel row above the
point, one row off what `SeekRgn` draws (a rectangular region is tested
with `PtInRect`).  Kept as the ROM has it.  `EqualRgn` 0x00315408
compares the row data from its second short to one short past the region
(a slip; the host compares the rows exactly, `DEVIATION`).
`IsWideOpenRgn` 0x00316bc8 is nil or the rectangle beyond ±0x7ffe.

## Pixel maps, patterns, ports and the pen (`src/qd/Ports.h`)

The ROM is built with `QD_Gray`: a `PixelMap` is 0x1c bytes, ending in
the `grayTable` pointer the DDK's public `ConfigQD.h` never enables
(another sync patch defines the switch), and a `GrafPort` is 0x54 bytes
(`portRect` +0x1c, `visRgn` +0x24, `clipRgn` +0x28, `fgPat` +0x2c,
`bgPat` +0x30, `pnLoc` +0x34, `pnSize` +0x38, `pnMode` +0x3c, `pnVis`
+0x3e, `grafProcs` +0x40, `picSave`/`rgnSave`/`polySave` +0x44..0x4c,
`patAlign` +0x50) - the offsets every pen function uses (`PenSize`
0x00304158 writes +0x3a/+0x38, `PenMode` 0x0030418c +0x3c, `HidePen`
0x00304050 decrements +0x3e, `FrameRect` 0x003150a4 tests `grafProcs`
at +0x40).  Pixels are big-endian in their rows, 1, 2, 4 or 8 bits deep
(`pixMapFlags & 0xff`), 0 white and all ones black; `baseAddr` is a
pointer, a handle or an offset from the map as the top two flag bits say
(`GetPixelMapBits` 0x0028a538).

The five standard patterns (`stdPatterns` 0x0c104e3c) are 8x8 one-bit
PixelMaps in ROM (`whitePattern` 0x00376eb8 ... `blackPattern`
0x00376f58, rows 00, 88/22, aa/55, 77/dd, ff) reached through *fake
handles* - a master pointer in ROM (`NewFakeHandle`); `MakeSimplePattern`
0x00302de4 makes one in a 0x24-byte handle with the rows after the map
(`baseAddr` an offset).  `DisposePattern` 0x00303b00 leaves the standard
ones alone.  `wideHandle` (0x0c1027b4, a fake handle to the region at
0x00377a50) is the rectangle beyond +-32767: every new port's clip region.

`InitGraf` 0x002be600 clears `qdGlobals` (0x0c104e50, 0x3c bytes: a
version word, the screen's `PixelMap` from `InitScreen`, the open
polygon's and region's buffers), makes the patterns and the wide region,
opens the default port (`gGrafPort` 0x0c103a98, `NewtGlobals + 0x0c` once
a task has its own: `GetCurrentPort` 0x002be868) and registers the QD
protocols (NOT YET).  `OpenPort` 0x002be72c makes the two regions and,
like `InitPort` 0x002be88c, zeroes the port, gives it the screen's bits
and rect, `visRgn` the screen and `clipRgn` wide open (`InitPortRgns`
0x002be934), black over white, a 1x1 copying pen, and makes it current.
`SetOrigin` 0x002be758 shifts the bits' bounds, the port rect and the
visible region.

### 16.16 the way the ARM does it

`Ports.h` has four inline helpers - `ToFixed`, `AddFixed`, `ScaleFixed`,
`RoundFixed` - and **every place in the reconstruction that makes or
combines a 16.16 number goes through them**, in the view system as much
as here.

The reason is that QuickDraw works on numbers that go negative (a
coordinate off the left of the screen, a descent below the baseline, a
paragraph whose right edge is inside its left one) and on values
`FixedDivide` has already saturated to 0x7fffffff. Shifting a negative
signed long left, and overflowing a signed multiply or addition, are
both undefined in C++ and both perfectly ordinary on the ARM: a host
build with the sanitiser on stops dead where the machine would have
carried on. `(Fixed) width << 16` with a width of -3 is a crash;
`ToFixed(width)` is 0xfffd0000, which is what the ARM's `mov r0,r0,lsl
#16` leaves behind.

They are not a correction - they compute the same bits the ROM computed.
Writing the shift out by hand is the bug.

## Drawing (`src/qd/Draw.h`)

`FrameRect`/`PaintRect`/`EraseRect`/`InvertRect`/`FillRect`
(0x003150a4, 0x00314114, 0x00314120, 0x0031412c, 0x00314138 - `FillRect`
installs its pattern as the port's for the call) go through `CallRect`
0x00314844 to the port's `rectProc` or `StdRect` 0x00314170; the region
verbs likewise through `CallRgn` 0x0031582c to `StdRgn` 0x0031567c.  The
standard procs record into an open picture or region (NOT YET) and draw:
`frame` with `FrRect` 0x00314a80 (four strips the pen's width and height
thick, or the whole rectangle when the pen fills it) or `FrRgn`
0x00315ee0 (a rectangular region as `FrRect`; otherwise the region less
itself inset by the pen); the other verbs fill through `PushVerb`
0x00314cfc - frame and paint in the pen's mode and pattern, erase
`patCopy` with the background pattern, invert `patXor` with black, fill
`patCopy` with the port's pattern - and `DrawRect` 0x00314d78 /
`DrawRgn` 0x00315918, which blit the port's bits onto themselves clipped
by `visRgn`, `clipRgn` and (for a region) the region.  A hidden pen
(`pnVis < 0`) draws nothing.

Everything ends in `RgnBlt` 0x003172e0: the destination rectangle is cut
to the map's bounds and the three clip regions' boxes (`RSect`), a
single non-rectangular clip is first tried as a rectangle (`TrimRect`),
rectangular clips go to `BitBlt` 0x00287e20, and otherwise the regions
are scan-converted over the rectangle (`RgnState`, `LSeekMask`
0x003170d0 AND-ing their masks) and each row is transferred under the
mask.  The transfer mode's bits: 0-1 the operation (copy, or, xor, bic),
2 the source inverted (`notSrc...`), 3 the pattern for the source
(`pat...`); a pattern is expanded once per blit (`PatExpand` 0x0030356c)
aligned to the port's `patAlign`.  "Or" on a gray map is not a bitwise
or: every non-white source pixel replaces the destination pixel (the
ROM's per-depth loops clear the destination pixel first).  The ROM's
blitter works a halfword-aligned word at a time with per-depth loops
(`BBSrcCopy`, `BBSrcOr2`, ...); the host's `Draw.cpp` works a pixel at a
time with the same semantics (`DEVIATION`: the code, not the pixels),
copying a row ahead of writing it and bottom-up when the source lies
above the destination in the same map.  `CopyBits` 0x00289898 goes
through the port's `bitsProc` (`StdBits` 0x00288abc: clipped by the
port's regions and the mask) when the destination is the current port's
bits, else straight to `StretchBits`.
`test_Draw` checks every verb, mode, clip and depth pixel by pixel on
offscreen maps.

### StretchBits (`src/qd/Stretch.cpp`)

`StretchBits` 0x002ada5c is the ROM's blitter between maps of any depth
and rectangles of any size (maps of one depth and one size with no gray
table go to `RgnBlt`).  The destination is written a row at a time, top
down, a vertical Bresenham sum (from `-(srcH >> 1)`, `+= dstH` per source
row, `-= srcH` per destination row) saying which source rows land on it.
The first of them is shifted to a word boundary and converted to the
screen's kind (`SetupConversion` 0x002ae540: an indexed row through its
gray table, 16- and 32-bit direct colour - padded, unpadded, by
component - made four-bit grays, `PixelConvert.h`); the rest are folded
into it (`SetupCombine`: OR for one bit, the darker gray kept for
indexed rows, the direct `CombineDirect*to4` with the ROM's bugs kept -
eight-bit colours handed to `RGBtoGray`, the gray ORed in, the
by-component one comparing the wrong nibble); the row is taken across to
the destination's width and depth (`SetupStretchRatio`: 33
`Unscaled`/`Stretch`/`Shrink` routines by source and destination depth,
the ratio the smaller width over the larger, a running fraction from half
of it; a ROM bug kept: two bits to four unscaled uses `Unscaled1to2`);
and it is written into each destination row it covers through the mode
(`BlitModeCopy`/`Or`/`Xor`/`Bic` and the two- and four-bit `Or`s) under
the masks of the two clip regions and the mask region (`MSeekMask`
0x0011b93c; a region's mask is made at the current port's depth, as
`InitRgnRec` 0x003428f0 makes it - the host had made every mask one bit
per pixel, which lost pictures and text drawn into a complex visible
region of the four-bit screen).  The routines work on rows as the ARM's
memory holds them, through `LW`/`SW`; most are transcribed from Ghidra's
output by `tools/newton-rom/analysis/transcribe_words.py`.  NOT YET:
`TGrayShrink` 0x000e471c, which a one-bit map flagged 0x1000000 (an
anti-aliased ink word) is shrunk into four bits through - with no
implementation registered the ordinary stretch follows, as on the ROM
when there is none.

## Shapes (`src/qd/Shapes.h`)

Ovals, round rectangles and arcs are one shape: an oval `ovalWidth` by
`ovalHeight` set into the corners of a rectangle (`StdOval` 0x002fb2bc
passes the rectangle's own size, `StdRRect` 0x00318eac the corner size -
`CallRRect` 0x00318fcc turns square corners into a rectangle - and
`StdArc` 0x00285df8 adds the angles).  The ROM's rasteriser is `OvalRec`
(0x34 bytes) with `InitOval` 0x002fb7c8 and `BumpOval` 0x002fb698: the
row's left and right ends in 16.16 start at the flat top edge, half the
oval's width in from each side; an accumulator with first and second
differences - the squared height/width ratio (`FixedDivide`, `CompMul`)
and its double - is compared against a decision term that falls by
`4 * (row + 1)` per row (`row` from `1 - height` by two), the ends
widening by half a pixel while the accumulator is below it and narrowing
while above.  `PutOval` 0x002fb3e8 writes the shape's change points (the
row's ends wherever they move, a duplicate cancelling the point before
it) for an open region; the ROM's `DrawArc` 0x00285f50 draws the same
rows straight into the bits (the pen's inner oval for a frame, the
angles' slopes for an arc, the clip regions' masks).  The host packs
`PutOval`'s points into a region (`OvalRgn`) and draws it through
`DrawRgn` - the same pixels, the classic QuickDraw ovals (`test_Shapes`
pins the 8x8 circle and others) - and frames as the shape less the same
shape inset by the pen.  An arc of less than a full turn is drawn as the
ROM's `DrawArc` 0x002aaaf8 draws every shape: a row at a time within the
box the clip and visible regions, the port and the shape share, each row
of the oval (and of the inset one, framed) cut by the two lines from the
centre at the start and end angles - QuickDraw's, nought straight up and
clockwise - whose slopes are `SlopeFromAngle`'s scaled by the box's
aspect, their x followed down from the top.  A line whose ray points up
(its angle within 90 of the top) cuts the row; one pointing down does not;
at the centre row the two lines change places, and an arc lying wholly in
one half leaves the other out.  Each cut piece is a one-row rectangle
drawn through `DrawRect` (the ROM's `DrawSlab` 0x00347784 into the bits).
`DrawArc` draws nothing for a mode that is not a pattern mode.
`FixedMultiply` 0x0038b008 and `FixedDivide` 0x0038af20 (fplib assembly:
magnitudes, rounding half up, saturation) are `src/toolbox/FixedMath.cpp`.

Lines: `LineTo` 0x002d1e20 and `Line` 0x002d1e7c go through the port's
`lineProc` or `StdLine` 0x002d1eac, which records into an open picture
(NOT YET), polygon or region (`DoLine` 0x002d1f98, below), draws with
`DrawLine` 0x002d277c and moves the pen.  The ROM's `DrawLine` rasterises row by row from a fixed-point
slope (`FastLine` 0x002d209c for a one-pixel black or white pen); the
host stamps the pen (its size hanging below and right of each point, its
mode and pattern) along a Bresenham walk, clipped by the port's regions
- the odd diagonal pixel may differ (`DEVIATION`).

## Fonts (`src/qd/Fonts.h`)

A font family is a NewtonScript frame: the ROM has four in
`Rromfontlist` (0x63465d: espy, "Kräftig" = New York, "Einfach" =
Geneva, "Plakativ" = Handwriting; the packed font spec's family index)
and `vars.fonts` holds them by their family symbols - the ROM's globals
template (magic pointer 547) has `fonts: {_proto: {espy: @80, newYork:
@131, geneva: @104, handwriting: @571}}` (`FamilyNumToSym` 0x0017be98
names the packed family numbers; a family's `screenSym`, `'espyFont`,
is the font picker's) - where `GetFontFamily`/`SearchFont` 0x002bc358
look them up (the system font, `Rsystemfont` = `'espy`, when nothing
matches).  `GetFontSize`/`GetFontFace`/`GetFontFamilySym`
0x0017ccd8/0x0017dbf4/0x0017edd4 take a font spec apart.  A family holds `name`,
`macFontID`, `encoding` and `plainData`/`boldData`/`italicData`/
`boldItalicData`: each an `'sfnt` binary - a TrueType container whose
tables are `cmap`, `head`, `hhea`, `hmtx`, `hsty` and, for a screen font,
the bitmap strikes `bloc`/`bdat` (Apple's bitmap-only TrueType: a
bitmapSizeTable of 0x30 bytes per strike with its line metrics, glyph
range and ppem; index subtables mapping glyphs to `bdat` offsets; glyph
images of format 1 - small metrics, five bytes: height, width, bearing
x and y, advance - or 6 - big metrics, eight bytes - before byte-aligned
rows).  Times Roman has only widths (`hmtx`, for the printer).  espy
has strikes at 9, 10, 12 and 18 (`userSizes`) plus a bold data.

A `StyleRecord` (0x20 bytes: the family, the size in 16.16, the face,
a pattern) comes from a font spec (`CreateTextStyleRecord` 0x0025f980):
a packed integer - family index bits 0-9, size 10-19, face 20-29 - or a
frame `{family, size, face, color}`; no family means the user's
`userFont` preference, then the system font.  `OpenFont` 0x002bc514
(the ROM keeps four open fonts in a cache, `gFontGlobals`; NOT YET) calls
`SFNTOpenFont` 0x000af124: the data for the face (`ChooseStrike`
0x000af46c: bold italic, italic, bold, plain, in that order of what the
family has), the `cmap` subtable for the family's `encoding` (platform
id) and its mapping (`MapFormat0/4/6` 0x000af7ac..), the one-bit strike
nearest the size (`LocateEntry` 0x000afde4) with its line metrics, or
the widths font's metrics scaled from `head`'s units per em
(`SetupWidthsFont` 0x000af580), all into a `FontEngineInfo` (0xc4 bytes,
`FindSFNT` 0x000afe60) with the glyph functions `SFNTGetGlyphInfo`
0x000afb50 (the advance, through the index subtables; the missing glyph
0 for a glyph the strike lacks) and `SFNTGetGlyph` 0x000afc50 (the
metrics and bitmap).  The faces the data lacks are synthesised through
the style table at 0x00377324 (three bytes per face bit: an adjustment
index, the amount, the extra width): bold smears a pixel right and
widens by one, italic shears, underline takes an offset
and thickness, outline and shadow widen; superscript and subscript take
four fifths of the size and shift the baseline by three eighths of the
ascent.  `GetStyleFontInfo` 0x002bc17c answers ascent, descent, leading
and the widest glyph (espy 12: 12, 4, 0, 15).

### Making and changing a font spec

The other direction - putting a spec together, and changing one part of
it - is what the Styles slip is written in, and it is the same four
functions everywhere: `FamilySymToNum` 0x00179d90 and `FamilyNumToSym`
0x00179e68 between the family symbols and their numbers,
`MakeCompactFont` 0x0017a364 out of a family, a size and a face, and
`SetFontParms` 0x0017d164 which takes any of the three out of a frame
and leaves the rest alone.  `IntFontToFontParms` 0x00179358 goes the
other way, opening a packed integer out into a `canonicalFontSpec`
frame.

A family that has a number packs into an integer; one that has not (a
font from a package, say) stays a frame with the three slots.  The
ROM masks none of the three when it packs them, so a size or a face
beyond ten bits runs into the field above it.

`SetFontParms` on an ink word restyles the *word* rather than replacing
it with a spec - a word of writing carries its own measurements - which
is the ink area's business, so it goes out through a hook
(`gInkSetFontParms`, beside `gInkOpenFont` and `gInkFontParms`;
`ink/InkFont.cpp` installs all three).

The script-facing side is `views/FontNatives.cpp`: `GetFontFamilyNum`,
`GetFontFace`, `MakeCompactFont`, `SetFontFamily`, `SetFontSize`,
`SetFontFace`, `SetFontParms`, `GetDefaultFont`, `GetInsertionStyle`,
`GetTextFlags`, `GetRangeText` and `view:ChangeStylesOfRange`.

## Text (`src/qd/Text.h`)

The ROM draws text through *text objects* (`NewText` 0x00330e68: 0x50
bytes - the text, length, styles and run lengths, location, options,
flags and cached widths), laid out (`MeasureGlyphWidths`, `JustifyText`)
and drawn a chunk at a time into a one-bit slab that is blitted
(`DrText` 0x003313d4, `DrTextChunk` 0x00331794).  `DrawTextOnce`/
`MeasureTextOnce` 0x0032eec8/0x0032ef18 make one for a single use
(`DoTextOnce` 0x0032f2bc) and fill a `TextBoundsInfo` (0x1c bytes: left,
top, right, bottom, the leading, the advance and the vertical advance in
16.16; `MeasureOnce` 0x0025fd08 answers the width rounded).  The
drawing is the ROM's (`src/qd/DrText.cpp`, below); `test_Text` pins
"Hello Wg!" in espy 12, the bold strike underlined, Geneva 10 bold
smeared, and the outline, underline and italic faces.  The NewtonScript `FontAscent`/`FontDescent`
/`FontLeading`/`FontHeight` (0x001efeb4..) and `StrFontWidth` 0x001f2648
are here.  A `TextOptions` (0x1c bytes: the justification - the fraction
of the slack spread between the characters, spaces nine shares to a
character's one (`JustifyText` 0x0033057c) - the alignment - the fraction
of the slack before the text: a QD flush (`ConvertToQDFlush` 0x0017f0a0
maps the viewJustify text bits: vjRightH 1.0, vjCenterH 0.5, vjFullH
full justification) - the width to fit, a transfer mode, and the width
of what fit) lays a text out: `MeasureGlyphWidths` 0x00330948 cuts the
text object's length before the first character that would cross the
width.  Paragraphs: `TextBox` 0x0017dd5c/`TextBounds`/`DrawSimpleParagraph`
0x0017de74 wrap a rich string into a rectangle line by line
(`DrawSimpleLine` 0x0017e0e4: the text up to a carriage return, as many
characters as fit, cut back to a word boundary - `FindWordBreaks`
0x000ec09c - then `SkipUpToTwoSpacesAndCR`), the lines the font's height
apart, a box of no width or height taking the text's; the vertical bits
move the box down by the room left.  The NewtonScript `TextBox` is here.
`StyledStrTruncate` 0x001ecf64 (the NewtonScript `StrTruncate` and
`StyledStrTruncate`) cuts a string to a width with an ellipsis.

`FindWordBreaks` runs the locale's break table (`lineBreakTable` or
`wordBreakTable`, 'Intl binaries of the Macintosh Script Manager's kind):
its header gives a class table (a signed class for each Mac Roman
character) and a backward and a forward state machine, each a table of
row offsets and rows of signed bytes - the next state, with the top bit
marking the position.  The backward machine runs from the offset towards
the start of the text marking where the word may begin (skipped when the
offset is nearer the start than the table's +0xc says), the forward one
from there marking where it may end; a word that ends at or before the
offset starts the scan again after it.  In the U.S. tables a run of
letters is a word and so is a run of spaces.  (Host: with no table, the
words run between spaces.)  `DoTextOnce` reads its options' fitted width
as a selector first: 9 flags the object 0x40000 and 10 does so and drops
the options; with the flag 0x10000 as well (SetTextObjField's field 8)
`MeasureGlyphWidths` fits nothing.
### Drawing text (`src/qd/DrText.cpp`)

`DrText` 0x0035c530 lays the text object out and hands it a style run
at a time to `DrTextChunk` 0x0035c788, from the object's location plus
the justification's start, in the *options'* transfer mode (srcOr with
no options - the pen's mode does not apply).  `DrTextChunk` composes the
run as one block at the strike's own size:

- a one-bit slab from the pen plus the strike's `minOriginSB` (less
  italic's lean below the baseline) to the pen plus the run's advance
  (plus bold's smear, italic's lean above, the outline's spread, less
  `minAdvanceSB`), its left edge on a word of the port; from the strike's
  `maxBeforeBL` above the baseline down to `minAfterBL` below it and then
  `minAfterBL` again (or the underline's reach if that is lower);
- the glyphs ORed in at the pen, rounded, and their bearings, their rows
  clipped to `maxBeforeBL`/`minAfterBL`;
- bold: the whole slab smeared a pixel right, `fStyleAdjust[0]` times;
- italic: each row above the bottom one moved right by another
  `fStyleAdjust[1]` (8) sixteenths of a pixel, the whole pixels of the
  running sum - so even the baseline row moves (espy 12's by three);
- underline: `fStyleAdjust[4]` rows `fStyleAdjust[2]` below the baseline,
  with a gap wherever the ink in the rows around them, widened a pixel
  each way, would touch it (a descender breaks the line);
- outline and shadow (`fStyleAdjust[5]` 1 and 2): the slab smeared right
  and down by one more pixel than that into a block four rows taller, and
  the original, a pixel right and a row down, XORed out of it (a ROM bug
  kept: the first word is never ORed down);
- text whose style has a pattern that is not black (a font spec's
  `color`, `GetPattern` - registered by the views, which sit above
  QuickDraw here): `MakeGrayText` 0x0035dcd0 knocks a checkerboard out of
  the slab on a one-bit port, or makes it a two- or four-bit map in the
  foreground pattern's first gray;
- then `StretchBits` onto the port under its visible and clip regions,
  into the rectangle the slab comes to at the font engine's scale about
  the run's origin, and the pen moved on by the advance times that scale.

A strike at its own size in srcOr with none of the faces, on a port whose
regions are rectangles, skips the slab and ORs the glyphs straight into
the port's bits (one, two or four bits; a deeper port gets nothing - the
ROM has no loop for it).  A slab over 8000 bytes is drawn as two halves of
the run, recursively (a ROM bug kept: a single character too big is
not drawn at all and the pen is left where it was; and the port's
foreground pattern is left as the style's).  An object flagged 0x10000
with options is squeezed into their width - a pixel off each advance but
the first's in turn, the clip box widened by a sixteenth for the drawing.

Text is drawn at the scales its text proc is given (a picture played
into a rectangle of another size, the scaler): `DrText` opens each font
at the size times the scale, which the font engine answers with a strike
of that size or the nearest one and a ratio - which is the stretch the
slab is given.  A width to fit is measured with the stretched advances
too (0x0035be78), and `CalcTextAdvance` 0x0035b220 scales each run's sum
by its font's ratio.  Note that espy 24 at 1.0 and espy 12 at 2.0 are
not the same pixels: there is no 24-point strike, and the two ratios to
the 16-point one come out 1.49998 and 1.5.

The bounds are the text proc's operation 0x200: `CalcTextBounds`
0x0035b3f8 answers six words - the justification's start, the advance
across and down, and the greatest ascent, descent and leading of the
runs' fonts (a stretched strike's times its ratio) - and
`DispatchCalcBounds` 0x0035b32c makes the box of them about the text's
location (all noughts when there is no layout).

NOT YET: ink words, tabs.

### Text objects (`src/qd/TextObject.h`)

A text object is what every question asked of text goes through
(`NewText` 0x0035bfc4 makes one in a handle of its own; `DoTextOnce`
0x0035a418 makes one on its stack, not flagged as allocated, so
`DisposeText` 0x0035dc94 only throws its caches away -
`InvalCachedTextInfo` 0x0035b624).  The second byte of its flags word is
the *operation* - nought to draw, 0x100 the width, 0x200 the bounds,
0x800/0x1000/0x2000 CharToPoint, PointToChar, TextArrow - and the object
is handed to the port's `textProc` (`CallDrawText` 0x0035a60c at the
scales given, `DrawTextObj` 0x0035df74 at full size after clearing the
operation), or to `StdText` 0x0035b07c when the port has no procs or the
port is a printer's (pixMapFlags kind 0x200) and the operation is not
drawing.  So one proc sees all the text a port is asked about, which is
how a printer or an open picture sees it: `StdText`'s drawing is
`DoPutText` (recording, below) then `DrText` 0x0035c530.  On the host
the caches are never made (DEVIATION: the layout is worked out afresh
for each question, `TextLayout.h`); the layout's three numbers (0x400)
and `TextArrow` (0x2000) are NOT YET.
The DDK's `TextObjProc` takes the object as a `long`; a sync patch makes
it `Long`, the ARM's word, pointer-sized on a host.

## Pictures (`src/qd/Pictures.h`)

A NewtonScript picture is a *bitmap frame* `{bounds, bits, mask,
colorData}`: `bits` (and `mask`) a `'bits` binary, the ROM's `FramBitmap`
- the pixel map header without its base address (a word, unused; the
row bytes; a pad; the bounds - halfwords big-endian) and the rows from
byte 0x10 - `colorData` a frame `{bitDepth, cBits, colorTable}` or an
array of them, one per depth.  `TPixelObj` 0x0003e868 (0x50 bytes) holds
the frame locked and the pixel map over its bits (`FramBitMapToPixMap`
0x00041d40; `GetFramBitmap` 0x000419a4 picks the `colorData` entry of the
port's depth, else the nearest); `DrawBitmap` 0x0003ea68 copies it into a
rectangle (a rectangle of no width takes the bits' size); `Justify`
0x0018b5f0 places a picture's bounds in a box by the viewJustify bits (a
box of no size takes the picture's; centring never goes above or left of
the box); `DrawPicture` 0x0018b82c draws a bitmap frame so - mode 8
(patCopy) meaning the mask in srcBic then the bits in srcOr, a negative
mode the mask itself. `PtInPicture`
0x0003f3f0 asks a bitmap about a point, taken from the bitmap's own
origin, and is two functions in one: `PtInPicture(x, y, bitmap)`
0x0003f3c0 answers whether the point is in the picture, and
`GetBitmapPixel(x, y, bitmap)` 0x0003f3d8 answers the pixel under it.
A bitmap with a mask is the shape the mask draws, so that is what the
point is tried against (`PtInMask` 0x002af2c0, whose answer is 0 where
the mask is set and -1 where it is not); one without a mask is its own
shape, and a pixel that is not white is inside it (`PtInPixelMap`
0x002af130).  Asked for the pixel, the mask only says whether there is
one - outside it the answer is -1 - and the value comes from the bits
(`PtInCPixelMap` 0x002af1fc).  The exception handler around it is the
ROM's own: a picture that is not a bitmap throws out of `Init`, and a
Throw is a longjmp, which would otherwise leave the `TPixelObj` holding
the frame locked.  A `'picture` binary is a QuickDraw picture, played
by `qd/PicPlay.h` (below).

## QuickDraw pictures played back (`src/qd/PicPlay.h`)

A `'picture` binary is a Macintosh QuickDraw picture kept exactly as it
came, packed and big-endian: a size word, the frame at +2 (so not where
the ARM's `Picture` struct, word-aligned, would put it), then opcodes.
The ROM's own are the World Clock's world map (`Rworldmapbitmap`, a
version 1 picture of one PackBitsRect of a 360 x 179 bitmap) and the
clock, sun and moon icons.  `DrawShape` plays a picture shape into its
bounds and `DrawPicture(RefArg...)` a binary into a justified box, both
over `DrawPicture(PicHandle, Rect*, Boolean)` (ROM 0x003337fc), which:

- maps the frame onto the destination (a negative scale - an empty
  destination - draws nothing);
- saves the whole port, then starts it with an **empty clip**, a black
  pen, a white background, a one-pixel pen in patCopy and no pattern
  offset - so a picture without a ClipRgn opcode draws nothing at all
  (every real one starts with one, which is intersected with the clip
  the caller had);
- points `qdGlobals.fPicHandle`/`fPicOffset` at the bytes after the
  frame, where `StdGetPic` (the port's `getPicProc`) reads from, and
  calls `ParsePicCodes` (0x0033249c) until it answers 0;
- puts the port back.

`ParsePicCodes` reads one opcode - a byte in a version 1 picture, a word
kept word-aligned in version 2 (0x02ff; any other version ends the
picture) - and does what Apple's picture format says: the state opcodes
(clip, patterns, pen, text state, Origin, the RGB colours turned into
gray patterns by `GetStdGrayPattern`/`MakeSimpleGrayPattern` in
`Ports.cpp`), lines, the rectangle family (the low three bits the verb,
bit 3 "the same rectangle again"), polygons and regions (read by
`GetPicHandle` into a handle two bytes bigger, the box at +4 where the
ARM's structs keep it), comments, and the reserved ranges skipped by
their lengths.  Bitmaps and pixel maps (0x90-0x9f) are `GetPicBits`
(0x003346b4): it works out which rows of the source the destination's
part inside the visible and clip regions needs, unpacks only those
(`UnpackBits`, or `UnpackWords` for 16-bit pixels) into a temporary
buffer and hands a pixel map over them to `CallBits`, which does the
scaling.

Things the ROM does that are worth knowing: an exception while
`GetPicBits` reads the rows is swallowed (the picture simply ends) and
the row buffer is then not given back; a destination with no height
also ends the picture; and the reserved opcodes 0x6d-0x6f are read as
eight bytes where Apple's format gives them four (kept).

On top of Apple's opcodes the Newton has curves (0x0c80-0x0c84 and
0x8088-0x808c, a curve read and mapped *twice* before `CallCurve`),
paths (0x8190-0x8194) and styled text (0x81a0 options, 0x81a1 a style,
0x81a2 the style runs, 0x81a3 the text, 0x81a4 the families of the
styles that name theirs).

DEVIATION: the host reads every word through `toolbox/ByteOrder.h`, and
turns the halfwords of each rectangle, region and polygon read out of
the picture into its own order.

Text is drawn as a text object through `CallDrawText` at the picture's
text scales (the frame's scale times the last `TxRatio`).  The old
opcodes' `LongText` family (0x28-0x2b) is Mac Roman characters in a
style `TxFont` (a Mac font id, `SearchFont`), `TxSize` and `TxFace`
build at PicPlay +0xd4, with options of their own at +0xb8 that are
nought but the transfer mode.  The Newton's: 0x81a0 is the options
(+0x58, `kPicDefaultTextOptions` 0x00380ca0 to begin with - srcOr);
0x81a1 one style (+0x74); 0x81a2 several, with each run's length and
style index (the pointer blocks +0x94/+0x98, the styles in a temporary
block at +0x9c, each family a Mac font id or 0x800000 for one the
picture carries); 0x81a3 the location, flags (0x80 several styles, 0x40
options, 0x20 a 0x81a4 follows) and the UniChars, drawn by `DrawPicText`
0x003336ec (the options' width scaled by the picture's horizontal scale
meanwhile) unless 0x81a4 follows; 0x81a4 gives each 0x800000 family of
0x81a2's styles a block of its own - a length halfword and the bytes,
the form an ink word takes as an integer family - and draws.
`TextCleanup` 0x00333cd0 gives back what they allocated.  `TxMode` sets
both kinds' transfer mode.  ROM bugs: with the procs (PictToShape)
nothing is given back; with several carried families the ROM gives back
each one's block though all but the first are inside the first
(DEVIATION: the host gives the block back once); a single style's
carried family is never filled in, so the text is drawn with the
integer 0x800000 as its font (the host draws nothing).

Curves (0x0c80-0x0c84, and 0x8088-0x808c "the same curve") are read into
PicPlay +8, mapped onto the destination *twice* (`MapCurve`: a picture
drawn at another size has its curves scaled twice over - ROM bug kept)
and handed to `CallCurve`; paths (0x8190-0x8194: the handle's size and
bytes) are mapped once and handed to `CallPaths`.  Neither asks the
procs, so they are drawn even while a picture is being made into shapes.
ROM bug kept: `StdCurve` records the curve the picture already has as
0x0c88 + the verb, which playback reads as a reserved opcode of 0x18
bytes, so a curve drawn twice running leaves a picture that cannot be
read past it.

A pixel pattern of type 1 (in 0x12-0x14) is a pixel map of its own:
`GetPicPixPat` 0x00333dc0 reads its header, a colour table for an
indexed one (made a gray table of its depth, `GetPicGrayTable`) and the
rows - as they are, or packed a row at a time (`UnpackBits`; 16-bit
pixels packed as words, `UnpackWords`) - into a pattern whose pixels
follow its header, and `ConvertPixPat` 0x00334244 makes it the screen's
kind in place: one bit stays; indexed pixels go through the gray table
(`ConvertIndex2`/`ConvertIndex4` keep their depth, `ConvertIndex8to4`
packs eight-bit ones into four); direct 16- and 32-bit pixels (with a
pad byte, without one, or a component plane at a time) become four-bit
grays two to a byte (`qd/PixelConvert.h`, 0x00074c08-0x000755e0, through
`RGBtoGray`).  ROM bugs kept: `ConvertIndex8to4`'s first pixel of each
pair takes its gray's low four bits; a row that is not a multiple of four
bytes is laid out two bytes further on than the pattern's row bytes say,
so a one- or two-bit pixel pattern comes out skewed; and a four-bit
pattern recorded (`PutPixPat`: a gray ramp, white first, for its colour
table) comes back through `RGBtoGray` a shade out for some grays (3
comes back 2).

NOT YET RECONSTRUCTED: a picture drawn under a scaling transform
(`TQDScaler`).
`test_Ink`'s `TestInkWordPicture` records ink words carried in 0x81a4 and
plays them back.  `test_PicPlay` plays hand-written pictures (among them
pixel patterns of 32 and 8 bits), records the standard procs' scene, text
both ways (with bold and italic runs),
curves and paths, a four-bit pattern and arcs, and plays them back to the
same pixels;
`test_Views`'s `TestPicture` draws the ROM's world map.

### Pictures recorded (`src/qd/PicRecord.h`)

`OpenPicture` 0x00331980 makes the picture (a handle of 0x100 bytes to
begin with: the size word, the frame, `0x0011 0x02ff` - every picture the
Newton records is version 2) and the port's `picSave`, a 0x9c-byte
record of what the picture already says: the clip, the origin, the pen's
size, mode and patterns, the last rectangle, curve and oval size, the
text scales, options and style; and hides the pen.  Every standard proc
asks `CheckPic` 0x00335030 (a picture is open and pnVis >= -1; the
origin and clip written first when they changed), writes the pen state
its verb needs (`PutPicVerb` 0x00331d10 - frame the pen size and mode,
paint the mode and pattern, fill the pattern, erase the background -
only what changed), then its opcode; a rectangle the same as the last is
the opcode + 8 alone.  Everything goes through the port's `putPicProc`
or `StdPutPic` 0x00334e88, which grows the picture 0x100 bytes at a
time and on failure cuts it to an empty picture (size 0xffff).  Bitmaps
are recorded as the ROM's `StdBits` does (packed with `PackBits`
0x002aeed0 when the row is 8 bytes or more - with a one-byte row count,
a ROM bug kept).  `ClosePicture` 0x00331c94 writes the end and trims the
handle.

Text is recorded by `DoPutText` 0x0035a680, from `StdText`: a picture
made for the Macintosh gets TxFont/TxSize/TxFace and LongText every time
(the count a byte, though every character is written - ROM bug kept);
a Newton picture gets TxRatio when the scales changed, TxMode when only
the options' transfer mode did or else 0x81a0 (never remembered, so
written again every time - ROM bug kept), 0x81a1 when the one style is
not the one the picture has, or 0x81a2 for runs, then 0x81a3 and 0x81a4.
DEVIATIONS: a style's font pattern Ref and pattern pointer go into the
picture as their low 32 bits and are not read back; the options' last
word, which the ROM calls as a text getter when set, is ignored.

### A picture turned into shapes (`PictToShape`, `views/PictureShapes.cpp`)

`DrawPicture(picture, rect, toShapes)` answers the picture as an array
of NewtonScript shapes instead of drawing it.  The opcodes are played
exactly as for drawing - the port set up the same, the clip, patterns
and pen state followed, everything mapped onto the rectangle - but
`ParsePicCodes` hands each drawing opcode, with what it read left mapped
in the `PicPlay`'s `fProc*` fields, to the proc for its sixteen in the
`OpcodeProcs` table (0x00380a9c; the seventeenth entry for everything
from 0x100) instead of drawing it.  A state opcode under 0x20 calls its
proc twice: negated before it is played, as it is afterwards.  The
procs:

- rectangles, round rectangles (the oval size's *width* as the corners'
  diameter) and ovals: `MakeRect`/`MakeRoundRect`/`MakeOval`; arcs a
  `MakeWedge`; polygons `MakePolygon` - one of two points a `MakeLine`;
  regions `canonicalRegionShape` with a copy of the region; bitmaps a
  `MakeBitmap` the destination's size with the unpacked rows copied
  (scaled) into it and offset to the destination; text (0x28-0x2b and
  the Newton's 0x81a3) a text box as wide as the text plus five, from the
  font's ascent above the point to its descent below;
- lines are not shapes one by one: lines that start where the last
  ended are gathered into one open polygon (`fInkPoly`, an `OpenPoly`
  the lines are drawn into) until something else comes along
  (`FlushAnyInk`, which the state opcodes' negated call also runs);
  lines with an empty pen, a pen mode of 0x17 or more, no length, or
  wholly above and left of the origin are dropped.

Every shape goes through `storeShape`: it waits in `fShape` with a style
frame (`MungeStyleFrame`: a frame verb the pen pattern, pen width and
mode; paint the fill pattern; erase the background pattern as the fill
in mode 0; invert black in mode 2; fill the pen pattern in mode 8; a
bitmap its mode; text its font, left justified in black; a mode over 7
becomes 0).  A shape of the same class and bounds as the waiting one -
not a bitmap - is not added: only its style goes into the waiting one's,
so a rectangle painted and then framed is one rectangle with a fill and a
pen.  When a shape does go in (`flushShape`), its style frame goes in
front of it unless `StylesEqual` finds it the same as the style added
last.  The end of the picture flushes the last one.  Patterns are what a
script writes (`GetNSPattern`): 1 or 5 for all white or all black, a
one-bit pattern's eight rows as a `'pattern`, one gray all over as a
packed colour, else a `'grayPattern` of its pixels (the top half only
when the bottom half repeats it) - which `views`' `GetPattern`, now
reconstructed in full (the packed colours, `MakeGrayPattern`, the
`'ditherPattern` frames), turns back.

ROM quirks kept: `StylesEqual` finds two styles equal only when every
slot is the same integer (or the same region) - a pattern binary or a
font spec makes them differ, and so does being empty, so a style with a
binary in it is repeated before every shape; the text style starts at a
size of `0xc` (twelve sixty-five-thousandths, where its twin is made at
`0xc0000`), so text before any TxSize is sized nought; the Newton's text
waits for 0x81a4 when its flags say its families come later, and then
never reaches the proc, so it is lost; and the text block 0x81a3 reads
is never given back on this path.  `test_Views`'s `TestPictToShape`
turns a hand-made picture and the world map into shapes and checks that
`DrawShape` of them puts down exactly the picture's pixels.

### Making a bitmap and drawing into it

`MakeBitmap(width, height, options)` 0x0004173c makes an offscreen
bitmap: a `canonicalBitmapShape` whose `data` is a `'pixels` binary -
a PixelMap header with the rows after it, the map's `baseAddr` being the
offset from the header to them (`kPixMapOffset`), which is what
`MakePixelsObject` 0x000415a4 builds.  The row bytes are the width
rounded up to a whole word, multiplied by the depth; the options frame
may override them, give a `depth` (which must be a power of two) or a
`resolution`, and every slot that is not one of those is copied into the
shape.  A `store` asks for the bitmap to be kept on a store as a large
binary instead, which is NOT YET.

DEVIATION: the ROM's header is 0x1c bytes because a Newton pointer is
four; a host pointer is eight, so the header is written as a host
`PixelMap` and the offset is its own size.  Nothing outside reads the
header as bytes - a `'pixels` binary is cast straight to a `PixelMap*`
wherever it is drawn (`TPixelObj::Init`) - so the format only has to
agree with itself.

`DrawIntoBitmap(shape, styles, bitmap)` 0x0003eee0 draws into one: a
port is opened over the bitmap's pixel map, the shape is drawn at the
origin, and the port that was current is put back.  Two details matter
and are easy to miss: the port's copy of the map has its bits *pointed
to* rather than offset from it (`kPixMapPtr`), since the copy is not the
map inside the binary; and the port rect and the **visible** region are
set to the bitmap's bounds, because a freshly opened port's visible
region is the screen's, and without that nothing is drawn at all.

A bitmap whose `deviceRes` is given and is not 72 dpi each way is drawn
into through `DrawShapeScaled` 0x000df8a8: a transform of 72 by 72 onto
the resolution put in force under the shape's own style, the scaler
forced on (a 144-dpi bitmap takes a rectangle at twice the size, its
pen too - `test_Views`); otherwise the scaler is forced on around the
drawing (a ROM bug kept: a throw leaves it forced).

## Curves and paths (`src/qd/Curves.h`, `src/qd/Paths.h`)

A curve is a quadratic from a first point through a control point to a
last one, in 16.16.  The verbs (`FrameCurve` 0x002d1c7c ... `FillCurve`)
go through the port's `curveProc` or `StdCurve` 0x002d202c, which records
it (0x0c80 + the verb, `PutPicCurve` 0x003323c4 - the opcode + 8 alone
for the curve the picture has) and draws it: framed, as lines from its
first point (`FrCurve` 0x002d1dd8 halves it at its middle five times,
de Casteljau, 32 lines); otherwise the inside of the curve closed by a
line back to its start, a region (`DrawCurve`), if its bounds meet the
clip and visible regions.  `GetCurveBounds` 0x002d211c starts its
maximum at -0x7fa6 and never looks at a point for the maximum when it
lowered the minimum, so the first point never counts for the right and
bottom (ROM bug kept); `MapCurve`, `OffsetCurve`, `ScaleCurve`,
`SetCurve`, `EqualCurve`.

Paths are TrueType outlines: a handle of contours, each its point count,
a bit per point (set: off the curve) and the points in 16.16.  The path
walker (`InitPathWalker` 0x003279ac, `NextPathSegment` 0x00327a74,
`OnCurve`) turns a contour into lines (two on-curve points) and
quadratic curves (an off-curve point between two on-curve ones; between
two off-curve points the curve passes through their middle), starting at
the first on-curve point; it does not close the contour back to its
start.  `StdPaths` 0x00327e08 records (0x8190 + the verb, `PutPicPaths`:
the handle's size and bytes) and draws: framed, each contour from its
start (`FramePath`, `FrCurve` for a curve); otherwise the outlines made
into a region - even-odd, since a region is its inversion points - and
drawn (`DrawPaths`).  `MapPaths`, `OffsetPaths`, `ScalePaths`,
`GetPathsBounds` (a plain box, with the same -0x7fa6 start),
`CopyPaths` (on a failed resize a path of more than four bytes is
emptied, a smaller one copied regardless), `SizeOfPaths`, `DisposePaths`.
A sync patch makes the DDK's path words `Long32`, the ARM's word, since a
host's `long` may be wider.

`SetStdProcs` 0x002e45b8 fills every proc with the standard one (the
ROM's table at 0x00380bcc), which is what the scaler starts from when a
port has none.

### Pictures made (`MakePict`, `views/DrawShape.cpp`)

`MakePict(shapes, style)` (`FMakePict` 0x000dd6a0) records shapes into a
picture within their bounds: `CommonMakePict` 0x000dc8c0 (a view's
paragraphs first told to keep bounds of their own and refill their
caches, `SetStandAloneBoundsInViewsRecursively`) and `ROM_CommonMakePict`
0x000e3d20, which saves the clip and pen, opens a picture (for the
Macintosh when the style has a `macPict` slot), clips to the bounds,
draws the view (a view straight under the root erasing its box first) or
the shapes with the scaler forced on, closes the picture, and answers a
clone of `canonicalPictureShape` with the bounds and the picture's bytes
as a 'pictureData binary.  `MakeShape` of a view makes the view's
picture the same way.  The one ROM caller is the About slip's credits
view (the slip is magic pointer 152; after eight seconds idle it closes
its basic view and opens the credits view when the application's
`aboutInfo.credits` is a list): each line becomes a `MakeText` shape (a
line starting with '%' a heading), all of them one picture that the
view's `viewDrawScript` draws with `CopyBits` and its `viewIdleScript`
scrolls.  No application in the MP2x00 ROM has credits;
`src/host/demo/credits.ns` (ctest `host.NewtonCredits`) opens the slip
for one that has.  `CopyBits` goes through `DrawPicture(RefArg...)`
0x001897fc, which draws anything that is not a bitmap frame - a picture
shape among them - as a shape in its transfer mode.

## Polygons and recording (`src/qd/Polygons.h`)

`OpenRgn` 0x003150f4 makes a point buffer (the globals' `fRgnHandle`,
`fRgnOffset`, `fRgnSize` at 0x0c104e80-0x0c104e88, the port's `rgnSave`)
and hides the pen; every line drawn until `CloseRgn` 0x003154e4 goes
through `PutLine` 0x002d30a0, which appends the line's *inversion points*:
a horizontal line its two ends, any other its x on every row it crosses
(the slope in 16.16 from half a pixel in - a slope under 1.0 added once
more, one under -1.0 a pixel over), a pair (row, old x) (row, new x)
wherever the x steps and one for the lower end when it is not the last
x.  `CloseRgn` sorts and culls the points (a pair at one place cancels:
the outline's corners meet) and packs them (`PackRgn`), so a polygon's
region holds the pixels whose centres fall inside the outline - the
classic QuickDraw shapes; `test_Shapes` pins a diamond and a triangle.
`StdRect`, `StdOval` and `StdRRect` record their frame verb into the
open region likewise (`PutRect`, `PutOval`), so a rectangle or oval
framed between OpenRgn and CloseRgn becomes the region of its inside.

A polygon is a handle to `Polygon` {polySize, filler, polyBBox, points}:
`OpenPoly` 0x0030ff38 makes it (`fPolyHandle`/`fPolySize`, the port's
`polySave`) and hides the pen, `DoLine` appends the pen's location (first)
and each line's end, `ClosePoly` 0x0030ffa4 finds the bounds and cuts
the handle to size.  The verbs `FramePoly`/`PaintPoly`/`ErasePoly`/
`InvertPoly`/`FillPoly` 0x003102fc-0x0031032c go through `CallPoly`
0x0031010c to the port's `polyProc` or `StdPoly` 0x00310364: framed, the
outline as lines (`FrPoly` 0x0031014c - in an xor pen mode the ROM draws
every point's line twice, from the first, as reconstructed); otherwise
`DrawPoly` 0x00310204 records the closed outline into a region and
`DrawRgn`s it.  `OffsetPoly` 0x0031027c, `MapPoly` 0x00310084, `KillPoly`
0x00310278.

## The screen (`src/qd/Screen.h`)

The screen is the QD globals' `fScreenBits` PixelMap (0x0c104e54), set
up by `InitScreen` 0x001cec68 from the screen driver's `ScreenInfo`
(`SetupScreenPixelMap` 0x001ceee4: height, width, depth, resolution; the
row bytes the width rounded up to a 64-bit word's pixels times the depth;
the bits allocated once, big enough for either orientation).  The driver
is the `TScreenDriver` protocol (0x0037ee40-0x0037eee0: ScreenSetup,
GetScreenInfo, PowerInit/On/Off, Blit, Get/SetFeature - 0 contrast, 2
backlight, 4 orientation - AutoAdjustFeatures, DoubleBlit, Enter/
ExitIdleMode), the ROM's `TMainDisplayDriver` from the ROM extension
driving the LCD; the host's `THostScreenDriver` (`hal/host/HostScreen.h`)
keeps gray bytes and writes them out (`WritePGM`, `WritePBM`).  Drawing
to the screen is bracketed: the blitter's `QDStartDrawing`/`QDStopDrawing`
0x001cf1e0/0x001cf228 (called by `RgnBlt`, `DrawLine`, `DrawArc`,
`StretchBits`, the text - the host's from `RgnBlt`, where everything
ends up) lock the screen RAM and add the rectangle drawn to
`gScreenDirtyRect`; the views' `StartDrawing`/`StopDrawing`
0x001cf6b8/0x001cf704 (`TRootView::Update` around an update) hold the
lock across many, so the display is updated once, by
`UpdateHardwareScreen` 0x001cf35c blitting the dirty rectangle through
the driver (`BlitToScreens` 0x001cf3b8).  The ROM's screen update task
(`ScreenUpdateTask` 0x001cf4f0) does that every 33 ms when the LCD
semaphores say so; the host does it when the last bracket closes (the
semaphores stand in as a count).  `ReleaseScreenLock` 0x001cf7f8 drops the
lock however deep it went - the ROM unlocks its semaphore group over and
over until there is nothing left to unlock - and the event dispatch calls
it at the end of every event (`TNewtWorld::AEDispatch`).  That matters far
more on the host than on the Newton: `TNewtWorld::MainConstructor` takes a
bracket and never gives it back, which on the ROM only keeps the update
task out for a while, but on the host means the depth never returns to zero
and nothing is ever blitted.  Without it the OS drew a perfectly good
screen that no-one could see - the display only changed when something
called `UpdateHardwareScreen` itself, which is what `ScreenSnapshot` does,
so the snapshots looked right while the window sat frozen.  `GetGrafInfo` 0x001cf828 answers the
screen's map, resolution, depth and the driver's features; `SetGrafInfo`
0x001cedb0 sets contrast and orientation (the map re-made, its bits
cleared); `SetOrientation` 0x0020040c turns the screen and re-makes the
default port and the `screenWidth`/`screenHeight` globals (the tablet
and the gestalt NOT YET).  `test_Screen` drives it over a 64 x 48 host
display.

`ScreenNatives.cpp` is what a script may ask of the screen:
`GetLCDContrast` 0x00200634 and `SetLCDContrast` 0x0020030c over
`GetGrafInfo`/`SetGrafInfo`, `GetOrientation` 0x002003b4 the same way and
`SetOrientation` 0x002003dc through `SetOrientation` rather than
`SetGrafInfo`, because turning the screen moves the root view and the
ports with it.  The ROM's NewtonScript boot asks for the contrast while
it is setting its globals up, so these answer before the view system is
running; with no screen driver `GetGrafInfo` answers a contrast of 0 and
an orientation of 1.

## Not yet

Arcs of less than a full turn, the text, curves and paths of pictures,
`ScrollRect`, `ZoomRect`, the screen update task and the alert screen
info, the per-task globals, `TGrayShrink`, the font cache, the
`TQDLibraryDriver` protocol.

## Transforms (`qd/Transform.h`)

`view:DrawShape(shape, {transform: ...})` draws the shape somewhere other
than where its own coordinates put it.  The slot holds either
`[srcRect, dstRect]` - map the one rectangle onto the other - or
`[dx, dy]`, a pair of numbers, which `TStyleSave::SetStyle` turns into two
ten-by-ten rectangles offset by them: a transform that scales by one and
only moves.  `TTransform::Setup` (0x001973e8) works out the scale each way
and keeps both rectangles; `Scale` maps a point or a rectangle's two
corners through them.

That is how a list draws the hilite of any of its rows out of a single
shape.  `protoOverview`'s `hiliter` is
`self:DrawShape(shape, {transform: [0, index * lineHeight]})` - one row's
rectangle, moved down to the row that is hilited - and the country picker
in the Setup assistant is one of these.

The transforms in force are `TQDScaler::gScale` (0x00196018-0x001973c8).
`StartScaling` pushes a transform (the first makes the scaler: `Setup`
keeps the current port's procs - or the standard ones - and installs a
copy of them with its own in place of the eleven that take coordinates);
`StopScaling` pops one (the last deletes the scaler, which gives the port
its procs back); `ReplaceScaling` changes the innermost.  After each,
`RecalcTransform` 0x001970dc works out the one transform they come to:
from the first one's source rectangle, its corner moved by the first
one's offset and then by each later one's offset scaled by the scales
before it, at the product of their scales.

The scaler's procs (`ScaledRect`, `ScaledRRect`, `ScaledOval`,
`ScaledArc`, `ScaledLine`, `ScaledPoly`, `ScaledRgn`, `ScaledBits`,
`ScaledCurve`, `ScaledPaths`, `ScaledText`, 0x00196634-0x00196f8c) map
what they are given through that transform - a rectangle's corners
(`Scale`, a point rounded to the nearest pixel), a copy of a polygon,
region, curve or paths (`MapPoly`, `MapRgn`, `MapCurve`, `MapPaths`), a
bitmap's destination and mask, text's location and its options' width,
with text drawn at the scales times the transform's (a sixteenth less
with feature 1) - and hand it to the proc the port had.  For a frame the
pen is scaled too (`SetupScalingPen`, never below a pixel).  ROM BUG,
kept: the pen's height is its *width* times the vertical scale.  The
port's clip region is set by the drawing in its own coordinates, so
`SetupScalingRegions` maps it whenever it has changed and cuts it by the
clip the port had outside the scaling (kept aside the first time it
changes, taken up again when the clip goes back to it); with `mapVis`
the visible region is mapped through the first transform.  Nothing is
scaled while a picture, region or polygon is being recorded
(`SkipScaling`), unless `ForceScaling(1)`; `ForceScaling(2)` turns the
scaling off.  `GetActualClip`/`GetActualVis`/`ReplaceClip` answer and set
the clip really drawn through.

`views/DrawShape.cpp` draws a shape at the caller's origin only when no
transform is in force (the first transform holds the origin), and forces
the scaler on around `HitShape`, `PointInShape` and `DrawIntoBitmap`'s
drawing (so a region recorded there is mapped like the shape drawn) and
off around `MakeRegion`'s, as the ROM does.  `test_Shapes`'
`TestScaling` pins a rectangle, a frame (the pen bug), a clip and a region
recording under a stretching transform; `test_PicPlay`'s `TestScaledText`
text under the scaler; `src/host/demo/scaledmap.ns` (ctest
`host.ScaledMapDemo`) draws the World Clock's map at half size and shapes
with text doubled and stretched.
