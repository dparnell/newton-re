# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is open, what the candidates for the next piece of
work are, and the working notes that keep being needed.  Keep it short:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page under `docs/`, and take it out of
here - this file says what is *not* done.  The history of how things got
here, the plans of finished work and the host and ROM bugs found along
the way are all in `docs/work-log.md`.

## State at 2026-09-28

- `cmake --build build/host` clean, `ctest --test-dir build/host` 113/113
  (`intl.Dates` fails about one run in ten: it reads the real clock).
- `analysis/coverage.py build/MP2x00US --check`: 12020 citations, 0 bad;
  6864 of 16671 functions (41.17%) - the digit reader's statics are
  unnamed, so they add citations and not functions.
- `analysis/natives.py --unbound`: 318 of the ROM's 1326 natives are
  unanswered (table below).

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
  Formulas=Formulas2` makes a loadable copy of a built-in one.
- **The Newton's own test tools**: the journal records and plays back
  strokes, and the test agent runs a test manager on the machine
  (`src/host/demo/journal.ns`, `testagent.ns`; `docs/testing/README.md`).

## Candidates for the next piece of work

The owner's order - the package manager, host package loading, the
recognition system, the testing system - has been worked through.  What
could come next (not ranked; the owner chooses):

- **The sound server**: `TSoundServer`/`TSoundChannel`, the codec and DMA
  channels, and a host audio driver behind `hal/` - the Sound Recorder
  (the one built-in application that does not open: `FSoundOpen`), the pen
  clicks, alarms and button sounds.  The codecs are done
  (`docs/sound/README.md`).
- **Packages, what is left** (`docs/packages/README.md`):
  - **Units**: five ROM parts carry `_ExportTable`s (two `_ImportTable`s),
    installed without them (a stderr line at boot); a third-party package
    importing a ROM unit needs `InstallExportTables`/`InstallImportTable`
    0x000cfcd4-0x000d0758 and the unit natives.
  - The `'book` part handler (the help book is refused for want of it)
    over the book reader (`TLibrarian`, 20 natives), then `'dict` and
    `'comm`.
  - Streamed sources (`TPackageLoader`, `CPartPipe`, `TPipeApp`) and
    packages on a store (the ROM domain manager, large binaries), which
    the remaining package natives (`ActivatePackage`, `ObjectPkgRef`, ...)
    stand on.
- **The comms stack**: 121 unanswered natives - endpoints, CCL, AppleTalk,
  IR, NTK and the desktop connection.  The test server's link and the IR
  sniffing (below) wait on it.
- **The text engine**: `TXRun` and `TXRunRange`, then `TXRulerRange`
  (below).
- **Drawing speed**: the blitter and the lines work a pixel at a time
  through region scan conversion, which is why a busy screen redraws
  slowly on the host.  A faster blitter with identical output is host
  work only, but it makes the interactive build pleasant to use.
- **The rest of pictures**: text, curves and paths inside a picture
  (`docs/qd/README.md`; `PictToShape` makes a picture's text into text
  boxes but `DrawPicture` does not draw it).
- **The ROM-free track** (below).
- Small: the date the Assistant's "tomorrow" comes to ("schedule lunch
  with Daniel tomorrow" puts the meeting on today).

## Open, by area

### Recognition

The recognition system is finished for everything the ROM's own fields
reach (`docs/recognition/README.md`; all 116 of its natives answered).
Still open:

- **Being finished now** (2026-09-28): `TEditView::TrackDistort`
  0x000a9634 (a selected shape's corner dragged to distort it; it waits
  on the polygon hilites, `TPolygonView`'s `MakeHilite`, so no view
  answers `ClickOptions` bit 2 yet), `RotTiledBitmap` (a screen-sized
  bitmap turned in tiles out of a large binary on a store, over `TTile`)
  and the French/German accent checks (`CheckDiacriticsDirections`
  0x0007c9a0, `AnalyseDiacriticsDirection`; only a French or German
  letter set reaches them).
- The sixteen-bit Airus walkers (no dictionary in this ROM is sixteen-bit;
  `docs/recognition/README.md`, "What is left of the engine").
- The printing path's outlined paths for ink (`CSMakePathsGroup`,
  `FramePaths`), which want the PostScript path machinery, and
  `TWRecognizer::EndInkStrokeGroup` (the CIC library's
  `WRecEndInkStrokeGroup`).
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

### Natives whose machinery is there, or is one function away

- `Dispatch` (`instance:Dispatch`, 0x00195228), `RegisterGestalt` and
  `ReplaceGestalt` want `PrimCallProtocolFromFrames` - the marshalling of
  NewtonScript values into a C call.
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.
- `MakePict`.  `GetBitmapInfo` also wants `GetBinaryStore`/
  `GetBinaryCompander`, which answer nil on a host because there are
  never large binaries.
- `HiliteBlock` 0x00164d64 looks like a view native but is the book
  reader's: it wants `TLibrarian` and the page frames.
- `natives.py --unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  `comms` and `books` are subsystems not
  reconstructed at all: a native there is a project of its own rather
  than a wrapper.

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

## The natives still unanswered

`python tools/newton-rom/analysis/natives.py --unbound` lists them by
area (`--csv` for a table, `--sizes build/MP2x00US` for the cheapest work
inside an area).  At 2026-09-28:

| area | how many | what is under them |
|---|---|---|
| comms | 121 | endpoints, CCL, AppleTalk (the `...Zone...` natives are AppleTalk's), IR, NTK, the desktop connection |
| frames | 115 | natives.py's catch-all: a handful each across many areas |
| packages | 26 | units, packages on a store (the ROM domain manager, large binaries), 1.x packages |
| books | 20 | the book reader and newspapers (`TLibrarian`) |
| views | 17 | |
| sound | 9 | the sound server |
| system | 8 | |
| qd, intl | 7 each | |
| testing | 6 | the serial debugging, Uriah, the IR sniffing |
| stores | 5 | large binaries on a store, store passwords |
| recognition | 0 | all answered |

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
