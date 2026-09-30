# Where to pick up

A standing note for whoever (or whatever) comes back to this next: where
the tree stands, what is open, what the candidates for the next piece of
work are, and the working notes that keep being needed.  Keep it short:
when a piece of work is finished, record it in `docs/work-log.md` (newest
first) and in its subsystem's page under `docs/`, and take it out of
here - this file says what is *not* done.  The history of how things got
here, the plans of finished work and the host and ROM bugs found along
the way are all in `docs/work-log.md`.

## State at 2026-10-01

- The full `ctest`: 319 of 319 (several agents work in parallel, each
  building in its own directory under `tmp/`).
- The OS boots from the reconstructed data by default
  (`<build>/romsrc-objects.bin`, from the committed `romsrc/`); `--rom` is
  only for cross-checks, and nothing reads the ROM image at run time.
  A configure with no ROM image builds, boots and runs every test that
  does not compare against the ROM.
- `analysis/coverage.py build/MP2x00US --check`: 18118 citations, 0 bad;
  12199 of 16671 functions (73%).
- `analysis/natives.py --unbound`: 1280 of 1326 natives answered (96.5%).
  Left: comms 40 (AppleTalk and NBP, the online services and eWorld's
  `EW*`, the TV remote), intl 4 (the AppleTalk zones), testing 2 (IR
  sniffing).
- `analysis/notyet.py` lists the NOT YET markers that name something
  already defined; the sweeps of 2026-10-01 left only genuine gaps
  (below) and performance paths.
- **Linux** builds and runs with the system compiler
  (`-DCMAKE_CXX_COMPILER=clang++`); `docs/host-lp64.md`.  Still
  Windows-only: a package dropped onto the window (XDND), and
  `tools/host/stacksample.py`, `profile.py`, `whichfunction.py`.  macOS
  has no window or sound implementation yet (it would run headless).

## In progress

- **The fixture applications used for what they are for**
  (`demo/apps-*.ns`, ctests `host.NewtonApp*`): left - Newt's Cape's
  other helpers (audio into the In Box, PalmDoc, MOD, the encodings,
  PkgInfo, RoutBook, NewtPack) and Register.

## Open

### Left by decision or out of reach

- **The NIE's link modules** (Ethernet, LocalTalk, Modem & Serial PPP/SLIP)
  are ARM protocol parts with no host stand-in; by the owner's decision
  the host's own network stands in (the Host network setup,
  `comms/host/HostLink.ns`).  The Lantern card handler is re-expressed;
  its driver world (`TLanternEventWorld`) is NOT YET.
- **The math views** (classes 84-86): nothing in the U.S. ROM, its
  extension or the fixtures makes one, and the math recogniser is not in
  this ROM.
- **Recognition**: the French/German accent checks and the sixteen-bit
  Airus walkers (nothing in the U.S. ROM asks for them); the journal's
  replayed *units* (`HandleReplayUnit`, `SetCaseAndTime` - the host
  journal replays strokes); the armistice samples (a debugger feed).
- **Hardware**: `TResistiveTablet`, `CheckTabletHWCalibration`, the
  interconnect pin's `TICHandler`, the Cirrus battery driver and the
  platform's power side, `SCCPowerInit`, `TFlashAMD`, RAM stores
  (`TStoreDriver`).
- The applications' own faults, which a MessagePad has too: Daleks tapped
  before its set-up, NetSched with no URLs, the Calls slip's delayed
  action on a closed view, Newt's Cape 2.0 refused over 1.6, RPNcalc's -0
  for negative trig results, Note2Net's hand-typed URL; Copperfield opened
  without a book.

### Comms

- AppleTalk: NBP, ADSP (with the NTK's ADSP connection and
  `TEzEndpointPipe`'s), the zones; the online services and eWorld (`EW*`);
  the TV remote; IR sniffing (`StartIRSniffing`/`StopIRSniffing`, 42
  functions); the Hammer translators; `RegisterNetworkROMProtocols`, P3,
  LocalTalk, Keyboard, VRemote and `PMuxServiceStarter` in the comm
  manager's list.
- The dock: 'rpat' and `BackupPatches` (a system patch, which the host
  cannot install), V.42bis's internal-buffer mode, tests for 'islp' and
  'gpwd', and a real desktop (NCX, UnixNPI) over localhost:3679 - never
  tried.
- armcpu: frames in a code binary; protocol parts through the CPU (every
  protocol part among the fixtures is the NIE's); a partly re-expressed
  package (NativeEntry's fast path goes straight into the binary); a
  RefVar handle a native keeps past its call.

### Testing (`docs/testing/README.md`)

- The test server (`TCommServer`, an AppleTalk endpoint), the C test
  cases (`TTestCaseTask`, a `'tstp` part), the tests kept on a store
  (`MakeTestStore` and its kin), the serial debugging natives.

### Frames and the rest

- Reachable from developer settings or tools: tracing and breakpoints,
  `NTKStackTrace`, the task stack limits, the GC profiler's hooks.
- `instance:Dispatch`, `RegisterGestalt`, `ReplaceGestalt` want
  `PrimCallProtocolFromFrames` (NewtonScript values marshalled into a C
  call); a host protocol's methods need numbered thunks.
- A paragraph's lines are laid out the host's way, not by the ROM's
  `LineLoop` (0x0010d8d4-0x0010ecd4 and on) with a text object per run: what
  `ComputeLineBounds` moves a line by is therefore always nought here, and
  so is `GetRangeProperties`' `offset` slot, which is made of it.
- Packages: XIP packages (the ROM domain manager's page faulting, about
  11 KB); a card's `'stor` event and `GetCardReinsertionInfo`; card
  packages in attribute memory (`TCardPipe`); ATA cards.
- The NIE built into the ROM extension: the page tables `ptpt`/`glpt`
  still name the patch table's old physical page (0x7ee000); 'fimp is not
  generated.
- The system alerts: the card position alert's trigger, the fault
  monitor's route into `ReinsertCard`, the screen semaphores.
- Flash stores past 128 MB (the migrated-entry cap: slow lookups, or
  256 KB erase regions - the owner's call).  A round trip with a real
  Einstein build (an image it wrote, one of ours opened in it).
- How well the recognisers read synthetic writing: a perfectly round "c"
  ties every reading in Rosetta; ParaGraph's synthetic "mum" and "nun"
  lose the arbitration to the scrub.  An emulator trace (`BPNetEvaluate`,
  the xr streams) would be the reference if one is ever wanted.
- Sound heard by ear (windowed `newton --script src/host/demo/sound.ns`,
  then the Recorder); GSM against the standard 06.10 test sequences.
- A package with a `'book` part in `fixtures/` (Copperfield has only read
  the help book).
- Comms demos that still sleep fixed times (echo, dns, stream, inet,
  inetfsm).

### The ROM-free track, optional later

The Unicode, collation and locale tables and the recognisers'
dictionaries as text (word lists plus a trie builder); a strike's derived
metrics recomputed from its glyphs.  An oracle for the builder:
**mosrun** (https://github.com/MatthiasWM/mosrun) runs Apple's MPW-based
Newton tools (ARM6asm, ARMLink, Rex) on the host, so a ROM extension made
by Apple's Rex builder could be compared with ours byte for byte; Kelvin
Sherlock's **mpw** (https://github.com/ksherlock/mpw) is a second opinion.
Either must be vendored or fetched by a documented script if used.

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
- **Host tests wait on conditions**: a demo loads `common.ns`
  (`HostInclude`), polls with `waitFor` and ends with `HostQuit()`;
  never a fixed wait for something asynchronous.  `tools/host/stress.py`
  reproduces a timing race.  A test comparing two readings of the clock
  pins it (`SetRealClockSeconds`).  Run `analysis/romsizes.py` (and
  `--lp64`) after reconstructing message or reply code.
- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`. This matters most in functions that
  build `Rect`s and `Point`s on the stack.
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
- **Several agents in parallel**: each builds in its own directory
  (`build/host` for one, `tmp/build-<area>` for the others), stages its
  own files path by path (never `git add -A`/`commit -a`), and keeps the
  shared tree compiling between steps - one agent's broken file stops
  everyone's `newton` linking.  Only ever stop a process you launched
  yourself, by the PID kept at launch.  Heap damage from one area shows
  up as hangs everywhere: run booted demos under `NEWTON_HEAPCHECK`
  before committing.
- A host that looks hung: `tools/host/stacksample.py <pid>` (its busy
  thread's stack, no debugger needed) and `NEWTON_TRACE_UPDATE=1` (each
  region repainted) - `tools/host/README.md`.
- `test_Views` leaves the port's visible region narrowed by earlier
  tests (160x100 at one point); a test that checks update regions should
  not assume the whole screen is visible.
- A script run by `newton --script` sees a fresh store unless `--store`
  is given, so it walks the Setup assistant first (`walkSetup` in
  `common.ns`).  Pen demos: a tap within half a second of the pen coming
  up after writing is more writing, and a press within 60 ticks of the
  previous click is a tap-drag's second half - wait on `Ticks()`.
- **A reference for the cursive reader**: PhatWare, who bought
  ParaGraph's recogniser, published a descendant of it under the GPL v3
  (https://github.com/phatware/WritePad-Handwriting-Recognition-Engine;
  `WindowsTools/NNLegacyTool/` is the oldest tree - LOW/, XRWS/, POST/).
  It is a later version and not the ROM, so it is never transcribed: it
  names things (the xr types are its `X_...` codes, `LOW/STD/XR_NAMES.H`)
  and says what a function is for, and where the ROM and the port
  disagree with it, the ROM's disassembly decides.  A transcribed store
  order is the thing to check first when a stage's output looks mirrored
  or shifted.
