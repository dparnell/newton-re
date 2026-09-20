# Curiosities

Things found while reconstructing the MP2x00 ROM that are worth telling
somebody about: clever tricks, surprising decisions, bugs that shipped, and
idioms of Apple's ARM compiler that are easy to misread.

The per-subsystem pages under `docs/<area>/` explain how things work. This
page is for the things that made us stop and look twice. Each entry says
where it lives, so you can go and read it.

---

## `mov pc,lr` is not an empty method

`TDataView::GetTextView` (ROM 0x000a31c0) and `TDataView::GetHiliteView`
(0x000a31bc) are each a single instruction:

```
mov pc,lr
```

It is tempting to read that as a method that does nothing. It is not: on the
ARM, `r0` holds `this` on entry and the return value on exit, so a body that
touches nothing **returns `this`**. The source was `return this;` and the
compiler had nothing to do.

This matters. The reconstruction originally had `GetHiliteView` answering
nil, which is a different thing entirely - it meant nothing owned a
selection. Found when `TEditView::PositionCaret` asked a child for its text
view and kept getting nothing back.

*`src/views/DataView.cpp`.*

---

## Finding the text under a point by pretending to write an 'A'

`TEditView::TextContainingPoint` (0x000a8844) has to answer "which of my
children has text at this point?". You might expect it to walk the children
comparing rectangles. Instead it asks each child that holds data **how well
it would take the letter 'A' written at that point** - `HandleWord` with a
one-character string and no unit - and takes the best answer.

The editor reuses the recogniser's own routing to answer a geometric
question, so a tap and a written word always agree about where they land.
There is no second implementation to disagree with the first.

*`src/views/EditView.cpp`.*

---

## Typed text is made to look like handwriting

There is no text cursor on a Notepad page in the way a desktop has one.
`TEditView::JamText` (0x000ab70c) takes what you typed, measures it with
`TextBounds` into a box at the caret, and hands it to the same `HandleWord`
the recogniser calls when it has just recognised a word you wrote.

Typing and writing converge one step after the input arrives, which is why a
typed word and a written one land in the same place, get the same styles and
undo the same way. It also means the keyboard needs almost nothing of its
own: `RealDoCommand`'s key case is a dozen instructions ending in a call to
`JamText`.

*`src/views/EditView.cpp`.*

---

## A tap is not acted on where it arrives

Tapping the page to place the caret does not place the caret. The editor
notes *where* the tap was, sets a pending flag, and asks the root view to
idle it after the double-tap interval (`TEditView::RealDoCommand`,
0x000a4360). Only if no second tap has turned it into something else by then
does `TEditView::Idle` (0x000a9f64, reason 2) call `HandleTap`, which calls
`PositionCaret`.

Every tap on a page is provisional until the double-tap window closes. It
cost an afternoon to find, because `PositionCaret` was being called exactly
once - by the application opening - and never by a tap.

*`src/views/EditView.cpp`.*

---

## Handwriting tidies itself into columns

`TEditView::AlignBounds` (0x000a26c4) is why a page of Newton handwriting
looks neater than what you actually wrote. When a new paragraph is about to
go down, it walks the existing children and snaps the new bounds to line up
with a neighbour: five alignments per axis (left-to-left, right-to-right,
left-to-right, right-to-left and centre-to-centre), each within a ten-pixel
tolerance, and the tolerance re-opens when a much closer neighbour turns up.

The two axes are crossed, which is the clever part: things that overlap
*vertically* get their *horizontal* edges aligned, and things that overlap
horizontally get their vertical edges aligned. Two words on the same line
line up their left edges; two lines in the same column line up their tops.

`RangeDistance` (0x000a2670) is the primitive underneath: 0 when one range
holds the other, 1 when they merely overlap, and the gap between them when
they do not.

*`src/views/EditView.cpp`.*

---

## A paragraph is added by the undo machinery

`TEditView::AddForm` (0x000ab28c) does not add the view. It builds an
`aeAddData` command, sets the paragraph's context frame as its parameter,
and sends it through `TApplication::DispatchCommand`; the command handler
puts the child in the soup, posts the `aeRemoveData` that undoes it, and
hands the new view back through the command's own parameter.

So a paragraph you type arrives by exactly the same road as one you drop on
the page, and comes off the undo stack the same way. Nothing had to be
written twice.

*`src/views/EditView.cpp`.*

---

## The keyboard takes the short way through

`TEditView::AddNewParagraph` (0x000a1b2c) is 721 instructions, and most
of them are geometry: measure the text, line the result up with the page's
other children (`AlignBounds`), line it up with the ruled lines
(`AlignToLineSpacing`). One test at 0x000a1e94 decides whether any of that
runs:

```
teq r8,#0x0            ; is there a unit?
ldreq r1,[sp,#0x54]    ; no - is there an ink font?
teqeq r1,#0x0
beq  0x000a22c0        ; neither: jump over all of it
```

A written word arrives with a unit and goes the long way. A typed one
arrives with neither, because the keyboard has already measured its own
box in `JamText`, so it jumps straight to the tail: make the paragraph
form, add it, make it the key view. Typing is about eighty instructions of
a seven-hundred-instruction function.

The last of those is what makes the *second* keystroke cheap too. The new
paragraph's text view becomes the key view with the caret after the word,
so the next character never comes back to the editor at all - it goes
straight into `TParagraphView`.

*`src/views/EditView.cpp`.*

---

## A paragraph wraps at its parent's right edge, not its own

A word typed or written on a Notepad page becomes a paragraph exactly as
wide as the first character. Every character after it would wrap onto a
line of its own, because the only thing that can widen the view -
`FixupBBox` (0x001815b8) - takes the width its *lines* came out at.

What breaks the circle is two instructions in `LineLoop`'s constructor
(0x0010d9d0):

```
mov  r0,r5              ; the paragraph
add  pc,r1,#0x20        ; TextFlags()
tst  r0,#0x4
moveq r0,r7             ; not growing: its own bounds
addne r0,r8,#0x10       ; growing:     its PARENT's bounds
ldr  r0,[r0,#0x6]       ; .right
```

Bit 2 of a view's text flags means "size yourself to your text", and a
view that does lays its lines out to its *parent's* right edge. So the
paragraph grows rightwards, a character at a time, until it reaches the
edge of the page - and only then wraps.

`FixupBBox` then has a matching trick. It does not set its own bounds: it
offsets them back by the parent's contents origin, writes them to the
`viewBounds` slot and tells the parent `ChildBoundsChanged`. Setting them
directly would put the parent's scroll origin on a second time, and a
paragraph typed into on a scrolled page would walk down it a line at a
time.

*`src/views/ParagraphView.cpp`.*

---

## The mu-law coder never clamps

The 8-bit mu-law encoder in `sound/SampleConvert.h` has the shape of
G.711's, but on a 14-bit magnitude biased by 33 - and it never clamps. The
loudest samples overflow the exponent search and come out as a sign flip or
as silence.

This is a bug, it shipped, and the reconstruction keeps it: `test_SampleConvert`
asserts the wrong answers, because a port that produced the right ones would
not be the same machine.

*`src/sound/SampleConvert.cpp`.*

---

## `StringLeftTrim` never trims anything

One of the string natives walks its string looking for the first
non-blank - and then does nothing with what it found. Kept, commented, and
reproduced exactly.

*`src/frames/StringNatives.cpp`.*

---

## The dictionary engine can only do one thing at a time

The Airus dictionary engine keeps its working state in a single global,
`AE_Parms`. Every call - look a word up, add one, walk the trie - runs
through it. There is no second dictionary operation in flight anywhere in
the machine, ever, and the design quietly assumes it.

*`src/recognition/Airus.cpp`; `docs/recognition/README.md`.*

---

## An invisible view is the one you are meant to open

The boot went nowhere for a long time because opening an application did
nothing. The ROM's `AddView` refuses a context whose `viewFlags` lack
`vVisible` - and none of the root's preallocated children has it (the
Notepad's is 4, the button bar's 2560).

The answer is that opening a view does not go through `AddView` at all.
`aeAddChild` reaches `TView::AddChild`, which tail-calls `BuildView` on that
very context. `AddView` is for children a parent names in `viewChildren`,
where an invisible one is meant to be skipped; the open path is for a view
that has been waiting, invisible, to be asked for.

*`src/views/View.cpp`; `docs/newt/README.md`.*

---

## Reading a big-endian halfword with a rotate

Apple's ARM compiler had no `LDRH`. To read the halfword at address `X` it
emits `ldr rN,[X+2]`: the unaligned load makes the ARM rotate the aligned
word right by 16, which brings the halfword at `X` into the low half of the
register.

So `ldr r0,[sp,#0x3e]` is reading the halfword at `sp+0x3c`, not at
`sp+0x3e`. Ghidra models the load without the rotate and gets these the
wrong way round, which is how a `Rect`'s `bottom` reads as its `right`.
Every `Rect` and `Point` in the view system is touched by this idiom.

For an aligned halfword the compiler uses `ldr` plus `asr #16` instead,
which takes the high half. Both shapes are everywhere in the view code.

---

## Ghidra's decompiler is not a second opinion here

Twice in the same afternoon the decompiler's C was wrong in ways that would
have become bugs:

- `MakeParagraphForm` (0x0017a4d8) lost a nil test, so a path that reads a
  style array looked as though it called `Length` on nothing.
- `TEditView::HandleWord` (0x000abaa4) was given `TDataView::HandleWord`'s
  signature. They are different functions that share a name, and the ROM's
  own symbol table says so.

The citation check (`analysis/coverage.py --check`) caught the second one by
refusing the address. For the view system, transcribe from the disassembly
and use the decompiler for control flow only.
