# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is open, what the candidates for the next piece of
work are, and the working notes that keep being needed.  Keep it short:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page under `docs/`, and take it out of
here - this file says what is *not* done.  The history of how things got
here, the plans of finished work and the host and ROM bugs found along
the way are all in `docs/work-log.md`.

## State at 2026-09-30

- A full `ctest` in a parallel agent's build: 209 of 209 (`intl.Dates`
  fails about one run in ten: it reads the real clock).  Several agents
  work in parallel, each building in its own directory under `tmp/`.
- **Linux**: the tree builds and runs there too, with the system compiler
  (`-DCMAKE_CXX_COMPILER=clang++`, not the zig toolchain - its linker
  cannot take the system's X11 and ALSA shared objects), and `newton`
  shows the booted machine in an X11 window.  `ctest` there: 233 of 233
  (five of them want `build/<ROM>/symbols.json`, so run `dump_symbols.py`
  on a fresh checkout).  `docs/host-lp64.md` is the standing note on what
  a 64-bit `long` changes and how such a value is to be spelt.  Still
  Windows-only: a package dropped onto the window (XDND is NOT YET), and
  the crash-time tools `tools/host/stacksample.py`, `profile.py` and
  `whichfunction.py`, which read PE images and Windows debug APIs.
  macOS has neither window nor sound implementation yet, so it would run
  headless.
- `analysis/coverage.py build/MP2x00US --check`: 16380 citations, 0 bad;
  10805 of 16671 functions (64.81%).
- `analysis/natives.py --unbound`: only comms' are left (comms 102 of
  147, the AppleTalk `*Zone*` four and IR sniffing).  `instance:Dispatch` works only on a
  monitor protocol (a host protocol's methods need numbered thunks, NOT
  YET).
- **Stores**: the internal store is the ROM's own flash format in a host
  file (`newton --store`, `stores/flash/`).  **Memory cards mount**: a
  card is a host file (`newton --card`), formatted through the ROM's own
  dialogs, mounted and unmounted as it goes in and out (ctest
  `host.NewtonCard`); a card with Einstein's default CIS mounts
  (`host.NewtonCardEinstein`).  Left: a round trip with a real Einstein
  build (an image it wrote, one of ours opened in it), card packages in
  attribute memory (`TCardPipe`), ATA cards.
- **System alerts**: done (`src/alert/`, `docs/alert/README.md`; the card
  reinsert alert, `host.NewtonCardAlert`).  NOT YET: the card position
  alert's trigger, the fault-monitor route into `ReinsertCard`, the
  screen semaphores.

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
natives answered (the `'book` handler came with the book reader).  Left:
`SuckPackageFromEndPoint` (comms), a protocol part's class info (raw
ARM), a card's `'stor` event and `GetCardReinsertionInfo` (PCMCIA),
XIP packages (the ROM domain manager's page faulting, about 11 KB).

**Third-party packages** (`fixtures/packages/`, ctest
`host.NewtonThirdPartyPackages` with a restart half): every fixture
installs - apps and Internet Setup as `FormEntry`, NHSounds and the NIE
modules as `AutoEntry`, fonts and ISP Templates as `'????Entry` (the ROM's
own `HandleNewPackage` does the same) - except MDaleks1, which the ROM
refuses as a second "Daleks:Avarice".  Removal (the drawer's delete,
`SafeRemovePackage`, `DeActivatePackage` + `RemovePackage`) is clean and
survives a restart.  RPNcalc computes; Daleks, NewtHack, Register and
Internet Setup open.  Left: Mahjongg carries a compiled native (ARM)
function the host cannot run; the NIE's protocol parts (their table is
in `docs/packages/README.md`) get host implementations from the comms
work (the owner's decision: the host's own TCP/IP stack, not the NIE's);
the card server (`TCardServer`) - a `'cdhl` part is registered with no
sockets to serve.

## Pictures: finished

Done (2026-09-29; `docs/qd/README.md`; the plan as it was worked through is
in `docs/work-log.md`): recording pictures (`OpenPicture`/`ClosePicture`
and every standard proc's recording branch), text, curves, paths and type 1
pixel patterns played and recorded, `MakePict` and the credits picture,
`TQDScaler` with scaled text, and the ROM's own blitting of pictures and
text (`StretchBits` whole, text composed a style run at a time into a slab
and stretched, `CalcTextBounds`, `DrawShapeScaled`).  Left, recorded as
NOT YET where they lie: `TGrayShrink` (the view protocol that shrinks an
anti-aliased ink word into grays - the ordinary stretch stands in, as on a
ROM with none registered), a text object's layout numbers (0x400) and
`TextArrow` (0x2000), `ZoomRect`, a `MakeBitmap` kept on a store.

**Host tests wait on conditions**: a demo script loads
`src/host/demo/common.ns` (`HostInclude`), polls for what it needs
(`waitFor`) and ends with `HostQuit()`; `tools/host/stress.py`
reproduces a timing race.  Still sleeping fixed times: the comms demos
(echo, dns, stream, inet, inetfsm); `comms.MNPLongHeaders` failed once
under `stress.py --suite --hogs 16`.  Host layout: run
`analysis/romsizes.py` (and `--lp64`) after reconstructing message or
reply code (the whole tree is clean, 60335af);
write new ones the same way, never with a fixed wait for something
asynchronous - under a parallel ctest the packages `--package` queues
and the NIE's procrastinated setup arrive late.

## Candidates for the next piece of work

The owner's order - the package manager, host package loading, the
recognition system, the testing system, finishing packages - has been
worked through.  What could come next (not ranked; the owner chooses):

- **Sound: finished** (2026-09-29; `docs/sound/README.md`): the server,
  the client and frame channels, the `protoSoundChannel` natives, the codec
  channel (IMA, mu-law, GSM 06.10, the DTMF synthesiser), recording, the
  power handler; the host driver with a waveOut loudspeaker and a waveIn
  microphone when windowed.  The Sound Recorder records and plays through
  GSM (`host.NewtonRecorder`).  Left: the loudspeaker and microphone heard
  by ear (windowed `newton --script src/host/demo/sound.ns`, then the
  Recorder's Rec, Stop, Play); GSM checked bit for bit against the
  standard 06.10 test sequences, if they can be brought in;
  `NewWiredPtr` in `memory/` (the `NewPtr` fallback works).
- **The book reader**: its C++ side is complete (2026-09-29;
  `docs/books/README.md`).  A package with a `'book` part is wanted in
  `fixtures/` to test the
  part handler with a real book (Copperfield has only read the help book
  under another ISBN).  The rest of the reader is its NewtonScript side,
  which runs as it is.
- **The comms stack**: being worked (`docs/comms/README.md`; the rounds
  so far are in `docs/work-log.md`).  Networking goes to the host's own
  TCP/IP stack - the owner's decision; no TCP/IP stack is written or
  emulated.  Done: the comm tool and manager over `hal/host/HostSockets.h`,
  the endpoint, protoBasicEndpoint and protoStreamingEndpoint (ctests
  `host.NewtonEcho`, `host.NewtonStream`), all ten translators, the NIE's
  `inet` and `dnst` services as host services (`host.NewtonDNS`), and the
  NIE's protoFSM engine - its 19 native-compiled functions re-expressed
  as host code in `src/thirdparty/nie/` (`thirdparty.NIEProtoFSM`).
  Package native code: the owner's decision is host re-expressions for
  the NIE and the ARM interpreter (`src/armcpu/`) as the fallback for
  other packages, both behind `frames/PackageNatives.h`.
  **The NIE works end to end on the host through its own API**:
  `InetGrabLink`, `DNSGetAddressFromName`, a TCP echo, release and
  disconnect (ctest `host.NewtonInet`), over the host's own link
  (`comms/host/HostLink.ns`, embedded; the `ictl`, `dnst` and `inet`
  services).  Internet Setup lists, opens and offers the Host network
  (ctest `host.NewtonInetSetup`; its pages are Ethernet's less the card
  picker - the Configuration picker and the IP page are still shown and
  ignored), and the NIE's protoEndpointFSM runs as a TCP client over it
  (`host.NewtonInetFSM`).  Waiting on NIE client packages (mail, web)
  for `fixtures/`; the modem navigator.  **The desktop connection**
  (being done): 2.1 has no TCP dock, so the plan (`docs/comms/README.md`,
  "The desktop connection (Dock) - the plan") is the ROM's own serial
  dock - `TDocker` and the `FConn*` natives, `TMNP`/`TMNPService`,
  `TSerTool`/`TAsyncSerTool`, the `TSerialChip` registry - over a host
  `TSerialChip` whose wire is a TCP socket (port 3679, as Einstein), so
  NCX or UnixNPI connect to localhost as to an emulator; about 80 KB of
  ROM, layer by layer, ending in ctest `host.NewtonDock`.  Layer 1 done
  (the serial chip seam and registry, the host's TCP serial port -
  `hal.HostSerialChip`), layer 2 done (the serial tools and 'aser,
  `comms.SerialTool`), layer 3 done (MNP with class 5 compression,
  `tools/dock/mnp.py` the desktop end; `comms.MNP`,
  `comms.MNPLongHeaders`, `comms.MNPClass5`; V.42bis's coder NOT YET,
  since done).  Layer 4 part 1 done:
  `TDocker`'s package-loading path, the protocol extensions, 13 `Conn*`
  natives, `newton --serial-port` (default 3679) starting the serial port
  and services at boot (`host.NewtonDocker`); a package loads end to end
  through the Connection app's autodock (`host.NewtonDock` over
  `tools/dock/dock.py`); a docking session's handshake and password
  exchange are in and load packages (`host.NewtonDockSession`); the store
  and soup commands (9bd3a3a) and the cursor and entry commands
  (8a2af65) are in, soups are made, sent and backed up, and 'gpin' lists
  the packages (cc1b30a, 1235149); all of `ProcessCommand` is in except
  the system patches ('gpat'/'rpat') (a0966b4), with `ConvertEntry`,
  `IsDuplicateEntry` (untested: autodock never asks for a selective
  restore) and the app's read/write natives; a desktop's slip is shown
  and answered headless; the keyboard passthrough and 'gpat' are in.  The
  docker answers every desktop command but 'rpat' (installing a system
  patch, which the host cannot do); V.42bis is in.  Left: 'rpat' and
  `BackupPatches`, V.42bis's internal-buffer mode, tests for 'islp' and
  'gpwd', and a real desktop (NCX, UnixNPI) over localhost:3679 - not
  yet tried.  **Beaming** (being done; `docs/comms/README.md`, "Beaming - the
  plan"): layers 1-3 done - a Note beams from one host to another over
  Sharp IR (`host.NewtonBeam`, `tools/host/twonewtons.py`); the probe
  'pkir' (layer 4) answers IrDA between two 2.1s, and the IrDA stack
  ('irda', `comms/irda/`, ctest `comms.IrDA`) is in: **beaming is done**,
  over Sharp IR (`host.NewtonBeam`) and the default path, probe then IrDA
  (`host.NewtonBeamIrDA`).  **The NTK inspector** connects over the host
  serial port (`comms/NTK.h`, `tools/ntk/inspector.py`, ctest
  `host.NewtonNTK`); protoEndpoint, the 1.x endpoint, too
  (`comms/ScriptEndpoint.h`, `host.NewtonProtoEndpoint`); **the modem**
  dials and answers through `tools/modem/fakemodem.py`
  (`host.NewtonModemDial`, `host.NewtonModemAnswer`); and the comm
  trace frame's natives and translate (`comms/CommTrace.cpp`).  Left in
  comms: Class 2 fax (being done; a fax is sent end to end -
  `host.NewtonFaxSend` - and received,
  shown and turned - `host.NewtonFaxReceive`),
  AppleTalk/NBP
  and ADSP (with the NTK's ADSP connection), the Hammer translators,
  eWorld (EW*), the TV remote.
  The livelock between `TPMIterator::Init`'s semaphore and `TForkWorld`'s
  mutex was the host runtime's, fixed (12e55a2; ctest
  `host.NewtonDockGetPackages`).  `test_NIEProtoFSM` also runs
  each check on the package's own ARM code through armcpu, and the two
  agree (2ae6d73).  armcpu left: frames in a code binary; protocol parts
  through the CPU - no fixture needs them yet (every protocol part among
  the fixtures is the NIE's); and a *partly* re-expressed package - under
  the CPU one native calling another in its own binary goes straight into
  the binary's code (NativeEntry's fast path) and never reaches a
  re-expression.  The rest of comms (CCL, AppleTalk, IR, NTK, the desktop
  connection - which the test server's link, the IR sniffing,
  `SuckPackageFromEndPoint` and fax reception wait on) comes after.
- **Third-party apps**: all five fixture applications open from Extras
  and respond (ctest `host.NewtonThirdPartyApps`).
- **Printing to the host**: done - "Host printer (PNG files)" in the
  Print slip, pages to `newton --print-dir` (`host.NewtonHostPrinter`).
  Seen on the way: a card made with `cardfile:AddCard` shows no name (on
  screen or printed; its name view's shapeArray is empty -
  `AddAllInfoItem`); a note made with `paperroll:MakeTextNote(text,
  true)` is not drawn on the Notepad's screen, though it prints.
- **Now reachable over the large binaries**: the text engine's
  `TXNewtStreamFactory` (a compressed large binary for a stream above
  4K).
- **Text engine: finished** (2026-09-29; below).
- **Drawing speed**: done for the blitter and the display (2026-09-30;
  `docs/work-log.md`).  What remains is the ROM's own animation pacing and
  the unoptimised default build - `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
  roughly halves processor time again for interactive use; `VisibleRow`
  and `StretchBits`/text are the next hot spots if wanted.
- **The ROM-free track** (below): step 1, the decompiler, done; step 2 (the object area as editable source, rebuilt byte-identical) done; **the OS boots with no ROM image** to the same screen as with one, from the committed, editable `romsrc/`.
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
- `natives.py --unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  `comms` and `books` are subsystems not
  reconstructed at all: a native there is a project of its own rather
  than a wrapper.

### The text engine

Finished (2026-09-29; `docs/text/README.md`): every TX/Textension function
in the ROM is cited and all 39 `protoTXView` methods are bound, pages and
page breaks included (`txview.ns`, `txpages.ns`; ctests
`host.NewtonTXView`, `host.NewtonTXPages`).  Nothing in the ROM itself
uses protoTXView (`analysis/protousers.py`), so the demos and host tests
are the check.  ROM bug kept and visible: with three or more pages in
view, the edit note (room for two) overflows into `gTXParagCtrlChars` and
the pages after the second do not redraw properly after an edit.  Left
open nearby: `StrokeCentral::UpdateCompressGroup`, the last NOT YET in
`TRootView::PostDraw`, and the inker task itself (`TInker`/`TLiveInker`),
which would retire the host's inking in `StrokeTime`.

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

1. A NewtonScript **decompiler** whose output compiles back to the same
   bytes - **done**: `analysis/nsdecompile.py` round-trips all 5507 of
   the ROM's functions (`docs/frames/decompiler.md`; ctest
   `host.NSDecompileRoundTrip` requires 100%).
2. **Resource extraction**: bitmaps to images, sounds to sound files,
   fonts, strings, locale bundles, the object graph that ties them
   together, as files a person can edit (`docs/rom-free/README.md`).
   Done bar the details: `analysis/romsrc.py` extracts the object area
   as source - functions as decompiled NewtonScript (compiled back by the
   host with no ROM image), bitmaps as PNG, simple sounds as WAV,
   pictures as PICT, fonts as .ttf - and builds it back byte-identical
   (ctest `host.ROMSourceRoundTrip`); no bytecode is left in the tree.
   Left: the IMA sounds and the tables opaque; files cut by address
   rather than grouped by what they belong to.  The tree is generated,
   not committed, until it is worth editing.
3. A **builder** that makes the object area (and the packages) from the
   sources and resources, in the form `frames/ROMImport.cpp` reads today.
4. Booting from that output with no `--rom`, the generated tables that
   already live in `src/` (romtable.py, romconstants.py, nsgrammar.py,
   ...) supplying the rest - **the OS boots with no ROM image** to the
   same Setup screen as the `--rom` boot, pixel for pixel (`newton
   --objects <file>`; ctests `host.NewtonNoROM`,
   `host.NewtonNoROMSameScreen`); the ROM extension's ten packages are in
   the tree, their frames parts as source (`rex/<Package>/`); an edit
   that moves objects still boots to the same screen (`build
   --relayout`; ctests `host.ROMSourceEdit`,
   `host.NewtonEditedSameScreen`, `host.ROMSourceEditValue`) - new frames
   get maps and symbols, the extension's parts relay out.  **The tree is
   committed as `romsrc/` and is the source** (the owner's decision,
   2026-09-30; `romsrc/README.md`); the extractor is not run over it
   again, and `host.ROMSourceCommitted` (does it still build the ROM
   byte for byte?) is to be retired at its first intentional edit.
   The fonts are editable too: a BDF file per bitmap strike and a text
   file per table (`tools/fonts/README.md`); possible follow-ups: a
   strike's derived metrics (widthMax, the bearings) written as `auto`
   and recomputed from its glyphs, and a test of adding a strike.
   Complete for the object area and the extension.  Optional later: the
   Unicode, collation and locale tables and the recognisers' dictionaries
   as text (word lists plus a trie builder); ROM code for packages with
   native ARM code.

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

- **Worktrees**: never put a junction or symlink to a shared build
  directory (e.g. `build/MP2x00US`) inside a git worktree - `git worktree
  remove --force` deletes through it; copy what the tests need instead.
- **Committing beside other agents**: commit with `git commit -m ... --
  <paths>`, which takes only those paths, so nothing another agent has
  staged is swept in.

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
