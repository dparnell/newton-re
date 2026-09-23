# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-23 (commit `2345d68`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 75/75.
  (`intl.Dates` fails about one run in ten: it reads the real clock.)
- `analysis/coverage.py build/MP2x00US --check`: 8551 citations, 0 bad;
  4587 of 16671 functions (27.51%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 50 --script src/host/demo/ink.ns` boots, and writing on the
  Notepad stays on the page (`build/ink-kept.pgm`).

Writing travels the whole way from the tablet to a paragraph; the route
across the five areas is `docs/ink/README.md`'s "From the pen to ink on
the page", and `src/host/demo/ink.ns` photographs it with the pen down
and again after the recogniser has finished.

The last run of work closed, in order:

- the word domain and the engine's side of it, the readings
  (`TWordList`), the try string, the word info frame, the word
  recogniser front, an ink-only engine and the `aeInkWord` command -
  which together are what put writing on the page;
- `AddNewParagraph`'s geometry for a word the recogniser *reads*
  (`TextBounds`, `AlignBounds`, `AlignToLineSpacing`);
- `UpdateStyleTable`, so a font drawn scaled gets its synthesised faces
  scaled with it;
- `SetInkWordFontParms`/`TInkWordGlyph::SetFontParms`, so a style run
  restyles the writing in it;
- the ink half of the join gesture - two words of writing merged into
  one;
- the area information an engine keeps per writing area
  (`DomainParameter`, `ConfigureArea`, `SetParameters`, `DomainOn`);
- the writer's recognition preferences (`ReadCursiveOptions`);
- **things put into a paragraph from outside**
  (`TParagraphView::HandleInsertItems` and the eleven-argument appender
  under it, `DoInsertItems`, `InsertItemsAtCaret`,
  `GetAppendDelimiter`), which was the piece three others were waiting
  on;
- the **caret side of `aeInkWord`**: a page whose caret is in one of its
  own paragraphs now takes the word into that paragraph;
- **`CheckAndDoSplitInk`**, a word of writing cut in two at a caret -
  and with it `OffsetToBounds` answering a character's right edge as
  well as its left.
- **`TParagraphView::HandleWord`** and everything under it - the
  Finder, `FindWordInRun`/`FindWordInParagraph`/
  `SetFinderBelowParagraph`, `AddWord`, the last-added-word geometry -
  so a recognised word goes into the paragraph it was written on, and
  `TextContainingPoint` finally finds the text under a point;
- the **third case of `aeInkWord`**, a word that starts a new line in
  the paragraph above the caret.

## Next: things that are still stubs on paths that now work

Nothing on the writing path is blocked on a single big piece any more.
What is left there is a handful of named holes, each small and each
with its ROM address already in a `NOT YET RECONSTRUCTED` comment:

- **`TParagraphView::ReplaceCharacter`** (0x00174e14), the Finder's
  strongest claim - a character written over a character of the text
  replaces it, and the paragraph answers 6, which stops
  `TEditView::HandleWord` asking anybody else.  It is the one branch
  of `FindWordInRun` that is not there.  Most of what it wanted is now
  in place - the correction information, `AreStrokesAfterUnit`,
  `UsesLetters`, the reading editors.  What is left:
    - `WordOverSpaces` (0x0017bc84) and `CoordToInterCharGap`
      (0x0017d614), both small; the second wants the host treatment the
      rest of `FindWordInRun` got, because it asks a text object.
    - **`DoReplaceSym`** (0x0017b1b8), 700 instructions.  It is
      readable and its shape is understood: find the word the writing
      fell on (`FindWordBreaks` over the paragraph's word break
      table), find or make its correction entry, ask the unit for its
      readings again, and put the single character in through
      `HandleInsertItems`.  One branch of it needs the engine:
      `ReclassifyCharacter` (0x000348e4), over `MakeCharArea`
      (0x00035a50) and `TController::ClassifyInArea` (0x00209f78),
      asks the engine to read the writing again as a single character
      of a known height - which our ink-only engine cannot do, so that
      branch would be NOT YET whatever happens.  The `UsesLetters`
      branch (the single-letter fields: names, dates, numbers) does not
      need it.
    - `GetInterpretationsCopy`/`SetInterpretationsCopy`/
      `DeleteInterpretationsCopy` (0x0021f6a8-), which save and put
      back the unit's readings around that reclassification - so they
      are only wanted with it.
- **The corrector view itself**, so `CorrectorUp` (0x001767b8) has a
  `correct` to find in the root view's context.  It answers false out
  of hand today, which is right for a machine that has no corrector
  but is not what the ROM does.  (The remote-writing bracket around
  it - `SetRemoteForCorrector`/`RestoreRemoteForCorrector` - is done.)

## Also still open

- The **dictionaries** (`LookupWord` 0x0013f4f4, `ExpandWord`
  0x001aa930, `BuildChains` 0x0013d808, `LookupWordOrVariant` 0x0008f098
  and everything under them).  This is the other large open area.  With
  none of them, a word list comes out in the order a machine with an
  empty dictionary would put it, an area has no chains, and nothing is
  ever "known".
- An engine that reads something: the ROM's own is the CIC handwriting
  library, and `TInkOnlyRecognizer` (`recognition/InkRecognizer.h`) is
  the socket it - or a modern one - plugs into.  Nothing above the
  socket would change.
- `SetUpRosetta`, `SetUpParaGraph` and `ReadDictPrefs`, which
  `ReadCursiveOptions` would call: all three belong to the engines.
- The printing path's outlined paths for ink (`CSMakePathsGroup`,
  `FramePaths`), which want the PostScript path machinery.
- `TWRecognizer::EndInkStrokeGroup` (the CIC library's
  `WRecEndInkStrokeGroup`).
- Nothing chooses *which* word recogniser is in use at boot: `UseWRec`
  is a native and `SetWordRecognizer` is reconstructed, but the ROM
  calls them from a script, and the host calls `SetWordRecognizer`
  itself in `HostStartViews` and `TNotebook::InitToolbox`.

## Working notes that keep being needed

- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`. This matters most in functions that
  build `Rect`s and `Point`s on the stack: Ghidra's output for
  `AddNewParagraph`'s geometry is almost unreadable, and the assembly is
  not.
- The view classes have no vtable in `romfacts.json` - they are built
  by `BuildView`, not by a self-allocating constructor - so a virtual
  call like `add pc,r3,#0x148` cannot be named from it.
  `analysis/vtable.py build/MP2x00US --find <mangled name> --slot 0x148`
  works back from any method of the class to the table it sits in.
- A function with more than four arguments spills the rest above the
  frame: with `sub r11,r12,#N`, argument five is at `[r11,#N]`. The
  decompiler often loses these entirely.
- `__rt_sdiv`/`__rt_udiv` take (divisor, dividend) and answer the
  quotient in r0 and the remainder in r1.
- `TArray::IArray(elementSize, count)` sets `fCount = count`: the array
  *has* that many entries, so `TStroke::Make(n)` starts with n points at
  nought. Use `Make(0)` when the points are to be added, `Make(n)` when
  they are to be written in place.
- ROM arithmetic wraps; host `long` is 32-bit on Windows and traps under
  the sanitiser. Wrap explicitly through `ULong`.
- **Editing the sources from a script: match the file's line endings.**
  Most of `src/` is CRLF in the working copy and LF in the repository.
  A Python helper must read and write with `newline=''` and splice with
  the endings the file already has, or the whole file shows up as
  changed. `sed -i` is safe; a bare `io.open(p, 'w')` is not.
- A Bash heredoc whose delimiter is *not* quoted (`<<PY`, not
  `<<'PY'`) runs command substitution on the backticks inside it,
  which silently mangles any Python that writes Markdown.  Quote the
  delimiter and put paths in the script rather than interpolating
  them.
- A `\n` inside a C string literal written through a Bash heredoc loses
  a backslash. Use the Write/Edit tools for those, or build the two
  characters as `chr(92) + 'n'` in a Python helper.
- `git checkout -- <file>` throws away uncommitted work. Commit the
  piece first, or stash it.
- `RemoveView(parent, child)` takes two arguments; calling it with one
  throws `evt.ex.fr.intrp` from inside `Eval`, which is easy to misread
  as a fault in whatever was being tested.
- A `StyleRecord`'s scalars now start clear (`qd/Fonts.h`), because the
  ROM's callers allocate theirs in cleared memory and rely on it. It
  holds a `RefStruct`, so it still must not be `memset`.
- A test that starts the recognition system without booting must put a
  `userConfiguration` and an `international` frame in the globals first:
  `ReadCursiveOptions` reads both, as it does on a real machine.
- A test that needs the protocol registry (anything making an instance
  by name) must run as the kernel services task: `gHostKernelServicesTask
  = ...; OsBoot();`, ending with `HostStopTasks()`.
- A `starterParagraph` form is built through the `'para` stationery,
  which only a booted Notepad has registered; a test that has no
  stationery can check the form but not the view made from it.
- `coverage.py --check` matches one citation per line and checks the
  mangled name against the ROM's symbol at that address - a plain name
  where the ROM has a mangled one (or the other way round) is reported.
