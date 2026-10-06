# Curiosities

Things found while reconstructing the MP2x00 ROM that are worth telling
somebody about: clever tricks, surprising decisions, bugs that shipped, and
idioms of Apple's ARM compiler that are easy to misread.

The per-subsystem pages under `docs/<area>/` explain how things work. This
page is for the things that made us stop and look twice. Each entry says
where it lives, so you can go and read it.

---

## A backup that cannot be cancelled

While the docker sends a soup to the desktop (`SendSoup`, `BackupSoup`),
it calls `TDocker::CheckCancel` (ROM 0x00098f94) for each entry. This is
meant to look at the link at most every 90 ticks and stop if the desktop
has sent 'opca' (cancel). But the test is back to front:

    if (*lastLook + 90 <= Ticks()) return;

It returns when 90 ticks *have* passed since the last look. The last look
is recorded only when it does look, and it starts at nought. So once the
machine has been up for a second and a half, `CheckCancel` never looks
again, and a backup always runs to the end.

`BackupSoup` has a bug of its own nearby. An id that doesn't fit in a
short makes it announce a new base ('base'), but the base is never kept.
The following ids are still sent less nought, so every later id above
0x7fff makes another 'base'. Both bugs are ported as they are
(`comms/Docker.cpp`), and both are now fixed by default: a desktop can
cancel a backup, and the base is kept (`NEWTON_ROM_BUGS=1` for the ROM's
behaviour).


---

## The Newton's DES is not quite the standard's

The desktop connection's password exchange and the store passwords use DES
(`utility/DES.cpp`, from ROM 0x002d4420 onwards). The tables are the
standard's, but `DESKeySched` (ROM 0x002f7264) shifts each 32-bit half of
the key left one bit before PC1 selects from it. The published test
vectors therefore do not come out, and the bits DES leaves out are not the
key's parity bits. `DESCharToKey`, which turns a password into a key, sets
odd parity in bit 0 of every byte. That changes the key DES actually uses.

`DESCharToKey` has a quirk of its own. It encrypts the password four
characters at a time, each piece under the key made from the pieces before
it, starting from the key "W@h`bmtd". It then ORs the parity-corrected
bytes into the encrypted block rather than storing them. So a byte whose
low bit was cleared to make its parity odd keeps the bit set.

`test_DES` does not use the published vectors, because the ROM does not
match them. It runs the ROM's own `DESEncodeNonce`, `DESDecodeNonce` and
`DESCharToKey` on the ARM interpreter (`armcpu/ARMCPU.h`) over the ROM
image, and the reconstruction has to agree with them.

---

## The digit reader knows "good" is not 9009

ParaGraph's digit and number reader runs first in any field that allows
numbers, and handwriting is full of letters that look like digits. So after
all its geometry has decided the writing is a number, `Digits` asks one more
question (the unnamed static at ROM 0x002a0d74, reconstructed as
`LooksLikeAWord` in `recognition/ChunkDigitsMain.cpp`). It spells out the
characters it read, at most four, and checks them against a short list of
words it is known to misread as numbers.

- **"is"**: a 1 followed by a 5 written in one stroke. A 5 whose bar is a
  stroke of its own is a real 5, but a looping one is an s.
- **"good"**: 9009 and 9004. The 9s are g's and the 0s are o's; a d read
  as a 4 is the other spelling.
- **"goo"**: 900 with other strokes beside it.
- **"gas"**: 995 with one run of other strokes. This needs the 5 to be
  looping as well.
- **Question marks**: 7., .7, 7-, 7.- and similar.
- **"g//" and "g//1"**: 9// and friends.

Two full stops in adjacent chunks also count, and that test returns at once
without putting the object list's class back.

The per cent sign is caught from the other side. `SearchDigit_S` reads a
ring, a slash and a ring as `%` only when the slash runs from the first
ring's foot to the second's top and across both
(`recognition/ChunkSearchS.cpp`, `S_PercentSlash`).

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
character is and whether a given character is in it somewhere. Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour):
the index wraps before the write, so the string stays a ring of two.

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
notice when the two are reached through the same call. Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour):
the test is the eight-bit walker's.

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

Every word in a Newton dictionary can carry an *attribute* — a small
number stored beside it in the trie. How many bytes it takes is a
property of the dictionary; one, two and four are all allowed.

`PutAttr` writes it with `PutDictBytes`, which lays the bytes down high
one first — the first byte it writes is `value >> 24` for a four-byte
attribute, `value >> 16` for a three-byte one, and so on down. `GetAttr`
reads it back with `GetDictBytes`, the matching reader, also high byte
first. So far so consistent, and so it should be: everything
else in the format — the character sets, the sibling offsets — is
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
dictionary the machine writes into — the user dictionary, the expansion
dictionary, the auto-add dictionary — is made with
`NewDictionary(kind, 1)`: one byte of attribute, where the two orders
agree. The multi-byte case exists in the format and in both readers, and
nothing in the ROM ever takes it. Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour):
the attribute is assembled high byte first, as `GetAttr` reads it.

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
at three, for `ay`, `ah`, `aa` or `aw` at four, and for two dozen more —
`augh`, `eigh`, `ough` — at six. There are 181 such entries, one for
every letter and every group of letters English spells a sound with, and
they are sorted so the matcher can stop early.

The corrector walks that table and the dictionary's trie *at the same
time*. At each step it asks the trie which characters can follow what it
has built so far, and only tries the table's alternatives that begin
with one of them — so it never wanders into spellings the dictionary
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
it off moves it — the clipping goes away, because it has gone into
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
— a second and a third — of the *previous* one copies rather than moves:
the clipping stays where it is and a duplicate goes to the target. There
is no modifier key on a Newton and no menu in sight; the gesture is
simply "do it again quickly", and the machine keeps one global word of
state to notice it.

What makes it pleasant is that the rule is about the interval between
two drags rather than about a double *tap*. Dragging a clipping into a
note, then straight back to another note, leaves the clipping on the
screen — which is exactly what someone filing the same address into
three places wants, and they never have to be told the rule. Waiting a
second and a half before the next drag puts it back to moving.

*`src/views/ClipboardView.cpp`.*


## The clipboard hides from the button bar, but only on one side

A clipping's icon remembers which edges of the application area it came
to rest against in a `pin` slot — bit 1 left, 2 top, 4 right, 8 bottom —
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

An icon pinned to the left edge goes back to the left edge — unless the
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
otherwise purely geometric functions — and it is the only place in the
view system that has to know about it.

*`src/views/ClipboardView.cpp`.*

## The hilite line is drawn at both ends only

The pen held still on something and then drawn across it is how the
Newton is told what to select, and the line the pen leaves behind is the
only feedback there is. `DrawHiliteLine` (0x000a37ec) draws one segment
of it, stepping a pixel at a time along whichever axis the segment moves
further in — and then does almost none of the drawing:

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
round cap at each end — and the caps are the only places the roundness
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

The effect is that a document which has just been typed � every chunk
filled to 512 characters except the last � has a chunk table of exactly
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
filled with, and it is normally nought � a blank. The one exception is
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
kind, 0)` � always nought. So *every* decimal tab a script makes has a
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

The frame is cloned, a copy of the iterator is made � and then the
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
one hides the second. Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour):
the copy constructor links each copy to the one before it and sets the
copy's stack, and `PrivateClone` hands back the copy and registers it.

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
engine is ParaGraph International's Calligrapher � the company Stepan
Pachikov founded in Moscow, whose recogniser replaced Apple's own in
NewtonOS 2.0 and is the reason the second-generation MessagePads could
read printing at all.

The join between the two is visible in the code as well as in the
names. Everything above `Rosetta*` is written in Apple's house style �
`TRosRecognizer::AllocateAndConvertStrokeForRosetta`, capitals and
`f`-prefixed fields � and everything below it is plain C with
underscores and abbreviations that run out of vowels
(`extract_num_extr`, `str_com`, `xt_st_zz`). The boundary is one file
thick.

*`src/recognition/Rosetta.h` draws that boundary explicitly;
`docs/recognition/README.md` has the layers.*

Some of the names are not English at all but Russian written in Latin
letters. The circle finder asks whether a loop is a `vozvrat_move` -
*vozvrat*, a return: the pen coming back - and the pass that turns
sticks into arcs is `lk_duga`, *duga* being an arc (`arcs_processing`
sits under it, in English, for whoever came next).

*`src/recognition/LowCircle.cpp` (`vozvrat_move`); `lk_duga` is still
NOT YET.*


## The engine learns how tall you write, an eighth at a time

The word recogniser has to know how tall a capital letter is in the
hand it is reading, because nearly everything else it measures is a
fraction of that: how far apart the letters are, how far a descender
goes below the line, how big a dot has to be before it is a dot. It
starts with a number ParaGraph trained � 18.85 pixels � and then
learns yours, from every word it manages to read.

`WordRecogComputeCapHeight` (0x00274818) is the whole of it, and it is
four lines of arithmetic. Take the word just read. Look up a nominal
width for each of its characters in the engine's own table and average
them. Divide the width the writing actually took by that average, and
what comes out is how tall a capital must be for those characters at
that size. Then � and this is the nice part � believe an eighth of it:

```
	fRun[20] = 0.875 * fRun[20] + 0.125 * estimate;
```

So one word nudges the estimate and ten words move it properly, which
means a single misreading cannot send the engine off. It will not
believe an estimate smaller than the floor in its common info at all,
nor one more than two and a half times what it already had, and a word
it could not read � `FailureString`, four question marks � teaches it
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
the rest is 66.7, and the square root of that is 8.17 � which is
almost exactly the constant `WordRecogAddStroke2` multiplies by when it
works that second number out again. The same holds for all nine: 6.22
and 54.28 (spread 3.95), 23.10 and 643.0 (spread 10.47), 14.69 and
283.1 (spread 8.21), and so on. Every one is *mean squared plus spread
squared*, and the spread is a fixed fraction of the mean.

What the nine are is neater still. One of them is how big a single
stroke is. The other eight are **four measurements times two
situations**: the gap in front of a stroke, that gap as a fraction of
how big the writing is, and both of those again in the direction the
writing runs � each with one distribution for a gap *inside* a letter
and another for a gap *between* letters. The caller says which by
handing in a number between nought and one; under 0.4 the gap counts as
within, over 0.6 as between, and in the band in the middle it is not
counted at all, because the engine would rather learn nothing than
learn the wrong thing.

Each is learnt an eighth at a time � seven parts of what was there and
one of what was just measured � and then held inside a quarter either
side of what ParaGraph trained (double and half, for the stroke size).
So the engine bends towards your hand without ever being able to be
argued a long way from the hand it was taught on.

**And half of it does not work.** The four between-letter distributions
go through a routine that works the second moment out from a mean it
never changes, so it writes back the number that was already there.
The four within-letter ones move; their four counterparts are frozen at
the trained values for ever. The shape of the code says what was meant:
it is the other half of the learning routine with the two lines that
update the mean left out.  The reconstruction now puts the mean's update
back by default (`NEWTON_ROM_BUGS=1` for the frozen distributions).

*`src/recognition/WordRecog.cpp`, `WordRecogAddStroke2`;
`docs/recognition/README.md` has the layers.*


## The grammar the Newton reads your writing against is a grammar of *kinds of word*

`ROMGrammar` (ROM 0x00366e0c) is eight grammars the handwriting engine
reads a word against, and a field asks for one by name: General, Date,
Numbers&Money, Numbers, Phone, Time, Money, PostalCode. That much you
could guess. What is in one is the surprise.

A grammar is a list of **kinds of word** � 46 of them across the eight
� and a kind of word is a *lexicon*: `numbers`, `money`, `hyphen`,
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
number costs **nothing at all** � which is exactly the shape of
555-1234, and why writing a telephone number into a phone field reads
so much better than writing it into a note.

Two more things it says out loud. `endpunct` and `closequote` score
0x7ffe � 32766, the engine's "never" � for being the *first* kind of
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

� the whole ROM mapped a **second** time, with the cacheable and
bufferable bits clear. Everything else reads the ROM through the
cached mapping at 0x00100000; the net reads its weights through the
uncached alias of the same bytes.

Why bother? The StrongARM SA-110 has a sixteen-kilobyte data cache.
Streaming ninety-one kilobytes of weights through it, once, in order,
never looking at any of them twice, would evict everything � including
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
divides the whole length of the writing � every stroke, and the jumps
between them � into twenty equal steps, and walks it at a steady
speed. At each step it writes down which of eight directions the pen
is going (spread between two neighbouring buckets, which *wrap*,
because a direction does) and one more number: how much of that step
the pen was **up**.

So the Newton is not reading a picture of your writing. It is reading a
picture of your writing *and a recording of the gesture that made it*,
including the bits where you lifted the pen � which is why it can tell
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
accident rather than a decision.  The reconstruction now takes the extra
zero out by default (`NEWTON_ROM_BUGS=1` for the ROM's slider).

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


## The Newton learns what your spaces look like

Ask a handwriting recogniser where one word ends and the next begins
and you expect a threshold: a gap wider than so many pixels is a space.
The Newton does something else. It keeps **eight running Gaussians**
about your hand and asks four of them, every time, whether the gap it
is looking at is a space or a join.

They are four measurements in two situations. How far apart two pieces
of ink are *within* a word and *between* words, taken both between
their boxes and between the middles of their ink - and each of those
four both in pixels and in multiples of how big your strokes are. Every
stroke you write updates them, an eighth of the way towards what it
says, as a mean and a mean of the square, so that a standard deviation
is one square root away and no second pass over the data is needed.

None of the eight is used raw. Each is **pooled** first, with three
other estimates of the same thing: a nominal for a writer of ordinary
habits, scaled by how big this writing has turned out; the same
measurement in the other situation, rescaled by the constant ratio of
their two nominals; and the other two measurements of its group,
rescaled the same way. They are all measuring much the same thing, and
four noisy estimates of it beat one. The result is then held to between
a quarter and four times the nominal, so a few strange strokes cannot
run the model away - which is what makes it safe to learn from every
stroke without ever asking whether the stroke was any good.

The question itself is then the textbook one. Below the within-word
mean it is certainly a join; above the between-words mean it is
certainly a space; in between, the difference of the two squared
z-scores is the log-likelihood ratio of the two Gaussians, and a
logistic curve turns that into a probability. The four probabilities
are averaged and compared against a threshold - a half, or nine tenths
if you have told the machine your writing is joined up.

And the slider in the Handwriting Recognition preferences, the one that
says how far apart you leave your words? Its setting is kept as a
**logarithm**, and it is *added* to that log-likelihood ratio. Moving
the slider does not change a threshold; it changes your prior.

For a machine with 4MB of RAM and no floating point, written in 1996,
that is a startling amount of statistics to spend on the question of
whether you meant a space.

*`src/recognition/Segment.cpp`'s `SegmentWordXGap`; the eight Gaussians
live in `WordRecog::fRun[2..17]`, the nominals in `kSegGapNominal`, and
`test_Segment` drives the whole thing.*

## The Newton squares your boxes with a constraint solver

Draw a wobbly box on a Newton and a moment later it is a crisp rectangle.
It would be natural to guess that the recogniser simply rounds each side
to the nearest level or upright line. It does something far more
ambitious: it treats the shape as a **system of geometric constraints**
and solves it.

Every side is measured - length in whole pixels, direction in whole
degrees - and the lengths and directions are each clustered (`TTrend`, a
little one-dimensional clustering with running Gaussian-ish spreads that
merges clusters when the gap between them is small beside their spread).
The direction clusters are then related to one another: two about 90
degrees apart are made exactly perpendicular, and a cluster half way
between two others becomes the axis they are mirrored in. Out of all
that come **linear equations over the shape's edge vectors**: these two
sides parallel and in a ratio of exactly 2, those two at equal angles to
the axis, and every side adding up to nothing so the shape still
closes.

There are usually more equations than the shape has freedoms, so they
are not solved; they are *minimised*. Each is squared into one quadratic
form, its gradient worked out once as a linear form per variable, and
the edge vectors started from the shape as you drew it and moved
downhill by **Polak-Ribiere conjugate gradients** - Numerical Recipes'
`frprmn`, with `linmin`, `mnbrak` and Brent's method with derivatives
(`dbrent`) under it, all transcribed into 16.16 fixed point on a CPU
with no floating point. The answer is then stretched back over the box
the shape originally covered, so the tidied shape sits where you drew
it.

Two touches give away that it was written by someone who had used the
book in anger. The line search starts from a step of an eighth, not
one. And when the Polak-Ribiere factor comes out larger than 10, the
code computes the new direction as `h + g/gamma` instead of
`g + gamma*h` - the same direction, scaled down, so the fixed-point
arithmetic does not overflow.

(One more oddity: the directions are measured from the *vertical*, so to
this code a level line is at 90 degrees.)

*`src/recognition/ShapeEquations.cpp` writes the equations,
`src/recognition/ShapeSolver.cpp` minimises them, and
`src/host/demo/shapes.ns` shows the result; `test_ShapeDomain`'s
`TestSolver` and `TestEquations` drive them directly.*

## The Newton has two handwriting recognisers, and your handwriting style picks one

The MP2x00 ships two complete word recognisers: Apple's own Rosetta for
printed writing, and ParaGraph International's cursive "xr" engine, which
reads writing as a string of arcs, hooks and loops matched against a
table of every way each letter may be written.  Nothing in the
recognition preferences says "recogniser": the Handwriting Style slip's
letter set does it - set 2, printed, puts Rosetta in use and every other
set ParaGraph's (`SetUpRosetta`/`SetUpParaGraph`, called by
`ReadCursiveOptions`).  The Letter Shapes slip, where the writer taps the
ways they do and do not write each letter, only exists for the cursive
one.  And whether Rosetta cuts a ligature in two is decided by asking
Gestalt how fast the processor is: faster than 90 MHz, it can afford to.
(`recognition/WordRecognizer.h`, `Recognizer.cpp`.)

## The reconstruction has the Newton's 2010 bug, and sets the clock back to 1992

Walk the reconstructed machine through its Setup assistant on a 2026
host and everything is right up to the time page: the date page shows
September 2026 with today picked out, the time page shows the time.
Tap Continue and the machine thinks it is Thursday 17 September 1992.
The status bar, Dates and every note stamped afterwards agree.

That is not a host bug. It is the Newton's own "2010 problem",
reproduced bit for bit. NewtonScript integers are 30 bits (a Ref's low
two bits are its tag), and `TimeInSeconds` (ROM 0x00089b64,
`intl/Dates.cpp`) answers the seconds since 1 January 1993 as one. That
count passed 2^29 - the largest positive 30-bit integer - on 5 January
2010. The Setup time page reads the clock through `TimeInSeconds`,
adjusts it to the hour and minute on the digital clock, and writes it
back through `SetTimeHardware` (`FSetTimeInSeconds`, ROM 0x0008a20c).
At 08:18 on 27 September 2026 the count is 1,064,650,680 seconds, which
wraps to -9,091,144 - and 9,091,144 seconds before 1993 is 17 September
1992. (2^30 seconds is 34 years and ten days.)

Real Newtons did exactly this from 2010 on, which is why the Newton
community's "Fix 2010" patch exists. The reconstruction keeps it: the
ROM's arithmetic is what is being preserved, and `MAKEINT` on the host
wraps at 30 bits just as the ARM's `mov r0,r0,lsl #2` does.

Where: `FTimeInSeconds`/`FSetTimeInSeconds` in `src/intl/Dates.cpp`;
seen by walking Setup with `src/host/demo/assist-tasks.ns`.


## A native that reads an argument nobody passes

`MoveCorrectionInfo` moves the corrector's record of a word from one view
to another. Its C function (ROM 0x00079c98,
`FMoveCorrectionInfo__FRC6RefVarN41`) takes five RefVars - the receiver,
the source view's name, the offset, the destination's name and the new
offset - but the ROM's native function table says it has three arguments.
The interpreter passes the receiver and three arguments in r0-r3, and the
fifth parameter, which ARM's calling convention puts on the stack, is read
from wherever the interpreter's own stack happens to be: the word's new
place is whatever the interpreter left there. The same function also hands
both offsets on as the Refs they are (four times the integer), so even with
a real new offset the word would be looked for at four times where it was
asked. No ROM script calls it; it was evidently never used.

The reconstruction keeps the Refs and, having no ARM stack to read, takes
nil for the missing argument (a `DEVIATION:`). `src/views/tests/
test_Views.cpp` pins what that does.

## Two ways of losing a Ref's tag

`FindShape` keeps the nearest shape so far in a path array whose first slot
is its distance as a NewtonScript integer, and a new find replaces it when
it is no further away. `DoFindShape` (ROM 0x000e1be0) compares the new
distance - a plain C integer - with that slot's raw Ref, which is the
distance shifted left two bits. So a shape up to four times further away
than the one already found still wins, and with nothing found yet the limit
is 0x200: the Ref of 128, read as 512 pixels. Kept, and commented, in
`src/views/ShapeVerbs.cpp`.

## Where one word of ink ends is decided by a little neural net

Ink that nobody read is still cut into words, and the cutting is done by
ParaGraph's word segmenter: every stroke is laid along a histogram of the
line (a byte for every half pixel), the runs of empty columns become
gaps, and each gap is put to a small trained net - eleven measurements of
the gap and the line, through an 11x11 matrix and 120 Gaussian cells for
each of "a space" and "not a space" - whose answer, with the writer's
spacing setting, says whether the next stroke starts a new word.  The
writing's slope it lays the strokes in by is learnt from their steep
steps, a step down counting eight times as much as a step up.  And after
all that, when a script asks `Recognize` to read strokes that turn out to
be ink, the ROM groups them faithfully - and then drops them, because the
word info they are wrapped in has no word and `AddWordInfo` keeps only
those that do.  (`recognition/WordSegment.h`, `InkGroups.h`,
`views/Rerecognize.cpp`.)


## The journal plays every stroke but its last point

The journal (`testing/Journal.h`) is how Apple's test tools wrote on the
Newton: strokes recorded as tablet sample words and played back into the
tablet buffer at the pace they were written.  `JournalReplayHandler::
GetNextTabletSample` hands out a stroke's samples as they fall due, and
when the last has gone it answers a pen-up instead - in the same word, so
the pen-up *replaces* the stroke's final sample rather than following
it.  Every replayed stroke is one point short.  For most strokes nobody
would notice, but it is enough to change what the recogniser reads: the
host's demo writes "ton", plays the four strokes back, and the page gets
"tor" - the `n` has lost the end of its last leg.  The function that puts
the samples in is also spelt `JournalInsertTabletSamople` in the ROM's
own symbols, and a replay leaves the tablet bypassed - the real pen shut
out - until the test agent is told to stop.


## A running mean that starts from whatever the heap held

Rosetta keeps a running mean of how tall the writer's strokes are, in the
word recogniser's state block at +0x68 (`WordRecogAddStroke2`, ROM
0x002751cc: `mla` the old mean by the count less one, add the new
height, `__rt_sdiv` by the count).  Nothing ever sets it first: the
block is `NewPtr`'d and +0x68 is written in exactly one place, the mean
itself.  The mean is only taken over strokes tall enough to count, while
the counter it divides by goes up for every stroke, so if the first few
strokes are small the first one that counts averages the rubbish in
((rubbish x (n - 1) + height) / n), and `WordRecogIsStrokeTooWide` reads
the rubbish outright - tripled - for any stroke before one has counted.
A large enough rubbish value overflows those products; the ARM wraps and
the engine carries on with some other number.  The host keeps the bug with the ARM's
arithmetic; what gave it away was the sanitizer trapping the overflow on
the few runs where the heap happened to leave a big number there.


## Handwriting measured in units of its own height

ParaGraph's cursive reader does not keep a word's points in pixels for
long.  The first thing its low level does (`transfrmN`, ROM 0x001baaf8,
under `BaselineAndScale`) is find two lines under the writing - the feet
of the small letters and their tops, a y for every point of the trace -
and then rewrite every point's y against them: the upper line becomes
0x2796, the lower 0x27e6, 80 units apart whatever size the word was
written, with ascenders and descenders carrying on at the same scale
above and below; x is rescaled to the same 80-units-to-the-height from
the box's left edge, plus 0x50.  Everything after that - the circle
finder, the arcs, the crossings, the letter matching - compares against
fixed numbers like 0x2746 and 0x2836 (`DefLineThresholds`, ROM
0x002f8b0c), so a small word and a large one are the same word to it.
The finder also reports how sure it is of the two lines (45 to 90, by
how many extrema it found and how little it had to correct), and a
synthetic run of eight arches 40 pixels high on y = 200 comes back from
the reconstruction as exactly that: height 40, base 200, sure 90/90.

## A "stick" that lies down

ParaGraph's cursive reader sorts every stroke of a word before it reads
it, and one of its kinds, mark 7, looks like it ought to be an upright
stick - an l, the stem of a t.  It is the opposite.  `SPDClass` (0x0032f960)
accepts a stroke only when its longest piece's slope - dy over dx, in
hundredths - is *below* a trained limit of 25 to 90, so only a level
stroke qualifies: a dash, a hyphen, a t's bar.  The trained table of the
bend allowed goes further: indexed by the bands a stroke's top and bottom
fall in, it holds -32767 ("never") for a stroke that runs from the very
top of the word down to the line, which is exactly what an l is.  What
the reader then does with a mark 7 confirms it: it straightens it in the
trace (`FantomSt`) and looks for the *upright* sticks that cross it
(`FillCross` over `VertSticksSelector`'s list) - a t's bar and its stem.
(`src/recognition/LowPict.cpp`; `test_LowLevel`'s `TestPict`.)

## Reading handwriting with one register

The cursive reader matches a letter against the writing by dynamic
programming, and its innermost loop - one prototype xr against every
position of the word - is the one piece of ParaGraph's engine written by
hand in ARM assembly (`CountXrAsm`, 0x0038cd38, 308 bytes, and a twin
that also records its choices).  It is built around the prototype's
tables being *nibbles*: what each of 64 xr types, 16 heights, 16 shifts,
16 links and 32 directions is worth, two to a byte, 0x4c bytes for the
whole prototype.  The xr being read is loaded as two words, so its type,
attribute, penalty and height are one register; `lsr #25` of it is the
type's byte in the table and bit 24 says which half, and shifting the
same register left by 24 and then by 8 brings the next field's index into
the same position - five table lookups without ever unpacking the xr.
The prototype's own first word is rotated right by eight on the way in,
which puts the cost of skipping the prototype in the top byte and its
"only next to a break" flag in the bottom one, both testable without
another load.  The traced twin gets one thing different: where the plain
loop lets a tie go to the diagonal (`movle`), it lets it go to the skip
(`movgt`), so the path the layout walks back is not always the one the
score came from.  (`src/recognition/XrMatrix.cpp`; `test_XrMatrix`.)

## A letter table that carries programs

ParaGraph's letter table does not only describe what each letter looks
like; next to its prototypes it ships, for most variants of most letters
(373 of 374 in the U.S. ROM), a *program* that checks a reading of the
letter after the fact.  Each is a handful of queues of bytecode for a
fifteen-entry stack machine: push a constant or a field of one of the
xrs the letter was read from, do arithmetic, compare *fuzzily* (less,
greater and equal answer 0 to 20, not true or false) and call any of 74
geometry functions - how much the trace between two points bends, where
the sharpest corner is, the box of the letter or its neighbour, whether
another letter of the answer is claiming the same xrs.  The weights are
all negative and each queue is floored, so a rule can only take points
away: every queue measures something that should *not* be true of a
good letter.  One of an o's, decoded: 300 times (how far its right side
comes back up plus how far its top has drifted right) over its height,
fuzzily less than 150.  The rules only run to settle a close call between
two good answers.  (`src/recognition/XrPostCalc.cpp`,
`docs/recognition/README.md`, "The post-processing".)

## The absolute value of nought

The digit reader starts by working out the line the digits sit on from
the boxes of the strokes (`DefHeightsForNumber`, ROM 0x002853ec).  Before
it averages them, a helper (0x00285554) is meant to join a stroke's box to
the one before when the two overlap across the line and are close enough
in height - two pieces of one digit written with a lift between them.
The test for "close enough" is `HWRAbs(0) * 3 > height`: the argument
passed is a register the function set to nought at its start and never
loaded (`mov r9,#0x0` ... `mov r0,r9; bl HWRAbs`).  Three times the
absolute value of nought is never more than a height, so the join never
happens, on any Newton, and every stroke's box is averaged on its own -
the next helper, which drops or joins the *small* boxes, is the only one
that does anything.  Whatever variable was meant (the gap between the two
boxes, most likely) was lost somewhere between ParaGraph's source and the
compiler; the reconstruction keeps the call as it is
(`recognition/ChunkDigits.cpp`, `JoinOverlappingBoxes`).


## The Newton keeps your letters as cosine transforms

When a field asks for it (`bigLearningEnabled`), ParaGraph's cursive
reader learns what *your* letters look like, and the way it keeps them is
the idea behind JPEG.  Each letter the writer settles on is taken out of
the trace (`LearnPartsCopy`), scaled to its box, resampled at sixteen
points evenly spaced along its length, and each of x and y put through a
sixteen-point discrete cosine transform (`FDCT16` at ROM 0x0007a3e8,
over the `_2C16` cosine table at 0x0037415c; `recognition/OrthoDB.cpp`).  The
constant term - where the letter is - is thrown away, the next seven
coefficients of each axis are kept, the fourteen normalised to unit length
and rounded into a byte each.  A letter of any size and any number of
points becomes fourteen bytes, and two letters are compared by the
distance between their fourteen numbers: the low frequencies are the
letter's overall shape, and the wobbles of the pen are in the ones left
out.  The database holds up to 32 of these per letter and stroke count in
24 KB, and *Occam* - the name is ParaGraph's - only keeps a new one when
it would change an answer: when the nearest letter to it is a different
one, or the right one only just.

## The bitmap rotator has a special case for fax pages

`MungeBitmap` turns a bitmap a quarter turn in memory, 32 columns by 8
rows at a time - except when `Tilable` (ROM 0x00040ee0) says the bitmap
is one of four exact sizes: 0x3c6f0, 0x78de0, 0x3cc00 or 0x79800 bytes.
They are all rows of 216 bytes - 1728 pixels, the width of a Group 3
fax line - by 1146, 2292, 1152 or 2304 rows: a fax page at standard and
fine resolution, in two page lengths.  A received fax lives in a large
binary on a store, far too big for the heap, so `RotTiledBitmap` makes
the turned copy on the same store with the same compander and fills it a
tile at a time (`TTile`), never holding the page in memory.  The comments
in the reconstruction had called these "screen-sized"; the MP2x00's
screen is 40 bytes by 480 rows.  (`qd/MungeBitmap.cpp`.)
## Every page of a stored package knows where its objects begin

A package installed from a card or the Connection is kept on a store in
1K pages, each compressed on its own, and mapped into memory a page at a
time as it is touched - so any page may be the first one read, and its
frames objects must be made right (their pointer refs moved to where the
package is now mapped) without looking at the page before.  The writer
therefore walks the frames part's objects as it fills each page
(`TFrameRelocationGenerator`) and puts one word in front of it: where the
first whole object starts, where the frames end, and - when an object
runs over from the previous page - how many of its header words were
left behind, whether it is slotted (so the words that did arrive are
refs) and whether its last word is padding.  Code pages get a list of
the word offsets that hold addresses instead.  The generator that picks
those entries out for each page has an end test that compares a pointer
plus the entries' size with the same pointer, so it never ends: it walks
on past the last entry until something in memory happens to look like an
entry for a later page.  (`packages/StorePackages.cpp`.)

---

## Mahjongg can freeze NewtHack one second in five

The machine has one random-number generator, the C library's Park-Miller
`rand` (`utility/Random.cpp`), shared by every application. The
unregistered copy of Mahjongg Solitaire 2.1 (`shuffleTiles`, package offset
0x9e11) seeds it with `TimeInSeconds() mod 5 + 1` whenever it deals, so an
unregistered player gets only five different deals.

`TimeInSeconds` counts seconds from 1 January 1993 and returns them as a
NewtonScript integer, which holds 30 bits. The count passed 2^29 in 2010,
so the value has wrapped round and is now negative (`intl/Dates.cpp`,
exactly as the ROM does it). NewtonScript's `mod` takes the sign of the
dividend, so the seed is now anything from -3 to 1, and one second in
five it is **0**. Nought is Park-Miller's fixed point: every later `rand`
answers 0, so every `Random(lo, hi)` answers `lo`.

The next program to want random numbers suffers. NewtHack's level builder
(`CreateRandomRooms`, then `ConnectRooms`) puts every room in the same
block and gets a level of one room. It puts the up stairs on the room's
lowest corner, then looks for a floor square for the down stairs. The
square it tries is always that same corner, now the stairs, so it looks
forever. A Newton with its clock set to the present day would hang the
same way. `src/host/demo/thirdparty-apps.ns` gives NewtHack a seed of its
own. That test hung only under load, because load decided which second
Mahjongg happened to deal in.

## Every store has a master password

A card store can be given a password (`store:SetPassword(old, new)`). The
password isn't kept: it goes through `DESCharToKey` into an 8-byte DES key,
and that key is kept in a store object of its own, which the fifth word of
the root data names. `CheckStorePassword` (ROM 0x0035268c) then makes the
key of whatever password it is given and compares the two.

Before it compares, it tries one fixed key: `c1855223 d339abef`. A
password whose key is that opens every store, whatever its own password
is. Which string has that key is not known yet. The internal store never
has a password at all: `StoreSetPassword` (ROM 0x003527f4) refuses it by
answering nil.

Ported as it is, with the key in `stores/Soups.cpp`.

---

## A flash erase that, interrupted at the wrong moment, loses everything a start later

The internal flash (`stores/flash/InternalFlash.cpp`) never erases a
region in place. `TNewInternalFlash::Erase` (ROM 0x0013c10c) swaps the
region with a spare in three writes to their four-byte headers: the old
region is marked 0x000F ("giving itself up"), the spare is given the
logical region's number, and the old header is wiped to nought. The old
region is then erased in the background and becomes the new spare. At
every start `SetupVirtualMappings` (ROM 0x0013b214) reads the headers and
finishes whatever an interruption left half done, accepting only five
combinations of oddities - kept as the string `"011110000001"` on its
stack and indexed by a four-bit mask.

One of the five, 11, is what the power failing between the first two
writes leaves: a marked region, the spare still erased, and a logical
region that no header claims. The recovery erases the marked region and
makes it the spare - but leaves the old spare erased as well, and the
logical region with nowhere to live. Nothing is lost yet. At the *next*
start the headers show two erased regions, which is not one of the five,
and the whole flash is wiped (`Clobber`) and the store told to format.
`test_Flash` walks through it. Ported as it is.

---

## The flash store never gives out an id one bit away from another

`IsValidPSSID` (ROM 0x000c509c, `stores/flash/FlashStoreParts.cpp`) turns
down an object id that is nought, all ones, or has only one bit set - or
only one bit clear. `TFlashBlock::NextPSSID` steps over them. Flash
changes by clearing bits, and an id written into a header or a directory
entry that lost a bit on the way could otherwise land on another valid id;
these are the ids a single stuck bit could turn a blank word into. The
store's log entries are guarded the same way, by their own address
XORed with "dyer" and, complemented, with "foo!" - so an entry copied or
left over somewhere else is never valid there.

---

## The system alert draws in the corner of the screen and shows it in the middle

The Newton's system alerts ("Newton still needs the card you removed",
`src/alert/`) are drawn by their own little graphics engine straight into
the screen's bits - not somewhere spare, but at the alert's own
coordinates, which for every alert in the ROM are (0, 0) to (80, 192): the
top-left corner of the screen, over whatever the application had there.
The screen driver is then told to copy that corner onto the display a
quarter of the way down and centred (`TAlertDialog::DisplayAlert`,
`gDisplayRect`). The application's picture in the corner is simply lost,
and put back by asking the application to redraw the whole screen once
the last alert has gone. It works because the alert holds the LCD while it
is up (`BlockLCDActivity`), so the scribbled corner is never shown where it
really is. `docs/alert/README.md`.

## Powering off takes a Japanese office's approval process

The NewtonScript functions that put the MessagePad to sleep are named for
the stages a proposal goes through in a Japanese office. They are ROM
built-in functions; read them with `analysis/nsdecompile.py build/MP2x00US
PowerOff PowerOffSoodan ...`.

| Function | The name | What it does |
|---|---|---|
| `PowerOffSoodan` | *sōdan*, a consultation | starts the sequence, deferred there by `PowerOff` |
| `PowerOffJooHooShuuShuu` | *jōhō shūshū*, gathering information | asks every registered power-off function `'okToPowerOff`; a single "no" and nothing happens |
| `PowerOffYobiKaiGi` | *yobi kaigi*, a preliminary meeting | tells each of them `'powerOff`; one that answers `'holdYourHorses` holds the meeting until it calls `PowerOffResume` |
| `PowerOffRingiSho` | *ringisho*, the proposal passed round for everyone's seal | the native that finally puts the machine to sleep (`FPowerOff`, ROM 0x00201b00) |

In the ringi system, a written proposal goes round every stakeholder for
their seal before anything is done. The Newton decides to sleep the same
way: the power stays on until everybody has agreed. `docs/power/README.md`.

## No application installs off a card's own package

A PC card can carry a package of its own: Apple's vendor-unique CIS tuple
(0x8e, maker 200) names one in the card's common or attribute memory, and
the card server loads it the moment the card goes in
(`TCardServer::LoadCardPackage`; from attribute memory a byte in every two,
through a `TCardPipe` - `pcmcia/CardPipe.h`). The ROM's NewtonScript
`InstallFormPart` (ROM 0x5579c1) then writes which socket the card is in
into the new application's base view:

    if (a1.deviceKind = 1) then
        l5.cardSocket := deviceNumber

`deviceNumber` is a slot of the install-info frame `a1` - but the code reads
it as a *variable* (`find-var 'deviceNumber`; `nsfunctions.py --disasm
InstallFormPart`, offset 180), and there is no such variable. So every
'form part on a card throws -48807 (undefined variable), `InstallPart`'s
handler catches it, and the user is told "An error occurred activating the
package ... It may not work with this system". Only 'auto parts and the
other part kinds come off a card whole. The reconstruction ports the bug
and fixes it by default: `romsrc/`'s `InstallFormPart` asks the host's
`RomBugFixed()` and reads `a1.deviceNumber` (`NEWTON_ROM_BUGS=1` for the
ROM's behaviour; booted with `--rom` the ROM's own function runs, bug and
all); `tools/cards/streamedpkg.py --kind form` makes a card package that
shows it (ctests `host.NewtonCardFormPackage` and `...romBugs`). `docs/stores/README.md`, "Card
packages".

## An o written alone is a zero until it is written over a word

Rosetta, reading a single round stroke as a word, answers "0" first (score
0, "o" at 76).  Written over the n of "ton", the Notepad does not take
that reading: its overwrite path (`DoReplaceSym` -> `ReclassifyCharacter`)
reads the stroke again in an area made for one character
(`MakeCharArea`: the line's baseline, an x-height of ascent - ascent/2.75,
a grid of the writing's box), where the same stroke is an "o" (23, "0"
at 102) - so "ton" becomes "too".  The x-height is what tips it: a
zero stands as tall as a capital.  Found 2026-10-01 (ctest
`host.NewtonOverwrite`, 172b0e91).

## The World Clock's city form throws every time it closes

Close the World Clock with a city's form open and the form's
viewQuitScript flushes its fields into `target.name` - but `TView::Delete`
(0x267584) runs the parent's viewQuitScript before the children's, and
the World Clock's own has already set `target := nil`.  The read throws,
and `Delete` catches the root exception and drops it, so nobody ever saw
it - until the host's exception trace printed every throw where it
happens (found by the application sweep, 2026-10-01).
