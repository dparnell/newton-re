# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-24 (commit `eebc881`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 81/81.
  (`intl.Dates` fails about one run in ten: it reads the real clock.)
- `analysis/coverage.py build/MP2x00US --check`: 9264 citations, 0 bad;
  5103 of 16671 functions (30.61%).
- `analysis/natives.py`: 860 of the ROM's 1326 natives answered.
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
- **searching the text of entries** (`docs/stores/README.md`): a `text`
  or `words` query walks the compressed text object beside each entry
  rather than reading the entry, so Find now finds a note.  What is left
  of it is the word hints (`TWordHintsHandler`, so the filter in front
  of the walk is always open) and the large binaries of an entry.
- **the spelling checker** (`recognition/Spelling.h`): the session and
  its chains, whether a word is spelled right, and what it might have
  been meant to be - five kinds of change against the dictionary, two of
  them walking the trie and a table of 181 letter groups together
  (`analysis/spellmaps.py`).  With it, a double tap on a word opens the
  corrector.
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
  answers out of the ROM's own word lists.  All thirteen of the
  dictionary and cursor natives a script uses are answered, the last of
  them being `TAirusIterator` (`recognition/AirusIterator.h`), the
  cursor a script walks a dictionary with.  NOT YET in the engine: the
  sixteen-bit walkers and the `AEnum_FirstLast`/`AEnum_NextPrevious`
  enumerators, which are a second way of stepping through a dictionary
  that nothing in the ROM appears to use.

- **the machine's own power natives** (`docs/system/README.md`): the
  battery frame and its raw twin, `BatteryLevel`, `BatteryCount`,
  `SetBatteryType`, the backlight pair and `SetRandomSeed` - all of them
  over `hal/Power.h`, which a port supplies.  Finding them turned up a
  hole in the reconstruction: `SetGrafInfo` had no case for the
  backlight (selector 5 is the driver's feature 2) and a case for a
  selector 6 the ROM has not got, so nothing could switch the light.

- **a selection restyled** (`docs/views/README.md`,
  "Restyling a range"): `ChangeStylesOfRange` is now the ROM's own -
  the range replaced by itself through `aeReplaceText`, so a restyle
  undoes - with the `fontParms`/`command` form the Styles slip sends
  (add, remove or toggle face bits, the toggle deciding on the first
  run).  The font arithmetic under it is `qd/Fonts.h`
  (`MakeCompactFont`, `SetFontParms`, `IntFontToFontParms`,
  `FamilySymToNum`, `GetFontFamilyNum`) and the script side is the new
  `views/FontNatives.cpp`, with `GetRangeText`/`ExtractTextRange`.
- **the caret from a script** and the four word scanners
  (`docs/views/README.md`, "The caret from a script"): `SetCaretInfo`,
  `ShowCaret`/`HideCaret`, `ScanNextWord`/`ScanPrevWordEnd`.

- **plugging in the native functions**, which is a measured job now:
  `tools/newton-rom/analysis/natives.py` says which of the ROM's 1326
  natives the reconstruction answers, groups what is missing by area,
  and with `--unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  Three batches went in - the strokes, ink and
  try string; the popups and what a view allows to be written on it; the
  key commands; text put in, views tied and the key commands sorted;
  the colours, `StrWidth` and the protocol registry; a bitmap made and
  drawn into, and a view drawn into one; the tones, the sound settings
  and the large-binary questions; the power statistics; two sweeps of
  the thin wrappers picked out with `natives.py --sizes`; the polygon
  shapes; a class info as a script reads it - taking it from 728 to 832.  Two reconstruction bugs
  came out of the tests for them: `FMakeRichString` wrote its halfwords
  the ROM's way round, so no rich string it made ever read back as
  having ink in it; and `TRootView::SetPopup` was missing the arm that
  closes the popup that is up, without which `DismissPopup` loops for
  ever.

- **the clipboard** (`docs/views/README.md`, "The clipboard"):
  `TClipboard` and the root view's two arrays of contexts, so a drag let
  go on the background now becomes a clipping - the items' data taken
  off the source view, the picture of it kept in the clipping's `bits`,
  the label cut to fifty pixels with an ellipsis and laid out as an icon
  pinned to whichever edges of the application area it touches (and put
  back against them by `FReOrientLabelForm` when the screen turns).
  `GetClipboard`, `SetClipboard`, `ClipboardCommand` and
  `GetClipboardIcon` are answered; `TView::GetClipboardDataBits`,
  `TView::DoMoveCommand`, `PointOnClipboard`, `CheckViewBounds` and
  `OffsetBoundsRef` came with it.  NOT YET: the pen-tracked drag itself
  (`TView::Drag`, the icon following the pen), so the host's simplified
  `DragAndDrop` takes a drop with no target as "let go on the
  background", and `MoveIcon` has no caller.

- **the hilite stroke** (`docs/views/README.md`, "The hilite stroke"):
  the pen held still on something and then drawn through it or round it,
  which is how the Newton is told what to select.  `TRootView::Hiliter`
  follows the pen and draws the line over a saved copy of the screen;
  `TView::AddHiliter` and `TEditView::AddHiliter` decide whether it was a
  lasso and offer it to the children, who answer with the kind of hilite
  they would take (`TContainerView::HandleHilite` promoting a child's
  whole-object claim to the container); `TParagraphView` answers all
  four of its kinds, the interesting one being `HiliteRange`, which
  walks the stroke's outline (`TUnitPublic::RoughShape`/`AsPolygon`)
  from each end for the word boundaries it runs through.  `hiliter` and
  `HiliteViewChildren` are answered.

  Two holes in the reconstruction came out of it: `TView::DoCommand` did
  not pass an unanswered command on to the parent, so nothing posted to
  a view could ever reach the root or the application (and `aeHide` and
  `aeDropChild` were not marking themselves taken); and
  `TRootView::CommonSetKeyView` asked `GetHiliteView` where the ROM asks
  `GetEnclosingEditView`, so selecting a second paragraph dropped the
  first one's selection.

- **the Intelligent Assistant's C++ side** (`docs/assist/README.md`):
  the whole of the ROM file at 0x00084064-0x000871d0 - the class
  hierarchy a sentence is matched against (`ISATest` and the six
  functions over it, `GetClasses` picking one class per word out of what
  it might mean), the task templates (`RegTaskTemplate`,
  `GetRelevantTemplates`, `FillPreconditions`, `AddEntry`) and the
  string tidying a sentence goes through (`GenerateSubstrings`, which
  makes every run of consecutive words so that a phrase of several is
  found in the lexicon at all).  `GetRelevantTemplates(@8.person)` now
  answers `["schedule", "find", "mail", "fax", "call"]` - the five
  things the machine knows how to do to a person.

  NOT YET: the lexicon's own trie (`TrieAdd`, `DynaTrieDelete` over
  `gDynaTrie`), which sits on top of the Airus engine (that engine
  itself is reconstructed, `docs/recognition/README.md`), so a
  registered template's words are not indexed and nothing finds it by
  writing one of them.

- **the text engine, from the bottom** (`docs/text/README.md`): the
  Newton's other text system - the document engine behind protoTXView,
  1800 symbols of its own - started at its foundation, `text/TXArray.h`:
  the growable array in a relocatable handle whose `chunk` is the whole
  memory policy, the array sorted by a leading long, and `TXRanges`,
  which records a division of the text by storing only the end of each
  range and answers `OffsetToRangeIndex` and `SectRanges` over it.  That
  one representation is how every division - style runs, lines,
  paragraphs - is kept, so it is what the rest stands on.  Then the
  attributes a run points at (`text/TXAttributes.h`) and the character
  storage itself (`text/TXChars.h`): the text in chunks of at most 512
  characters, the three ways `Replace` tries to get text in, and the
  running-together of chunks that keeps an edited document from ending
  up made of crumbs.  Then the byte streams under all of it
  (`text/TXStream.h`): `TXStream` and its `ReadBytes`/`WriteBytes`, the
  handle stream, the binary stream with its slack, the temporary stream
  factory, and the chunk table written out and read back
  (`WriteChunksRanges`/`ReadChunksRanges`) - which is what a text
  descriptor needs to name a stream at either end.

  Then the rulers (`text/TXRuler.h`): `TXTab` and the sorted
  `TXTabsArray`, `TXBasicRuler` and `TXAdvancedRuler` over the attribute
  object, the blanks and tab widths a line is laid out with, the line
  spacing, and the ruler frame a script sees.  Then the object ranges
  (`text/TXObjectRange.h`), where the rulers and the styles meet the
  text: which run of characters points at which attribute object, the
  sharing of equal objects and the running-together of neighbours that
  hold one, `TXObjectIterator` and the six-slot pool of shared objects.

  NOT YET in the streams: the factory's large-binary arm (it wants
  `FLBAllocCompressed` and large binaries, which are not reconstructed;
  it answers `kError_No_Memory` as the ROM's own does when nothing came
  of it).  NOT YET in the rulers: `TXRulerRange` (the rulers a
  document's paragraphs actually point at, which wants the runs) and the
  ruler's user interface (`TXRulerUI` and its three bars).  Above them,
  nothing yet.  The next piece is the runs themselves - `TXRun` (the
  attribute object a piece of text points at, with `TXTextRun` and
  `TXGraphicsRun` under it), `TXRunRange` and `TXRulerRange` over the
  object ranges, and `TXStyledText`, which holds the characters and the
  two ranges together.  Then `Textension`, the formatter and the lines,
  and `TXView` with its forty-one `FTX...` natives.

## Next: what is left of the natives, and why

The thin wrappers are done.  What `natives.py --unbound` still lists is
466 natives, and they are not a long tail of small jobs: nine out of ten
of them are the script-facing face of a subsystem that has no
reconstruction behind it at all.  Binding one of those means writing the
subsystem, not the wrapper.

| how many | what is under it |
|---|---|
| 145 | communications: endpoints, CCL, AppleTalk, IR, NTK, the desktop connection |
|  47 | the Intelligent Assistant: its lexicon and the sentence-level functions |
|  46 | the books and newspaper system |
|  38 | the text engine (TXView/TXFrames: styled documents with rulers) |
|  38 | the CIC handwriting engine: letters, training and reading |
|  36 | the test agent and the debug hooks |
|  35 | the package manager and the card |
|  12 | sound channels (the sound server) |
|   6 | the text engine's ranges and the book reader's HiliteBlock |
|   4 | large binaries on a store, and store passwords |
|  55 | everything else, a handful each |

Regenerate that table at any time with `natives.py --unbound --csv`, and
find the cheapest work inside a group with `--sizes build/MP2x00US`.

The smallest of those that would close a group of its own:

- **sound channels** (12): `TSoundServer`/`TSoundChannel` above the
  codecs, which are done.

And a handful that are blocked on one function each, named in the list
that `natives.py --unbound` prints: `Dispatch`, `RegisterGestalt` and
`ReplaceGestalt` want `PrimCallProtocolFromFrames` (the marshalling of
NewtonScript values into a C call); `GetBitmapInfo` wants the
large-binary questions to mean something; `ComputeParagraphHeight` wants
its geometry read out of the assembly rather than the decompiler.

Keep going through `natives.py --unbound`.  The areas whose machinery
exists are `views` (18 left), `recognition` (46), `qd` (13), `sound`
(13), `system` (16) and `stores` (11); `comms`, `books`, `assist`,
`testing` and `packages` are mostly areas that are not reconstructed at
all, and a native there is a project of its own rather than a wrapper.
The named pieces whose machinery *is* there:

- `HiliteBlock` 0x00164d64, which looks like a view native but is the
  book reader's: it wants `TLibrarian` and the page frames;
- the rest of the bitmap and shape verbs (`MakePict`, `PictToShape`,
  `MungeShape`, `MungeBitmap`, `GetShapeInfo`, `FindShape`);
  `GetBitmapInfo` also wants `GetBinaryStore`/`GetBinaryCompander`,
  which answer nil on a host because there are never large binaries;
- `instance:Dispatch` 0x00195228, which wants
  `PrimCallProtocolFromFrames` - the marshalling of NewtonScript values
  into a C call - and with it the `Gestalt` registration natives
  (`RegisterGestalt`, `ReplaceGestalt`);
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.

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

- **The text engine's `TXOffset` is a two-word struct, not a long.** Its
  mangled name appears as a class (`...F8TXOffset`), and the ROM passes
  it in two registers: the offset, and a flag saying whether an offset
  that falls exactly on a boundary belongs to the range it ends or the
  one it starts.  `src/text/` renders it as a `long` plus an explicit
  `atStart` argument, which is right for every function reconstructed so
  far; but `TXRulerRange::CharRangeToParagRange(TXOffset*, TXOffset*)`
  takes two of them *by pointer* and writes the flag back, so that one
  needs the real struct.  Introduce it (offset + atStart) before
  reconstructing the ruler range, and let the existing two-argument
  calls keep working.

- The text engine's next piece is `TXRun` (0x00245e64: an abstract
  attribute object with twelve virtuals, of which only `Assign`,
  `FullJustifPortion`, `VisibleLen`, `Click`, `SetHilite` and
  `DrawHilite` have bodies - the rest are pure and answered by
  `TXTextRun` and `TXGraphicsRun`) and `TXRunRange` (0x00245cc4: a
  TXObjectRange whose `CharToTextRun` searches backwards and then
  forwards for a range whose run `IsTextRun`).  Then `TXRulerRange`
  (0x00242c68), which is a TXObjectRange plus a `TXChars*`, a *pending
  ruler* and a flag: when the caret sits at the very end of the text
  after a line break, the ruler a slip sets belongs to the paragraph not
  yet typed, so it is held in `fDefaultRuler` until a character arrives
  (`GetPendingRuler` 0x00242eac, `InvalidatePendingRuler`,
  `NukePendingRuler`, and the `OffsetToObject`/`UpdateRangeObjects` that
  answer out of it).  It wants `TXGetParagStartOffset`/
  `TXGetParagEndOffset` as well.
