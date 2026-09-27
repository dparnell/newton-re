# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is being worked on, what is next, what is known to
be open, and the working notes that keep being needed.  Keep it current:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page, and update this file.  The history
of how things got here - the order of the work, and the host and ROM
bugs found along the way - is `docs/work-log.md`.

## State at 2026-09-28

- `cmake --build build/host` clean, `ctest --test-dir build/host` 108/108
  (`intl.Dates` fails about one run in ten: it reads the real clock).
- `analysis/coverage.py build/MP2x00US --check`: 11425 citations, 0 bad;
  6462 of 16671 functions (38.76%).
- `analysis/natives.py --unbound`: 318 of the ROM's 1326 natives
  are unanswered (table below); the recognition area's 116 are all
  answered.

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
(`natives.py --unbound`, the recognition area: all 116 answered since
2026-09-28), in the order planned:

1. ~~**Deferred recognition**~~ - DONE 2026-09-27 (`views/Rerecognize.h`,
   `docs/recognition/README.md`'s "Deferred recognition", ctest
   `host.NewtonRecognize`).  The grouping of unread strokes into ink is
   DONE too (2026-09-28, `recognition/InkGroups.h` over ParaGraph's word
   segmenter `WordSegment.h`; `HandleExpiredStroke` hands strokes to it);
   `Recognize` still answers nothing for them, as the ROM's own does.
2. DONE (2026-09-28, `docs/recognition/README.md`'s "The cursive
   recogniser and the letter styles"; the cursive engine's reading is
   NOT YET - measured below) **Letter styles**: `DoCursiveTraining`, `GetLetterWeights`/
   `SetLetterWeights`, the letter-shape natives, `RosettaExtension`.
3. DONE (2026-09-28, `views/ShapeVerbs.cpp`,
   `docs/views/README.md`'s "Questions asked of shapes") **Shape verbs**:
   `MakeInk`, `FindShape`, `GetShapeInfo`, `StrokeInPicture`,
   `AnimateSimpleStroke` (and `WedgeBox`, a stub until now).  NOT YET:
   - ~~`MungeShape`~~ DONE (2026-09-28, `views/ShapeVerbs.cpp`,
     `qd/MungeBitmap.cpp`, `toolbox/Matrix.h`; `MungeBitmap` too) bar
     `RotTiledBitmap` (a screen-sized bitmap turned in tiles out of a large
     binary on a store, over `TTile`).
   - ~~`PictToShape`~~ DONE (2026-09-28, `views/PictureShapes.cpp` over
     `DrawPicture`'s toShapes path, `docs/qd/README.md`'s "A picture
     turned into shapes"; `GetPattern` now takes every pattern form and
     `qd/Ports.h` has `MakeGrayPattern`/`MakeNSPattern`).  The picture's
     text becomes text boxes but is still not *drawn* by DrawPicture.
4. DONE (2026-09-28) `InkConvert` (over the codec's converter
   `ConvertData`, `ink/CICConvert.cpp`), `ConvertDictionaryData`,
   `MoveCorrectionInfo`/`AddUnit`/`HandleInkWord`; the boot's `UseWRec`
   choice was already made by `ReadCursiveOptions` (item 2).  NOT YET:
   `TEditView::TrackDistort` 0x000a9634 (dragging a selected shape's
   corner to distort it, which `MungeShape`'s neighbours would draw).

### The cursive reader (ParaGraph's xr engine), measured

Measured 2026-09-28 with `analysis/callgraph.py build/MP2x00US
GCTryToRecognize__FP13PS_point_typeP15GCWordDescrTypeP7rc_typeP17GCGroupParmStruct
CallGroupAndClassify__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitUiN24`:
**715 functions reached, 663 not done, about 431 KB** - twice the whole
of Rosetta - and a lower bound, since the domains' own `Classify` calls
reach it through function pointers (`TStrXrDomain::Classify` is 56 bytes).
The biggest pieces: a digit and number reader over "chunks" of writing
(`Digits` 24 KB, `SearchDigit_S`/`_K`/`_L`/`New_SearchDigit_V` 17-24 KB
each, `SearchNumber`, `FindPound`, `RecognizeZCCW` - about 110 KB
together), the low-level feature extraction over `low_type`
(`BaselineAndScale`, `transfrmN` 6.4 KB, `Extr`/`BigExtr`,
`line_pos_mist`, `StrElements`), punctuation and apostrophes
(`punctuation`, `RestoreApostroph`), and the lexical correction
(`ChunkCorrectByLexDB`).  The word descriptors the GC layer keeps for it
(`GCWordDescr*`, `GCWriteNewGroupResults`, the rest of
`GCTryToRemoveLastWords`, `InkGroups.h`) come first.  A plan, in the
order the reading reaches things: (1) the word descriptors and
`GCTryToRecognize`'s own frame (`GCLockRecognitionData`,
`GCFillBaseLineParameters`, `GCMergeLinesAndRemoveDash`); (2) the
low-level layer (`low_type`: the trace cut into elements and
extrema - the part once mistaken for Rosetta's feature extraction);
(3) the xr matching against the DTE/PPD tables already loaded
(`ParaGraph.h`); (4) the word search and the lexical DB; (5) the digit
and number reader last, since it is a reader of its own inside the
engine.  Like Rosetta it should sit behind the `TWRecognizer`-style seam
so a modern cursive recogniser can replace it.  Until then a cursive
letter set on the host reads nothing (its writing stays ink).

**Stage 1 DONE (2026-09-28, commits 46030bd, 2a0b8be;
`docs/recognition/README.md`'s "The way writing reaches the cursive
reader"):** the word descriptors (`WordDescriptors.h`), `GCTryToRecognize`'s
frame and the base line handed to the engine (`CursiveReader.h`:
`GCFillBaseLineParameters`, `SetRCB`, `GCLockRecognitionData`), and the
strokes-to-xrs domain that feeds it (`StrXrDomain.cpp`: `TStrXrDomain`,
`TStrXrUnit`, `CallGroupAndClassify` and the rest of the GC layer,
`WriteRecResults`/`GCWriteRW` making each word read a unit).  With a
cursive letter set the writing is now grouped into words and each word
reaches the reader; the reader answers -8 (the low level is NOT YET), so
the word is kept as ink.  `test_WordDescriptors`;
`src/host/demo/cursive.ns` with `NEWTON_TRACE_CURSIVE=1`.  Left of
stage 1: `SetStrXrRC` (0x000651e4, a configuration's `strxrCommands`).

The lock-up found on the way (`cursive.ns` stopping one run in three in
the heap's compaction) was heap damage done at boot: `CreateTrigramHeader`
asked for the ROM's 0x98 bytes and zeroed the host's 0xa8-byte header
over the next block's header - fixed (2026-09-28, `docs/work-log.md`).
The 'STXR' bisect only moved the heap's layout.  `cursive.ns` is now
ctest `host.NewtonCursive` (18 of 18 runs clean, where 3 of 6 locked up).

**Stage 2 sized** (callgraph.py, not-done functions below each root):
`low_level` 389 functions, 213 KB (the trace cut into xrs: `low_type`,
`BaselineAndScale`, `transfrmN`, `Extr`/`BigExtr`, `line_pos_mist`,
`StrElements`, ...); `xrw_algs` 70, 29 KB (stage 3/4: the xr matching and
the word graph); the `Chunk*` digit reader 72, 140 KB (stage 5).  Stage 2
is itself several rounds; a first testable piece would be the trace's
preprocessing and `BaselineAndScale` checked against a word of known
shape, then `Extr` (the extrema) - each layer can be tested on its own
because `low_level` writes an `xrdata_type` (0x18-byte elements) that
can be printed and compared with what the letter shapes imply.

**Stage 2 begun (2026-09-28, commits 9242531, 170373e, bdf5d96, 4334066,
d8a649a; `docs/recognition/README.md`'s "The low level"):** the
`low_type` state and its memory, the strokes, the filters (`Errorprov`,
`PreFilt`, `Filt`, `PSProc`), the extremum finders (`Extr`, `BigExtr`,
`DirectExtr`), the element list operations and about thirty of the
base-line finder's pieces (`LowBaseline.cpp`) - 84 functions, about 24 KB,
each checked by `test_LowLevel`.  Left below `low_level`: 305 functions,
189 KB.  The next piece is the rest of `transfrmN` (32 functions, 38 KB:
`classify_strokes`, `bord_correction`, `line_pos_mist`, the gap and
glitch finders, `calc_med_heights`, `extract_all_extr` and the
punctuation tests under it), which completes `BaselineAndScale` and
gives the first check against a word of known shape; then
`AnalyzeLowData`'s passes and `exchange`.  At this round's pace that is
seven or eight more rounds for `low_level`, and the whole reader
(`xrw_algs`, the lexical search, the digit reader) perhaps fifteen.
Working notes: the decompiler mangles this code's signed-byte and
two-result patterns (`__rt_sdiv` answers the quotient in r0 and the
remainder in r1; a direction kept as two shorts shows as bytes), so each
function's arithmetic is checked in the assembly (`analysis/disasm.py`),
and a struct holding pointers (`SPEC_TYPE`, `EXTR`, `low_type`) is
`sizeof`-allocated on the host.

**Stage 2, round 2 (2026-09-28, commits b66b2ca, 3089d57, 65ba664):** the
base-line finder is whole - `transfrmN` and the 32 functions below it
(`LowPunct.cpp`, `LowGeometry.cpp`, `LowLine.cpp`, `LowClassify.cpp`,
`LowBorders.cpp`) and `BaselineAndScale`, 38 functions and about 41 KB -
and the first end-to-end check passes: synthetic arches 40 high on y = 200
come back as a height of 40 on a lower border of 200, the trace rescaled
to 0x2796..0x27e6 (`test_LowLevel`'s `TestBaseline`).  AnalyzeLowData's
first seven passes are done too (`LowAnalyze.cpp`).  Left below
`low_level`: 267 functions, 148 KB - the rest of `AnalyzeLowData` (`Pict`,
the circle finder `Circle` with `work_with_circle`/`Orient00` and the back
and forward circles, `angl`, `FindSideExtr`/`PostFindSideExtr`, `Cross`,
the `lk_*` passes over sticks, circles and arcs, `Adjust_I_U`, `xt_st_zz`
with its dozen helpers, `RestoreColons`) and `exchange` (the xrs written:
`FillXrFeatures`, `AssignInputPenaltyAndStrict`, `check_xrdata`,
`MarkXrAsLastInLetter`, `GetLinkBetweenThisAndNextXr`).  Revised estimate:
at this round's pace (about 45 KB a round) three to four more rounds for
`low_level`, and the whole reader perhaps twelve.

**Stage 2, round 3 (2026-09-28, commits a95c376, d02ec5a, 87c4fa2):**
`Pict` whole (`LowPict.cpp`: the stroke descriptions, the dashes, dots,
hatches and crossings, `VertSticksSelector`'s upright sticks, `FantomSt`,
`FillCross`, `Recount`; 59 functions, about 34 KB) and `angl`
(`LowAngles.cpp`, 4 functions), with `CreateSDS`/`DestroySDS`.
`test_LowLevel`'s `TestPict` takes a word through the base line and
AnalyzeLowData's first steps into Pict (a dash marked 7, a dot 8, every
stroke described); `TestAngles` finds a hairpin's corner.  Worth knowing
for the rest: mark 7 is a *level* straight stroke, not an upright stick;
the decompiler reads an unaligned `ldr` at a word + 2 as the halfword there
when a `strb` of its low byte takes the halfword *before* it - check every
such copy in the disassembly.  Left below `low_level`: 207 functions,
115 KB - `Circle` (26 functions, 10 KB: `work_with_circle`, `Clash_my`,
`circle_type` and the `is_*_circle` tests), `FindSideExtr` (8 KB), `Cross`
(8 KB), the `lk_*` passes, `Adjust_I_U`, `xt_st_zz`, `RestoreColons`,
`PostFindSideExtr`, and `exchange` (20 KB).  At this pace (about 36 KB a
round) three more rounds for `low_level`; the whole reader perhaps eleven.

**Stage 2, round 4 (2026-09-28, commits cd697fb, 12949ca, 6d79cb8,
b20942c, 4462175, c65b08d):** `Circle` (`LowCircle.cpp`, 26 functions),
`FindSideExtr` (`LowSide.cpp`, 10, two of them unnamed), `Cross`
(`LowCross.cpp`, 6), `lk_begin` (`LowBegin.cpp`, 11) and `Adjust_I_U`
(`LowAdjust.cpp`) - about 55 functions and 25 KB, each with a test in
`test_LowLevel`; the `eps0`..`eps3` and `nbcut` tables and the rest of
`const1` from romtable.py.  AnalyzeLowData now runs, by hand in the test,
from the start through `lk_begin` and `Adjust_I_U`, and the cursive "uou"
comes out coded (a start and end at tops, three tops, four bottoms, the
o's crossings).  `low_level` is still not called.  Left below it: 157
functions, 90 KB - `lk_cross` (`del_inside_circles`, `analize_sticks`,
`analize_circles` and their helpers, about 12 KB), `lk_duga`
(`arcs_processing`, `conv_sticks_to_arcs`, the circle neighbours, about
10 KB), `xt_st_zz` (the t-bars, umlauts, quotes and punctuation,
`make_different_breaks`, `FindDArcs`: the biggest, about 30 KB),
`RestoreColons` (3 KB), `PostFindSideExtr` (3 KB), and `exchange` with
`FillXrFeatures` (about 12 KB: its layout and the tables it needs are in
`docs/recognition/README.md`'s low-level section).  The quickest route to
a first xr stream is `exchange` next - the test can call it after
`Adjust_I_U` without the passes in between - then the passes in the
order AnalyzeLowData calls them.  Revised estimate: about three more
rounds for `low_level` (this round did about 25 KB, every function from
the disassembly), and the whole reader perhaps ten.

**Stage 2, round 5 (2026-09-28, commits edb3d06, 94c1eb6, 7e8b3df):**
`exchange` and `FillXrFeatures` (`LowExchange.cpp`, `LowXrFeatures.cpp`:
the first xr stream - `test_LowLevel`'s `TestExchange` takes the "uou"
through to breaks at each end, 5 upper and 4 lower extrema, points and
boxes inside the trace; `penlDefX`/`penlDefH`, `xr_type_merits`,
`ratio_to_angle` and FillSHR's two limit tables from romtable.py),
`RestoreColons` and `PostFindSideExtr` (`LowRestore.cpp`: `TestRestore`
moves a colon written last back between two u's), and `lk_cross`
(`LowLkCross.cpp`, 32 functions: sticks, loops, the point-in-polygon
test - `TestLkCross` codes the uou's o as a closed loop).  About 60
functions and 42 KB.  Left below `low_level`: `lk_duga` (40 functions
not done, about 16 KB: `arcs_processing`, `conv_sticks_to_arcs`, the
circle neighbours) and `xt_st_zz` (67 not done, about 40 KB: the
t-bars, umlauts, quotes and punctuation, `make_different_breaks`,
`FindDArcs`), then wiring `low_level` into `GCTryToRecognize` (the
order is AnalyzeLowData's: `lk_begin`, `lk_cross`, `lk_duga`,
`Adjust_I_U`, `xt_st_zz`, `RestoreColons`, `PostFindSideExtr`; then
`exchange`).  Estimate: `lk_duga` one round, `xt_st_zz` one or two, then
`low_level` is whole; the reader after it (`xrw_algs`, the lexical
search, the digit reader) perhaps seven or eight more.

## Then: the testing system

The 38 `testing` natives (`docs/testing/README.md`); 32 are answered.
DONE (2026-09-28): **the journal** (`testing/Journal.h`; ctest
`host.NewtonJournal` records "ton" written on the Notepad and plays it
back), the tablet's bypass natives, **the test agent**
(`testing/TestAgent.h`: the `'tagt` world, its event handler and idler,
`TTestReporter`/`TAgentReporter`, the message queue, the `'tstp`/`'tsps`
part handlers, the newt world's `'tsse` handler, `ActivateTestAgent`/
`DeactivateTestAgent` and every `Test*`/`TestM*` native - a test manager
on the machine works end to end, ctest `host.NewtonTestAgent`; the
agent's idle proc now plays the journal, as the ROM's does) and the debug
hooks (`debug`, `DebugRunUntilIdle`, `DebugMemoryStats`, `StdioOn`/
`StdioOff`, `HobbleTablet`).  Left:

- The test server (`TCommServer`, 0x00209654-0x00209d5c; `Setup`,
  `ProcessTestServerCommand`, `DoDropConnection`'s sending): an AppleTalk
  endpoint, so it waits on the comms area.
- The C test cases (`TTestCaseTask` 0x0022afe0-0x0022b3b8,
  `StartCTestCase`, `DoNewtCTestCase`): a test case is a protocol in a
  `'tstp` part, run as a task of its own.
- The tests kept on a store (`MakeTestStore`, `TTestCommandQueue`,
  `TTestStoreFileList`, `DoRunTestsFromStore`, `StartACardTestCase`).
- The six testing natives still unanswered: the serial debugging
  (`InitSerialDebugging`, `PreInitSerialDebugging`); Uriah (`Uriah`,
  `UriahBinaryObjects` - `TObjectHeap::Uriah` 0x0031b154 and
  `UriahBinaryObjects` 0x0031bae0, a census of the frames heap printed to
  the REP, about 2.4 KB walking the heap's own block layout, with
  `gUriahROM`/`gUriahPrintArrays`/`gUriahSaveOutput` choosing what it
  prints); and the IR sniffing (`StartIRSniffing`/`StopIRSniffing`, 42
  functions and 4 KB of the IR stack not yet done - `callgraph.py`).
- `HobbleTablet` reaches nothing on the host (no inker port).

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
inside an area).  At 2026-09-27 (testing at 2026-09-28):

| area | how many | what is under them |
|---|---|---|
| comms | 121 | endpoints, CCL, AppleTalk (the `...Zone...` natives are AppleTalk's), IR, NTK, the desktop connection |
| frames | 115 | natives.py's catch-all: a handful each across many areas |
| testing | 6 | the serial debugging, Uriah, the IR sniffing (the agent, the journal and the debug hooks are done) |
| packages | 26 | units, packages on a store (the ROM domain manager, large binaries), 1.x packages |
| recognition | 0 | all answered (the cursive engine's reading is NOT YET behind them) |
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

- `ConvertDictionaryData` (0x0008f06c) is answered (2026-09-28).
  `GetRandomDictionaryWord` is answered (the random word generator,
  `recognition/RandomWords.h`).  The sixteen-bit Airus walkers are not
  done, and no dictionary in this ROM is sixteen-bit
  (`docs/recognition/README.md`, "What is left of the engine").

### Natives whose machinery is there, or is one function away

- `Dispatch` (`instance:Dispatch`, 0x00195228), `RegisterGestalt` and
  `ReplaceGestalt` want `PrimCallProtocolFromFrames` - the marshalling of
  NewtonScript values into a C call.
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.
- The rest of the bitmap and shape verbs: `MakePict` (`PictToShape`,
  `MungeShape`, `MungeBitmap`, `GetShapeInfo` and `FindShape` are done).  `GetBitmapInfo` also wants
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
- DONE (2026-09-28): the boot chooses the word recogniser the ROM's way,
  by the letter set (`SetUpRosetta`/`SetUpParaGraph` in
  `ReadCursiveOptions`); the host's own `SetWordRecognizer` calls are gone.

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

- **Heap damage**: `NEWTON_HEAPCHECK=N` (every Nth allocation; 1 for
  all) makes `newton` walk the newt task's heap after allocations and
  before every `DisposPtr`, and stop at the first damaged block with the
  C stack as image offsets for `tools/host/whichfunction.py`
  (`host/HostHeapCheck.h`; `NEWTON_HEAPDUMP` lists the blocks as it
  goes).  A ROM size handed to an allocator for a struct with pointers
  in it is the usual culprit: grep for literal sizes.

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
