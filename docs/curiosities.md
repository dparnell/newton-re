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

## The ink is drawn behind the view system's back

`InkerLine` (0x002f7c64) takes no port, no pen state and no clipping
region. It is handed two points, a nib size and a pixel map, and it walks
the pixels itself. That is the whole point of it: the inker runs on its own
task, at the tablet's rate, and cannot wait for whatever the view system is
doing to finish.

The nib is a rectangle whose *top left* follows the line, so what gets
drawn is the parallelogram the nib sweeps out. That is why there are two x
accumulators rather than one - a leading edge and a trailing edge, both
stepped by the same dx/dy - and why they are pulled apart at the start by a
term that depends on the sign of the slope. A line going right has its
leading edge at the bottom of the nib; one going left has it at the top.

Each row is then filled a *word* at a time: a mask for the first word, one
for the last, and all-ones for everything between, with the masks and the
shifts read out of three little tables in `qdConstants` indexed by the
map's depth. A 32-pixel-wide word at one bit deep is filled with a single
`str`.

*`src/qd/Draw.cpp`; `src/recognition/Stroke.cpp`.*

---

## A key is given square corners by drawing it somewhere else

Every key of the soft keyboard is a rounded rectangle, except the ones at
the edges of the keyboard, which are square on the side they meet.
`TKeyboardView::DrawKeyFrame` (0x000fbde0) does not have a second way of
drawing those. It pushes the rectangle a *hundred pixels* out on the sides
that want square corners - so the rounded corner is off somewhere else
entirely - and clips the drawing back to where the key really is:

```
if (info & 0x80000) face.left   -= 100;
if (info & 0x20000) face.top    -= 100;
if (info & 0x10000) face.right  += 100;
if (info & 0x40000) face.bottom += 100;
ClipRect(&theKeyItself);
FrameRoundRect(&face, round, round);
```

Four bits and one clip, instead of a rounded-rectangle routine that takes
a corner mask.

*`src/views/KeyboardView.cpp`.*

---

## The shift key on the soft keyboard is sticky, and that is not a special case

`TKeyboardView::HandleKeyPress` (0x000fc300) puts a tapped key into the
key map as a press *and a release* - and then releases every modifier that
was down with it:

```
ch = KeyIn(code, true, this);
KeyIn(code, false, this);
if (shiftDown)   KeyIn(kShiftKey, false, this);
if (optionDown)  KeyIn(kOptionKey, false, this);
...
```

So shift stays on for exactly one key and then lets go by itself, which is
what you want when you are tapping keys with one pen and cannot hold two
down at once. Tapping shift itself toggles it instead, and the keyboard
dirties itself so the legends change.

The hit test is worth a look too. `TKeyboardView::InsideView` (0x000fc760)
answers false unless the point is actually on a key, so a tap in one of
the gaps between the keys is not the keyboard's at all and falls through
to whatever is underneath it.

*`src/views/KeyboardView.cpp`.*

---

## A pointer-sized word is fine until something has to be a parameter block

This reconstruction makes `Ref` and `ULong` pointer-sized so that the
object system can carry host pointers, which is the right call and costs
nothing almost everywhere. It costs something in exactly one place: a
structure whose bytes are *shared with something else*.

`kGestalt_Ext_VolumeInfo`'s parameter block is twenty bytes - four flag
bytes, a double, two longs - and the Extras drawer reads it back through
the template `['struct, 'boolean, 'boolean, 'boolean, 'boolean, 'Real,
'long, 'long]`. Declared with `ULong fDecibelRange[2]` the double lands at
offset eight instead of four on a 64-bit host, and every field after it
reads four bytes late: the drawer got the double's high word where the
number of volume settings should have been.

The same edge has a second side to it. A double sits on a *four*-byte
boundary in an ARM structure, where a host compiler wants eight, so
`UnmarshalValue` takes its bytes with `memcpy` rather than dereferencing a
`double*` - the host traps on the misaligned load that the ARM is happy
to do.

The rule that falls out: anything that is a layout rather than a value -
a parameter block, a persistent format, a packet - is declared in fixed
widths, and read by its bytes.

*`src/frames/Marshalling.cpp`; `src/sound/SoundChannel.h`.*

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

---

## Half a turn there, and half a turn back again

The world map's `CoordinateToLongitude` (0x00255280) is the inverse of
`LongitudeToCoordinate` (0x002551c0), which puts Greenwich in the middle of
the picture by adding half a turn:

    x         = FractMultiply(width, longitude * 2 + 0x20000000)
    longitude = (FractDivide(x, width) * 4 + 0x80000000) / 8

The way back adds half a turn again instead of subtracting it. It comes out
right because the two halves make a whole turn, which overflows a 32-bit
word to nothing - the code is written for a machine where that is simply
what addition does. Ported to a host where signed overflow is undefined it
has to be spelled out in unsigned arithmetic, or the optimiser is entitled
to decide that the sum can never be negative.

The same trust in the machine shows up in `CircleDistance` (0x00255348),
which needs the difference of two longitudes and takes it *without
unpacking either of them*: an integer Ref is the number shifted up by two,
so the difference of two integer Refs is already the Ref of the difference,
tag bits and all. One `RINT` then does the work of two.

And the answer is rounded to the nearest ten by `(d + 6) / 10 * 10`. Six is
not half of ten, so a distance of 344 km comes back as 350 rather than 340.

---

## One subtraction for two edges

`FLayoutTableX` (0x001eb4b0) asks twice whether the table still fits: is
this row's bottom above the view's, and is this cell's right edge inside
it. A `Rect` is four halfwords - top, left, bottom, right - so the two
questions want `bottom - top` and `right - left`, which sit one word
apart.

For the horizontal one the compiler loads both *words* and subtracts them
whole:

    ldr r0,[sp,#0x58]      ; the (bottom, right) word
    ldr r1,[sp,#0x54]      ; the (top, left) word
    sub r0,r0,r1
    mov r0,r0, lsl #0x10
    cmp r9,r0, asr #0x10   ; the low half: right - left

One subtraction computes both differences at once, packed into the two
halves of a register, and the shifts pick the one wanted. It is safe
whichever way round the rectangle is: a borrow out of the low half can
only corrupt the high half, which is thrown away.

The vertical test, a few instructions earlier, does not use the trick -
it loads the two halfwords separately through the `ldr [x+2]` rotate.
The same compiler, on the same rectangle, two different ways within a
dozen instructions.

---

## The machine says it is out of memory when it means it lost a key

The To Do list rolls its unfinished tasks over at three in the morning,
and asks for an alarm to wake it up. Its install script does that inside
a `try`:

    try
        AddAlarm("To Do", tomorrowAtThree, nil, theRollOverFunction, []);
        RegSetTime('todo, ...);
    onexception |evt.ex| do
        GetRoot():Notify(3, "To Do",
            "There is not enough space in the internal memory to
             automatically roll-over To Do tasks. Deleting some data and
             resetting may help.")

The handler catches `|evt.ex|` - *every* exception - and blames memory
for all of them. What actually went wrong in the reconstruction was that
`AddAlarm` starts by removing the alarm it is about to replace, the
index could not find the key to remove, and the store error came out
here as a full disk.

Two hours of the chase were spent on the memory: the heaps, the page
manager, `TotalSystemFree`. The message was never about memory at all.


---

## The drawer that filled up

The Extras drawer's icon for a package's form part is a soup entry, and
the ROM makes it in `ExtrasDrawer:HandleNewHighROMPart`:

    if info.partType = 'form and not soup:GetInfo('extrasState) then
        ... make the entry ...

`extrasState` is read there and **written nowhere in the ROM** - the
symbol appears in that function's literals and in no other code. The
guard is dead on a real machine, because the packages built into the ROM
are installed once at a hard reset and never installed again; the
question the guard asks can only arise on something that installs them
twice.

A host that installs them at every boot is exactly that something, and
the answer was a drawer with seven Docks in it. The fix is to write the
slot the ROM only ever reads.


---

## Three handles a date

`TDate` is a plain value class - a year, a month, a date and the rest -
with three Refs on the end for the format frames it fetches out of the
locale. In the ROM's constructor (0x00089ad0) each of the three is made
like this:

    mov  r0,#0x2            ; NILREF
    bl   AllocateRefHandle
    str  r0,[r4,#0x1c]
    str  r5,[r0,#0x4]!      ; r5 = 0: the handle's stack position

A `RefVar` writes `gCurrentStackPos` into that second word; writing 0
instead is what makes it a `RefStruct`, and a `RefStruct`'s handle
survives `ClearRefHandles`. It has to: a `TDate` is embedded in a view
(`TMonthView::fDate` at +0x60), and the view outlives by a long way the
event that constructed it. Had they been `RefVar`s, the next end of
event would have put all three handles back on the free chain while the
month view still held them, and the first write through one of them -
`fLongDateFormat = GetProtoVariable(...)` on the next draw - would have
put a pointer where the chain keeps an index. The allocation after that
reads the pointer as the index of the next free handle and the frames
heap is gone. (That is not hypothetical: it is precisely what the
reconstruction did until the three fields were changed, and the crash it
gives is a wild read inside `AllocateRefHandle` a good second after the
damage was done.)

There is no `__dt__5TDate` symbol anywhere in the ROM, so nothing ever
gives the three handles back by that name; whether a stack `TDate`'s
handles are reclaimed depends on the compiler having inlined the
member destructors at each site. A `TDate` that lives in a view is
never destroyed at all while the view is up, which is the case that
matters: three of the table's 256 entries belong to the month view for
as long as it exists, and the table grows (by 512 bytes at a time, the
collector sliding it down into the free space below it) when enough of
them accumulate.


---

## Two tables in the same eight bytes

`AngleFromSlope` (0x002aa530) is the arctangent the recogniser and the
gesture tests measure everything with: a 16.16 slope in, a whole number
of degrees out. It works by table lookup - a byte table says roughly
which degree the slope is in, and a table of the tangents of the half
degrees says whether to step on - and it has two of each, one pair for a
slope below one and one for a slope of one or more:

    0x00380d8e  64 bytes   the degree of a slope's fraction (frac >> 10)
    0x00380dd0  46 longs   tan(0.5 deg) .. tan(44.5 deg), then 0xffff
    0x00380e80  64 bytes   the degree of a slope's whole part (slope >> 13)
    0x00380ec0  46 longs   tan(45.5 deg) .. tan(89.5 deg), then 0xffffffff

The third table starts at 0x380e80. The second one, 46 longs from
0x380dd0, ends at 0x380e88 - eight bytes *past* where the third begins.
They are not two tables laid out badly; they are two tables sharing eight
bytes, and both are read as written. The trick is in the index: the
byte table is only ever reached as `table[slope >> 13]`, and that branch
is taken only when the whole part of the slope is at least 1, so the
index is at least 8. Entries 0 to 7 of the byte table can never be
asked for, and Apple put the last two tangents there - 0x0000fb92,
tan(44.5 degrees), and 0x0000ffff, the sentinel that no fraction can
exceed.

It saves eight bytes. It is also why a reconstruction that lays the four
tables out as four C arrays is bigger than the ROM's, and why
`analysis/romtable.py` is asked for 46 entries of the second table
explicitly: counting to the next symbol would have stopped short.

## The loop that scrolls until the caret is in sight

The editor a note is written on keeps the caret on screen with a
NewtonScript method called `checkCaret`, queued half a second after every
edit through `AddProcrastinatedSend`. Its shape is this:

    loop
        info := GetCaretInfo();
        ...give up if there is no caret, or it is not in this view...
        where := info.view:CaretRelativeToVisibleRect(box);
        if where = 'top then layout:PixelScrollBy(...)
        else if where = 'bottom then layout:PixelScrollBy(...)
        else break;

There is no iteration count and no guard: the loop ends only when
`CaretRelativeToVisibleRect` (0x00171e8c) stops saying the caret is above
or below the visible rectangle. The scrolling itself redraws the page, so
a loop that never ends does not merely hang - it rolls the display
upwards for ever, a line at a time, like a television with no vertical
hold.

What keeps it honest is the paragraph's line cache. The first question
`CaretRelativeToVisibleRect` asks is the cheap one: is the caret's
character offset past the end of the last cached line? For that answer to
be right, a paragraph whose text ends in a carriage return must cache the
empty line the return leaves behind, because that is the line the caret
is on. Leave that line out - as this reconstruction did until the bug was
found by typing "hello" and pressing return - and the caret is for ever
one character past the last line, for ever "below", and the page scrolls
for ever. A line nobody can see turns out to be the thing that stops the
screen rolling.


## Sixteen words in one handle

`TWordList` (0x0022eb28) is what a recogniser's readings come out as: up
to sixteen guesses at what was written, each with a score and a label.
The obvious way to hold them is an array of sixteen string handles. The
ROM holds them in **one**.

Inside the list's single handle the words are packed end to end,
separated by 0xFFFF and terminated by a NUL:

    h e l l o FFFF h e l l FFFF h e 0000

So a word ends at 0xFFFF as well as at NUL, and the area carries its own
pair of string functions - `Wstrlen` (0x0022ef8c) and `Wstrcpy`
(0x0022f190) - that stop at either. `ScanTo` (0x0022ed60) walks to the
n-th word by counting separators. `InsertLast` (0x0022ec90) appends by
overwriting the NUL that ended the last word with a separator and
writing the new word where it stood; the very first insert therefore
needs no special case, because the empty list already holds its own
terminator. The scores and labels stay as two arrays of sixteen
halfwords at the front of the object, which is where the sixteen-guess
ceiling comes from: `InsertLast` simply returns once the count reaches
it.

The same object goes further. `TWordList::operator new` (0x0022edb0)
keeps a pool of twelve lists in RAM (`gPreallocWordLists`, 0x0c107030,
0x360 bytes) and only falls back on the heap when all twelve are in
use; `operator delete` (0x0022ede0) decides which it is by comparing the
address against the pool's bounds. A slot is *free* when its handle
field is nil, which is the one thing both the destructor and `operator
delete` make sure of - there is no separate free list. The recogniser
makes and throws away word lists on every stroke the user writes, so
between the pool and the packing, a whole reading of a word costs one
allocation, and usually none at all.


## The recogniser remembers being corrected

A handwriting recogniser cannot tell a '0' from an 'O', or a '1' from an
'l' or a '|'. Nothing in the ink distinguishes them; the difference is in
what the writer meant. The MP2x00 ROM's answer is to remember being
corrected.

When a reading is chosen by hand out of the list of guesses, the
character goes into the *try string* (`AddTryString`, 0x0022ee38) - a
tiny buffer of the last characters the writer picked for themselves.
Afterwards, every word list the recogniser produces is passed through
`TWordList::Reorder` (0x0022f0a8), which looks at the first character of
that buffer: if it was a letter, the guesses '0', '1' and '|' are bubbled
towards the *back* of the list; if it was a digit or a slash, '0' and '1'
are bubbled towards the *front*. The recogniser's own scores are left
exactly where they are - only the characters move (`BubbleGuess` and
`SwapSingleCharacterGuesses` exchange first characters, not entries), so
this is a reordering of the guesses rather than a re-scoring of the
readings. Someone writing a date gets digits; someone writing a sentence
gets letters; and the machine works it out from the last correction
rather than from a mode switch.

Two details are worth keeping. Choosing a character that is *already* in
the try string clears the string first, because picking the same
character twice says the writer has settled on it rather than that they
are alternating between two - the buffer is a record of indecision, and
decisiveness empties it. And the two directions are not mirror images:
writing letters pushes three guesses away ('0', '1' and '|'), while
writing digits pulls only two back ('0' and '1'). The bar '|' is
something you can be written out of but never written into.

The buffer itself has a small bug, kept here. It is meant to be a ring
of two: an index cycles 0, 1, 0, 1 as characters arrive. But the wrap
happens *after* the write rather than before, so the third character
lands on the terminator instead of overwriting the first, the string
quietly becomes three long, and that third character stays there for
ever - no later write ever reaches position 2 again. Nothing notices,
because the only questions ever asked of the string are what its first
character is and whether a given character is in it somewhere.

---

## Writing in columns gives you spaces, because `FindTab` was switched off

`TParagraphView` has a whole apparatus for intuiting tab stops from
handwriting. `MinWidthToIntuitTab` (0x001733e4) works out how wide a gap
has to be before it counts as a tab rather than a space - four times the
average width of one of the word's letters, the narrow ones (i, l, I)
counting half, never less than 22 pixels. `NearTabStop` (0x00173cc4)
finds an existing stop within ten pixels of a coordinate. `AddTabStop`
(0x00173b34) adds a new one. `AddWord` (0x00172eb4) will put up to twelve
tab characters in front of a word, and `FindWordInRun`,
`FindWordInParagraph` and `SetFinderBelowParagraph` each ask, at the
right moment, which tab stop the word was written at.

They ask `FindTab` (0x00173ea0). In the shipping MP2x00 ROM that
function is:

```
FindTab:
  00173ea0  mov r0,#0x0
  00173ea4  mov pc,lr
```

Two instructions. Every caller gets "no tab", so `AddTabStop` is never
reached, the tab characters are never inserted, and the tab stops array
a paragraph can hold is never written by the recogniser. The only piece
that survives is `MinWidthToIntuitTab`, and only because
`FindWordInParagraph` reuses its number for a different question - is
this gap small enough to be a space?

`PreviousLineNeedsCR` (0x00173268) is the same story in two
instructions: it decides whether a carriage return is wanted before a
word, and always says no.

Whoever cut these left every caller in place. From the outside the
machine looks as though it is thinking about tabs on every single word;
it is asking a function that was answered once, at build time, with a
constant.

*`src/views/ParagraphView.cpp`.*

---

## The sixteen-bit dictionary walker tests a flag the wrong way round

Every dictionary lookup in the Newton can answer a fourth question
besides "is this a word": *if the word can only go on one way, which
character is it?* That is what offers you the rest of a word as you
write it. The walker works it out by stepping along the children of the
node it stopped at: if every one of them has a character set of exactly
one character, and they are all the same character, then that is the
only way the word can continue.

The eight-bit walker (`AL_Verify`, 0x0002bf78) stops that loop on the
*last* child:

```
  0002c134  tst r0,#0x4        ; the "last sibling" flag
  0002c138  bne 0x0002c158     ; -> hand the character back and stop
```

The sixteen-bit one (`AL16_Verify`, 0x0002b918) is the same function
again, written out a second time for two-byte characters - and there the
test is inverted:

```
  0002bb18  tst r0,#0x4
  0002bb1c  beq 0x0002bb3c     ; -> hand the character back and stop
```

So it stops at every child *but* the last. Two things follow. When a
node has several children, it hands back the first child's character as
though it were the only possibility - "a" in a dictionary holding "at"
and "an" comes back as "must be followed by t". And when a node has
exactly one child, which is the common case and the only one the answer
is meant for, it does not stop at all: it adds the node's size to the
last child's offset and carries on reading whatever bytes lie beyond the
end of the sibling list, leaving only when those bytes happen to
disagree.

Nothing crashes, because a ROM dictionary is followed by more ROM. The
feature simply gives the wrong answer in the sixteen-bit lexicons and
the right one in the eight-bit lexicons, which is a difficult thing to
notice when the two are reached through the same call.

*`src/recognition/Airus.cpp`; `test_Airus.cpp` pins both behaviours.*


## A table that is not in the ROM

The Newton's 129 built-in lexicons - the letters, the word lists, the
days and months, the phone and money and postcode lexicons - are reached
through one table of pointers, `gROMDictionaryData`. A dictionary
descriptor's `romDictID` is a slot in it, and `GetROMDictionaryData`
just indexes it.

The table itself is in RAM, and it is filled in at boot by
`InitROMDictionaryData` (ROM 0x0019b0d4), which is nothing but this,
about seven hundred instructions of it:

```
  0019b0d8  ldr r1,[0x19b384]
  0019b0dc  ldr r0,[0x19b388]
  0019b0e0  str r1,[r0,#0x10]   ; gEnum80Empty
  0019b0e4  str r1,[r0,#0x14]   ; gEnum80Empty
  ...
  0019b104  ldr r2,[0x19b38c]
  0019b108  str r2,[r0,#0x3c]   ; gLex8phone
```

Every one of the 129 addresses is a constant known at build time, and
every one of them points into the ROM. The table could have been a
static array in the ROM and cost nothing; instead it is 0x2b0 bytes of
RAM that must be written before any dictionary can be opened. It is what
a C file full of

```c
gROMDictionaryData[kEmpty1] = gEnum80Empty;
```

compiles to when the initialiser is written as code rather than as data,
which is what happens when the array is a global whose entries are set
by an `Init` function - the compiler has no way to know that what it is
being asked to do is describe a constant.

For the reconstruction this has a practical consequence: the table
cannot be read out of the ROM the way every other table is. It has to be
recovered from the instructions that write it, which is what
`tools/newton-rom/analysis/romdicts.py` does - it decodes the two
instruction forms, keeps a value per register, and refuses anything
else.

*`src/recognition/ROMDictionaryData.cpp`, `ROMDictionaryTable.cpp`
(generated); `test_Dictionaries.cpp` looks real words up in the result.*

## One field, two byte orders

Every word in a Newton dictionary can carry an *attribute* â€” a small
number stored beside it in the trie. How many bytes it takes is a
property of the dictionary; one, two and four are all allowed.

`PutAttr` writes it with `PutDictBytes`, which lays the bytes down high
one first â€” the first byte it writes is `value >> 24` for a four-byte
attribute, `value >> 16` for a three-byte one, and so on down. `GetAttr`
reads it back with `GetDictBytes`, the matching reader, also high byte
first. So far so consistent, and so it should be: everything
else in the format â€” the character sets, the sibling offsets â€” is
big-endian, because the machine is.

Then there is the other way of reading a dictionary. `AE8_NextSet9`
walks one row of the trie and hands each child to a callback, along with
whatever that child carries. It assembles the attribute like this
(ROM 0x0002aba8):

```
  0002aba8  ldrb r6,[r5,r0]        ; byte 0
  0002abb4  ldrbgt r1,[r0,#0x1]
  0002abb8  orrgt r6,r6,r1, lsl #0x8    ; byte 1 << 8
  0002abc0  ldrbgt r1,[r0,#0x2]
  0002abc4  orrgt r6,r6,r1, lsl #0x10   ; byte 2 << 16
  0002abcc  ldrbgt r0,[r0,#0x3]
  0002abd0  orrgt r6,r6,r0, lsl #0x18   ; byte 3 << 24
```

Low byte first. The same bytes, read the other way round from the way
they were written.

It has no consequences, and that is why it is still there. Every
dictionary the machine writes into â€” the user dictionary, the expansion
dictionary, the auto-add dictionary â€” is made with
`NewDictionary(kind, 1)`: one byte of attribute, where the two orders
agree. The multi-byte case exists in the format and in both readers, and
nothing in the ROM ever takes it.

*`src/recognition/Airus.cpp`; `test_Airus.cpp` walks a one-byte
dictionary, where the bug is invisible, exactly as the ROM does.*

## A hundred and eighty-one ways to spell a sound

The Newton's spelling corrector does not work by edit distance. It works
by *pronunciation*, out of a table of letter groups that get written for
one another:

```c
{ "a" }, { nil, 2 }, { "A" }, { nil, 3 }, { "ai" }, { nil, 4 },
{ "ay" }, { "ah" }, { "aa" }, { "aw" }, { nil, 6 }, { "ae" }, { "au" },
{ "augh" }, { "eig" }, { "eigh" }, { "ey" }, { "al" }, { "e" }, { "ea" },
{ "ei" }, { "i" }, { "ia" }, { "ie" }, { "io" }, { "o" }, { "oa" },
{ "oe" }, { "ou" }, { "ow" }, { "u" }, { "ua" }, { "ue" }, { "ao" }
```

That is the entry for `a`: an `a` in what was written may stand for an
`a` in the dictionary at no cost, for an `A` at a cost of two, for `ai`
at three, for `ay`, `ah`, `aa` or `aw` at four, and for two dozen more â€”
`augh`, `eigh`, `ough` â€” at six. There are 181 such entries, one for
every letter and every group of letters English spells a sound with, and
they are sorted so the matcher can stop early.

The corrector walks that table and the dictionary's trie *at the same
time*. At each step it asks the trie which characters can follow what it
has built so far, and only tries the table's alternatives that begin
with one of them â€” so it never wanders into spellings the dictionary
could not reach anyway. That is what makes it affordable on a twenty
megahertz ARM: the search is pruned by the data rather than by a cutoff.

The result is a corrector that is good at exactly the mistakes people
make. "seperate" comes back as "separate" because `e` may stand for `a`;
"recieve" comes back as "receive" because `ie` may stand for `ei`.
Neither is within one edit of the right word in the way a dictionary of
words would measure, and both are one *substitution* away in the way a
speaker would.

The table is a table of pointers in RAM, filled in at build time, and
each alternatives list overloads a pointer with a small integer: a value
below ten is not a spelling but the cost of the spellings after it, and
nine ends the list. `tools/newton-rom/analysis/spellmaps.py` follows the
pointers and writes the strings out, keeping that overload explicit.

*`src/recognition/Spelling.cpp`, `SpellMaps.cpp` (generated);
`src/host/demo/correct.ns` runs it on the machine.*

## Tapping a clipping twice copies it

When something is dragged out of a view and let go on the background the
Newton makes a *clipping*: a little icon of its label at the edge of the
screen that the pen can pick up again and drop somewhere else. Dragging
it off moves it â€” the clipping goes away, because it has gone into
whatever took it.

Except that `TClipboard::DragFromClipboard` (0x000a0380) begins like
this:

```c
ULong now = Ticks();
ULong since = now - gLastClipboardDragTicks;
gLastClipboardDragTicks = now;
return DragAndDrop(stroke, box, &box, nil, since < 80, dragInfo, nil);
```

The fifth argument is `copy`. So a drag that starts within eighty ticks
â€” a second and a third â€” of the *previous* one copies rather than moves:
the clipping stays where it is and a duplicate goes to the target. There
is no modifier key on a Newton and no menu in sight; the gesture is
simply "do it again quickly", and the machine keeps one global word of
state to notice it.

What makes it pleasant is that the rule is about the interval between
two drags rather than about a double *tap*. Dragging a clipping into a
note, then straight back to another note, leaves the clipping on the
screen â€” which is exactly what someone filing the same address into
three places wants, and they never have to be told the rule. Waiting a
second and a half before the next drag puts it back to moving.

*`src/views/ClipboardView.cpp`.*


## The clipboard hides from the button bar, but only on one side

A clipping's icon remembers which edges of the application area it came
to rest against in a `pin` slot â€” bit 1 left, 2 top, 4 right, 8 bottom â€”
so that turning the screen round can put it back against the same ones.
`FReOrientLabelForm` (0x0009f978) is the C function the icon's template
carries as its `ReOrientToScreen`, and each of its four arms has the
same shape:

```c
if (pin & 1)
{
    if (EQ(where, RSSYMleft))       // the button bar is on the left
    {
        bounds.right = appArea.right - appArea.left;
        bounds.left  = bounds.right - width;
    }
    else
    {
        bounds.left  = 0;
        bounds.right = bounds.left + width;
    }
}
```

An icon pinned to the left edge goes back to the left edge â€” unless the
button bar is *on* the left, in which case it is sent to the right
instead. The same exception is in `PointOnClipboard` (0x0009e2b0), the
question "was this drag let go on the background?", which counts a point
past the application area's edge as the background *unless* that edge is
the button bar's.

The reason is that the button bar is drawn on top of the application
area rather than beside it. Everything else in the view system can treat
the application area as the whole world; the clipboard cannot, because
it is the one thing that deliberately lives at the very edge of it, and
the edge is where the bar is. So the bar's position is threaded into two
otherwise purely geometric functions â€” and it is the only place in the
view system that has to know about it.

*`src/views/ClipboardView.cpp`.*

## The hilite line is drawn at both ends only

The pen held still on something and then drawn across it is how the
Newton is told what to select, and the line the pen leaves behind is the
only feedback there is. `DrawHiliteLine` (0x000a37ec) draws one segment
of it, stepping a pixel at a time along whichever axis the segment moves
further in â€” and then does almost none of the drawing:

```c
if (i <= 3)                 // the first four steps
{
    FillOval(&oval, pattern);
    corner = oval.topLeft;
}
else if (steps - 4 > i)     // everything in the middle
    pending = true;
else                        // the last four
{
    if (pending)
    {
        MoveTo(corner.h, corner.v);
        LineTo(oval.left, oval.top);
        pending = false;
    }
    FillOval(&oval, pattern);
}
```

Only the first four and the last four steps are filled ovals. Everything
between them is one straight `LineTo` from the fourth oval to the
fifth-from-last. A segment two hundred pixels long costs eight ovals and
one line rather than two hundred ovals, and looks identical, because an
eight-pixel round pen swept along a straight path is a rectangle with a
round cap at each end â€” and the caps are the only places the roundness
shows.

The same function draws the *first* segment of a stroke with a pen four
pixels fatter and then shrinks it back on the next step, which is what
puts a blob at the point where the pen was held still: the gesture
announces itself at the moment it is recognised, before the writer has
moved at all.

*`src/views/View.cpp`.*


## A document's chunk table is six bytes long

The text engine keeps a document's characters in chunks of at most 512,
and a `TXRanges` array says where each chunk ends. Writing that array
out to a stream is `TXChunkedChars::WriteChunksRanges` (ROM 0x002325f8),
and it does not write the array:

```cpp
long count = fChunks->GetCount();
TXWriteHalf(stream, count);
for (long i = 0; i < count; i++)
{
    long len = fChunks->GetRangeLen(i);
    if (len == fChunkSize)
        continue;                       // a full chunk is not named
    TXWriteHalf(stream, len);
    TXWriteHalf(stream, i);
}
TXWriteHalf(stream, 0);
```

Only the chunks whose length is *not* the default are named, each by a
`(length, index)` pair, and a nought ends the list. Reading it back
(0x00232704) walks the pairs and gives every chunk up to the next named
one the default length.

The effect is that a document which has just been typed — every chunk
filled to 512 characters except the last — has a chunk table of exactly
six bytes: the count, the last chunk's length and index, and the
terminator. It stays that small no matter how long the document is. A
document that has been edited for a while pays two bytes per ragged
chunk, and `Remove` runs neighbouring chunks back together whenever two
of them will fit in one, which keeps the ragged ones from accumulating.

Two hundred pages of text, and the index that finds any character in
them costs six bytes on the store.

*`src/text/TXChars.cpp`.*


## A decimal tab brings its own dotted leader

`TXTab::Set` (ROM 0x00245984) is four instructions of storing and one
test:

```cpp
void
TXTab::Set(int position, char kind, unsigned char fill)
{
    if ((unsigned char) kind == kTXTabDecimalPoint && fill == 0)
        fill = '.';
    fPosition = position;
    fKind = (unsigned char) kind;
    fFillChar = fill;
}
```

A tab stop carries the character that the run of space up to it is
filled with, and it is normally nought — a blank. The one exception is
a **decimal-point** tab that nobody gave a fill character to: it gets a
full stop.

A decimal tab is what a column of numbers is lined up on, and a column
of numbers with a label on its left is a table of contents or a price
list. So on the Newton you get the dotted leader by setting the tab you
were going to set anyway, and you never find out that there was a
choice. It is one line of code standing in for a whole preference.

The script side never offers the fill character at all:
`TabKindSymbolToNum` reads `'left`, `'center`, `'decimalPoint` and
`'right` out of a tab frame, and `FromObject` then calls `Set(position,
kind, 0)` — always nought. So *every* decimal tab a script makes has a
dotted leader, and nothing a script can say will change it.

*`src/text/TXRuler.cpp`.*


## The clone that is thrown away

`protoDictionaryCursor`'s `PrivateClone` is meant to hand back a second
cursor standing where the first one stands. `FAirusIteratorClone`
(ROM 0x0008f680) does this:

```cpp
Ref
FAirusIteratorClone(RefArg rcvr)
{
    RefVar copy(Clone(rcvr));
    new TAirusIterator(*GetScriptCursorRef(rcvr));      // made, and dropped
    SetFrameSlot(copy, RSSYMcursor, RefVar(GetFrameSlotRef(rcvr, RSSYMcursor)));
    RefVar cursors(GetFrameSlotRef(RefVar(GetFrameSlotRef(rcvr, RSSYMdict)), RSSYMcursors));
    AddArraySlot(cursors, rcvr);                        // the original, not the copy
    return copy;
}
```

The frame is cloned, a copy of the iterator is made — and then the
copy's `cursor` slot is filled in from the *original's* slot. The
iterator that was just built is never stored anywhere, so it leaks; the
two frames share one cursor, and stepping either of them steps both.
The `cursors` array, which is how a dictionary finds everything walking
it, is then given the original again rather than the copy.

The odd part is that this is what *saves* it. The copy constructor
(`TAirusIterator::TAirusIterator(const TAirusIterator&)`, 0x0002e2a8)
walks the source's state stack making a copy of each state, and then:

```cpp
    if (previous != nil)
        previous->fNext = copy;
    previous = source;              // the source, not the copy
    source = source->fNext;
```

It links each new copy onto the *source* state rather than onto the copy
before it, so the original cursor's stack ends up spliced onto the
copies from its second state on; and it never sets the new iterator's
own `fStates` at all, so the copy has no stack to walk. A copy that was
actually used would walk into whatever the allocator left behind, and
the original would be walking a chain of half-copies.

Because `PrivateClone` throws the copy away without ever destroying it
or reading from it, none of that ever happens. Two bugs, and the first
one hides the second.

*`src/recognition/AirusIterator.cpp` and `src/recognition/Words.cpp`;
`test_Dictionaries` pins the shared cursor.*


## The handwriting engine was written in Moscow

The MessagePad reads writing with an engine Apple licensed and shipped
as **Rosetta**. Nothing in the symbol table says whose it is, but the
names give it away:

```
neibour_susp_extr       glitch_to_super_min     is_umlyut
spec_neibour_extr       sub_max_to_line         lead_punct
Errorprov               conv_top_elem_to_ST     RestoreApostroph
```

`neibour` for *neighbour*, `umlyut` for *umlaut*, `Errorprov` for an
error provider: this is English written by Russian speakers, and the
engine is ParaGraph International's Calligrapher — the company Stepan
Pachikov founded in Moscow, whose recogniser replaced Apple's own in
NewtonOS 2.0 and is the reason the second-generation MessagePads could
read printing at all.

The join between the two is visible in the code as well as in the
names. Everything above `Rosetta*` is written in Apple's house style —
`TRosRecognizer::AllocateAndConvertStrokeForRosetta`, capitals and
`f`-prefixed fields — and everything below it is plain C with
underscores and abbreviations that run out of vowels
(`extract_num_extr`, `str_com`, `xt_st_zz`). The boundary is one file
thick.

*`src/recognition/Rosetta.h` draws that boundary explicitly;
`docs/recognition/README.md` has the layers.*


## The engine learns how tall you write, an eighth at a time

The word recogniser has to know how tall a capital letter is in the
hand it is reading, because nearly everything else it measures is a
fraction of that: how far apart the letters are, how far a descender
goes below the line, how big a dot has to be before it is a dot. It
starts with a number ParaGraph trained — 18.85 pixels — and then
learns yours, from every word it manages to read.

`WordRecogComputeCapHeight` (0x00274818) is the whole of it, and it is
four lines of arithmetic. Take the word just read. Look up a nominal
width for each of its characters in the engine's own table and average
them. Divide the width the writing actually took by that average, and
what comes out is how tall a capital must be for those characters at
that size. Then — and this is the nice part — believe an eighth of it:

```
	fRun[20] = 0.875 * fRun[20] + 0.125 * estimate;
```

So one word nudges the estimate and ten words move it properly, which
means a single misreading cannot send the engine off. It will not
believe an estimate smaller than the floor in its common info at all,
nor one more than two and a half times what it already had, and a word
it could not read — `FailureString`, four question marks — teaches it
nothing.

The measurement runs backwards through the recogniser, which is what
makes it work: the engine guesses the characters, and the characters
tell it how big your handwriting is, and knowing that makes the next
guess better.

*`src/recognition/WordRecog.h`; `docs/recognition/README.md` has the
layers.*


## Nine Gaussians are what the Newton knows about your handwriting

The word recogniser keeps twenty-two numbers it calls the *run*
(`fRun`, `recognition/WordRecog.h`), and until you see what the pairs
are it looks like an arbitrary block of trained constants. It is not.
The first eighteen are **nine Gaussians**, each stored as two numbers:
the mean of what has been measured, and the mean of its square. For a
distribution whose spread grows with its mean, that pair is everything
the classifier needs to score a new measurement.

You can read it straight out of the starting values `WordRecogReset`
writes. The first pair is 18.85 and 421.98; 18.85 squared is 355.3, so
the rest is 66.7, and the square root of that is 8.17 — which is
almost exactly the constant `WordRecogAddStroke2` multiplies by when it
works that second number out again. The same holds for all nine: 6.22
and 54.28 (spread 3.95), 23.10 and 643.0 (spread 10.47), 14.69 and
283.1 (spread 8.21), and so on. Every one is *mean squared plus spread
squared*, and the spread is a fixed fraction of the mean.

What the nine are is neater still. One of them is how big a single
stroke is. The other eight are **four measurements times two
situations**: the gap in front of a stroke, that gap as a fraction of
how big the writing is, and both of those again in the direction the
writing runs — each with one distribution for a gap *inside* a letter
and another for a gap *between* letters. The caller says which by
handing in a number between nought and one; under 0.4 the gap counts as
within, over 0.6 as between, and in the band in the middle it is not
counted at all, because the engine would rather learn nothing than
learn the wrong thing.

Each is learnt an eighth at a time — seven parts of what was there and
one of what was just measured — and then held inside a quarter either
side of what ParaGraph trained (double and half, for the stroke size).
So the engine bends towards your hand without ever being able to be
argued a long way from the hand it was taught on.

**And half of it does not work.** The four between-letter distributions
go through a routine that works the second moment out from a mean it
never changes, so it writes back the number that was already there.
The four within-letter ones move; their four counterparts are frozen at
the trained values for ever. The shape of the code says what was meant:
it is the other half of the learning routine with the two lines that
update the mean left out.

*`src/recognition/WordRecog.cpp`, `WordRecogAddStroke2`;
`docs/recognition/README.md` has the layers.*


## The grammar the Newton reads your writing against is a grammar of *kinds of word*

`ROMGrammar` (ROM 0x00366e0c) is eight grammars the handwriting engine
reads a word against, and a field asks for one by name: General, Date,
Numbers&Money, Numbers, Phone, Time, Money, PostalCode. That much you
could guess. What is in one is the surprise.

A grammar is a list of **kinds of word** — 46 of them across the eight
— and a kind of word is a *lexicon*: `numbers`, `money`, `hyphen`,
`endpunct`, `closequote`, `daymonth`, `Prefixes`, `Suffixes`,
`WorldPhone`, `FunnyPhone`, `SpellCheckIgnore`, `wordlike`. Each names
one of the 129 dictionaries built into the ROM, carries a score for
being the kind of word the writer is writing, and carries another score
for every kind that may follow it. `BiGrammar` means *bigram grammar*,
and the bigrams are over dictionaries rather than over characters.

The Phone grammar is the whole idea in five lines:

```
PhoneR_US_C  -> hyphen 458
hyphen       -> PhoneR_US_C 0, WorldPhone 458, FunnyPhone 804
WorldPhone   -> hyphen 458
FunnyPhone   -> hyphen 458
LexicalSymbols
```

A lower score is better, so after a hyphen, going back to a telephone
number costs **nothing at all** — which is exactly the shape of
555-1234, and why writing a telephone number into a phone field reads
so much better than writing it into a note.

Two more things it says out loud. `endpunct` and `closequote` score
0x7ffe — 32766, the engine's "never" — for being the *first* kind of
word, so a full stop can never begin a piece of writing, though it may
happily follow one and even follow itself. And the General grammar
holds six kinds called `~user`, `~null1` ... `~null5` whose dictionary
is nought: they are the slots an application's own word list is
dropped into when a field is set up, wired into the grammar in advance
so that a custom lexicon costs nothing to add.

*`src/recognition/ROMGrammar.cpp` is generated by
`tools/newton-rom/analysis/bigrammar.py`; all eight are written out in
`docs/recognition/grammar.md`.*


## The handwriting engine reads its weights through a second, uncached copy of the ROM

`BPNetEvaluate` is the bottom of reading a word: a back-propagation net
with 1002 byte-sized units and 90,540 connections, whose weights are
ninety-one kilobytes of trained bytes in the ROM. Its seventh
instruction is

```
ldr r1,[r0,#0x38]       ; r1 = net->fWeights, which is bpWeight at 0x003948f0
add r1,r1,#0x3500000
```

and nothing ever takes that constant off again. The weights are read
from 0x038948f0, which is a long way outside the eight megabytes of
ROM.

The answer is in the MMU table the machine starts from,
`g8MegContinuousTableStart` at ROM address 0x100. Seven entries of
`{virtual, physical, size, flags}`, and the third one is

```
VA 0x03500000  <-  PA 0x00000000   8192 KB   flags 0x802
```

— the whole ROM mapped a **second** time, with the cacheable and
bufferable bits clear. Everything else reads the ROM through the
cached mapping at 0x00100000; the net reads its weights through the
uncached alias of the same bytes.

Why bother? The StrongARM SA-110 has a sixteen-kilobyte data cache.
Streaming ninety-one kilobytes of weights through it, once, in order,
never looking at any of them twice, would evict everything — including
the unit array and the connection program, which are read constantly
and *do* want to be cached. So the engine reads the one thing it
cannot reuse through a mapping that does not disturb the cache at all.

That is a non-temporal load, about a decade before any processor had
an instruction for one, built out of nothing but a spare entry in the
page tables and a constant added to a pointer.

*`src/recognition/BPNet.cpp`; `docs/recognition/bpnet.md` has the table
and the rest of the evaluator.*


## What the Newton actually looks at when it reads your handwriting

The classifier at the bottom of the handwriting engine has 384 inputs,
and the engine's own tables say exactly what goes into them. There are
four groups (`recognition/NetPattern.h`):

| | what | inputs |
|---|---|---|
| `ImageSplatLimited` | a fourteen-by-fourteen picture of the writing | 196 |
| `StrokePUD` | twenty steps along it, and which way the pen was going | 180 |
| `AspectNorm` | how wide it is against how tall | 1 |
| `StrokeCount` | how many strokes it took | 7 |

196 + 180 + 1 + 7 = 384. That is the whole of it. Two of those groups
are worth a second look.

**The picture is drawn, not sampled.** The engine has a renderer of its
own that anti-aliases by *drawing big and counting*: a one-bit bitmap
four times the size, a pen made of eight pre-shifted stencils so that
putting it down is an OR and never a shift, and then a table that says,
for each of the 256 byte values, how much each output cell of that byte
gains. A set sub-pixel is worth 15, so a four-by-four block comes to
240 and a byte still holds it.

And the scale is *limited*, which is the bit with judgement in it.
Each axis wants to fill the grid, but it is held to at most two and a
half times life size, and then the two axes are held to within three
times each other. Without that a lower-case `l` would be blown up into
a letter-shaped smear and an `m` squashed flat, and the net would be
shown two things that look alike and mean nothing.

**The second group knows that writing is a movement.** `StrokePUD`
divides the whole length of the writing — every stroke, and the jumps
between them — into twenty equal steps, and walks it at a steady
speed. At each step it writes down which of eight directions the pen
is going (spread between two neighbouring buckets, which *wrap*,
because a direction does) and one more number: how much of that step
the pen was **up**.

So the Newton is not reading a picture of your writing. It is reading a
picture of your writing *and a recording of the gesture that made it*,
including the bits where you lifted the pen — which is why it can tell
a hand-drawn `5` from an `S`, and why writing the same shape in a
different order reads differently.

*`src/recognition/NetPattern.cpp` and `Render.cpp`;
`docs/recognition/README.md` has the layers.*

## The first letter the reconstruction read was a plus sign

The handwriting engine has been coming together from the bottom for a
while - the strokes, the renderer, the seven patternizers, the trained
classifier and its 91 kilobytes of weights - and none of it said
anything you could read. `CharBox`, the boxed-character recogniser, is
the first piece that joins the two ends together, and it is only about
two kilobytes of code: make a recogniser over a rectangle, put strokes
into it, ask what was written.

So the test draws an upright stroke and crosses it with a level one,
and asks. The answer:

    +   0xf100
    t   0xe500
    T   0x0100

and nothing else at all, out of 256 character codes. A plus sign,
near enough certain; a lower-case `t`, nearly as likely; and a capital
`T` a distant third, because the cross-stroke is halfway down rather
than at the top. Which is exactly right, and is what a person would
say too.

Nobody at ParaGraph or Apple wrote that ranking down anywhere. It is
what falls out of 91,124 trained bytes, a picture of the writing, a
recording of the gesture that made it, and thirty years in a ROM.

There is a second nicety in how the answer is put together. The
classifier has 134 output nodes and the engine deals in 256 character
codes, so a good many codes have no node of their own: they are
*compound*, two characters the net was never shown as a pair. Those
score the **product** of their two parts' outputs, which means the
engine has a considered opinion about a shape it has never seen, worked
out from the shapes it has. And because an output byte is widened to
16.16 by a shift of eight, the best any single character can ever score
is 0xff00 and not 0x10000. Nothing is quite certain.

*`src/recognition/CharBox.cpp`, `src/recognition/tests/test_CharBox.cpp`;
`docs/recognition/README.md` has "One letter in a box".*

## Which two points are nearest? Ask the taxi driver

The handwriting engine constantly needs to know how close two strokes
come to each other - it is how it tells the crossed strokes of a `t`
from the merely touching strokes of a `V`, and so how it decides where
one letter ends and the next begins.

`SegmentStrokeMinDistance` answers it by comparing every point of one
stroke with every point of the other. A stroke can easily be forty
points, so that is sixteen hundred comparisons, and a StrongARM has no
square root. So it does not use one. The thing it compares is

    |dx| + |dy|

the taxicab distance, two subtractions and two absolute values. It
picks the same pair of points as the true distance would nearly always,
and when it does not the two candidates are so close together that the
answer above does not change. Only when the winner is known is the
real distance worked out, once, with `FractSquareRoot`.

There is a second economy in the same function. If any pair of points
comes out at **exactly** nought - which happens whenever the engine has
cut one stroke into two, since the cut point is copied into both halves
- the search stops there and then. Nothing can beat zero.

*`src/recognition/Segment.cpp`; `docs/recognition/README.md` has
"Where one letter ends".*

## The Newton looks three strokes back, and one forward

When the handwriting engine asks how near a stroke is to its
neighbours, it does not look at the stroke before it. It looks at the
**three** strokes before it.

The reason is in how people write. A letter is often made of pieces
that are not consecutive: you write the word, and *then* you go back
and cross the t and dot the i. By the time the crossing stroke
arrives, the stroke it belongs with may be two or three back in the
order they were drawn.

And `SegmentMultiStrokeMinDistance` has one test that looks the other
way as well. If the **next** stroke starts further left than this one
does, the writer has gone back to add something - so the pair worth
measuring is the one before this against that next one, rather than
against this. Three lines of code, and they are what let you cross
your t's when you feel like it.

There is a related surprise in the same pass. `SegmentOverlap`, which
decides whether two strokes are part of one letter, measures how much
of the **line** the two share - their horizontal spans, and nothing
about their heights. The two halves of an `x` fill exactly the same
span and are linked at once. The upright and the bar of a `t` cross
each other, and are *not*: an upright is one pixel wide, so the bar
covers it completely while the upright covers a twenty-first of the
bar, and the mean of the two fractions comes to a little over a half -
under all three of the thresholds. A `t` is two segments at this stage,
and something further up has to put it back together.

*`src/recognition/Segment.cpp`; `docs/recognition/README.md` has "The
first pass: one stroke against its neighbours".*

## The Newton does not decide where your letters are

The obvious way to read handwriting is to cut the strokes into letters
and then read each letter. The Newton does not do that, and the reason
is that cutting is the hard part: `cl` and `d` are the same ink, and so
are `rn` and `m`.

So `SegmentMakeSegments`, the second pass of the segment layer, refuses
to choose. For a run of strokes it cannot tell apart it emits **every
grouping the strokes allow** - the first stroke on its own, the first
two, the first three, then the same starting from the second stroke,
then from the third. Three ambiguous strokes come back as six
candidate letters:

    (0,1) (0,2) (0,3) (1,1) (1,2) (2,1)

The classifier is then shown all six, and the bigram grammar and the
dictionaries decide which path through the lattice is a word. The
cutting and the reading are the same decision, made once, at the top.

It does prune. A grouping that would end in the middle of a run of
strokes the first pass decided were one letter - the two halves of an
`x`, say - is never offered, and each segment records in `fRealCount`
how many groupings were skipped on its account. Two x's written as four
crossing strokes come back as exactly two segments, with no lattice at
all, because there was never any doubt.

*`src/recognition/Segment.cpp`; `docs/recognition/README.md` has "What
it hands up is a lattice, not a partition".*

## An extra zero in the handwriting engine's spacing slider

The Newton lets you tell it how tightly you write - a slider of nine
settings with "normal" in the middle - and `SegmentSetWordSpacing` is
where that becomes a number the segment layer works to.

Below the middle it does a careful job. The factor ramps as
`0.15 + 0.85 x (n/5)`, so even the tightest setting still weighs a gap
at about a third of normal rather than closing it entirely. Somebody
sat down with real handwriting to pick those two constants.

Above the middle, the ROM computes `1 + 23 x (n-5)/4`. Twenty-three.
The constant in the instruction is `0x170000` where the pattern of
everything around it wants `0x17000`, one and seven sixteenths. So the
slider reads:

| setting | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| factor | 0.32 | 0.49 | 0.66 | 0.83 | **1.00** | 6.75 | 12.50 | 18.25 | 24.00 |

One step to the right of normal multiplies the expected gap by nearly
seven; the loosest setting asks for a space twenty-four times normal.
With the extra zero gone it would have run 1.00, 1.36, 1.72, 2.08,
2.44 and joined the bottom half smoothly.

It is not quite as bad as it looks, and that is probably why it
shipped. The number is only ever used through its natural logarithm -
taken once, here, in double precision, because the layers above *add*
it to a score - and the log of that range runs from -1.14 to +3.18. So
the top half of the slider is compressed rather than broken. But it
does not do what the bottom half does, and the asymmetry is an
accident rather than a decision.

(This is also the only floating point in the entire 200 KB engine.)

*`src/recognition/Segment.cpp`; `test_Segment` pins the whole curve.*

## Every score in the handwriting engine is a logarithm

The Newton's grammar of handwriting is full of numbers: each kind of
word costs something to be, and something more to follow whatever came
before it. `wordlike` costs 1105 in the General grammar; a hyphen after
a digit costs 4; never costs 0x7ffe.

Those numbers looked arbitrary until the two tables the engine converts
them with were read. They are **negative natural logarithms of
probabilities, scaled by five hundred**:

    score = -ln(p) x 500

`ArProbEncodeLu2[1]` is 5545, and `-ln(1/65536) x 500` is 5545.2.
`ArProbEncodeLu1[1]` is 3119, and `-ln(1/512) x 500` is 3118.9. An even
chance costs 347. And `0x7ffe`, the "never" that turns up all over the
engine's tables, is simply the largest score a short will hold - a
probability of about one in 10^28.

That one fact explains a great deal of the engine's shape. Costs are
**added**, everywhere, because adding logarithms multiplies
probabilities - so the classifier's opinion of a letter, the grammar's
opinion of the word it is in, and the segment layer's opinion of the
gap before it all combine with a `+`. The word-spacing setting is kept
as its logarithm for the same reason. There is no floating point in
any of it, and only one logarithm is ever actually computed (in
`SegmentSetWordSpacing`); everything else goes through
`ArProbDecodeLu`, `ArProbEncodeLu1` and `ArProbEncodeLu2`, which are
the two directions of that formula in 7 KB of ROM.

It also explains `BiGrammarModifyContext`, which builds the grammar for
a particular field. To say "this field wants times and numbers, not
words", it cannot just cross out the kinds it does not want: it turns
every score back into a probability, gives nine tenths of the
probability to the kinds the field wants and a tenth to everything
else, and turns them back into scores. Nothing is forbidden; it just
becomes expensive. That is why the Newton will still read a word in a
date field if you insist on writing one.

*`src/recognition/RosEngine.cpp`, `src/recognition/Rosetta.cpp`;
`docs/recognition/README.md` has "What a grammar's scores actually
are".*

## The Newton stores what it is reading backwards, and shares the ends

While the Newton is reading a word it is holding a few dozen guesses at
once - not finished guesses, but partial ones, one per path through the
lattice of candidate letters it is still considering. Halfway through
"handwriting" it may be holding "handw", "hanciw" and "haridw" and
thirty more.

Keeping those as strings would be wasteful in a way that matters on a
machine with four megabytes: they share nearly all of their text.
Every one of those three ends in the same `w`, which came from the same
piece of ink, and two of them share "han" as well.

So a reading is stored **backwards** - a linked list of single
characters, each pointing at the rest of the word *before* it - and
reference counted. The shared ends are stored once. Two four-character
readings that agree on three cost two cells rather than eight, and
dropping one costs only the character no other reading is still using.

The nice part is how a reading is named. Not by a pointer, but by a
16-bit number, and the number is cut up:

| | |
|---|---|
| `0xffff` | the empty reading |
| `0xf000` and up | a whole *set* of alternatives |
| anything else | table `(ref >> 5) & 0x7f`, slot `ref & 0x1f` |

The cells live in tables of 32 that are made only as the readings get
longer - 128 of them at most, which is 4096 characters of guesses in
16 KB. Because the table number is part of the name, a cell never has
to move once it has been made, and a reading costs two bytes to refer
to instead of four.

And it buys a free comparison: two readings with the same number are
the same reading, and the search does not have to look at either of
them to know it.

*`src/recognition/WordTails.cpp`; `docs/recognition/README.md` has "How
the search remembers what it has read".*

## Write "Rosetta!" three times and the Newton answers back

Buried in the Newton's lexical search - the part that decides which of
the many possible readings of your handwriting is the one to return -
there is a function called `SearchCheckHashHit` that has nothing to do
with reading handwriting at all.

Every reading, on its way out, is compared against eight words. Write
one of them **three times in a row** and the recogniser hands back
something else instead:

| write it three times | and you get |
|---|---|
| `larryy` | The Doctor is on. |
| `Larry` | larryy@apple.com |
| `Mondello` | Fine food 408/257-2383 |
| `Brandyn` | brandyn@brainstorm.com |
| `Rosetta!` | Hey, that's me! |
| `stafford` | bill |
| `Les` | lesv@angeltech.com |
| `lyon` | Richard |

Those are the people who built the thing. Larry Yaeger wrote the
Newton's *other* recogniser, the printing one; Brandyn Webb did the
neural network; Bill Stafford, Les Vogel and Richard Lyon are the rest
of the recognition group. "Mondello" is Larry Mondello from *Leave It
to Beaver*, with what appears to be a restaurant's phone number. And
`Rosetta!` - the code name of the cursive engine this is all part of -
answers "Hey, that's me!".

The counts are kept per word and every counter but the one just matched
is cleared on each reading, so the three have to be consecutive: write
`Rosetta!`, then anything else, then `Rosetta!` twice more and you get
nothing. It also means the egg fires on the *reading*, not on the ink,
so it works however you write it as long as the engine reads it right
three times running.

*`src/recognition/Search.cpp`; the two tables are generated into
`SearchEasterEgg.cpp`.*


## How the Newton tells `rn` from `m`

Write `rn` and write `m`.  The ink is nearly the same - downstrokes with
arches between them, give or take - and a neural net looking at the
picture cannot reliably tell them apart.  The Newton's does not try to.
What decides it is a piece of arithmetic in `GeoContextPenalty` that
nobody would guess was in a 1997 handheld.

The engine carries a **nominal drawing** of every character it can read:
sixteen numbers apiece, all as fractions of the cap height.  Where the
character's bottom sits above the baseline, how tall it is, how wide,
how much room it wants before it and after it, and how big its smallest
stroke ought to be - once for when it is written in a single stroke and
once for when it is written in more.  The numbers are exactly what you
would expect if you looked: an `i` is 0.221 wide and an `m` is 0.753; a
full stop's bottom is 0.058 above the baseline and an apostrophe's is
0.720; an `i` drawn in one stroke has a smallest stroke of 0.482, which
is its stem, and drawn in two it has 0.084, which is its dot.

Given two character codes and the two pieces of ink they would be read
from, the engine lays out the two nominal boxes side by side, with the
gap between them that the two characters say they want.  It lays the two
observed boxes beside each other likewise.  Then it brings both pairs to
the same size and the same place - subtract the mean, scale so the eight
edges come to sixteen between them - and works out the one remaining
scale factor that brings the observed nearest the nominal, which is an
ordinary least-squares fit.  What is left over is nine numbers: how
wrong each box's bottom is, its top, its width, its smallest stroke, and
how wrong the gap between them is.

And then, instead of adding the nine up, it puts them through a
symmetric nine-by-nine matrix as a quadratic form - `sum over i of v[i]
times the sum over j of M[i][j] v[j]`.  That is a **Mahalanobis
distance**: the residuals are weighed *against each other* rather than
one at a time.  The matrix is positive definite (a Cholesky
factorisation goes through), symmetric to the last bit in all thirty-six
off-diagonal pairs, and sits in the ROM as 81 signed 16.16 numbers with
no symbol on it at all.

The off-diagonal entries are what make it work, and they are all large
and *positive* among the four height residuals: `M[bottom1][bottom2]` is
6.53 against a diagonal of 8.27.  Two residuals of the same sign
therefore cost far more than two of opposite sign - both boxes being
too tall by one unit costs 29.7, one too tall and the other too short
costs 3.55.  That looks backwards until you remember what the fit has
already taken out: a common shift and a common scale are gone, so an
error the two letters *share* is one the fit could not absorb and is
genuinely damning, while opposite errors are just two letters sitting a
little differently, which handwriting does all day long.

That is the whole `rn`/`m` decision.  Two boxes seven wide and fourteen
tall with a one-pixel gap score 478 read as `rn` and 2487 read as `mm` -
five times as much - and almost all of the difference is in those four
height numbers, because two `m`s would be far wider relative to their
height than this ink is, and both letters are wrong about it in the same
direction.  Reading the same ink as `oo` costs 315.

The answer is then multiplied by twenty and truncated to a short,
because every score in this engine is a negative logarithm times five
hundred and the search only ever adds.  Because the search asks the same
question over and over while it works through one pair of segments, the
last hundred answers are kept in a little cache keyed on the two
character codes and thrown away whenever the pair of segments changes;
the normalised boxes themselves are worked out once per pair, on the
first question asked about it.

*`src/recognition/GeoContext.cpp`; the matrix is generated into
`GeoTables.cpp` and the sixteen tables into `RosCITables.cpp`.  The
scores above are `test_GeoContext`'s own.*
