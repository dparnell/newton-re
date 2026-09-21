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

## NOT YET

The stroke compression itself.  `InkCompress` (0x00140b78) hands the
strokes to `CSCompress` (0x001543fc) and gets back a block of bytes;
`InkExpand` (0x00140c98) and `InkDraw` hand them to `GenericCSExpandGroup`
(0x00153934), which decodes them a point at a time through a callback.
Underneath both is a codec of about forty functions (`EncoderOpen`
0x0027f938, `DecoderOpen` 0x0028240c and everything between them) with
its own code books, vector quantisation and bit reader - the CIC
handwriting library's, not Apple's.  Until it is reconstructed ink can be
carried about, measured, scaled and stored, but not made from strokes,
expanded back into them, or drawn.

With it would come the rest: `TStrokesToInk`/`TStrokesToInkWord`
(0x00140608, 0x001404f0), `InkBounds` (0x001a3728),
`MakeInkPoly`/`MakeInkWordPoly` (0x001a31bc, 0x001a3250), `SplitInkAt`
and `MergeInk` (0x001a2c60, 0x001a2fc4 - both expand the ink to strokes,
work on those, and compress the answer), `AddInk` (0x001a2b70),
`TParagraphView::InsertInk` and `GetInkRefAndBounds`,
`TEditView::HandleInk`, `TInkWordGlyph` (the glyph an ink word draws as
in a line of text) and `TLiveInker` (the ink that follows the pen while
it is still down).
