# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-23 (commit `96e5bd0`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 73/73.
- `analysis/coverage.py build/MP2x00US --check`: 8377 citations, 0 bad;
  4434 of 16671 functions (26.60%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 20 --store <file>` boots.

The last run of work made **an ink word draw inside a line of text**,
four commits from `ac70775` to `96e5bd0`:

- `ink/InkFont.h`: `TInkWordGlyph` and `InkOpenFont` with its five
  procs.  A style whose "font" is an `'inkWord` binary (or an integer,
  which is the address of one outside the object heap) opens as a font
  with a single glyph; asking for the glyph draws the writing into a
  bitmap the text engine blits.
- `views/ParagraphView.h`: `CreateParagraphStyleRecord` and
  `IsFontFrame`, which is what puts the ink word into the style record
  in the first place.  `LayoutRuns` uses it now instead of
  `CreateTextStyleRecord`.
- `qd/Fonts.h` gained the `gInkOpenFont` hook (a DEVIATION: the ROM
  calls `InkOpenFont` straight out, but the ink area sits above
  QuickDraw here), `kStyleTable` became visible, and the two spare
  `FontEngineInfo` fields are named for what the ink font puts in them.
- `qd/Rects.h` gained `Encloses`.

Before that a fault in the draw path had to be fixed (`ac70775`): the
pen was being handed to the decoder as its *group*, so a pen of one
quietly asked for a different thinning mode, and both draw entries were
missing the flag that says the ink is wholly inside the clip.  While
fixing it the scaled entry's citation turned out to name the wrong one
of the two `GenericCSDraw`s.

`test_Views`'s `TestInkWordInText` is the end-to-end check: a word of
writing put in a paragraph's text draws, the line is as tall as the
word, and every lit pixel is one of the glyph's.

## Next: `DoInsertItems`, and `CheckAndDoSplitInk`

`TParagraphView::CheckAndDoSplitInk` (0x00176208) is the ink half of the
caret gesture - a caret drawn through an ink word splits it, which
`SplitInkAt` can now do.  It is blocked only on `DoInsertItems`
(0x00170f7c), which is the paragraph's *general* insert path and would
also replace three places where this reconstruction does the equivalent
by hand (see the `(host: the ROM inserts through DoInsertItems ...)`
comments in `views/ParagraphView.cpp`).

`DoInsertItems` itself is small: it clones `Rstarterinsertspec`, fills
in `insertItems`, `addSpace`, `undoable`, `insertOffset`,
`replaceChars`, `moveCaret` and `defaultFontSpec`, and sends the view
command 0x4d with that frame as the frame parameter (the helper at
0x00170e90 is "post a command with a frame parameter to the view of this
context").  The work is in whatever answers 0x4d.

## Also now within reach

- `GetFontSize` (0x0017ad54) and `GetFontFace` (0x0017bc70) make a
  `TInkWordGlyph` for the same reason `CreateParagraphStyleRecord` does:
  they answer a style's size and face, and an ink word's come from the
  glyph.  Both are small.
- `qd/Text.h`'s `DoRichString` says NOT YET against "the ink words (the
  ROM makes a style and a run for each ink word and text run between
  them)".  That is now only a matter of splitting the string into runs -
  `frames/RichString.h`'s `NumInkAndTextRunsInRange` already counts them
  and `GetInkData` fetches each word's bytes.

## What the machine actually does today

Worth knowing before picking the next piece, because it was measured
rather than reasoned about (`src/host/demo/ink.ns`, which walks the
setup assistant and then draws a stroke on the Notepad):

- The **live inker works**.  Drag the pen across the Notepad and the ink
  follows it, drawn by `TStroke::Draw` over `InkerLine` out of
  `StrokeTime`.
- **On pen-up the ink vanishes.**  The stroke goes to the recogniser,
  which takes the ink off the screen and would put a paragraph or an ink
  word in its place - and the domains that would do that are NOT YET.
  So everything the view side of ink can now do is still only reachable
  from tests.
- `IdleStrokes()` must not be called while the pen is down.  It runs the
  click through to the view under it, and a view that tracks the pen
  waits for pen-up, which only the script could queue and which is
  exactly what is blocked.  The inker task pumps the queued records
  every tick anyway, so a script never needs to.

## After the ink: the recogniser

The owner's order was the view side first, then the recogniser.  Nothing
generates `aeRawInk` or `aeInkWord` yet, because the domains above the
gesture domain are NOT YET - so the ink view side is reachable only from
tests until that is done.  In rough order:

- The shortest way to something visible is the *shell* of the word
  recogniser rather than the engine inside it.  `WordRecognizerHandleUnit`
  (0x00143f00) asks the recogniser what the unit is; when the answer is
  2 it is ink, and `GetInkCommand` over the unit's word info picks
  `aeRawInk` or `aeInkWord` for the view.  A recogniser that always
  answers ink is what the ROM itself comes to when the engine cannot
  read the writing, and it would put everything the view side can now do
  on the screen.
- `low_level` and `GetTraceFromStrokes` - the CIC feature extractor.
  `recognition/Words.h`'s `FindBaseline` already has the ROM's fallback
  path and will start answering properly once these exist.
- `TWRecDomain`/`TWRecognizer`, `TRosRecognizer`, the Airus
  dictionaries.
- The shape domain.

## Odds and ends still open

- The ink half of `TParagraphView::CheckAndDoJoin` (merging two ink
  words when a caret joins them) - `MergeInk` is ready for it.
- `TLiveInker` (0x00113840 onwards): the ink that follows the pen while
  it is still down.  It is also the fast line drawer `TInkWordGlyph::
  DrawAt` asks for when the clip holds the whole word, so it would take
  the `useInker` flag out of NOT YET as well.
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
