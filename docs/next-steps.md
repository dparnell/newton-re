# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-24 (commit `e7e1b6e`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 76/76.
  (`intl.Dates` fails about one run in ten: it reads the real clock.)
- `analysis/coverage.py build/MP2x00US --check`: 8693 citations, 0 bad;
  4704 of 16671 functions (28.22%).
- The machine boots into the Setup assistant, `src/host/demo/setup.ns`
  taps its way through to the Notepad, and Names, Dates, Extras and the
  Preferences roll (down to the Handwriting Recognition slip and its
  Options popup) all open and draw.
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 45 --script src/host/demo/ink.ns` boots, and writing on the
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
- **the correction information** (`recognition/CorrectInfo.h`), the list
  of what the machine remembers about the words already on a page -
  made, found, kept up to date as the text is edited, learnt from when
  a word falls off the end of it, and written to at last by
  `TEditView::HandleWord`;
- **a letter written over a letter of a word**
  (`ReplaceCharacter`/`DoReplaceSym`), the last branch of
  `FindWordInRun` and the only claim that scores 6 - and with it the
  remote-writing bracket the corrector puts round a word, and a rich
  string keeping its writing when it is dropped into a paragraph.
- **the whole of the writing path above the engine**: the machine boots
  into the Setup assistant, walks through it to the Notepad, opens
  Names, Dates, Extras and the Preferences roll, takes writing and keeps
  it, and a double tap on a word asks for the corrector.
- **the dictionaries**, end to end: the list and the three chains a
  lookup walks (`recognition/Dictionaries.h`), the 129 lexicons built
  into the ROM opened where they lie
  (`recognition/ROMDictionaryData.h`, whose table
  `analysis/romdicts.py` recovers from the code that writes it), the
  words the locale brings with it, the Airus delete path, and the
  writer's own dictionaries - the expansions and the words the machine
  adds on their behalf (`recognition/Learning.h`).  `LookupWord` now
  answers out of the ROM's own word lists.

## Next

The writing path is closed end to end now: a stroke is inked as it is
drawn, read (or not), placed in or beside the text it was written on,
registered with the corrector, and a letter written over a letter
corrects the word.  What is left on it are two named holes, and then
the two large open areas below.

- **The engine.** Everything above the socket is there; nothing reads
  anything.  `TInkOnlyRecognizer` (`recognition/InkRecognizer.h`) is
  where the ROM's CIC handwriting library - or a modern recogniser -
  plugs in, and nothing above it would change.  Two things in the tree
  are waiting only on this: the `!UsesLetters` branch of
  `DoReplaceSym` (`ReclassifyCharacter` 0x000348e4 over
  `MakeCharArea` 0x00035a50 and `TController::ClassifyInArea`
  0x00209f78, with `GetInterpretationsCopy` and its two companions
  around it), and `TWRecognizer::EndInkStrokeGroup`.
- **The dictionaries** are done, and the machine now reads against the
  ROM's own words.  `recognition/Dictionaries.h` has the list, the three
  chains and the lookups; `recognition/ROMDictionaryData.h` opens the
  129 lexicons built into the ROM where they lie (their table is
  recovered from the code that writes it by
  `tools/newton-rom/analysis/romdicts.py`); `recognition/Learning.h`
  has the writer's own dictionaries, the expansions and the words the
  machine adds on their behalf.  What is left of that area:
  The iterators are there too now (`AEnum_NextSet`, `WalkDictionary`,
  `DeletePrefix`, and the `Walk`/`PrivateDeleteWord`/`DeletePrefix` a
  script sees), so a dictionary can be read out word by word.  What is
  left of that area:
    - `AEnum_FirstLast`, `AEnum_NextPrevious`, `AEnum_ChangeAttribute`
      and the sixteen-bit walkers (AE16, `AE16_NextSet9`).
    - `gTrie`, which would be dictionary 32 if its descriptor had no
      `romDictID` - it has one, so this ROM never takes that path.
- **The corrector** is now wired from the pen down to the ROM's own
  `DoCorrection`: a double tap travels from the click-event recogniser
  to the edit view, down to the paragraph under the point, and out
  through `Correct` (`docs/recognition/README.md`, "The corrector").
  What stops it is the **spelling checker**, which is where
  `DoCorrection` goes next - `SpellDocBegin` (0x001f6360) makes a
  `spell_state` with its own dictionary chains (`InitSpellChains`,
  `InitNumberChains`, `MakeSpellFrame` 0x001f62ac, `GetSpeller`
  0x001f6314, `FSpellDocEnd`, `FSpellSkip`), and `FSpellCheck`
  (0x001f41bc), `FSpellCorrect` (0x001f44c8) and `CorrectWordInChain`
  (0x001f49ac) are the work.  With the dictionaries in place the
  chains it wants are there.
  Reproduce it with `build/host/host/newton --rom build/MP2x00US/rom.bin
  --display 320x480 --headless 48 --script <setup.ns + type a word +
  double tap>`: the run stops at `native not reconstructed:
  SpellDocBegin`.
- **The two ink arms of the double tap**, which ask for a word of
  writing to be read again rather than corrected: one for a tap on an
  ink word inside the selection (`HitsHilitedInkWord` is reconstructed
  and waiting), one for a tap on an ink word the corrector knows
  nothing about.  Both post a command to the application that the
  re-recognition path (`RecognizeInArea`) answers, and that is NOT YET.
- `CorrectorUp` (0x001767b8) still answers false out of hand: it asks
  the root view for a `correct` view, which only exists once the
  corrector has actually opened.

## Also still open

- `SetUpRosetta` and `SetUpParaGraph`, which `ReadCursiveOptions` would
  call: both belong to the engines.
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
