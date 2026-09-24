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

## The attributes (`text/TXAttributes.h`)

An *attribute* is a four-character tag and a value of up to twenty
bytes. The three the text runs carry are `'font'` (a family, as a
NewtonScript Ref), `'size'` and `'face'`; the rulers carry more.

`TXAttrValues` (0x00231340) is a list of them — a `TXArray` of 0x20-byte
elements, each a tag, a flag, a length and the value. It is how a set of
attributes is passed about: a style slip hands one down to be applied,
and a selection hands one back up saying what its runs have in common.
The flag says the value is an *object the list owns*, and `Remove`
(0x0023145c) — the array's `Remove` overridden — deletes those before
closing the array up.

`TXAttrObject` (0x002310e4) is the base of everything a run can point
at: a text style, a ruler, a graphics run. Its object is eight bytes,
and everything about it is virtual and does nothing — the subclasses
answer. What the base does supply is the reference counting
(`Reference` 0x002315bc, `Free` 0x002312fc: one reference to begin with,
and the object goes when the last one does, `FreeData` first) and two
walks over an attribute list:

- `Update` (0x00231148) applies every value of a list, last first, and
  answers the `GetAttributeFlags` of the ones that were applied or-ed
  together — which is what tells the caller how much of the layout has
  to be done again.
- `GetCommonAttrValues` (0x002311e4) narrows a list down to the
  attributes this object agrees about: an entry it does not share is
  taken out, one it does is written back with its own value. Run over
  every object of a selection in turn, what is left is what they all
  have in common — which is exactly what a style slip shows, and why it
  shows a blank where the selection disagrees.

`IsEqual` (0x00231298) on the base can only tell that two objects are of
the same *kind*: the same object, or the same class id. A subclass with
values to compare overrides it — `TXAdvancedRuler::IsEqual` compares its
tabs.

## The storage (`text/TXChars.h`)

`TXChars` is the face: count the characters, replace a range, copy a
range out, get one character, find one, ask for a run of them to look
at. Everything above it — the formatter, the lines, the view — only ever
talks to this.

`TXChunkedChars` (0x0023288c) is the implementation that makes a long
document possible. The text is kept in **chunks** of at most 512
characters and a `TXRanges` holds where each chunk ends, so an edit
touches one chunk or two rather than moving a whole document about. The
chunks themselves are handed out by three virtuals — `GetChunkPtr`,
`AllocateChunks`, `RemoveChunks` — which is how the same code works over
a binary in the heap and over a large binary paged in from a store.

`TXTextDescriptor` (0x00232820) is how text moves between them. It
describes a source or a sink, which may be a plain `UniChar` buffer, a
stream, or another `TXChars` at an offset, and one `CopyTo`
(0x00232914) moves characters between any two of those — including a
`TXChars` to itself, which is how the chunked storage ends up talking to
its own chunks. Both ends keep a position, so a descriptor can be handed
to one call after another and carry on where it left off.

### Putting text in

`Replace` (0x00232ad0) is the only way in, and it tries three things in
turn, each of which answers whether it managed it:

1. **`InsertInChunk`** (0x00232c60) — everything fits in the chunk it is
   going into. One `MungeChunk` and the chunk's end moves.
2. **`InsertUsingNearChunk`** (0x00231634) — the chunk and one of its
   neighbours have room between them. Characters are first shuffled
   across the boundary to make as much room in the chunk as the
   neighbour can take, and the new text is then split between the two.
   The neighbour before is tried first, then the one after.
3. **`InsertUsingExtraChunks`** (0x00231970) — as many new chunks as it
   takes are made after the one being written to. What was after the
   insertion point is moved to the end of the last new chunk *first*, so
   that it ends up where the new text will leave it, and the text is
   then poured through the chunks a chunkful at a time.

`MungeChunk` (0x00231eac) is what all three are made of: so many
characters at an offset of one chunk replaced by so many from a
descriptor. What follows them inside the chunk is moved by the
difference first — and moved *back* if the source fails part way, which
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
chunk — with the chunk's index, so the caller can release it — and
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
subclass supplies three things � how big the stream is, and how to read
and write at the position � and the base turns those into `WriteBytes`
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
`"TLZStoreCompander"` over it � which is what lets a document larger
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

## Not yet reconstructed

The rulers and their tab arrays, `Textension` and the runs, the
formatter and the lines, `TXView` itself and the forty-one `FTX...`
natives that are its script face. `TXAttrObject::ReadPublicData` /
`WritePublicData` are the base's empty pair; the subclasses that
actually put a style on a stream come with the runs.
