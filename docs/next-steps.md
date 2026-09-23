# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-23 (commit `c1e44ab`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 75/75.
- `analysis/coverage.py build/MP2x00US --check`: 8497 citations, 0 bad;
  4547 of 16671 functions (27.27%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 20 --store <file>` boots.

**Writing is now kept.** A stroke drawn on the Notepad becomes an ink
word in a paragraph of its own. The route crosses five areas and is
written out in `docs/ink/README.md` ("From the pen to ink on the page");
`src/host/demo/ink.ns` takes a picture of it with the pen down and
another after the recogniser has finished.

The last run of work added, from the bottom up:

- `TWordList` (`recognition/WordList.h`) - the readings a word unit came
  to, sixteen of them packed into one handle with 0xFFFF between them,
  out of a pool of twelve lists; and the try string, which is how the
  recogniser remembers being corrected about '0' against 'O'
  (`docs/curiosities.md`).
- The word info frame (`recognition/WordInfo.h`) - what a script is told
  about a piece of writing - and the `TUnitPublic` side under it
  (`MakeWordList`, `Words`, `WordInfo`, `SetWordBase`, `TrainingData`),
  plus `ExpandUnit`.
- `TWRecRecognizer`, `InstallWRecRecognizer`, `WordRecognizerHandleUnit`
  and `GetInkCommand` (`recognition/Recognizer.h`), with
  `SetWordRecognizer`/`UseWRec` deciding which word recogniser is in
  use.
- `TInkOnlyRecognizer` (`recognition/InkRecognizer.h`) - an engine that
  gathers strokes into words and says it cannot read any of them, which
  is the answer the ROM's own engine gives for writing it cannot make
  out. The host registers it and puts it in use.
- The `aeInkWord` case of `TEditView::RealDoCommand`, the ink-word half
  of `AddNewParagraph`'s geometry, and the ink-word branch of
  `CreateTextStyleRecord` that had been missing.

## What the machine actually does today

Measured rather than reasoned about, with `src/host/demo/ink.ns`:

- The live inker works: drag the pen across the Notepad and the ink
  follows it.
- On pen-up the writing is claimed by the word domain, found to be
  unreadable, and put on the page as an ink word - smaller than it was
  written, because an ink word is brought down to a size a line of text
  can hold, and starting at the middle of the box it was written in,
  which is what the ROM's arithmetic says.
- `IdleStrokes()` must not be called while the pen is down. It runs the
  click through to the view under it, and a view that tracks the pen
  waits for pen-up, which only the script could queue and which is
  exactly what is blocked. The inker task pumps the queued records
  every tick anyway, so a script never needs to.

## Next: a word the recogniser *reads*

Everything is in place except the geometry for a word that was read
rather than left as ink. `TEditView::AddNewParagraph` still drops such a
word rather than putting it down wrongly; the section is the ROM's
0x000a20f0-0x000a22bc and it is the one thing between here and text
appearing where it was written:

- the word is measured with `TextBounds` over a `TRichString` of it -
  our `TextBounds` takes a font spec where the ROM's takes the
  `StyleRecord` the function has already built, so it wants an overload;
- `AlignBounds` (0x000a26c4, reconstructed and tested) lines the result
  up with the page's other children;
- `AlignToLineSpacing` (0x000a2bc4, reconstructed) then lines it up with
  the page's ruled lines - but only when `AlignBounds` did not move it,
  which is what the comparison after `AlignBounds` decides;
- the result is put into the editor's own coordinates with
  `ContentsOrigin`/`OffsetRect`, which the ink-word path does *not* do -
  worth checking whether that is a ROM quirk or a misreading.

The point the word is placed around is already reconstructed for both
kinds: the middle of the base line for a word the recogniser read (the
unit's `fWordBase`), the middle of the top of the box for an ink word.

After that, the obvious next piece is an engine that reads something -
either the ROM's own (the CIC handwriting library) or a modern one, both
of which plug into `TWRecognizer` beside `TInkOnlyRecognizer` and need
nothing above them changed.

## Also still open

- The caret side of `aeInkWord` (0x000a51b0): the ROM looks at the view
  the caret is in and, when it is this page's, puts the word into the
  paragraph the caret is in rather than starting a new one, with
  `remoteWriting` deciding whether the writing may come from somewhere
  other than where the caret is. The corrector
  (`SetRemoteForCorrector`, `CorrectorUp`) goes with it.
- `TWRecRecognizer::ConfigureArea` (0x00144178) and the area information
  a recogniser keeps per writing area (`TWRecDomain::DomainParameter`,
  `SetParameters`, `ConfigureArea`), which want `DomainOn` and
  `BuildChains`.
- `ReadDomainOptions`/`FReadCursiveOptions` (0x0019cfd8), which reads
  the writer's recognition preferences at boot; the host does its job by
  hand at the moment.
- The dictionaries (`LookupWord` 0x0013f4f4, `ExpandWord` 0x001aa930 and
  everything under them), which is what would order a word list properly
  and let `TWordList::Reorder` matter.
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
- `TInkWordGlyph::SetFontParms` (over `SetInkWordFontParms` 0x000dbd0c)
  and the printing path's outlined paths (`CSMakePathsGroup`,
  `FramePaths`).
- `UpdateStyleTable` (0x002e2724): the style table scaled to the size,
  which both font openers want and neither has.

## Working notes that keep being needed

- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`. This matters most in functions that
  build `Rect`s and `Point`s on the stack: Ghidra's output for
  `AddNewParagraph`'s geometry is almost unreadable, and the assembly is
  not.
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
- A `StyleRecord`'s scalars now start clear (`qd/Fonts.h`), because the
  ROM's callers allocate theirs in cleared memory and rely on it. It
  holds a `RefStruct`, so it still must not be `memset`.
- A test that needs the protocol registry (anything making an instance
  by name) must run as the kernel services task: `gHostKernelServicesTask
  = ...; OsBoot();`, ending with `HostStopTasks()`.
- `coverage.py --check` matches one citation per line; a second name on
  the same line (or a trailing comma) breaks the match.
