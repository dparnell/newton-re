# Ink

Ink is what the pen leaves behind: the strokes as they were written,
kept rather than recognised.  The Newton keeps it in three places - as a
sketch on a page, as a word in a paragraph's text that nobody has
claimed, and as the picture a recogniser hands back when it is asked for
ink instead of letters - and all three are the same thing underneath: a
binary of compressed strokes.

## The three classes (`Ink.h`)

| class | what it is | `IsInk` | `IsRawInk` | `IsOldRawInk` | `IsInkWord` |
| --- | --- | --- | --- | --- | --- |
| `'ink` | raw ink, the older form | yes | yes | yes | |
| `'ink2` | raw ink | yes | yes | | |
| `'inkWord` | the ink of one word | yes | | | yes |

Raw ink is a sketch or a scribble - a drawing that stands on its own.
An ink word is the ink of a single word standing among real characters:
a paragraph's text has the character 0xf701 where it goes, and the rich
string keeps the word's bytes in its ink region beside the text
(`docs/frames/README.md`, "Strings and arrays").  `IsInkChar`
(0x001fe914) takes three characters, 0xf700 to 0xf702, but only 0xf701 is
an ink *word*, which is why `CheckAndDoJoin` tests for that one when it
asks whether two ink words could be joined.

## What an ink word says about itself (`Ink.h`)

An ink word is its compressed strokes followed by eight bytes of
information about the word.  Those eight bytes are what lets a line of
text be laid out around it without the ink being expanded first, and
what lets it be stretched to the size of the text it sits in rather than
redrawn.

	word 0:  31..22  the word's width
	         21..12  how far it rises above the baseline
	         11..2   how far it falls below it
	          1..0   the pen size, less one (1 to 4)
	word 1:  31..22  the x-height
	         21..6   the scale, a 16.16 Fixed to eight fractional bits
	          5..0   the type face

The face is squeezed into six bits by `GetRawFace` (0x0013ffc8): bold,
italic, underline and outline stay where QuickDraw has them and the two
script bits come down from 0x80 and 0x100 to 0x10 and 0x20; `GetQDFace`
(0x0013ffb8) puts them back.  `PackInkWordInfo` (0x0013ffd8) writes the
two words and `ExpandPackedInkWordInfo` (0x001400ac) opens them out into
an `InkWordInfo`, working out as it goes:

- the **font size** the x-height comes to - seven quarters of it
  (`GetInkWordFontSize` 0x0014003c) - and that size at the word's scale;
- the **pen width** that size wants (`GetStdInkWordPenWidth` 0x00140068):
  one pixel up to a size of ten, and then two more than forty divided by
  the size, so the pen thins out as the writing grows and never goes
  below two;
- the width, height, ascent, x-height and descent **at the word's
  scale**, the first three with the pen width added, because the ink is
  drawn with a pen of that width and spills half of it either side.

`GetPackedInkWordInfo`/`SetPackedInkWordInfo` (0x0014022c, 0x0014028c)
read and write those eight bytes where they are - the last eight of the
binary - so `SetInkWordPenSize`, `SetInkWordScale` and
`SetInkWordFontFace` change one field and leave the strokes alone.
`SetInkWordFontSize` (0x000dc180) is the odd one: it does not change the
word at all, it works out the *scale* that gets from the size the
x-height comes to up to the size asked for, so the ink is stretched into
the text rather than rewritten.

`AdjustInkWordXHeight` (0x00140940) mistrusts the x-height the recogniser
measured, because everything above is worked out from it.  For a word of
letters it only steps in when the word is more than four times as tall as
it is wide - a tall narrow scribble with no waist to speak of - and puts
two fifths of the ascent in instead.  For a view that expects numbers the
test is the other way about: a word whose x-height is more than three
fifths of its ascent and which falls less than a fifth of its height
below the baseline is one where the recogniser found no ascenders or
descenders to measure against, digits having none, so its x-height is too
big and fifty-five hundredths of the ascent goes in.

## Where the codec stops and the system begins (`InkCodec.h`)

Apple's side of the ink is three layers deep.  `InkCompress`,
`InkExpand`, `InkDraw` and `InkMakePaths` call `CSCompress`,
`CSExpandGroup`, `CSDraw` and `CSMakePathsGroup` (0x001543fc onwards),
which call `Decode` (0x001539c8) and `GenericCSCompress` (0x0015362c),
which call `EncoderOpen`/`Run`/`Close` and `DecoderOpen`/`Run`/`Close`
(0x0027f938, 0x0028240c).  The line between the third layer and the
fourth is where Apple's code stops and the CIC handwriting library's
begins, and it is already a stream interface: strokes in and bits out,
bits in and a stream of points out.

`TInkCodec` is that line made explicit.  It is not in the ROM - the ROM
has one codec and calls it - but it is the ROM's own seam, so the
reconstruction of the CIC codec (`CICCodec.h`, `TCICInkCodec`) is one
implementation of it and a modern one can be another, without the views,
the paragraphs or the stores knowing which is in use.

A decoder hands its points out through a callback, as the ROM's does:
`kInkBegin`, then a `kInkPoint` for each point of each stroke with
`kInkEndStroke` between them, then `kInkEnd`.  The ROM has three
callbacks over the same traversal - `PGCStorePointProc` builds strokes,
`PGCDrawPointProc` draws, `CSMakePathsGroup` makes paths - and passes
its decoder context as the callback's third argument, the callback
finding its own working store hanging off it; here the caller's
reference is passed as itself.  The points are in tablet units as 16.16
values; `PGCDrawPointProc` divides by `gTabScale` (8.0) to get pixels.

What cannot be swapped is the *reading* of ink already written: a note
written on a real Newton is in the CIC format for ever, so that decoder
has to stay whatever else is added.  `GetInkFormat` (0x00280950) reads a
format out of the first byte - the low nibble is 8 in every form the
codec writes, and then bit 7 marks one and bit 6 another, anything else
being the old uncompressed ink - so choosing a codec per object rather
than once for the machine is what the format was built for.
`InkCodecFor` does that: the first registered codec that says it can
read the format gets it.

## Reading a block of ink (`CICCodec.h`)

The bit stream is read least significant bit first within each byte, and
the bits of a value come out in that order too (`GetNBit` 0x00280d88).

**The code books.**  A book is eight tables laid end to end, each
starting with its own size in bytes, which is how `DcdrSelectCodeBook`
(0x00280df0) finds them.  A table's header says how many entries it has
and carries two escape values with the base each counts from - 30000
means "too big to hold, another word follows to be added to this base"
and -30000 the same downwards - and its entries are eight bytes each:
the value, the length of its code in bits, and the code.  The entries
are in order of length, so `DecodeWord_OLD` (0x00281c48) can read a bit
at a time and only look at the entries whose codes are as long as what
it has.  `DecodeWord_NEW` (0x00281b90) does the same over the two little
static tables in RAM: one says whether the next stroke is long, short or
the end of the group, and the other is the two-entry table a segment's
continue-or-stop word comes from.

There are two books.  Book 1 is for writing and book 2 for ink; book 3
is book 2's tables with book 2's step.  The step is what a decoded
number is worth - 0x800 for the writing book and 0x2000 for the ink one
- and the codec counts in thousand-and-twenty-fourths of a tablet unit,
so a step is two units in one book and eight in the other.

**A stroke.**  `ReadNewStroke` (0x00280f1c) reads the next stroke's kind
and moves the pen to where it starts.  The first stroke of a run carries
the format: a kind of 2 there is not the end of the group but the marker
for the newer format, and four bits follow saying how wide the starting
coordinates are (nothing, eight, twelve or sixteen bits) and which book
to read with.  The older format has no marker - book 2, nine bits each -
and one trick of its own: a first stroke that is short and starts at
(511, 511), which is no place at all, is a marker too, and the real
first stroke follows it.

A **short** stroke (`ReadShortStroke` 0x00281424) is written as it was
drawn: a step in x and a step in y for each point after the first, until
a word comes back 7.

A **long** stroke (`DecodeLongStroke` 0x00281dd0) is a chain of curved
segments.  `ReadSegmentNear` (0x00281240) reads where a segment ends and
four numbers that bend it, and `RestoreSegment` (0x00281a70) draws those
out into seventeen points by forward differences - the middle one first,
then eight forward and eight back, each from the one before by adding a
step, each step by adding a second difference, and that difference
itself growing by a fixed amount.  The two halves differ only in the
sign of that growth, so the curve is symmetrical about its middle; with
both bending numbers nought it comes out a straight line in sixteen even
steps.  Sixteen of the seventeen go out - the seventeenth is the next
segment's first - and the first of them is not the segment's own point
but the average of it and where the segment starts, which is what joins
one segment smoothly to the last.

**Thinning.**  The `group` a caller asks for is really a mode.  Mode 1
thins by hand: a point within a unit of the last one let through, in
both x and y, is dropped.  Anything else goes through `GetSkipPoint`
(0x0028153c), which cuts the plane into cells eight units square and
keeps only the point nearest each cell's middle - and holds three cells
back, because three in a row that step once in x and once in y are a
staircase across the diagonal and the middle of them is better thrown
away.  `ClearSkipPoint` (0x002819a0) drains what is still held when the
stroke ends.

## Writing a block of ink (`CICCodec.h`)

The encoder is the same machine backwards.  `PutBits` (0x00282aa0) lays
bits in least significant first, a piece at a time - as much of the
current byte as is left - and disturbs only the bits it writes, which is
what lets the encoder try a stroke two ways and keep the shorter.
`EncodeWord_OLD` (0x00282c04) writes a value through one of a book's
tables: between the two bases it goes out as its own code, and at or
beyond either base as that escape followed by what is left of it once
the base is taken away - which may itself be out of range, so it is the
same call again.  `EncodeWord_NEW` (0x00282d38) does the same over the
static tables, of which the encoder has its own pair, byte for byte the
same as the reader's.  `FindCodeWord` (0x00282bb8) is how an entry is
found: the decoder can walk the entries in order of code length, but the
encoder has only the value and has to look at all of them.

`QvantUN` (0x00282758) is the rounding everything goes through - a
length over a step, to the nearest whole one, a half away from nought -
and `EcdrSelectCodeBook` (0x0028294c) is `DcdrSelectCodeBook` the other
way about.  `WriteNewStroke` (0x00282d84) writes a stroke's kind and
where it starts, and settles the run's format on the first stroke;
`WriteShortStroke` (0x00283240) writes the points as they were drawn.

One ROM bug is kept and commented: a first stroke of the newer format
that starts exactly where the pen is - both steps nought - sets the
width of the coordinates to eight but never sets the byte that says so,
and writes whatever was in the register.

The rest of it is the fitting.  `EncoderRun` (0x002804f8) tells the
source to begin and then pulls points; a stroke of one point is a dot
and goes out as a short stroke, and anything longer goes to
`WriteLongStroke` (0x0027fffc), which grows the trace a point at a time
while a curve will still go through it, puts it back to the last point
that worked when one will not, fits again with no allowance at all -
which always answers - writes that segment, moves the points after it
down to the front of the trace, and begins again.

`TestStrokeSeg` (0x0027fde0) is the fit itself.  Each try finds where
the nine places fall on the stroke (`Repar` 0x00283424), turns those
into the four numbers (`RFFT_9_4`), puts the four back into the nine
places on the new curve (`RIFT_4_9`), and measures the nine against the
stroke again (`Tracing` 0x00283d9c) so that the next try looks in better
places.  It stops when the places it found and the places the curve puts
them agree to within the allowance and the stroke is not stretched by
more than about a sixteenth, or when the curve stops moving, or when
there is nothing left to fit.

`Repar` is the bridge between the two: the nine places are spread along
the *curve* and the stroke is a chain of straight steps, so the ratio of
the two lengths is worked out to twenty-four binary places by long
division, each place's distance is multiplied by it a byte at a time,
and the step that distance falls in is walked to.  Dividing into the
step is a long division done on the step's two sides at once, the
quotient never formed - its bits are used as they come out to add a
halving of each side.

`SegVectQuant` (0x0028279c) is the last touch.  The three numbers of a
coordinate - where the segment ends and its two bends - have each been
rounded to a whole step on their own, but the roundings are not
independent: the decoder works the other two numbers out from all three
at once, so a rounding that is worse on its own can come out better
together.  All twenty-seven ways of nudging the three by one either way
are tried against the fitted segment (`TryQuantVariant` 0x0028333c) and
the nearest is kept.

`TInkCodec::Encode` takes a *source* of points rather than a list of
strokes, which is the ROM's own layering: its encoder is opened on such
a proc too, and `GenericCSCompress` only puts `PGCGetPointProc` in place
of the default one to read a list of `TStroke`s.  So the codec knows
nothing about strokes, and neither does this area.

## Strokes, ink and the screen (`Ink.h`, `InkStrokes.cpp`)

`InkCompress` and `InkExpand` are where the strokes and the codec meet,
and the only part of this area that knows what a stroke is.
`PGCGetPointProc` hands the codec the points of a list of `TStroke`s and
`PGCStorePointProc` builds strokes out of the ones the decoder gives
back - the ROM's own arrangement, since its codec is opened on a point
proc and `GenericCSCompress` only puts `PGCGetPointProc` in place of the
default one.  Points cross in whole tablet units, so they are multiplied
by the tablet scale going in and divided coming out; both the ROM and
this take the usual scale of eight as a shift, because `FixedMultiply`
and `FixedDivide` overflow on a whole coordinate.

`TStrokesToInk` (0x00140608) and `TStrokesToInkWord` (0x001404f0) are
what the rest of the system calls.  Both move the strokes to the origin
before packing them - ink is kept where it was drawn, not where it is to
go - and answer the box the strokes came from, grown by the two pixels
the pen spills outside them (`InkBounds` 0x001a3728 is that growing on
its own).  A word gets one more step: `ScaleStrokesForInkWord`
(0x00140318) brings a word written larger than a line of text can hold
down to fit - two hundred and forty pixels across and sixty down are the
most, and whichever wants the smaller scale is the one used, so the word
keeps its shape - and the word's box then gets the user's pen width on
its bottom and right.

`InkDraw` (0x00140cd0) walks a block of ink into the current port: the
first point of a stroke moves the pen and the rest are lines from it.
`InkDrawScaled` is the ROM's `GenericCSDraw`, which takes the place and
the scale as 16.16 values.  The ROM keeps twenty points back at a time
and draws them in one go, which saves calls and nothing else.

`GetPackedInkWordInfoFromStrokes` (0x00140a4c) is what fills an ink
word's eight bytes in: the width and the height are the strokes' own box
with the pen's two pixels in them, the ascent is where
`WRecFindBaseline` says the short letters stand (held down to the height
in case it says something silly) and the x-height is how far the line
they reach up to is from that, the scale is the `inkWordScaling`
preference as a fraction of a hundred and the pen is `userPenSize`.

`FindBaseline` (0x00065b2c, `recognition/Words.h`) is the one that
answers the ascent, and the ROM's own function has two paths.  It asks
the CIC library's feature extractor first - `low_level` over the trace,
which among everything else says where the short letters stand and how
far up they reach - and when that cannot be done it falls back to the
strokes' own box, the word then sitting entirely above its baseline, and
answers 1 rather than 0 to say so.  That extractor is the handwriting
recogniser itself and is NOT YET, so the second path is the one taken:
an ink word made here is the right size and in the right place, but the
line its short letters stand on is the bottom of it rather than where a
reader would put it.

## NOT YET

The handwriting recogniser: `low_level` and `GetTraceFromStrokes`, which
is what would read a word rather than just measure it.

And everything that keeps ink for a view:
`MakeInkPoly`/`MakeInkWordPoly`
(0x001a31bc, 0x001a3250), `GetInkAt` (0x001a170c) and `NextInkIndex`,
`SplitInkAt` and `MergeInk` (0x001a2c60, 0x001a2fc4 - both expand the ink
to strokes, work on those, and compress the answer, which they can now
do), `AddInk` (0x001a2b70), `TParagraphView::InsertInk` and
`GetInkRefAndBounds`, `TEditView::HandleInk`, `TInkWordGlyph` (the glyph
an ink word draws as in a line of text) and `TLiveInker` (the ink that
follows the pen while it is still down, which is the other way the ROM's
draw proc can draw - `InkerLine` with a pen of its own).
