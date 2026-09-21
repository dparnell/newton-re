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

## NOT YET

The encoder.  `InkCompress` (0x00140b78) hands the strokes to
`CSCompress` (0x001543fc), which is `EncoderOpen`/`Run`/`Close`
(0x0027f938, 0x002804f8, 0x0027fae8) and the twenty-odd functions
between them - the segment fitting, the vector quantisation and the
code book selection that turn points back into the bits the decoder
reads.  Until it is reconstructed ink can be read, measured, scaled and
stored, but not made from strokes.

With it would come the rest: `TStrokesToInk`/`TStrokesToInkWord`
(0x00140608, 0x001404f0), `InkBounds` (0x001a3728),
`MakeInkPoly`/`MakeInkWordPoly` (0x001a31bc, 0x001a3250), `SplitInkAt`
and `MergeInk` (0x001a2c60, 0x001a2fc4 - both expand the ink to strokes,
work on those, and compress the answer), `AddInk` (0x001a2b70),
`TParagraphView::InsertInk` and `GetInkRefAndBounds`,
`TEditView::HandleInk`, `TInkWordGlyph` (the glyph an ink word draws as
in a line of text) and `TLiveInker` (the ink that follows the pen while
it is still down).
