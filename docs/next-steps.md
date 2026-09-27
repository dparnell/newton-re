# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is being worked on, what is next, what is known to
be open, and the working notes that keep being needed.  Keep it current:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page, and update this file.  The history
of how things got here - the order of the work, and the host and ROM
bugs found along the way - is `docs/work-log.md`.

## State at 2026-09-27

- `cmake --build build/host` clean, `ctest --test-dir build/host` 102/102
  (`intl.Dates` fails about one run in ten: it reads the real clock).
- `analysis/coverage.py build/MP2x00US --check`: 10565 citations, 0 bad;
  5742 of 16671 functions (34.44%).
- `analysis/natives.py --unbound`: about 390 of the ROM's 1326 natives
  are unanswered (table below).

## What works

- The machine boots into the Setup assistant, and `src/host/demo/setup.ns`
  taps its way through to the Notepad.  Names, Dates (month, day with its
  meetings, To Do list), Extras, the Preferences roll and Time Zones
  (world map, home and away cities, clock icons) open and draw;
  `src/host/demo/open-apps.ns` opens every built-in application a user
  reaches and reports what fails (only the Sound Recorder, below).
- **Writing already there is read again**: `Recognize`,
  `RecognizeInkWord`, `RecognizeTextInStyles`, `RecognizePara`/
  `RecognizePoly` and the double tap on an ink word
  (`src/host/demo/recognize.ns`).
- **Handwriting is read** by the ROM's own engine (Rosetta):
  `build/host/host/newton --rom build/MP2x00US/rom.bin --display
  320x480 --headless 50 --script src/host/demo/write.ns` writes "ton" and
  "to" and the Notepad shows the typed text "ton to"
  (`NEWTON_TRACE_ROSETTA=1` prints what went to the engine and what came
  back).  With a window, anything written with the mouse is read.
  Writing that cannot be read is kept as ink (`src/host/demo/ink.ns`;
  the route across the five areas is `docs/ink/README.md`'s "From the
  pen to ink on the page"), a double tap on a word opens the corrector
  (`src/host/demo/correct.ns`).
- **Shapes are recognised** with the Notepad set to shapes
  (`src/host/demo/shapes.ns`, `snapping.ns`; `NEWTON_TRACE_SHAPES=1`).
- **A selection can be made, dragged and resized** (`src/host/demo/drag.ns`),
  and a drag let go on the background becomes a clipping.
- **The Intelligent Assistant** parses a sentence and carries out its
  task: `src/host/demo/assist.ns` ("call Daniel"),
  `src/host/demo/assist-tasks.ns` (find, remind, schedule, time in, call,
  fax - each slip's button tapped and what it opens photographed).
- **Modal dialogs**, over real forked tasks (`src/host/demo/modal.ns`).
- QuickDraw pictures play back (the world map), the outline list, the
  meeting and its duration bar.
- **Packages are installed by the package manager**, the ROM's own at
  boot and one from a file with `newton --package file.pkg` (as many as
  wanted) or by dropping a .pkg onto the window (`host/HostPackages.h`).
  `packages.py build/MP2x00US --extract DIR --rename Formulas=Formulas2`
  makes a loadable copy of a built-in package to try it with (a ROM
  package's refs are ROM addresses; `--relocatable` rebases them).

## Now: finishing the recognition system

The owner asked (2026-09-27) for the recognition system to be put to bed,
then for the testing system.  What is left of recognition is its natives
(`natives.py --unbound`, the recognition area, 23 now), in the order
planned:

1. ~~**Deferred recognition**~~ - DONE 2026-09-27 (`views/Rerecognize.h`,
   `docs/recognition/README.md`'s "Deferred recognition", ctest
   `host.NewtonRecognize`).  Left of it: the grouping of unread strokes
   into ink (`IGGroupAndCompressStrokes`, the CIC library), which is
   also what `HandleExpiredStroke` waits on.
2. **Letter styles**: `DoCursiveTraining`, `GetLetterWeights`/
   `SetLetterWeights`, the letter-shape natives, `RosettaExtension`.
3. **Shape verbs**: `MakeInk`, `FindShape`, `GetShapeInfo`, `MungeShape`,
   `PictToShape`, `StrokeInPicture`, `AnimateSimpleStroke`.
4. `InkConvert`, `TrackDistort`, `ConvertDictionaryData` (below),
   `MoveCorrectionInfo`/`AddUnit`/`HandleInkWord`, and the boot's
   `UseWRec` choice.

## Then: the testing system

The 38 `testing` natives, starting with **the journal** - recording the
pen's strokes and playing them back (`JournalStartRecord`,
`JournalStopRecord`, `JournalReplay*`), which would make every demo
script a recording rather than hand-placed pen positions.  Then the test
agent (`TestM*`, `Test*`), the tablet bypass (`StartBypassTablet`,
`StopBypassTablet`, `InsertTabletSample`), the debug hooks
(`DebugMemoryStats`, `DebugRunUntilIdle`, `Stdio*`), Uriah and the IR
sniffing.

## The package manager: what is left

Done (2026-09-27, `docs/packages/README.md`): the manager task and its
events, the package list and registry, `TPMIterator`, the part handlers
('form, 'auto, 'soup), `CPackagePipe`, `LoadHighROMFramesPackages`
sending the ROM's packages to the manager, and the host's way in
(`--package`, drag and drop).  Left, in the order they are likely to
matter:

- **Units**: five ROM parts carry `_ExportTable`s (two `_ImportTable`s),
  installed without them (a stderr line at boot).  A third-party package
  importing a ROM unit needs `InstallExportTables`/`InstallImportTable`
  0x000cfcd4-0x000d0758 and the unit natives.
- The `'book` part handler (the help book is refused for want of it) over
  the book reader, then `'dict` and `'comm`.
- Streamed sources (`TPackageLoader`, `CPartPipe`, `TPipeApp`) and
  packages on a store (the ROM domain manager, large binaries), which the
  remaining package natives (`ActivatePackage`, `ObjectPkgRef`, ...)
  stand on.

## Next, after the package manager

- **The sound server**: `TSoundServer`/`TSoundChannel`, the codec and
  DMA channels, and a host audio driver behind `hal/` - the Sound
  Recorder (the one built-in application that does not open: `FSoundOpen`),
  the pen clicks, alarms and button sounds.  The codecs are done
  (`docs/sound/README.md`).
- **Drawing speed**: the blitter and the lines work a pixel at a time
  through region scan conversion, which is why a busy screen redraws
  slowly on the host.  A faster blitter with identical output is host
  work only, but it makes the interactive build pleasant to use.
- **The rest of pictures**: text, curves and paths inside a picture, and
  the picture turned into shapes (`docs/qd/README.md`).
- The date the Assistant's "tomorrow" comes to: "schedule lunch with
  Daniel tomorrow" puts the meeting on today.

## A long-term track: booting with no ROM image

The owner's goal (2026-09-27): the system boots without a ROM image.  How
they picture it: all the ROM's NewtonScript decompiled to NewtonScript
source that the reconstruction's own compiler turns back into the
*identical* bytecode (the byte-for-byte round trip being the proof), and
tools that put the ROM's resources - bitmaps, sounds, fonts, strings and
the locale data - into editable files in the repository, with a build
step packing them and the recompiled NewtonScript back into the objects
the OS loads, so a change to a source file or a resource is rebuilt and
used on the next run.  The pieces, roughly in order:

1. A NewtonScript **decompiler** (over `nsfunctions.py --disasm`'s
   decoding) whose output compiles back to the same bytes, checked
   function by function over all of the ROM's code objects - the
   compiler (`frames/Compiler.h`, the ROM's own yacc tables) being
   faithful enough to reproduce the ROM's code generation is the part to
   watch.
2. **Resource extraction**: bitmaps to images, sounds to sound files,
   fonts, strings, locale bundles, the object graph that ties them
   together, as files a person can edit.
3. A **builder** that makes the object area (and the packages) from the
   sources and resources, in the form `frames/ROMImport.cpp` reads today.
4. Booting from that output with no `--rom`, the generated tables that
   already live in `src/` (romtable.py, romconstants.py, nsgrammar.py,
   ...) supplying the rest.

Until then the ROM image stays how the reconstruction is checked against
the original; new run-time dependencies on it are to be avoided or noted.

## The natives still unanswered

`python tools/newton-rom/analysis/natives.py --unbound` lists them by
area (`--csv` for a table, `--sizes build/MP2x00US` for the cheapest work
inside an area).  At 2026-09-27:

| area | how many | what is under them |
|---|---|---|
| comms | 121 | endpoints, CCL, AppleTalk (the `...Zone...` natives are AppleTalk's), IR, NTK, the desktop connection |
| frames | 115 | natives.py's catch-all: a handful each across many areas |
| testing | 38 | the test agent and the debug hooks |
| packages | 26 | units, packages on a store (the ROM domain manager, large binaries), 1.x packages |
| recognition | 23 | the rest of the recognition system |
| books | 20 | the book reader and newspapers (`TLibrarian`) |
| views | 17 | |
| sound | 9 | the sound server |
| system | 8 | |
| qd, intl | 7 each | |
| stores | 5 | large binaries on a store, store passwords |

The areas whose machinery exists are worth sweeping with `--ready`;
`comms`, `books` and `testing` are subsystems of their own.

## Known open, by area

### Recognition

- **How well it reads.**  A perfectly round synthetic "c", as wide as
  an "o", comes back with every code under 0.6%, so the readings of a
  word with one in it all tie.  The picture the classifier is shown is
  upright and unmirrored (dumped from `NetPatternImageSetInput`), the
  path from there to the readings matches the ROM instruction for
  instruction, and a "c" a little narrower than an "o" reads as "C" or
  "c" - so the net is simply particular about its c's.  A trace of
  `BPNetEvaluate` from an emulator would be the reference to check the
  classifier's own numbers against, if ever one is wanted.

- `ConvertDictionaryData` (0x0008f06c) is unanswered but no longer
  blocked: the completions walk it waited on is done
  (`AEnum_FirstLast`/`AEnum_NextPrevious`, `FirstCompletion`/
  `NextCompletion`).  `GetRandomDictionaryWord` is answered (the random
  word generator, `recognition/RandomWords.h`).  The sixteen-bit Airus
  walkers are not done, and no dictionary in this ROM is sixteen-bit
  (`docs/recognition/README.md`, "What is left of the engine").

### Natives whose machinery is there, or is one function away

- `Dispatch` (`instance:Dispatch`, 0x00195228), `RegisterGestalt` and
  `ReplaceGestalt` want `PrimCallProtocolFromFrames` - the marshalling of
  NewtonScript values into a C call.
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.
- The rest of the bitmap and shape verbs: `MakePict`, `PictToShape` (the
  picture turned into shapes: `DrawPicture`'s other path), `MungeShape`,
  `MungeBitmap`, `GetShapeInfo`, `FindShape`.  `GetBitmapInfo` also wants
  `GetBinaryStore`/`GetBinaryCompander`, which answer nil on a host
  because there are never large binaries.
- `HiliteBlock` 0x00164d64 looks like a view native but is the book
  reader's: it wants `TLibrarian` and the page frames.
- `natives.py --unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  `comms`, `books` and `testing` are subsystems
  not reconstructed at all: a native there is a project of its own
  rather than a wrapper.

### From the audit of the code after virtual calls, and elsewhere

- From the audit of the code after virtual calls, still NOT YET:
  `TEditView::TrackDistort` (a corner of a selected polygon dragged -
  it waits on the polygon hilites, `TPolygonView`'s `MakeHilite` and
  the rest, so no view answers `ClickOptions` bit 2 yet); the double tap
  on a selection of text that sends its ink to be read again
  (`TEditView::RealDoCommand`'s re-recognition branch, and the
  paragraph's commands 0x19 and 0x1a - the ROM's deferred recognition,
  `MakeRerecognizeArea`/`RerecognizeWord`/`BuildRecConfigForDeferred`,
  a piece of the recognition system of its own); ink dropped on a paragraph (`InkConvert`,
  which needs the CIC library's transcoder `ConverterRun` under
  `ConvertData` 0x00280980); and `GetRangeProperties`' `offset` slot
  (two line heights the host's line cache does not keep).

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

### Running the applications

- `src/host/demo/open-apps.ns` opens each built-in application a user
  reaches in turn and reports what fails (with `NEWTON_TRACE_MISSING`
  naming any native the ROM's scripts ask for that is not there).  One
  fails today: the Sound Recorder (`FSoundOpen` - the sound server,
  `TSoundServer`/`TSoundChannel`).  Handwriting Practice now opens: it
  wanted the random word generator (`recognition/RandomWords.h`) and
  `:GetPolygons`/`:DrawPolygons` (the writer's strokes kept as 'polygon
  binaries relative to the view and drawn back as lines with the view's
  `drawPenMode`/`drawPenSizeX/Y` - `DrawSetPen`).  Run it after a piece
  of work that touches the view system or the recogniser.

### The text engine

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
  the sanitiser. Wrap explicitly through `uint32_t` (not `ULong`, which is
  pointer-sized on the host - see below).
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
  a backslash (the helper then writes a real line break into the C
  string, and the file no longer compiles).  Write any helper that edits
  sources with the Write tool and run it with `python file.py`; never
  pipe Python through a heredoc.
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
- **`ULong`, `Long` and `SLong` are pointer-sized on the host**
  (`uintptr_t`, `src/ddk/NewtonTypes.h`), while plain `long` is 32 bits
  on Windows.  So an `Int64`'s `lo`, a `TTimeout`, a `TRegister` read
  back as a `ULong`, all hold more than 32 bits: truncate explicitly
  where the ROM's word is 32 bits (`(ULong) (uint32_t) x`).  This is
  what made the clock jump at 2^32 ticks.
- A running `newton.exe` cannot be relinked: stop it before building.
- A host that looks hung: `tools/host/stacksample.py <pid>` (its busy
  thread's stack, no debugger needed) and `NEWTON_TRACE_UPDATE=1` (each
  region repainted) - `tools/host/README.md`.
- `test_Views` leaves the port's visible region narrowed by earlier
  tests (160x100 at one point); a test that checks update regions should
  not assume the whole screen is visible.
- A script run by `newton --script` sees a fresh store unless `--store`
  is given, so it walks the Setup assistant first
  (`src/host/demo/assist-tasks.ns` has the walk to copy).
