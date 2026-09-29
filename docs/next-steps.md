# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is open, what the candidates for the next piece of
work are, and the working notes that keep being needed.  Keep it short:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page under `docs/`, and take it out of
here - this file says what is *not* done.  The history of how things got
here, the plans of finished work and the host and ROM bugs found along
the way are all in `docs/work-log.md`.

## State at 2026-09-29

- `cmake --build build/host` clean, `ctest --test-dir build/host` 130/130
  (`intl.Dates` fails about one run in ten: it reads the real clock).
  Several agents work in parallel, each building in its own directory
  under `tmp/`; the counts here are refreshed as each reports.
- `analysis/coverage.py build/MP2x00US --check`: 12648 citations, 0 bad;
  7415 of 16671 functions (44.48%).
- `analysis/natives.py --unbound`: 1060 of the ROM's 1326 natives
  answered (79.9%); recognition 125 of 125.

## What works

- The machine boots into the Setup assistant, and `src/host/demo/setup.ns`
  taps its way through to the Notepad.  Names, Dates (month, day with its
  meetings, To Do list), Extras, the Preferences roll and Time Zones
  (world map, home and away cities, clock icons) open and draw;
  `src/host/demo/open-apps.ns` opens every built-in application a user
  reaches and reports what fails (only the Sound Recorder: the sound
  server).  Run it after any piece of work that touches the view system
  or the recogniser; `--script` runs see a fresh store unless `--store`
  is given, so a script walks the Setup assistant first
  (`src/host/demo/assist-tasks.ns` has the walk to copy).
- **Handwriting is read**, by both of the ROM's recognisers, chosen as the
  ROM chooses by the writer's letter set:
  - printed writing by Apple's Rosetta: `src/host/demo/write.ns` writes
    "ton" and "to" and the Notepad types "ton to"
    (`NEWTON_TRACE_ROSETTA=1`);
  - cursive writing by ParaGraph's reader: `src/host/demo/cursive.ns`
    reads "ton to" and learns from a correction; joined-up words
    (`cursive-joined.ns`) read "on", "no", "to", "nun" first
    (`NEWTON_TRACE_CURSIVE=1`, `NEWTON_TRACE_ARBITER=1`);
  - numbers by ParaGraph's digit reader: `src/host/demo/numbers.ns` types
    "42 10 217" ("217" offered as the date "2/7" too).
  Writing that cannot be read is kept as ink (`src/host/demo/ink.ns`),
  a double tap on a word opens the corrector (`correct.ns`), and writing
  already there is read again (`recognize.ns`).  With a window, anything
  written with the mouse is read.
- **Shapes are recognised** with the Notepad set to shapes
  (`src/host/demo/shapes.ns`, `snapping.ns`; `NEWTON_TRACE_SHAPES=1`), and
  the shape verbs (`FindShape`, `MungeShape`, `PictToShape`, ...) work.
- **A selection can be made, dragged and resized** (`src/host/demo/drag.ns`),
  and a drag let go on the background becomes a clipping.
- **The Intelligent Assistant** parses a sentence and carries out its
  task (`src/host/demo/assist.ns`, `assist-tasks.ns`).
- **Modal dialogs**, over real forked tasks (`src/host/demo/modal.ns`).
- QuickDraw pictures play back (the world map), the outline list, the
  meeting and its duration bar.
- **Packages are installed by the package manager**, the ROM's own at
  boot and one from a file with `newton --package file.pkg` or dropped
  onto the window; `packages.py build/MP2x00US --extract DIR --rename
  Formulas=Formulas2` makes a loadable copy of a built-in one.  A package
  so installed is kept on the internal store, as on a MessagePad: with
  `--store file` it is activated again at every later boot.
- **The Newton's own test tools**: the journal records and plays back
  strokes, and the test agent runs a test manager on the machine
  (`src/host/demo/journal.ns`, `testagent.ns`; `docs/testing/README.md`).

## Packages: finished for what the host can reach

Done (2026-09-27 to 2026-09-29; `docs/packages/README.md`'s "Status"
table says what is left and what each piece waits on; the plan as it
was worked through is in `docs/work-log.md`): 31 of the 32 package
natives answered.  Left: the `'book` handler (the book reader),
`SuckPackageFromEndPoint` (comms), a protocol part's class info (raw
ARM), a card's `'stor` event and `GetCardReinsertionInfo` (PCMCIA),
`StopFrameSound` (the sound server), XIP packages (the ROM domain
manager's page faulting, about 11 KB).

## Now: finishing pictures

The owner asked (2026-09-29) for the pictures to be finished.  What the
machine's own pictures use was measured first
(`analysis/pictures.py build/MP2x00US`, which walks every 'picture in
the ROM and the extension's packages opcode by opcode): 30 pictures, and
between them only bitmaps (BitsRect/PackBitsRect), clip regions,
comments, the pen size and short lines - all of which `DrawPicture`
already plays.  Text, curves, paths and pixel patterns only ever appear
in pictures the machine *records* itself: `MakePict` (the ROM's one
caller is the credits' `creditPict`, `MakeText` shapes recorded into a
picture) over `OpenPicture`/`ClosePicture` and the recording branches of
every standard proc.  So the order is recording first, then what
recording produces.  Sizes are `callgraph.py` lower bounds (not done):

1. DONE (`3ef412d`) **Recording**: `OpenPicture` (788 B), `ClosePicture`, `KillPicture`,
   `PutPicOpcode`/`Byte`/`Word`/`Long`/`Rect`/`Point`/`Data`/`Rgn`,
   `PutPicVerb` (the pen, patterns and oval size written only when they
   changed), `PutPicPat`/`PutPixPat`/`PutPat1Data`/`PutPixMap`/
   `PutColorTable`, `CheckPic` (the clip region), `EqualPat`, and the
   recording branches of `StdRect`, `StdRRect`, `StdOval`, `StdArc`,
   `StdPoly`, `StdRgn`, `StdLine`, `StdBits`, `StdComment` - about 2.5 KB
   plus the branches.  Test: a picture recorded and played back to the
   same pixels.
2. DONE (`be66d0e`; the text objects' other operations and scaled
   drawing NOT YET) **Text in pictures**: playing it (`DrawPicText`, `TextCleanup`,
   `NewText`, `CallDrawText`, `DisposeText`, `InvalCachedTextInfo` - 1 KB)
   and recording it (`StdText`'s `DoPutText` 2.5 KB, `UpdateLayoutState`).
3. DONE (`64a793c`) **`MakePict`** (`FMakePict`, `CommonMakePict`,
   `SetStandAloneBoundsInViewsRecursively`) - the credits' picture made
   and drawn.
4. DONE (`3f1d0f8`) **Curves and paths**: drawn and recorded (`MapCurve`/`CallCurve`/
   `StdCurve`/`DrawCurve`/`FrCurve`/`GetCurveBounds`/`OffsetCurve`/
   `ScaleCurve`/`PutPicCurve`/`EqualCurve`; `MapPaths`/`CallPaths`/
   `StdPaths`/`DrawPaths`/`FrPaths`/`FramePath` and the path walker/
   `GetPathsBounds`/`OffsetPaths`/`ScalePaths`/`PutPicPaths`) - about 3 KB.
5. DONE (`a2f0ceb`) **Pixel patterns of type 1**: `ConvertPixPat` (340 B) and its
   converters.
6. The neighbours a picture draws through: arcs of less than a full turn
   (`Shapes.cpp` - DONE, `fd38560`) and italic (`Text.h` - DONE,
   `cdfd8c8`).
7. DONE (round 3) **`TQDScaler`** (0x00196018-0x001973c8, about 5 KB): a picture (or
   any drawing) under a transform that scales.
8. DONE (round 4: `6ed07c2`, `fb762cc`, `e622a1b`, `0734071`, `0e10570`)
   **The ROM's own blitting of pictures and text**: `StretchBits` whole
   (`SetupConversion`, the `Combine*`, 33 row stretchers, the blit modes
   under region masks - now at the port's depth, as the ROM makes them);
   text composed a style run at a time into a slab and stretched
   (`DrText`/`DrTextChunk`, with outline and shadow, the broken underline,
   gray text through `MakeGrayText` and a font spec's `color`);
   `CalcTextBounds`; `DrawShapeScaled` for bitmaps not at 72 dpi; a
   `colorData` entry chosen and its colour table made a gray table.

Pictures are finished.  Left, and recorded as NOT YET where they lie:
`TGrayShrink` (the view protocol that shrinks an anti-aliased one-bit ink
word into grays - the ordinary stretch stands in, as on a ROM with no
implementation registered), a text object's layout numbers (0x400) and
`TextArrow` (0x2000), `ZoomRect`, a `MakeBitmap` kept on a store.

## Candidates for the next piece of work

The owner's order - the package manager, host package loading, the
recognition system, the testing system, finishing packages - has been
worked through.  What could come next (not ranked; the owner chooses):

- **The sound server**: being worked (2026-09-29; plan in
  `docs/sound/README.md`).  Round 1 done: the `PSoundDriver` seam, the
  server (`'sndm`, output and decompressor channels, `FillDMABuffer`
  mixing), the host driver (`hal/host/HostSoundDriver.h`, buffer ends as
  host interrupt sources, a null capture backend).  Round 2 done: the
  client `TUSoundChannel`, `TFrameSoundChannel`, `GlobalSoundChannel` and
  the twelve `protoSoundChannel` natives - `PlaySoundSync` of the ROM click
  plays through the server (`sound.PlaySound`, `host.NewtonSound`);
  `newton` installs the driver, with a waveOut loudspeaker when windowed
  (`host/win32/HostAudio.cpp` - not yet heard: every test run is
  headless).  Round 3 done: the codec channel (coded frames heard),
  recording (a test-signal microphone; IMA recording exact), the power
  handler, `TDTMFCodec`, `StopFrameSound` - the Sound Recorder opens, and
  open-apps reports 0 failed.  Round 4 done: 16-bit samples big-endian
  in memory on every host (`sound/SampleWords.h`, swapped only at the
  host driver); the waveIn microphone; the Sound Recorder driven through
  its buttons (`demo/recorder.ns`, `host.NewtonRecorder`; `newton
  --microphone-tone HZ`).  Next (round 5): `TGSMCodec` and the GSM 06.10
  full-rate coder under it (the Toast library, 0x002a85f8-0x00347000
  with unnamed helpers) - the Sound Recorder records through it and keeps
  plain samples until then; then re-check the Recorder demo's tone share
  (GSM is lossy).  Also left: the loudspeaker and microphone heard by ear
  (windowed `newton --script src/host/demo/sound.ns`; the Recorder's Rec,
  Stop, Play); `NewWiredPtr` in `memory/` (the `NewPtr` fallback works).
- **The book reader** (`TLibrarian`, 49 methods; 19 unanswered `books`
  natives) - the Newton's books and the help book, and with it the
  `'book` part handler (the ROM's help book is refused for want of it).
- **The comms stack**: 120 unanswered natives - endpoints, CCL, AppleTalk,
  IR, NTK and the desktop connection.  The test server's link, the IR
  sniffing, `SuckPackageFromEndPoint` and fax reception (the only real
  source of the fax-page bitmaps `RotTiledBitmap` turns) wait on it.
- **The recognition gaps** listed under "Recognition" below (seven
  methods, `ValidateWord`'s questions, `FindBaseline`'s first path, the
  arbiter's graphics words).
- **Now reachable over the large binaries**: the text engine's
  `TXNewtStreamFactory` (a compressed large binary for a stream above 4K)
  and `RotTiledBitmap` (only a fax page reaches it, so it still waits on
  the comms stack).
- **The text engine**: `protoTXView` works; the clipboard, text on a
  store, the ruler bar and pages are left (below).
- **Drawing speed**: the blitter and the lines work a pixel at a time
  through region scan conversion, which is why a busy screen redraws
  slowly on the host.  A faster blitter with identical output is host
  work only, but it makes the interactive build pleasant to use.
- **The ROM-free track** (below).
- Small: the date the Assistant's "tomorrow" comes to ("schedule lunch
  with Daniel tomorrow" puts the meeting on today).

## Open, by area

### Recognition

**Complete** (2026-09-29; `docs/recognition/README.md`'s status): all 125
of its natives answered, and the code gaps the NOT YET sweep found
(`ValidateWord`'s questions, `FindBaseline`'s first path over
`low_level`, the arbiter's shape-or-word rules, `EndInkStrokeGroup`)
filled - `024ee51`, `6a59f42`.  What remains is out of the U.S. ROM's
reach, hardware, or waiting on another area:

- **The inker's side** (hardware): the waiting ink redrawn on a screen
  update (`UpdateCompressGroup`, `UpdateStrokesInList`, `UpdateStroke`
  0x001455bc-0x00145728), like `StrokeUpdate`.

- **Unreachable from this ROM**: the French/German accent checks
  (`CheckDiacriticsDirections` 0x0007c9a0, 684 B; `AnalyseDiacriticsDirection`,
  2160 B; two helpers - only a French or German letter set asks for them,
  so they answer 0, no penalty), and the sixteen-bit Airus walkers
  (`AE16_*`, `AL16_NextSet*`; no dictionary in this ROM is sixteen-bit).
- **Hardware**: the inker task (`TInker`, `InkerOff`, `TBCWakeUpInker`)
  and `CheckTabletHWCalibration`.
- **Waiting on fax reception** (the comms stack): `RotTiledBitmap` (the
  four sizes `Tilable` looks for are *fax pages*, 216-byte rows by
  1146/2292/1152/2304 - the turned copy is built tile by tile in a large
  binary; the large binaries are there now, but nothing makes such a
  page).
- **Waiting on other areas**: the journal's replayed *units*
  (`HandleReplayUnit`, `SetCaseAndTime` - the host journal replays
  strokes), `CreateVMHeap`.
- `HiliteTraced` (about 7.5 KB): selecting part of a shape by tracing
  along it, so `RemovePoints`' partial branch and command 0x44 are not
  done either - every polygon selection is the whole shape.
- The printing path's outlined paths for ink (`CSMakePathsGroup`,
  `FramePaths`), which want the PostScript path machinery.
- **How well it reads.**  Rosetta: a perfectly round synthetic "c", as
  wide as an "o", comes back with every code under 0.6%, so the readings
  of a word with one in it all tie; the path from the classifier's input
  to the readings matches the ROM instruction for instruction, so the net
  is simply particular about its c's.  ParaGraph: the synthetic "mum" and
  "nun" are read but lose the arbitration to the scrub gesture (their
  retraced stems are a zig-zag), and "mum" prefers "Mom".  A trace from an
  emulator (`BPNetEvaluate`, the xr streams) would be the reference if
  ever one is wanted.
- `GetRangeProperties`' `offset` slot (two line heights the host's line
  cache does not keep).

### Testing (`docs/testing/README.md`; 32 of 38 natives answered)

- The test server (`TCommServer`, 0x00209654-0x00209d5c; `Setup`,
  `ProcessTestServerCommand`, `DoDropConnection`'s sending): an AppleTalk
  endpoint, so it waits on the comms area.
- The C test cases (`TTestCaseTask` 0x0022afe0-0x0022b3b8,
  `StartCTestCase`, `DoNewtCTestCase`): a test case is a protocol in a
  `'tstp` part, run as a task of its own.
- The tests kept on a store (`MakeTestStore`, `TTestCommandQueue`,
  `TTestStoreFileList`, `DoRunTestsFromStore`, `StartACardTestCase`).
- The six natives still unanswered: the serial debugging
  (`InitSerialDebugging`, `PreInitSerialDebugging`); Uriah (`Uriah`,
  `UriahBinaryObjects` - `TObjectHeap::Uriah` 0x0031b154 and
  `UriahBinaryObjects` 0x0031bae0, a census of the frames heap printed to
  the REP, about 2.4 KB walking the heap's own block layout, with
  `gUriahROM`/`gUriahPrintArrays`/`gUriahSaveOutput` choosing what it
  prints); and the IR sniffing (`StartIRSniffing`/`StopIRSniffing`, 42
  functions and 4 KB of the IR stack not yet done - `callgraph.py`).
- `HobbleTablet` reaches nothing on the host (no inker port).

### Frames (`docs/frames/README.md`'s "Not yet"; every frames native bound)

The NOT YET sweep of 2026-09-29 left 20 genuine gaps (40 comments before):
- Reachable from ordinary scripts: `TNumberParser` (`StringToNumber` is
  `strtod`, not the locale's separators); a store's own sort table; the
  aggregate and pointer cases of `UnmarshalValue`.
- Reachable from developer settings or tools: tracing and breakpoints (the
  printer they need is there now - the natural next frames piece),
  `NTKStackTrace`, the task stack limits the debugger uses, the GC
  profiler's hooks.
- Not reachable, or no effect on behaviour: FastRun1 (what SlowRun already
  computes), the proto caches (speed), native code stored as ARM code (a
  host limit), `IsFirstByteOf2Byte` (not for the US ROM), `TRichString::
  Verify`'s check of the ink words.

### Natives whose machinery is there, or is one function away

- `Dispatch` (`instance:Dispatch`, 0x00195228), `RegisterGestalt` and
  `ReplaceGestalt` want `PrimCallProtocolFromFrames` - the marshalling of
  NewtonScript values into a C call.
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.
- `MakePict`.
- `HiliteBlock` 0x00164d64 looks like a view native but is the book
  reader's: it wants `TLibrarian` and the page frames.
- `natives.py --unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  `comms` and `books` are subsystems not
  reconstructed at all: a native there is a project of its own rather
  than a wrapper.

### The text engine

`protoTXView` works (2026-09-29): the engine from `TXArray` up to `TXView`
(class 108, made by `BuildView`) with the containers, the undoable edit
commands and all 39 `protoTXView` methods (`docs/text/README.md`; demo
`src/host/demo/txview.ns`, ctest `host.NewtonTXView`).  A protoTXView's
page is the view's height unless `SetGeometry` gives one - ROM
behaviour: text below it is never drawn.  NOT YET:
- the clipboard (Copy does nothing, Paste answers false, so Cut only
  deletes), dragging a selection out, the scrub and caret gestures (they
  fall through to `TView`);
- text kept on a store (`TXVBOChars`, the large-binary side of the stream
  factory; `SetStore`'s store is ignored);
- the ruler bar (`TXRulerUI`: ShowRuler, HideRuler, UpdateRulerInfo do
  nothing);
- the paginated formatters (`TXMultiFrameFormatter`, `TXPageFrames`,
  `TXPageFormatter`, 0x002413e0-0x00242a2c).

## The natives still unanswered

`python tools/newton-rom/analysis/natives.py --unbound` lists them by
area (`--csv` for a table, `--sizes build/MP2x00US` for the cheapest work
inside an area).  At 2026-09-29:

| area | how many | what is under them |
|---|---|---|
| comms | 120 | endpoints, CCL, AppleTalk (the `...Zone...` natives are AppleTalk's), IR, NTK, the desktop connection |
| frames | 95 | natives.py's catch-all: a handful each across many areas |
| books | 19 | the book reader and newspapers (`TLibrarian`) |
| sound | 8 | the sound server |
| testing, intl | 6 each | testing: the serial debugging, Uriah, the IR sniffing |
| system | 6 | |
| qd | 4 | |
| stores | 2 | store passwords |
| packages | 1 | SuckPackageFromEndPoint (comms) |
| assist | 0 | all answered |

The areas whose machinery exists are worth sweeping with `--ready`.

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
- **Several agents in parallel** (2026-09-29 on): each builds in its own
  directory (`build/host` for one, `tmp/build-<area>` for the others),
  stages its own files path by path (never `git add -A`/`commit -a`), and
  keeps the shared tree compiling between steps - one agent's broken file
  stops everyone's `newton` linking.  Only ever stop a process you
  launched yourself, by the PID kept at launch: several `newton.exe`s from
  different build trees run at once, and one looked up by image name
  belonged to another agent.  Heap damage from one area shows up as hangs
  everywhere: run booted demos under `NEWTON_HEAPCHECK` before committing.
- A host that looks hung: `tools/host/stacksample.py <pid>` (its busy
  thread's stack, no debugger needed) and `NEWTON_TRACE_UPDATE=1` (each
  region repainted) - `tools/host/README.md`.
- `test_Views` leaves the port's visible region narrowed by earlier
  tests (160x100 at one point); a test that checks update regions should
  not assume the whole screen is visible.
- A script run by `newton --script` sees a fresh store unless `--store`
  is given, so it walks the Setup assistant first
  (`src/host/demo/assist-tasks.ns` has the walk to copy).

- **A reference for the cursive reader**: PhatWare, who bought
  ParaGraph's recogniser, published a descendant of it under the GPL v3
  (https://github.com/phatware/WritePad-Handwriting-Recognition-Engine;
  `WindowsTools/NNLegacyTool/` is the oldest tree - LOW/, XRWS/, POST/).
  It is a later version and not the ROM, so it is never transcribed: it
  names things (the xr types are its `X_...` codes, `LOW/STD/XR_NAMES.H`)
  and says what a function is for, and where the ROM and the port
  disagree with it, the ROM's disassembly decides.  The FillSHR slip (a
  transcribed store order into a stack array, two slots swapped) showed up
  as exactly such a disagreement - a transcribed store order is the thing
  to check first when a stage's output looks mirrored or shifted.
