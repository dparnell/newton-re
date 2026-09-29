# The text engine (`src/text/`)

The Newton has two text systems, and they have almost nothing in common.

The one most of the machine uses is the **paragraph**
(`docs/views/README.md`, `views/ParagraphView.h`): a string and a styles
array in a NewtonScript frame, laid out into lines by the view system.
A note, a name field and a date's title are all paragraphs.

The other is a **document engine** â€” styled text with rulers, tab stops,
page breaks, graphics runs and a store to page it out of. It is written
in ordinary C++ over storage of its own rather than over the object
heap, it has its own class library of some 1800 symbols, and a script
reaches it only through `protoTXView` and the forty-odd `TX...` methods
that view answers. It is what the Works word processor and the built-in
books are drawn with. Its class names all begin `TX`, which is where
this directory's name comes from.

This is a large subsystem and it is being reconstructed from the bottom
up. What follows is what is done.

## The arrays (`text/TXArray.h`)

Everything in the engine is built on `TXArray` (0x002306c8): a growable
array of fixed-size elements kept in a relocatable handle. Its object is
0x18 bytes â€” the element count, the element size, a lock count, the
handle, a *chunk* and the count the handle has room for.

The chunk is the engine's whole memory policy in one number. The array
grows by a whole chunk when the chunk is bigger than what is being put
in, and falls back to exactly what is needed when that will not fit
(`Insert` 0x00230ffc); and it gives memory back whenever more than a
chunk is unused (`CheckUnusedCount` 0x002310c8, which every `Remove` and
every shrinking `SetCount` calls). A chunk of nothing is taken as one,
so an array always has somewhere to grow and always has a slack to
measure against. Nothing else in the engine thinks about memory at all.

`Lock` (0x00230ae0) and `Unlock` (0x00230d60) nest: only the first lock
touches the handle, and only the last unlock lets it go. `GetElementPtr`
answers an address into an *unlocked* relocatable block, so it is good
only until the next allocation â€” which is why everything that walks an
array locks it first.

`TXLongTagArray` (0x00230944) is a `TXArray` whose elements begin with a
long kept in increasing order. `Search` (0x002309d4) is a binary search
that tries the two ends first â€” at or below the first element answers
index 0, past the last answers one past the end â€” and answers the index
of the first element *past* the tag when there is no exact hit, telling
the caller which it got through the `found` it fills in.
`AddToElements` (0x00230b2c) moves a run of tags along by a delta, which
is what an edit does to everything after the place it changed.

## The ranges (`text/TXArray.h`)

`TXRanges` (0x00230b8c) is the one that earns its keep. It is a
`TXLongTagArray` whose longs are the **ends** of consecutive ranges:
element *i* holds where range *i* ends, so range *i* is [element *i*-1,
element *i*) and range 0 starts at 0. Nothing is stored for the starts.
Four elements `[5, 12, 12, 30]` are four ranges covering characters 0 to
29 â€” and the third is empty, which the engine does keep, because a style
run with nothing in it yet is a real thing.

That single representation is how the engine records every division of
the text: which characters each style run covers, which each line covers,
which each paragraph covers. An edit that adds five characters at
offset 20 is one `AddToElements` on every one of those arrays.

The two questions asked of it are:

- `OffsetToRangeIndex` (0x00230db0) â€” which range an offset falls in.
  An offset that is exactly a boundary belongs to the range that
  *starts* there; the `atStart` flag asks for the one that *ends* there
  instead, which is what a caret at the end of a run wants.
- `SectRanges` (0x00230e14) â€” what a stretch of text covers: the range
  it starts in and how far into it, the range it ends in and how much of
  that range is left over, and the run of ranges between them that are
  covered end to end. That last is what lets an edit replace whole runs
  and trim only the two at the edges. It answers the number of ranges
  the stretch *spans*, before either of the two corrections that turn
  that into the count covered whole.

## The attributes (`text/TXAttributes.h`)

An *attribute* is a four-character tag and a value of up to twenty
bytes. The three the text runs carry are `'font'` (a family, as a
NewtonScript Ref), `'size'` and `'face'`; the rulers carry more.

`TXAttrValues` (0x00231340) is a list of them â€” a `TXArray` of 0x20-byte
elements, each a tag, a flag, a length and the value. It is how a set of
attributes is passed about: a style slip hands one down to be applied,
and a selection hands one back up saying what its runs have in common.
The flag says the value is an *object the list owns*, and `Remove`
(0x0023145c) â€” the array's `Remove` overridden â€” deletes those before
closing the array up.

`TXAttrObject` (0x002310e4) is the base of everything a run can point
at: a text style, a ruler, a graphics run. Its object is eight bytes,
and everything about it is virtual and does nothing â€” the subclasses
answer. What the base does supply is the reference counting
(`Reference` 0x002315bc, `Free` 0x002312fc: one reference to begin with,
and the object goes when the last one does, `FreeData` first) and two
walks over an attribute list:

- `Update` (0x00231148) applies every value of a list, last first, and
  answers the `GetAttributeFlags` of the ones that were applied or-ed
  together â€” which is what tells the caller how much of the layout has
  to be done again.
- `GetCommonAttrValues` (0x002311e4) narrows a list down to the
  attributes this object agrees about: an entry it does not share is
  taken out, one it does is written back with its own value. Run over
  every object of a selection in turn, what is left is what they all
  have in common â€” which is exactly what a style slip shows, and why it
  shows a blank where the selection disagrees.

`IsEqual` (0x00231298) on the base can only tell that two objects are of
the same *kind*: the same object, or the same class id. A subclass with
values to compare overrides it â€” `TXAdvancedRuler::IsEqual` compares its
tabs.

## The storage (`text/TXChars.h`)

`TXChars` is the face: count the characters, replace a range, copy a
range out, get one character, find one, ask for a run of them to look
at. Everything above it â€” the formatter, the lines, the view â€” only ever
talks to this.

`TXChunkedChars` (0x0023288c) is the implementation that makes a long
document possible. The text is kept in **chunks** of at most 512
characters and a `TXRanges` holds where each chunk ends, so an edit
touches one chunk or two rather than moving a whole document about. The
chunks themselves are handed out by three virtuals â€” `GetChunkPtr`,
`AllocateChunks`, `RemoveChunks` â€” which is how the same code works over
a binary in the heap and over a large binary paged in from a store.

`TXTextDescriptor` (0x00232820) is how text moves between them. It
describes a source or a sink, which may be a plain `UniChar` buffer, a
stream, or another `TXChars` at an offset, and one `CopyTo`
(0x00232914) moves characters between any two of those â€” including a
`TXChars` to itself, which is how the chunked storage ends up talking to
its own chunks. Both ends keep a position, so a descriptor can be handed
to one call after another and carry on where it left off.

### Putting text in

`Replace` (0x00232ad0) is the only way in, and it tries three things in
turn, each of which answers whether it managed it:

1. **`InsertInChunk`** (0x00232c60) â€” everything fits in the chunk it is
   going into. One `MungeChunk` and the chunk's end moves.
2. **`InsertUsingNearChunk`** (0x00231634) â€” the chunk and one of its
   neighbours have room between them. Characters are first shuffled
   across the boundary to make as much room in the chunk as the
   neighbour can take, and the new text is then split between the two.
   The neighbour before is tried first, then the one after.
3. **`InsertUsingExtraChunks`** (0x00231970) â€” as many new chunks as it
   takes are made after the one being written to. What was after the
   insertion point is moved to the end of the last new chunk *first*, so
   that it ends up where the new text will leave it, and the text is
   then poured through the chunks a chunkful at a time.

`MungeChunk` (0x00231eac) is what all three are made of: so many
characters at an offset of one chunk replaced by so many from a
descriptor. What follows them inside the chunk is moved by the
difference first â€” and moved *back* if the source fails part way, which
is what lets every insertion above it give up without having spoiled
anything.

A growth of more than ten characters asks `Preflight` first, so a
subclass that must find the memory somewhere else (a store) can say no
before anything has been touched.

### Taking it out

`Remove` (0x00231c4c) uses `TXRanges::SectRanges` to find what the
stretch covers: the chunks covered end to end are unmade, and the two at
its edges have their covered parts munged away. Then the chunks around
the hole are run together again while any two of them will fit in one
(`ConcatChunks` 0x00231e30), so that a document that has been edited for
a while does not end up made of crumbs.

### Looking at it

`GetLineChars` (0x00232280) is what the formatter lays a line out from:
at most 128 characters, answered *in place* when they all lie in one
chunk â€” with the chunk's index, so the caller can release it â€” and
gathered into `gTXLineCharsBuffer` when they cross one, with an index of
-1 to say there is nothing to release. `AcquireCharChunk` is the same
idea without the limit: as much as lies in one chunk.

`SearchChar`, `SearchCharBack` and `GetCtrlCharOffset` walk the chunks
one at a time over the plain-buffer versions (0x00234278 and following).
The plain ones have a wrinkle worth knowing: asking for a *form feed*
(0x0c) finds a carriage return (0x0d) **or** a line feed (0x0a), which
is how text that came in with either line ending is read the same way.

## The streams (`text/TXStream.h`)

`TXStream` (0x00245ec8) is eight bytes: a vtable and a position. A
subclass supplies three things — how big the stream is, and how to read
and write at the position — and the base turns those into `WriteBytes`
and `ReadBytes`, which move the position along afterwards. A read that
would run off the end reads what there is, moves the position to the end
and answers **-8702**, which is how a reader that does not know how long
a thing is finds out. (The ROM has no symbol for that number; it is
`kTXErrEndOfStream` here.)

Two subclasses exist.

`TXHandleStream` (0x002460fc) keeps its bytes in a `TXArray` of one-byte
elements, thirty at a time, and is the ordinary scratch stream. Its
`Write` is one `TXArray::Replace`: as many bytes as are left from the
position are replaced by *all* of the new ones, so one call both
overwrites and extends.

`TXBinaryStream` (0x0023e174) writes into a NewtonScript binary, which
is how a document becomes something a soup entry can hold. It keeps the
size it has written separately from the binary's length: a write at the
end grows the binary by the size wanted *plus a slack* (0x400 bytes for
the ROM's own), so the next few writes need not grow it again, and the
destructor cuts it back to what was written if it was asked to. A write
inside what is already there simply overwrites and the size does not
move. Its constructor's second argument is the difference between
opening one to read (start full) and making one to write (start empty)
over the same binary.

Which of the two a piece of the engine gets is the **temporary stream
factory**'s business (`TXSetTempStreamFactory` /
`TXGetTempStreamFactory` over `gTXTempStreamFactory`, 0x0c104e8c). The
ROM's own, `TXNewtStreamFactory::Create` (0x0023efc8), answers a handle
stream for anything under four kilobytes; for anything larger it tells
the busy box it is working, takes the first of `GetStores()`, rounds the
size up to a whole kilobyte and adds two more, and asks
`FLBAllocCompressed` for a `'binary` of that length on the store with a
`"TLZStoreCompander"` over it — which is what lets a document larger
than the heap be worked on at all. That arm is **not yet**: large
binaries are not reconstructed, so `Create` answers `kError_No_Memory`,
which is exactly what the ROM's own does when nothing came of it.

### Text through a stream

`TXTextDescriptor` may name a stream at either end, so a stream is also
how text gets into and out of the character storage. The ROM's copy is a
straight `BlockMove` of the bytes, which on a big-endian machine puts
the `UniChar`s in the stream most significant byte first. **DEVIATION:**
on a little-endian host they are turned round on the way in and out
(`toolbox/ByteOrder.h`), so the bytes in a stream are the same bytes a
Newton would have written.

### The chunks, written out

`TXChunkedChars::WriteChunksRanges` (0x002325f8) and `ReadChunksRanges`
(0x00232704) are the storage's own use of a stream: the chunk lengths,
as halfwords most significant byte first. What is written is the number
of chunks, then a `(length, index)` pair for **every chunk that is not
the default length**, then a nought to end them; reading lays the ranges
out again, giving the default length to every chunk up to the next one
that was named. A document that has not been edited much is nearly all
full chunks, so its whole chunk table is six bytes however long the
document is.

## The rulers (`text/TXRuler.h`)

A ruler is what a paragraph is laid out against, and it is a
`TXAttrObject` like a style is — so a run of the document points at one,
and the same machinery that narrows a style list down to what a
selection agrees about narrows a ruler list too.

There are two of them. **`TXBasicRuler`** (0x0024587c, class id
`'brlr`) is twelve bytes: a justification (1 left, 2 right, 4 centre, 8
full) and nothing else. Its margins are nought and its tab stops are the
default ones — every `gTXDefaultTabVal` pixels, for ever.
**`TXAdvancedRuler`** (0x0022f2b8, `'rulr`) is thirty-two: it adds the
first line's indent (`'ndnt`), the two margins (`'lMrg`, `'rMrg`), the
line spacing (`'lspc`, held between 1 and 20 — 1 single, 2 one and a
half, 3 double) and a `TXTabsArray` of real tab stops (`'tabs`).

`gTXDefaultTabVal` (0x0c104d7c) is nought until
`Textension::TextensionStart` makes it **30**. A ruler asked for a tab
before then divides by nought.

### The tabs

A `TXTab` is eight bytes of which six are used: a position in pixels, a
kind (left 0, centre 1, decimal point 2, right 0xff) and the character
the run up to it is filled with. `TXTabsArray` is a `TXArray` of them
kept sorted by position; `SearchTab` walks until it finds the position
or passes it, so a miss leaves the index at where a new tab would go.

`WidthToTab` is what the formatter asks: the first tab past a width (a
Fixed, rounded to whole pixels), or — past the last real tab — the next
default stop. Two things about it are worth knowing. It only ever copies
six bytes of a tab, and on the default-stop path it never sets the fill
character, so the ROM hands back whatever the stack held; and
`InsertTab` does not look at what `SearchTab` answered, so putting a tab
in at a position that already has one gives two entries there.

### Laying a line out

`GetLineLeftBlanks(firstLine)` answers the indent for the first line of
a paragraph and the left margin for the rest, as Fixed;
`GetLineRightBlanks` the right margin. `GetTabWidth` says which tab the
line has run into and how far away it is, and marks it **pending** when
it is not a plain left tab — because how wide a centre, right or decimal
tab is cannot be known until the text after it has been measured. That
is what `CalcPendingTabWidth` then does: a centre tab gives up half the
text's width, any other kind all of it, and what is left is held down to
what the line still has room for and up to nothing.

`AdjustLineHeight` is the line spacing: single leaves the line alone,
and each step past it adds half a line.

### Sharing, and not sharing

`TXAttrObject::Reference` answers *the object to use* — normally itself,
with one more reference. `TXAdvancedRuler::Reference` (0x00230234) is
the one place that answers something else: a ruler that has tab stops
cannot be shared, because its `TXTabsArray` is a plain owned pointer, so
it makes a fresh copy instead. A ruler with no tabs is shared like
anything else.

### A ruler as a script sees it

`GetNSObject` clones `Rtxcanonicalruler` and fills in `justification`,
`indent`, `leftMargin`, `rightMargin`, `lineSpacing` and an array of
`Rtxcanonicaltab` clones (`{value:, kind:}`); `SetNSObject` reads them
back, taking **only the slots the frame has**, which is what lets a slip
change one thing. `TXGetRulerAttrValues` (0x0022fa14) does the same
reading straight into a `TXAttrValues` without making a ruler at all, so
what comes back says what to change about a range's rulers and nothing
else.

`UpdateAttribute` is the one attribute that is not simply set. A ruler
slip asks for one tab to be added, moved or taken away, so the value of
a `'tabs` attribute in an update list is not an array but a
`TXTabUpdate` — the tab as it was, the tab as it is to be, and the ruler
the other tabs come from. Twenty bytes, which is exactly what a
`TXAttrValues` entry holds.

## The object ranges (`text/TXObjectRange.h`)

This is where the rulers and the styles meet the text. A
`TXObjectRange` (0x0024055c) is a `TXRanges` whose element is a range
end **and** a `TXAttrObject*`: range *i* covers the characters from
element *i*-1's end to element *i*'s, and points at the object that says
how they are shown. A document's styles are one of these and its rulers
are another, and the same thirty-odd functions serve both.

Everything in it turns on two ideas.

**Equal objects are shared and neighbours are run together.**
`SearchObject` walks the ranges for an object `IsEqual` to the one being
put in, and `MapObject` (0x002412f0) is what every write goes through:
it answers the object already here that matches, or the object itself,
or a copy of it, and it remembers the last one it answered
(`fLastObject`) because a run of text is usually all of one style.
`UpdateRangesBounds` (0x00240714) then compares the stretch's object
with the ranges either side and, when one of them already holds it,
simply lets that range grow over the stretch instead of making a new
one. A document that has been edited back and forth therefore does not
end up with a hundred ranges all saying the same thing.

**An object may stand for itself.** `GetObjFlags() & 4` says the object
is a thing in the text rather than a way of showing the characters — an
embedded picture. Such an object is never merged with a neighbour,
never shared with another range, and is changed *where it lies* rather
than copied first.

### Putting an object on a stretch

`ReplaceRangeObj` (0x00240924) is the one way in.
`UpdateRangesBounds` pulls the ranges either side back off the stretch —
cutting a range short at its start, or, when the stretch is wholly
inside one range, making the part before it a range of its own — and
says through `firstIndex`/`lastIndex` which ranges the stretch now takes
up. If it answers *true* a neighbour has already absorbed the stretch
and there is nothing to do but take the covered ranges out; otherwise
one range is written over or a new one inserted, and the covered ones
are removed.

`ReplaceRange` (0x00240b20) is the same with the text changing length:
`oldLen` characters at `at` become `newLen`, and everything after them
moves by the difference (`AddToElements`). A nil object means the new
text takes whatever was already there. `ClearRange` (0x002409f4) is a
removal: whichever neighbour can be stretched over the hole is, and if
none can — the one before stands for itself, or there is none — the
ranges the removal covered are taken out.

The other `ReplaceRange` (0x00240cf0) takes a whole run of ranges from
another `TXObjectRange`, which is how a paste keeps the styles of what
was copied. Its fast arm copies the elements straight across and then
moves their ends to where they now stand; it takes it that the stretch
covers whole ranges, and does not check.

`UpdateRangeObjects` (0x00240e58) changes every object a stretch points
at by a `TXAttrValues` list: each one is copied first — because it may
be shared with text outside the stretch — changed, and then mapped back,
which is how two runs that end up saying the same thing become one.

### The walk and the pool

`TXObjectIterator` (0x00240f60) walks the ranges from an offset, keeping
the object, where its range starts and how much of it is left.
`TXRegisteredObjects` (0x002359e4) is the small pool of objects a
document shares — `Textension::RegisterRuler` and `RegisterRun` put them
there. It is a fixed array of **six** and `Add` does not look at whether
there is room.

## Places and stretches (`text/TXOffset.h`)

The ROM's `TXOffset` is two words: a character offset and a flag saying
whether an offset exactly on a boundary belongs to what starts there or
to what ends there - the caret at the end of one line and at the start
of the next are the same offset told apart that way.  Most of the engine
passes it in two registers, which the reconstruction writes as a
`TXOffset` (a long) and an `atStart` argument; where the ROM passes one
*by address* and writes the flag back, the struct itself is
`TXOffsetPos`.  `TXOffsetRange` is two of them, a start and an end;
ROM quirk kept: `CheckBounds` swaps the offsets of a range that is the
wrong way round but leaves each end its own flag.

## The runs (`text/TXRun.h`)

A `TXRun` is the attribute object for *what* a stretch of text is: it
measures, breaks, draws and hit-tests the characters it covers.  It is
abstract - the text runs (`TXNewtTextRun`, characters in a font) and
graphics runs (`TXGraphicsRun`, a picture standing in the text as one
character) are what a document holds - and its virtuals follow the
ROM's slots from +0x54 (`IsTextRun`, `GetHeightInfo`, `PixelToChar`,
`CharToPixel`, `Draw`, `FullJustifPortion`, `VisibleLen`,
`MeasureWidth`, `LineBreak`, `Click`, `SetHilite`, `DrawHilite`; the pure
ones named from TXGraphicsRun's vtable, since TXRun's shows them as
`__pvfn`).  `TXRunRange` is the object range of a document's runs:
`CharToTextRun` answers the text run an offset takes its style from - its
own, or for an offset in a picture the nearest text run before it, else
after it.

## The ruler ranges (`text/TXRulerRange.h`)

The rulers a document's paragraphs point at, plus the text (to find
paragraphs) and a *pending ruler*: in an empty text, or at the very end
after a line break, the next character typed starts a paragraph with no
range yet, but a ruler slip must still show it and may change it.
`GetPendingRuler` answers that ruler for such a place - brought up to
date the first time it is asked since it was invalidated (a fresh
default one for an empty text, a copy of the ruler before it otherwise)
- and `OffsetToObject`/`UpdateRangeObjects` answer out of it there.
Every range starts a paragraph: `ValidateRuler` runs a range that starts
mid-paragraph into the one before, `ValidateRulerRange` does so for both
ends of an edited stretch (ROM quirk kept: after merging the first it
calls itself again and throws the answer away), and
`CharRangeToParagRange`/`GetReplaceExtraChars` widen a stretch to whole
paragraphs.  `TXGetParagStartOffset`/`TXGetParagEndOffset` measure a
paragraph through the text's own search: `SearchChar` with 0x0c means
"any line break".

## The small helpers (`text/TXUtilities.h`)

The document is laid out in longs, not QuickDraw's shorts:
`TXLongRect`/`TXLongPoint` (ROM quirk kept: `IsPointInside` counts both
edges in).  `TXTempReferences` is a pool of five scratch objects (with
all five out, `Get` makes one outside the pool and `Done` frees it);
`gTXTempRegions` is the pool of regions the clipping helpers borrow.
`TXClipFurther`/`TXCalcClipRect` narrow the port's clip or a rectangle
(a rectangular clip - `rgnSize == kRectRgnSize` - takes a short cut),
`TXInvalSectRect` dirties a rectangle on the root view (its vtable +0x54,
`TRootView::Dirty`) when it shows through a region, and
`TXGetNewDefaultObject` copies the default run or ruler from the
registered objects (`gRegisteredRuns`/`gRegisteredRulers`).  NOT YET:
`TXScrollRect`, over QuickDraw's `ScrollRect`.

## The lines' heights (`text/TXLinesHeights.h`)

`TXLinesHeights` keeps every line's height without a word per line: an
array of groups, each a run of consecutive lines with one height (and
one ascent).  Setting a line's height splits its group in three,
moves the line to an equal neighbour, or changes the group in place when
the line is all there is of it; removing lines joins the neighbours of
an emptied group when they match.  The total height and the last line's
number are kept, so the whole text's height is never summed.
`PixelToLine` walks the groups to the line a pixel is on and gives back
that line's top.  (The `TXFormatReflowLines` argument some of these take
is not read, in the ROM either.)  `TXParagCtrlChars` records up to
thirty-two control characters of a stretch of a paragraph, stopping at
its line break, so that laying a line out does not search for each tab
again.

## The text runs (`text/TXNewtTextRun.h`)

A `TXNewtTextRun` is a font family, a size and a face - the whole of a
style, as the engine sees one.  The family is a NewtonScript Ref (a
symbol out of `vars.fonts`, or the number of a ROM font) in a RefHandle
of the run's own, and travels in attribute lists as a
`TXNewtFontFamilyInfo`, a little object the list owns and deletes, so
the Ref is always somewhere the collector can see it.  A new run takes
the user's `userFont` preference.  Faces are added and taken away bit by
bit (`UpdateAttribute` with `how` 4 or 8), and two faces agree about the
bits they share, which is what a style slip shows for a mixed selection.

Everything the run does to its characters goes through QuickDraw's text
objects (`qd/TextObject.h`): `MeasureWidth` is `MeasureTextOnce`, `Draw`
is `DrawTextOnce` in `or` mode on the line's baseline, `CharToPixel` and
`PixelToChar` are a text object's `CharToPoint`/`PointToChar` (stretched
over the run's width and its share of a fully justified line's slack),
and `LineBreak` asks a text object for the length that fits
(`GetTextObjField`, field 1) and cuts it back to a word with
`FindWordBreaks` over the locale's line break table, answering 2 when
all of it fits (the room it took off the width), 0 when it was cut at a
word, and 1 when it was cut inside one - which only happens on a line
with nothing on it yet.  (A ROM quirk kept: a word starting the text on
a line that may not cut one answers a length of `-start`.)  The height
is `GetStyleFontInfo`'s, cached until an attribute changes.  What a
fully justified line may stretch the run by is a thirty-second of its
point size for every space in it (`FullJustifPortion`).

The options' +0x14 word is set to 9 by `Draw` and 10 by `MeasureWidth`.
The ROM's `DoTextOnce` reads it as a selector: 9 marks the text object
(its flag 0x40000) and 10 throws the options away altogether, which is
why `MeasureWidth` leaves the rest of them as the stack had them (the
host zeroes them, since the stack's garbage cannot be reproduced and is
never read).

## The graphics runs (`text/TXGraphicsRun.h`)

A `TXGraphicsRun` is a picture standing in the text as one character: a
box of a size its subclass answers, all of it above the baseline.  It
stands for one thing, so its flags have bit 4 (never shared - a
reference to it is a copy - and never run together with a neighbour)
and bit 2 (the caret goes over it in one step, and a line hit-tests it
as one thing: `TXIndivisiblePixelToChar`, where a tap in its middle
selects it whole and a tap in the quarter at either end, when its
character is a control character, puts the caret beside it).  One too
wide for the room left goes on an empty line squeezed by the difference
(a negative `fExtraWidth`).  Selected, it is framed in a gray pattern in
XOR, so drawing the frame again takes it away.  `TXNewtGraphicsRun`
('graf, public type 'shap) is a frame whose `shape` slot is drawn with
`DrawShape`, two pixels in from its box; 16 by 16 when there is none.

## Styled text (`text/TXStyledText.h`)

`TXStyledText` is the characters and the runs together, with the port
they are measured in (the current one when it has none), and it owns
both.  `CharToWord` is a double tap: `FindWordBreaks` over at most 64
characters either side of the offset, with the spaces after a word taken
with it or - a tap on the spaces - the word before them.
`AdvanceOffset` is how far the caret moves: one character, or a whole
run whose object moves as one.

## A line (`text/TXLine.h`)

`TXLine::DoLineLayout(start, length, width)` lays out one line of a
styled text.  The line is cut into *pieces*: each run's stretch of it
(`TXObjectIterator` over the runs), cut again at every control character
(`gTXParagCtrlChars`), each control character a piece of one character
whose kind is the character itself - 9 a tab, 13 a line end.  The
paragraph's ruler (out of the `TXRulerRange`) gives the margins; its
justification decides the rest:

- *left*: the text starts at the left margin (the indent on a
  paragraph's first line);
- *right*: the trailing spaces are left out of the line's visible length
  and the line moves right by the room left;
- *centre*: it moves right by half the room;
- *full*: the trailing spaces are left out and the room is shared among
  the text pieces after the last tab in proportion to what each says it
  can take - except on a paragraph's last line (one ending in a line
  break, or at the end of the text), which is laid out left.  A ruler
  value of 0x10 stretches the last line too.  ROM BUG kept: the shares
  are handed out from the line's end backwards one per text piece, but
  the list of portions holds a nought for each line end among them too.

`DefineRunWidths` measures the pieces: text by its run, a line end as
nothing, and a tab by the ruler's `GetTabWidth` - a left tab's width at
once, any other kind *pending* until the text after it is known: a right
or centre tab when the next tab or the line's end is reached
(`CalcPendingTabWidth`), a decimal tab as soon as a piece holds its
character (`CalcAlignTabWidth` - the character is the tab's fill
character, a full stop unless the ruler says otherwise).  All widths are
16.16, so the text is placed to a fraction of a pixel and rounded
only when it is drawn.

The line then answers what a display asks of it: `Draw` (each text
piece by its run, from the line's left edge; a line of nothing but
spaces is not drawn at all), `CharacterToPixel` (whole pixels, an
offset at the start of what follows counting as the end of what
precedes it), `PixelToCharacter` (a tap: the run's own `PixelToChar`,
or a whole piece that is one thing, or a line end's start) and
`GetLineHilite` (the stretch a selection covers, out to the line's edges
when asked; a caret is one pixel wide).

`test_TXLine` lays out "Name<tab>Value" in two styles with a tab stop at
60, checks every piece and every character's place against QuickDraw's
own measurements, draws it into an offscreen port and compares the bits
with QuickDraw drawing the two strings itself, taps it and hilites it;
and it tries the right, decimal and centre tabs, the default stops, and
the four justifications.

## The frames (`text/TXFrames.h`, `text/TXFrameFormatter.h`)

A *frame* is one rectangle the text flows through: a view has one, a
paginated document one per page.  `TXFrames` works in *absolute*
coordinates - longs, the document's own, which may run far past a
QuickDraw Rect's sixteen bits - and turns them into *draw* coordinates
for the port through three origins: where the view has scrolled to
(`FramesScrolled`), where drawing starts in the port (`SetDrawOrigin`)
and where the frames start (`SetFramesOrigin`); a draw coordinate is
clipped to ±0x7fff.  A frame's text rectangle starts at the margins' top
left and is the size the subclass keeps; the margins lie round it.  The
lines' heights belong to the *frame formatter*, a `TXLinesHeights`
(below) that also says which lines are in which frame, so the frames
answer which line a point is on (`PointToLine`), a line's rectangle
(`GetLineBounds`) and the lines a rectangle crosses (`SectLines`: one
`TXSectLine` per band of equal lines - built, in the ROM, in a Rect moved
on with unaligned word loads, which on the ARM rotate the halfwords, so
only the low half of each sum is the one meant).

`TXMonoFrame` is a view's one frame: a `TXMonoFrameFormatter` (every line
in frame 0, the frame 0x7fff pixels tall), a width, and a height that is
unbounded (0x40000000) until one is given.  An edit leaves a note of the
frames it touched in `gFramesEditInfo` - which has room for two, and
`CatchFrame` does not check - with, for the mono formatter, how much the
text's height changed, which is what a display redraws by.  The
multi-frame and page formatters and `TXPageFrames` are NOT YET.

## The formatter (`text/TXFormatter.h`)

`TXFormatter` keeps where every line ends (a `TXRanges` of line ends) and
gives each line's height to the frame formatter.  `BreakLine` puts the
runs on a line one after another (`BreakRun`): each run fits what it can
into the room left through its own `LineBreak` (`BreakVisibleChars` - a
text run may always cut the line's only word, a picture may start a line
however wide it is), the control characters are stepped over as they
come (`BreakCtrlChar`: a tab takes the ruler's width at once, or waits on
what follows as `TXLine` has it, a decimal tab settled by
`BreakAlignTabChars`), and no line is longer than 128 characters.  The
line's height is the tallest ascent, descent and leading of the runs on
it, adjusted by the paragraph's ruler for its spacing - which is why the
second word of a line's height is the *ascent*: the spacing adds half of
it a step.

`Format(start, end)` formats the whole text (`FormatAll`) or the lines a
stretch touches (`FormatRange`): from the line the stretch starts in -
or the one before when that one does not end a paragraph, since an edit
may pull its first word back up - breaking each line again, inserting a
line when the text now wraps onto more of them, setting it when not (and
removing the lines it has swallowed: `RemoveFormattedLines`), until a
line comes out ending where it used to, past the stretch.  `ReplaceRange`
is what an edit calls: the line ends after it moved by what was put in or
taken out, the lines wholly inside what went removed, then `Format`.  A
text ending in a line break always has an empty last line
(`AppendEmptyLine`: the height of the text run at the end, or 12 with an
ascent of 9).  `CheckRulerSettings` brings the rulers within the width:
a margin leaving less than 50 pixels goes, and so does a tab past the
edge.  On a stream the lines are a count and then a byte per line (a line
being at most 128 characters).

`test_TXFormatter` formats a two-paragraph text into 80 pixels and checks
every line word by word against the run's own measurements, then inserts
and deletes text and checks that the incremental reflow comes out the
same as formatting the edited text afresh.  The word breaks are the
locale's line break table's (`FindWordBreaks`), in which a run of spaces
is a word of its own - so a line that fills exactly to a word's end
breaks after the spaces that follow it.

## The display and the hilite (`text/TXDisplay.h`, `text/TXHilite.h`)

`TXDisplay` draws the formatted text through a *view region* of the
port, with the port's origin at nought while it does (`Focus`,
`UnFocus`, nested through `SetDrawEnv`/`RestoreDrawEnv`).  `Draw`
erases and draws every line the rectangle crosses, a band of equal
lines at a time (`TXFrames::SectLines`, then `TXLine::DoLineLayout`
and `Draw` for each), and the hilite over it.  `Scroll` blits the view
with QuickDraw's `ScrollRect` (`qd/ScrollRect.h`, added for it) and
draws only what was uncovered; a scroll never goes past the text
(`AdjustScrollValues`), and after an edit that shortened it the view
scrolls back (`CheckScroll`).  An edit is bracketed by `BeginEdit` and
`EndEdit`, which redraws as little as it can per frame: the lines that
changed (`FrameEndEdit`), the lines after them moved by a blit when the
height changed (`ScrollFrame`, `UpdateScrolledArea`), the frame's
bottom erased when the text got shorter.

`TXHilite` is the selection and a state - hidden, shown, or inactive
(framed rather than inverted).  All of it is drawn in XOR, so drawing
it again takes it away: growing a selection inverts only the stretch
that changed (`SetHiliteStart`, `SetHiliteEnd`), and a selection is
drawn per frame as a first partial line, a block of whole lines and a
last partial line.  The caret is *not* drawn here - the Newton's root
view draws it.  `Click` counts clicks (the pen's double-click time and
one pixel), selects a boundary, a word or a line by the count, extends
with the shift flag, and follows the pen (`DragHilite`), scrolling the
view when the pen leaves it - by 18 pixels, doubled for each second it
stays out, up to three times.  The arrows move by a character (a
picture in one step), a word or a line; up and down keep the column
they started from.

## The document (`text/Textension.h`)

`Textension` is the styled text with its rulers, formatter, display and
hilite, put together from `TXHandlers` (whichever is nil is made).
`TextensionStart` makes the engine's globals; the Newton registers its
default run and ruler (`RegisterRun`, `RegisterRuler`).  An edit is
`ReplaceRange`: the rulers', the runs', the characters' and the line
ends' parts, with the display bracketed round them; typing is `KeyDown`
over it (characters replace the selection in the *pending run* - the
style typing uses, taken from the text at the caret whenever the
selection has moved - backspace and escape clear, the arrows move).
A stretch of a document moves in and out as a *container*: `Export`
writes the stretch's runs, rulers and text into one, and `ReplaceRange`
takes one in place of characters (`TXReplaceParams(container, types)`).

`test_TXDisplay` fills a document by an edit, draws it offscreen (the
same bits as drawing each line itself), taps a caret into it, hilites
"quick" and checks that exactly its rectangle is inverted, drags a
selection from "quick" to "fox", moves it with the arrows, scrolls a
line (the same bits as drawing the scrolled view afresh), and types
and backspaces.

## The containers (`text/TXContainer.h`)

A container is a piece of a document as up to three *values*: 'TEXT'
(the characters), 'txrn' (the runs - or a picture's run by its public
type, 'shap') and 'txrl' (the rulers).  `Import` copies the values one
container has into another, runs first, then rulers, then text,
bracketed by `BeginWrite`/`EndWrite`; `TXContainerImportInfo` says which
values to take and answers which were, and how many of each.  A value
the source lacks (-102) is skipped; any other error ends the write as
failed.

- `TXStdContainer` keeps its values on a TXStream: a count and a table
  of three (type, count, size) entries, 0x28 bytes, then the values one
  after another.  The characters' value has a count of nought - only
  `WriteObject` counts - and a size of two bytes a character.  (Host:
  the table's words are big-endian.)
- `TXLocalContainer` is what an undo buffer is: each object is a length
  and a pointer to the object itself, a reference taken, so it only
  lives as long as the objects do; a failed write, and `FreeObjects`,
  give the references back.  (DEVIATION: the pointer is the host's
  eight bytes.)
- `TXPrivateContainer` is a stretch of a live document.  Reading it
  reads the document's characters and objects in place (each run's
  length clipped to the stretch); writing to it *replaces* the stretch -
  the characters at once, the runs and rulers gathered into ranges of
  their own and put in when the write ends.  A picture written on its
  own (by its public type, with no text) brings the character that
  stands for it, U+2206 (`gTXGraphicsRunChar`).

`Textension::ReplaceRange` from a container imports it over the stretch
first: with all three values the rulers are only checked
(`ValidateRulerRange`), with runs or rulers but no text the lines are
simply formatted again (and the pending run is to be worked out
afresh), and with text alone the runs are filled in as for plain
characters.

`test_TXContainer` exports "big" in bold into a local container on a
handle stream and checks its table word by word, imports it into
another document whole, as text alone and as runs alone, puts a picture
in on its own, and checks that a failed import gives back its
references.

## The edit commands (`text/TXCommand.h`)

`TXCommand::Execute` does a command the first time, undoes it the
second and redoes it the third, going round the states 0 (to do),
1 (done), 2 (undone); 4 is a command that failed or cannot be undone.
`TXEditCommand` is how the engine's own edits undo: before the edit,
the stretch it changes is exported into a local container on a
temporary stream (from `TXGetTempStreamFactory` when there is text, a
handle stream otherwise), and the rulers of the paragraph at its end
into a second; undoing puts the container back over what the edit made,
having first saved that the same way (the redo container).  What is
kept depends on the edit - the runs for a restyle (kind 2), the rulers
for a paragraph change (3), everything otherwise.  An empty range keeps
nothing: undoing is then only taking away.  When the container cannot
be made (no memory), the command is done but cannot be undone.

- `TXKeyCommand` (1) is typing: one command for as long as the keys
  follow on from each other (`NewKey` answers 3 for a key that does not,
  which starts a command of its own), typed as they come, so the first
  Execute undoes it.  A delete key widens what is kept back to the start
  of the line before, since backspacing may reach it, and the selection
  shown after an undo starts where the deleting reached.
- `TXMoveTextCommand` (4) moves or copies a stretch through a container
  of its own.  A move swaps its two places over, so undoing a move is
  doing it again; undoing a copy takes it away.
- `TXReplaceTextCommand` (5) is ReplaceRange undoably.

`test_TXCommand` does, undoes and redoes a replacement, a restyle,
typing and backspacing, a move and a copy.

## Not yet reconstructed - the plan

The 39 `protoTXView` methods (`natives.py --unbound --area text`: `Cut`,
`Copy`, `Replace`, `PointToChar`, `ShowRuler`, `Scroll`, ... all
0x00249b7c-0x0024ae90) stand on the whole engine.  Measured on
2026-09-29 with `analysis/callgraph.py build/MP2x00US <the 39 natives>`:
**292 functions not done, about 45 KB** - a lower bound, since the engine
calls through its own vtables a great deal.  By class:

| class | functions | bytes | what it is |
|---|---|---|---|
| TXView | 51 | 10 KB | the view: the script's face, editing commands, undo |
| TXLine | 18 | 4.5 KB | one line: its runs, widths, justification, tabs, hit-testing |
| TXFormatter (+ Frame/MonoFrame/MultiFrame formatters) | 20 | 4.2 KB | breaking the text into lines and lines into frames |
| Textension | 21 | 3.6 KB | the document: text, runs, rulers, commands, start-up |
| TXDisplay (+ TXNewtDisplay) | 22 | 2.8 KB | drawing, scrolling, the edit bracket |
| TXHilite (+ TXNewtHilite) | 16 | 2.4 KB | the selection and the caret |
| TXFrames (+ Mono/Sect/Page frames) | 20 | 1.9 KB | where the lines go on the page(s) |
| TXContainer (+ Private/Newt) | 9 | 1.7 KB | reading and writing a document to a stream |
| TXRulerUI and its bars | 14 | 1.8 KB | the ruler a script shows with ShowRuler |
| free functions | ~60 | 9 KB | clipping, scrolling, the default objects, stream helpers |
| smaller (TXStyledText, commands, TXVBOChars, ...) | ~40 | 3 KB | |

Bottom up, in the order the layers need each other:

1. DONE: `TXOffset`/`TXOffsetRange`, `TXRun`/`TXRunRange`,
   `TXRulerRange`, the helpers, `TXLinesHeights`, `TXParagCtrlChars`.
2. DONE: the concrete runs, `TXNewtTextRun` and `TXGraphicsRun`
   (with `TXNewtGraphicsRun`), and `TXStyledText` - over QuickDraw's
   text-object questions (`qd/TextObject.h`: `CharToPoint`,
   `PointToChar`, `GetTextObjField`), which came with them.
3. DONE: `TXLine` (0x0023cba8-0x0023ded4), the pieces, tabs,
   justification, drawing and hit-testing.
4. DONE for a view's one frame: `TXFrames`, `TXMonoSizeFrames`,
   `TXMonoFrame`, `TXSectFrames`, `TXDisplayChanges`, `TXFrameFormatter`,
   `TXMonoFrameFormatter`, `gFramesEditInfo` and `TXFormatter`.  NOT YET:
   the paginated side - `TXMultiFrameFormatter` (0x002415b0-0x00242704),
   `TXPageFrames` and `TXPageFormatter` (0x002413e0-0x00242a2c), which
   only a document laid out on pages needs.
5. DONE: `TXDisplay` and `TXHilite` (with `qd/ScrollRect.h`).  The
   Newton subclasses `TXNewtDisplay`/`TXNewtHilite` (a TView's visible
   region, the root view's caret and key view) come with `TXView`.
6. DONE: `Textension`, the containers (`TXContainer`,
   `TXStdContainer`, `TXLocalContainer`, `TXPrivateContainer`) and the
   edit commands (`TXCommand`, `TXEditCommand`, `TXKeyCommand`,
   `TXReplaceTextCommand`, `TXMoveTextCommand`) with undo.
7. `TXView` and the 39 natives, `TXNewtContainer` (a document as a
   NewtonScript frame), the ruler UI (`ShowRuler`), `TXVBOChars` (the text kept in
   a large binary - `stores/LargeBinaries.h` is there now) and
   `TXNewtStreamFactory`.  The demo: a `protoTXView` on the host with
   text set and typed, drawn and looked at.

`TXAttrObject::ReadPublicData`/`WritePublicData` are the base's empty
pair; the subclasses that put a style on a stream come with the runs.
