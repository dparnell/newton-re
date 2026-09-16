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
0x003152c0 scans the rectangle's rows until a mask has a bit.  Ports are
NOT YET RECONSTRUCTED, so the host masks at one bit per pixel;
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

## Not yet

Ports and the current port (`GetCurrentPort`, `SetPort`, `OpenPort`),
pixel maps, patterns, the pen, the drawing bottlenecks (`StdRect`,
`StdRgn`, `FrRect`, `FrRgn`, `DrawRect`, `DrawRgn`, the blitter),
polygons, pictures, `OpenRgn`/`CloseRgn`, `ScrollRect`, `ZoomRect`, text
and fonts, the `TQDLibraryDriver` protocol.
