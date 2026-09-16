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
bits, else straight to `StretchBits` 0x00288eb4 - whose stretching and
depth-conversion tables are NOT YET (the host samples nearest-neighbour).
`test_Draw` checks every verb, mode, clip and depth pixel by pixel on
offscreen maps.

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
shape inset by the pen; arcs of less than a full turn are NOT YET.
`FixedMultiply` 0x0038b008 and `FixedDivide` 0x0038af20 (fplib assembly:
magnitudes, rounding half up, saturation) are `src/toolbox/FixedMath.cpp`.

Lines: `LineTo` 0x002d1e20 and `Line` 0x002d1e7c go through the port's
`lineProc` or `StdLine` 0x002d1eac, which records into an open picture,
polygon or region (NOT YET), draws with `DrawLine` 0x002d277c and moves
the pen.  The ROM's `DrawLine` rasterises row by row from a fixed-point
slope (`FastLine` 0x002d209c for a one-pixel black or white pen); the
host stamps the pen (its size hanging below and right of each point, its
mode and pattern) along a Bresenham walk, clipped by the port's regions
- the odd diagonal pixel may differ (`DEVIATION`).

## Fonts (`src/qd/Fonts.h`)

A font family is a NewtonScript frame: the ROM has four in
`Rromfontlist` (0x63465d: espy, "Kräftig" = New York, "Einfach" =
Geneva, "Plakativ" = Handwriting; the packed font spec's family index)
and the boot puts them, by `screenSym`, into `vars.fonts`, where
`GetFontFamily`/`SearchFont` 0x002bc358 look them up (the system font,
`Rsystemfont` = `'espy`, when nothing matches).  A family holds `name`,
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
widens by one, italic shears (NOT YET drawn), underline takes an offset
and thickness, outline and shadow widen; superscript and subscript take
four fifths of the size and shift the baseline by three eighths of the
ascent.  `GetStyleFontInfo` 0x002bc17c answers ascent, descent, leading
and the widest glyph (espy 12: 12, 4, 0, 15).

## Text (`src/qd/Text.h`)

The ROM draws text through *text objects* (`NewText` 0x00330e68: 0x50
bytes - the text, length, styles and run lengths, location, options,
flags and cached widths), laid out (`MeasureGlyphWidths`, `JustifyText`)
and drawn a chunk at a time into a one-bit slab that is blitted
(`DrText` 0x003313d4, `DrTextChunk` 0x00331794).  `DrawTextOnce`/
`MeasureTextOnce` 0x0032eec8/0x0032ef18 make one for a single use
(`DoTextOnce` 0x0032f2bc) and fill a `TextBoundsInfo` (0x1c bytes: left,
top, right, bottom, baseline, width, height in 16.16; `MeasureOnce`
0x0025fd08 answers the width rounded).  The host draws each glyph as a
region from its bitmap through `DrawRgn` at its bearing from the
baseline, advancing by the glyph's width, in the pen's mode with the
style's or the port's pattern - the same pixels for an unscaled strike
(`test_Text` pins "Hello Wg!" in espy 12, the bold strike underlined,
and Geneva 10 bold smeared).  The NewtonScript `FontAscent`/`FontDescent`
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
0x000ed674, the ROM's through the locale's lineBreakTable, the host's at
spaces - then `SkipUpToTwoSpacesAndCR`), the lines the font's height
apart, a box of no width or height taking the text's; the vertical bits
move the box down by the room left.  The NewtonScript `TextBox` is here.
NOT YET: ink words, scaled glyphs, persistent text objects, `StdText`
recording, tabs.

## Not yet

Arcs of less than a full turn, polygons, pictures, `OpenRgn`/`CloseRgn`,
`ScrollRect`, `ZoomRect`, the screen (`InitScreen`, `QDStartDrawing`),
the per-task globals, `StretchBits` proper, the font cache, text layout
(justification, wrapping), the `TQDLibraryDriver` protocol.
