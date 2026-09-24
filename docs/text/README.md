# The text engine (`src/text/`)

The Newton has two text systems, and they have almost nothing in common.

The one most of the machine uses is the **paragraph**
(`docs/views/README.md`, `views/ParagraphView.h`): a string and a styles
array in a NewtonScript frame, laid out into lines by the view system.
A note, a name field and a date's title are all paragraphs.

The other is a **document engine** — styled text with rulers, tab stops,
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
0x18 bytes — the element count, the element size, a lock count, the
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
only until the next allocation — which is why everything that walks an
array locks it first.

`TXLongTagArray` (0x00230944) is a `TXArray` whose elements begin with a
long kept in increasing order. `Search` (0x002309d4) is a binary search
that tries the two ends first — at or below the first element answers
index 0, past the last answers one past the end — and answers the index
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
29 — and the third is empty, which the engine does keep, because a style
run with nothing in it yet is a real thing.

That single representation is how the engine records every division of
the text: which characters each style run covers, which each line covers,
which each paragraph covers. An edit that adds five characters at
offset 20 is one `AddToElements` on every one of those arrays.

The two questions asked of it are:

- `OffsetToRangeIndex` (0x00230db0) — which range an offset falls in.
  An offset that is exactly a boundary belongs to the range that
  *starts* there; the `atStart` flag asks for the one that *ends* there
  instead, which is what a caret at the end of a run wants.
- `SectRanges` (0x00230e14) — what a stretch of text covers: the range
  it starts in and how far into it, the range it ends in and how much of
  that range is left over, and the run of ranges between them that are
  covered end to end. That last is what lets an edit replace whole runs
  and trim only the two at the edges. It answers the number of ranges
  the stretch *spans*, before either of the two corrections that turn
  that into the count covered whole.

## Not yet reconstructed

Everything else: the attribute objects a run carries (`TXAttrObject`,
`TXAttrValues`), the chunked character storage (`TXChars`,
`TXChunkedChars`), `Textension` and the runs, the formatter and the
lines, `TXView` itself and the forty-one `FTX...` natives that are its
script face.
