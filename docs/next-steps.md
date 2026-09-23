# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-23 (commit `00966da`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 73/73.
- `analysis/coverage.py build/MP2x00US --check`: 8378 citations, 0 bad;
  4435 of 16671 functions (26.60%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 20 --store <file>` boots.

The view side of ink is now finished as far as it can go without the
recogniser.  A word of writing draws as a shape, on a page, as a style
run of a paragraph, as a glyph of a font, and inside a rich string.

The last run of work added, after the font piece:

- `GetFontSize`/`GetFontFace` answer an ink word's own size and face,
  through a second registered proc beside the opener.
- `qd/Text.h`'s `DoRichString` cuts a range with ink in it into text and
  ink runs, over the new `TRichString::GetLengthsAndDataInRange`, and
  gives an ink run the address of its blob as its font.
- `PackedInkWordInfo` is eight big-endian bytes again.  It was a pair of
  `ULong`s, and a `ULong` here is pointer-sized, so the Newton's eight
  bytes were sixteen little-endian ones - inside a string object that
  goes into a soup as it stands.

## What the machine actually does today

Measured rather than reasoned about, with `src/host/demo/ink.ns` (which
walks the setup assistant first, because the host's store is in memory
and every boot starts there):

- The **live inker works**.  Drag the pen across the Notepad and the ink
  follows it, drawn by `TStroke::Draw` over `InkerLine` out of
  `StrokeTime`.
- **On pen-up the ink vanishes**, because nothing claims the stroke.
  Everything the view side of ink can now do is still reachable only
  from tests.
- `IdleStrokes()` must not be called while the pen is down.  It runs the
  click through to the view under it, and a view that tracks the pen
  waits for pen-up, which only the script could queue and which is
  exactly what is blocked.  The inker task pumps the queued records
  every tick anyway, so a script never needs to.

## Next: the word domain, and a recogniser that answers "ink"

This is what would put writing on the page, and it is the last thing
between the view side of ink and something visible.  The chain, read out
of the ROM:

- `TRecognitionManager::InitRecognizers` (0x0019d438) installs the
  gesture, event, stroke and click recognisers at any level, and above
  level 1 the shape (`InstallShapeRecognizer` 0x0014456c) and word
  (`InstallWRecRecognizer` 0x00144094) ones.  Only the first four are
  reconstructed, which is why no unit is ever made for a finished
  stroke.
- `InstallWRecRecognizer` makes a `TWRecDomain` over the controller and
  a `TRecognizer` over that with services 0x17ef000 - but it gives up
  first if `ClassInfoByName("TWRecognizer")` finds nothing.  That
  protocol is the handwriting engine, and `RegisterWRec` registers the
  ROM's.  **A registration that always answers "ink" is enough**: it is
  the answer the ROM itself comes to when the engine cannot read the
  writing.
- `WordRecognizerHandleUnit` (0x00143f00) is the shell above it.  It
  asks the recogniser what the unit is (vtable +0x24); when the answer
  is 2 the unit is ink, and `GetInkCommand` (0x00143dec) over the unit's
  word info picks the command for the view.
- `GetInkCommand` reads the word info's `strokes` - a stroke bundle,
  which `recognition/StrokeBundle.h` already makes - takes the midpoint
  of its `bounds`, finds the view under it, and asks that view's
  recognition configuration for `doInkWordRecognition`: set means
  `aeInkWord`, clear means `aeRawInk`.  Both of those the view side
  already answers (`views/EditView.h`).
- The word info itself is `MakeWordInfo(TUnitPublic*)` (0x00077fd8): the
  ROM's `protoWordInfo` cloned, with `unitId`, `strokes` (`ExpandUnit`
  0x001a2554), `words` (`MakeWordList`) and `unitData`
  (`TUnitPublic::TrainingData`).  For ink the last two are empty and
  flag 8 goes on (`SetWordInfoFlags` 0x00077dc0).
  `TUnitPublic::WordInfo` (0x0022d684) makes it once and keeps it.

The real work in this is `TWRecDomain` - the domain that decides which
strokes belong to the same word, by where and when they were written.
Everything above it is small.

## Also still open

- `DoInsertItems` (0x00170f7c) and `TParagraphView::HandleInsertItems`
  (0x001700a0), the paragraph's general insert path - and
  `CheckAndDoSplitInk` (0x00176208), which is blocked on it.
  `DoInsertItems` is small (it clones `Rstarterinsertspec` and sends the
  view command 0x4d); `HandleInsertItems` is ~460 lines of decompiled
  output and branches over every kind of item that can be inserted, so
  the sensible first cut is the string and ink-word kinds, which are
  what `CheckAndDoSplitInk` needs.
- The ink half of `TParagraphView::CheckAndDoJoin` - `MergeInk` is ready
  for it.
- The `aeInkWord` case of `TEditView::RealDoCommand` (0x000a51b0): it
  wants `SetRemoteForCorrector`, `CorrectorUp` and
  `ResetHilitesForNewWord` first.
- `TInkWordGlyph::SetFontParms` (over `SetInkWordFontParms` 0x000dbd0c)
  and the printing path's outlined paths (`CSMakePathsGroup`,
  `FramePaths`).
- `UpdateStyleTable` (0x002e2724): the style table scaled to the size,
  which both font openers want and neither has.

## Working notes that keep being needed

- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`.
- `__rt_sdiv`/`__rt_udiv` take (divisor, dividend) and answer the
  quotient in r0.
- `TArray::IArray(elementSize, count)` sets `fCount = count`: the array
  *has* that many entries, so `TStroke::Make(n)` starts with n points at
  nought. Use `Make(0)` when the points are to be added, `Make(n)` when
  they are to be written in place.
- ROM arithmetic wraps; host `long` is 32-bit on Windows and traps under
  the sanitiser. Wrap explicitly through `ULong`.
- A `\n` inside a C string literal written through a Bash heredoc loses
  a backslash. Use the Write/Edit tools for those, or build the two
  characters as `chr(92) + 'n'` in a Python helper.
- `RemoveView(parent, child)` takes two arguments; calling it with one
  throws `evt.ex.fr.intrp` from inside `Eval`, which is easy to misread
  as a fault in whatever was being tested.
- A `StyleRecord` holds a `RefStruct`, so it must be filled in field by
  field - `memset`ing one over dereferences a null handle on the next
  assignment.
- `coverage.py --check` matches one citation per line; a second name on
  the same line (or a trailing comma) breaks the match.
