# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is being worked on, what is next, what is known to
be open, and the working notes that keep being needed.  Keep it current:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page, and update this file.  The history
of how things got here - the order of the work, and the host and ROM
bugs found along the way - is `docs/work-log.md`.

## State at 2026-09-28

- `cmake --build build/host` clean, `ctest --test-dir build/host` 112/112
  (`intl.Dates` fails about one run in ten: it reads the real clock).
- `analysis/coverage.py build/MP2x00US --check`: 11904 citations, 0 bad;
  6843 of 16671 functions (41.05%) - the digit reader's statics are
  unnamed, so they add citations and not functions.
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
- **Cursive writing is read** by ParaGraph's reader, with a cursive
  letter set: `src/host/demo/cursive.ns` writes "ton" and "to" and the
  page types "ton to"; joined-up words (`cursive-joined.ns`) read "on",
  "no", "to", "nun" first (`NEWTON_TRACE_CURSIVE=1`,
  `NEWTON_TRACE_ARBITER=1`; the digit reader is NOT YET - stage 4 below).
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

**Stage 2, round 5 (2026-09-28, commits edb3d06, 94c1eb6, 7e8b3df,
44f6286):**
`exchange` and `FillXrFeatures` (`LowExchange.cpp`, `LowXrFeatures.cpp`:
the first xr stream - `test_LowLevel`'s `TestExchange` takes the "uou"
through to breaks at each end, 5 upper and 4 lower extrema, points and
boxes inside the trace; `penlDefX`/`penlDefH`, `xr_type_merits`,
`ratio_to_angle` and FillSHR's two limit tables from romtable.py),
`RestoreColons` and `PostFindSideExtr` (`LowRestore.cpp`: `TestRestore`
moves a colon written last back between two u's), and `lk_cross`
(`LowLkCross.cpp`, 32 functions: sticks, loops, the point-in-polygon
test - `TestLkCross` codes the uou's o as a closed loop).  About 60
functions and 42 KB; and of `lk_duga` (`LowLkDuga.cpp`, `TestLkDuga`)
the arc, loop and stick passes: `arcs_processing` (with `DyLimit`,
`IsDx_Dy_in_arcs_OK`, `IsDx_Dy_in_tips_OK`, `IsTipOK`),
`delete_CROSS_elements`/`ins_third_elem_in_circle` and
`check_IUb_IDf_small`.  Left below `low_level`: the rest of `lk_duga` (30
functions, about 11 KB: `lk_duga` itself, `prevent_arcs`,
`conv_sticks_to_arcs` over `cos_horizline`, `del_before_after_circles`
and the circle neighbours over the `NxtPrvCircle_type` block -
`check_before_circle`, `check_after_circle`, `check_next_for_*`,
`UpElemBeforeCircle`/`DnElemBeforeCircle`, `Is_8`, `O_GU_To3Elements`,
`HardOverlapRect` - and `delete_UD_before_DDL`; the disassembly is
0x002fa2f8-0x002fd920) and `xt_st_zz` (67 not done, about 40 KB: the
t-bars, umlauts, quotes and punctuation, `make_different_breaks`,
`FindDArcs`), then wiring `low_level` into `GCTryToRecognize` (the
order is AnalyzeLowData's: `lk_begin`, `lk_cross`, `lk_duga`,
`Adjust_I_U`, `xt_st_zz`, `RestoreColons`, `PostFindSideExtr`; then
`exchange`).  Estimate: `lk_duga` one round, `xt_st_zz` one or two, then
`low_level` is whole; the reader after it (`xrw_algs`, the lexical
search, the digit reader) perhaps seven or eight more.

**Stage 2, round 6 (2026-09-28, commits 25a1bd7, 948b6e4, b04803c):**
`lk_duga` whole (`LowLkDuga.cpp`: `prevent_arcs`,
`conv_sticks_to_arcs`, `del_before_after_circles` and the fifteen
circle-neighbour functions over `NxtPrvCircle_type`,
`delete_UD_before_DDL`; `cos_horizline` and `x/y/HardOverlapRect` in
`LowGeometry.cpp`; `TestLkDugaWhole` takes the uou through it), and of
`xt_st_zz` (`LowXtSt.cpp`) everything but `FindDArcs`: 50 functions and
about 30 KB - the late strokes found and placed, quotes, punctuation and
`RestoreApostroph` (with four unnamed helpers), umlauts, angstroms, the
crossed-out x, the parentheses, the breaks weighed
(`make_different_breaks`, `GetDxBetweenStrokes`,
`GetTraceBoxInsideYZone`), `del_close_MAX_MIN`, `CheckSequenceOfElements`
(`TestXtSt`).  Left below `low_level`: `xt_st_zz` itself (240 bytes; its
order is in `LowXtSt.cpp`'s header) and `FindDArcs`'s group (about 10 KB:
`CheckSZArcs` and `CheckDArcs` are 2.9 KB each, the rest small; they
share an `SZD_FEATURES` block of 0x44 bytes on the ROM's stack - +0 the
low_type, +4/+8 the two elements looked at, +0xc a new element, +0x10..
+0x20 x, y, the initial x and y and the point map); then `low_level`
and `AnalyzeLowData` themselves and the wiring into `GCTryToRecognize`.
Estimate: one round for `FindDArcs`, `xt_st_zz` and the wiring; the
reader above it (`xrw_algs`, the lexical search, the digit reader) about
seven more.

**Stage 2 DONE, round 7 (2026-09-28, commit 548e364):** `FindDArcs` and
its group (`LowDArcs.cpp`, 17 functions and about 10 KB, all from the
disassembly: the S/Z and d-bowl finders over `SZD_FEATURES`), `xt_st_zz`
itself, `AnalyzeLowData` and `low_level` - **the low level is whole** -
and `GCTryToRecognize` now calls `low_level`.  `test_LowLevel`'s
`TestDArcs` (an S gets its 0x23 element between the arcs) and
`TestLowLevelWhole` (the uou from its trace to its 13 xrs through
`low_level`); with a cursive letter set `cursive.ns` cuts "ton" into 13
xrs and "to" into 8 and the reader stops at `xrw_algs`, -9 (ctest
`host.NewtonCursive` checks both; clean under `NEWTON_HEAPCHECK=5`).

**Stage 3 DONE, round 8 (2026-09-28, commits cfc92ea, 1f1ffc2, efe50d9):
`xrw_algs` whole** - the matrix (`XrMatrix.h`: `xrcm_type`, `CountWord`,
`CountLetter`, `CountSym`, `CountVar`, `MergeVarResults`, the trace and
layout, and the two hand-written assembly loops `CountXrAsm`/`TCountXrAsm`
transcribed register by register), the Viterbi (`Xrlv.cpp`: all of
`xrlv`, `XrlvCHLXrlvPos` included), the dictionaries it asks what may come
next (`XrLex.cpp`, over two new Airus routes: `AEnum_NextSet9` and the
lexicon's `AL_NextSet9`), the word graph (`XrWordGraph.cpp`:
`create_rwg_ppd`, `GetCMPAliases`, `fill_RW_aliases`, `SortGraph`,
`GetSymBox`, `GetBaseBord`) and `SetMultiWordMarksWS`/`Dash`; tables from
romtable.py (`XrReaderTables.cpp`).  `test_XrMatrix`: six letters of the
ROM's table each read their own ideal xrs best, and `xrlv` reads the
ideal xrs of l and o as "lo" first.  Wired into `GCTryToRecognize`: with
a cursive letter set `cursive.ns`'s "to" comes out of the word graph as
"to" first, "ton" as For/ER/Eon/FR/EN (`NEWTON_TRACE_CURSIVE=1`; ctest
`host.NewtonCursive` checks both graphs; clean under
`NEWTON_HEAPCHECK=5`).  The host still answers -9 after the graph.

**Stage 4 begun, round 9 (2026-09-28): the answers, and the first
cursive word typed.**  `MakeAndCombRecWordsFromWordGraph`/
`MakeRecWordsFromWordGraph` (the graph made into readings, sorted, scaled
and cut - `XrAnswers.cpp`), `FillRecwordSplitInfo` and its helpers (which
strokes each word of a reading of several is: `connect_trajectory_and_*`,
`AddStrokesOfSymbol`, `AttachLostStrokeToWord`, `FillSplitInfoFromRWG`),
`GCFillLearningHandle` over `LHAddEntry`, and the word domain's own
reading - `TXrWordDomain::Group`/`Classify`/`Reclassify`/`ClassifyXrWord`
and `TXrWordUnit` - so an STXR unit becomes an 'XRWR' word unit, the
arbiter hands it to `TWordRecognizer`, and the page types it.
`cursive.ns` read **"For to"** then ("ton to" since FillSHR was put right,
below; ctest `host.NewtonCursive` checks the
answers and the page's text; `test_XrAnswers` the readings, the split
information and the training data).  The rules' headers are walked too
(`XrRules.cpp`: `PDFGetRule` and its address helpers, checked against
the ROM's 87 characters' rules in `test_XrMatrix`).

**Stage 4, round 10 (2026-09-28): the post-processing.**
`EvaluateAndSortAnswers` is real (`XrPost.h`, `XrPostCalc.cpp`,
`XrPostEval.cpp`; `docs/recognition/README.md`, "The post-processing"):
the letter table's rules are little programs, and the stack machine that
runs them (`CalculateQueueResult`, from the disassembly) and all 74
functions of their `Functions` table are there, with `EvaluateCharQuality`,
the side reasoning, the boxes, the missing crosses and `CheckDigitsLine`.
It only scores close calls between good answers (the best at least rc
+0x100 = 60 and no more than rc +0x102 = 10 ahead), so `cursive.ns` reads
as before; `test_XrMatrix` runs hand-made queues and the ROM's rules for
an l and an o, and a joined-up demo (`src/host/demo/cursive-joined.ns`,
ctest `host.NewtonCursiveJoined`) writes "on", "no", "mum", "to", "nun" in
one stroke each: they read "OR", "bb", "maps", "to", "Rap" - "no" was the
one scored, its o's rules sinking "Do"/"no" below "bb".  Left of stage 4:
`CheckDiacriticsDirections`/`AnalyseDiacriticsDirection` (only for a
French or German letter set, rc +6 bits 2-3), `MakeRecWordsFromGraph`/
`MakeNewPath`/`FillRecWordsElement`/`MergeTwoRecWordsSets` (the readings
of a fixed-string field's graph, rwg type 2), and `ORCreateLearnInfo`/
`Orto*` (about 2 KB; only with rc +0xb2 bit 6, which the Notepad does not
set).

**Stage 4, round 11 (2026-09-28): the readings put right.**  The poor
readings (and their capitals) were a port bug, found by isolating the
stages (`docs/recognition/README.md`, "Joined-up writing"): words made of
the letters' ideal xrs read as themselves with the Notepad's capitals
allowed (`test_XrMatrix`'s `TestIdealWords`), so the matcher and `xrlv`
were sound; the fault was **FillSHR**, whose four bracketing xrs had the
last two swapped in all six cases, negating every shift class
(`test_LowLevel`'s `TestFillSHR`).  Fixed: `cursive.ns` reads "ton to";
with the demo's o drawn as a cursive o is (its join arriving at the top
right), `cursive-joined.ns` reads "on" 82, "no" 90, "Mom" 67 ("mum"
fifth), "to" 86, "nun" 76.  "mum" and "nun" then lose to the scrub
gesture - their synthetic stems are retraced exactly, a zig-zag -
and go down as ink words; `TestScrub` agrees with the ROM, so that is the
drawing (`NEWTON_TRACE_ARBITER=1` prints each arbitration).  Left of
stage 4 as above; then the `Chunk*` digit reader (only for a field that
allows numbers, rc +0xb6; 82 not done, about 146 KB), three or four
rounds.  Other stages might hold slips like FillSHR's - a transcribed
store order is the thing to check: the rest of FillXrFeatures
(FillOrients) agreed with ParaGraph's own later source where compared,
and the published source (below) is the quickest way to find a
suspect.

**Round 12 (2026-09-28): the reader's leftovers.**  Done: `SetStrXrRC`
(a configuration's `strxrCommands`, byte commands reaching the host's own
fields by their ROM offsets); the readings of a graph of alternatives
(`MakeRecWordsFromGraph`, `MakeNewPath`, `FillRecWordsElement`,
`MergeTwoRecWordsSets` - a fixed-string field's graph, read twice, once
for a number and once for a word, and merged); **learning** - `DoLearning`
now hands the pen's trace (`GetTraceFromStrokes`, which was there all
along; the NOT YET note named a wrong address) to the word domain, and a
word info's unit id is read back right (`UnitID` read a host ULong out of
two UniChars, so `DoIndexedLearning` found no recogniser and crashed):
`cursive.ns` reads "ton" again with learning on, learns it, and the
letter weights are no longer the defaults; and the digit reader's context
(`Chunk.h`: `ChunkAllocCtx`, `ChunkCleanUp`, `IsChunkNumbers`,
`ChunkModifyRC`/`ChunkRestoreRC`, `ChunkWriteParamCtx`), called where
`GCTryToRecognize` calls them, and the first of its geometry
(`v_MostFarFromChord`, `v_QDistFromChord`, `GetDirection` over
`ChunkTables.cpp`).  Left:
- **The orthographic learning** (only with rc +0xb2 bit 6 / +0xb8 bit 3,
  which the Notepad never sets): `ORCreateLearnInfo` over `OrtoCreate`,
  `OrtoGetmem`/`OrtoCalcSize`/`OrtoResize`/`OrtoFasten`, `OrtoEntries`
  (796 B, which the decompiler mangles - read the disassembly) and
  `RemovePointAndSort` (0x00147548-0x00147d70, about 2.5 KB); and
  `ORTraining` (a tail call into `OrtoTraining`, 0x00147e74) over
  `LearnPartsCopy` and `TrainTrajectory` - a letter-shape database of its
  own (`FillNwtSample` over `TraceToOdata`/`RjctAppr` and the DCT
  (`FDCT4/8/16`, `IDCT...`), `AddToDataBase`, `SearchInDataBase` with
  `FirstSearch`/`SecondSearch`, `Occam`, `SQRT32_ORTO`): 56 not done,
  about 15 KB.  The `_LEARN_ARRAY_tag` block goes into the training data
  ('ORTL'), so keep its bytes as the ROM lays them out (big-endian halves;
  the pointer at +0x14 is only a cache, recomputed from +0x04 each time -
  on the host leave it unused).  `ConfigureArea`'s base-line and grid
  geometry is still NOT YET too.
- **The digit reader** (below).

**The digit reader, sized** (`callgraph.py build/MP2x00US ChunkAllocCtx
ChunkProcessor ChunkModifyRC ChunkWriteParamCtx ChunkPatchXrdata
ChunkRestoreRC ChunkSortAnswers ChunkCorrectByLexDB ChunkCleanUp
--through-done`): 176 functions reached, **102 not done, about 150 KB**.
`ChunkProcessor` (0x002a6b50, 2.6 KB) is the whole of it: the points made
a `tag_WORD_TRACE`, `ExtrWordTrace_V` (its extrema) and `GetLineApprox`
(a polyline approximation, `tag_wapx_type`, with `SetAllDirections`,
`GetDirection`, `v_MostFarFromChord`, `v_QDistFromChord` - about 5 KB
together), `ChunkConstruct` (the writing cut into chunks: `ApxToBrackets`,
`ApxToCLine`, `ChunkFillMainData`, `ChunkMakeStrokes`, `LO_*` - the list
of low objects), then **`Digits`** (24 KB) over the four digit searchers
`SearchDigit_S` (24 KB), `SearchDigit_K` (17 KB), `SearchDigit_L` (3.7 KB)
and `New_SearchDigit_V` (20 KB), `SearchNumber`, `FindPound` (the £ sign,
6 KB), `RecognizeZCCW`, `GetCircles`, `Check_4`, `CutNumberInDigits`,
`DefHeightsForNumber`, and after the xr reader `ChunkPatchXrdata` (1.2
KB), `ChunkSortAnswers` (an unnamed sort at 0x002a4c04) and
`ChunkCorrectByLexDB` (3.5 KB).  In that order, bottom up: (1) the trace,
`ExtrWordTrace_V`, `GetLineApprox` (with `SetAllDirections`; its
`v_MostFarFromChord`, `v_QDistFromChord` and `GetDirection` are done) and
the `LO_*` list (`LO_Create` is a 0x482c-byte block with a pointer at
+0x28 and 0x3c-byte objects from +0x1dc - a host layout of its own; the
ROM's `LO_Destroy` frees it with an inlined `DisposHandle` of the handle
`HWRMemoryAlloc` keeps in front of the block), each testable on a drawn
digit; (2) `ChunkConstruct` and its helpers; (3) `Digits` with
`SearchDigit_L` (the smallest searcher) first, then `_V`, `_K`, `_S`;
(4) the rest, and `ChunkProcessor` itself wired in, with a demo writing
"42" into a numbers field (rc +0xb6 is set by a field whose
recognition flags allow numbers).  Four or five rounds.

*Progress (2026-09-28)*: steps (1) and (2) are done -
`recognition/ChunkTrace.cpp` (`ExtrWordTrace_V`, `GetLineApprox`,
`SetAllDirections`), `ChunkLowObj.cpp` (all eleven `LO_*` and the free
list), `ChunkConstruct.cpp` (`ChunkConstruct`/`ChunkDestroyData`,
`ChunkFillMainData`, `ChunkMakeStrokes`, `ApxToBrackets` with its unnamed
bracket maker, tidier (0x00286fb4) and hook dropper, `ApxToCLine` and
the classes it gives a chunk (300 a line, 400 an arc, 500 mixed, 600
two lines, 700 two arcs, 1400 more), the unnamed reclassing pass
0x00285bc8 (from the disassembly - the decompiler garbles it),
`CreateRealChunkInd`, the arc measures) and, of step (3), the two the
searchers start from (`ChunkPutClassesToLO`, `DefRectForChunks`).
`test_Chunk` draws a 4 and a 2 with a synthetic pen and checks the
turns, the polyline, the chunks (the 4's bent stroke is class 600, its
upright 300; the 2's hook an arc), the brackets and the list.  Left
(`callgraph.py build/MP2x00US ChunkProcessor__FPvP13PS_point_typei
--through-done`): 60 functions, about 124 KB.  **`SearchDigit_L` is not
the small one it looks**: its body (0x0028ddb4-0x0028eb9c, ten unnamed
statics, 3.5 KB) calls seven more unnamed statics that sit inside
`RecognizeZCCW`'s extent (0x0028ffe8, 0x0029082c, 0x002909f0,
0x00290c68, 0x00290de8 - which writes the digit found - and 0x00290e2c)
and `DgtFromDnHorseshoe`'s (0x0028f17c), so it and `RecognizeZCCW`
(4.5 KB) are one piece of about 8 KB; it also reads staff +0x40, which
`Digits` sets.  So step (3) is better begun at `Digits` itself (read its
top level first to see what it sets up and in what order it calls the
searchers), then `RecognizeZCCW` with `SearchDigit_L`; then `_V`, `_K`,
`_S`.  Three or four rounds.

*Progress (2026-09-28, round 3)*: of step (3), `Digits`' top level has
been read (its order: `DefHeightsForNumber`, `ChunkPutClassesToLO`,
`GetCircles`, `SearchDigit_L`, `SearchDigit_K`, `New_SearchDigit_V`,
`SearchDigit_S`, `FindPound`, `Check_4`, unnamed 0x002a09b0 and
0x002a2758, `CutNumberInDigits`, the second-look pass 0x0029ce20, then
with staff +0x50 clear 0x002a1a98, 0x0029e888, 0x002a19ec and
`SearchNumber` until one answers, else 0x0029ccd4; then 0x002a2078,
0x0029fbcc over a scratch block the size of the stroke count,
0x002a0d74, the 0x834 class's objects copied out as (first point, last
point) pairs for the caller, and 0x0029ffc8 deciding the answer's second
bit), and these are done (`recognition/ChunkDigits.cpp`,
`ChunkSearchL.cpp`, `test_Chunk`'s `TestLineAndCircles`, `TestSearchL`,
`TestSecondLooks`): `DefHeightsForNumber` with its four statics,
`GetCircles`, `SearchDigit_L` with its seventeen statics (the $, 2/7, 5
with its bar, 4/9, 3/5/9 tests), the searchers' shared geometry
(`direct_suits`, `distance_between_directions`, `take_next_point`,
`take_prev_point`, `x_in_line`, `x_in_curve`, `cross_with_line`,
`CheckQIntersec`/`XY`), `ThreeToFive`, `RecognizeZCCW` and `Check_4`.
The `DgtFrom*`, `GreyDgtFromELink` and `SignFromTwoSections` statics
that sit between them belong to `New_SearchDigit_V` (it is their only
caller, and they take its `tagLocalStuff`), so they go with it.

Next, in order: (a) the second-look pass 0x0029ce20 - it sorts up to
thirty digits by their first node, runs twelve statics over the sorted
array (0x0029d900, 0x0029dd6c, 0x0029dbd8, 0x0029d808, 0x0029e30c,
0x0029e530, 0x0029e048, 0x0029d428, 0x0029e6bc, 0x0029eaac, 0x0029eeb4 -
the largest, 870 instructions - and 0x002a0740), then `ThreeToFive` and
`RecognizeZCCW`, then files the class-1200 objects between the digits
and writes each digit and gap out as class 0x76c objects; about 2800
instructions in all, and testable by laying digit objects over drawn
writing as `TestSecondLooks` does; (b) `SearchDigit_K` (17 KB); (c)
`New_SearchDigit_V` with its statics (20 KB + 3 KB); (d) `SearchDigit_S`
(24 KB), `FindPound`, `SearchNumber` and the other statics `Digits` runs;
(e) `ChunkProcessor` wired in, `ChunkPatchXrdata`, `ChunkSortAnswers`,
`ChunkCorrectByLexDB`, and a demo writing "42" into a numbers field.
About five more rounds.

*Progress (2026-09-28, round 4)*: (a) and (b) are done -
`recognition/ChunkSecondLook.cpp` (the second-look pass and all twelve
statics, with the variant test 0x002a01dc under 0x002a0740;
`DigitsSecondLooks` in `Chunk.h`) and `recognition/ChunkSearchK.cpp`
(`SearchDigit_K` and its nineteen statics, `find_direct_forward`/
`_backward`); `test_Chunk`'s `TestSecondLookPass` and `TestSearchK` (a 4,
x, 7, #, 8 and % drawn and found).  Next: (c) `New_SearchDigit_V`
(0x00296e04, 20 KB; its statics `DgtFromDnHorseshoe` 0x0028eb9c,
`DgtFromUpCCWArc`, `DgtFromAloneDnCCWArc`, `GreyDgtFromELink`,
`SignFromTwoSections` take its `tagLocalStuff`) - it is the one that
reads a lone 1, which K leaves alone; then (d) and (e) as above.  Three
or four more rounds.

**A reference for the cursive reader**: PhatWare, who bought ParaGraph's
recogniser, published a descendant of it under the GPL v3
(https://github.com/phatware/WritePad-Handwriting-Recognition-Engine;
`WindowsTools/NNLegacyTool/` is the oldest tree - LOW/, XRWS/, POST/).
It is a later version and is not the ROM, so it is never transcribed:
it names things (the xr types are its `X_...` codes, `LOW/STD/XR_NAMES.H`:
0x14 `X_UD_F`, a forward lower arc; 0x0e `X_UU_B`; heights 1..13 from
super-uplinear to super-underlinear) and says what a function is for,
and where the ROM and the port disagree with it, the ROM's disassembly
decides.  The FillSHR slip showed up as exactly such a disagreement.

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

A lead the owner pointed at (2026-09-28), to look into when this track
starts: **mosrun** (https://github.com/MatthiasWM/mosrun) - "short for
'MacOS runtime environment', a program that runs m68k based MPW tools on
Mac OS X, Linux, and MSWindows", a minimal Mac OS 7.6 and a 68020
emulator whose main purpose is "to run the Apple Newton developer tools,
such as the cross compiler and the Rex builder, natively and as part of a
build chain" (ARM6asm, ARMLink, Rex).  Apple's own tools running on the
host could serve as an oracle for the builder (step 3: a ROM extension
made by Apple's Rex builder to compare ours against, byte for byte) and
perhaps for the code generation step 1 has to reproduce, if a NewtonScript
compiler is among the tools it runs - to be checked.  It is an outside
tool: if it becomes part of the process it must be vendored or fetched by
a documented script, per the project's rule that every tool lives in the
repository and is reproducible.  A second, older option the owner also
pointed at: Kelvin Sherlock's **mpw** (https://github.com/ksherlock/mpw),
a "Macintosh Programmer's Workshop (mpw) compatibility layer" - a 68k
emulator with the MPW toolbox calls, which its README says runs "only [on]
OS X 10.8+ with case-insensitive HFS+" and does not name the Newton tools;
mosrun is the one aimed at them and runs on Windows too, so it is the
first to try, with mpw as a second opinion where a tool misbehaves.

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
