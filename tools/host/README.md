# tools/host

Tools for working with the host build of the reconstruction (`build/host`),
as opposed to the ROM itself (`tools/newton-rom/`).

## whichfunction.py - name the function a crash address is in

When the host falls over, `src/host/newton.cpp`'s crash handler prints the
faulting address twice: as it stood in memory, and as an offset into the
executable's own image.

    [host] the machine fell over: an exception (0xc0000005) at 00007FF65A985265 (image + 0x175265)

The first is useless on its own - Windows loads the image wherever it likes,
so it changes from run to run - but the second is fixed for a given build.
This tool turns it into a function name:

    python tools/host/whichfunction.py build/host/host/newton.exe 0x175265

    the function runs 0x174ea0-0x1752de (1086 bytes); the fault is 0x3c5 into it
    _ZN19TRecognitionManager4IdleEv
        build\host\recognition\CMakeFiles\recognition.dir\Recognizer.cpp.obj

**Inputs:** the executable that crashed, and the image offset from its crash
line. `--objs DIR` says where the object files are (default: the `build`
directory beside the executable, searched recursively for `*.obj`).

**Output:** the bounds of the function the offset falls in, how far into it
the fault is, the mangled name, and the object file it came from. Nothing is
written; it exits non-zero when no name can be found.

**On Linux** the executable is ELF and keeps its own symbol table, so the
name comes straight from `.symtab` (demangled through `c++filt` when there
is one) - no object files needed:

    [host] the machine fell over: a signal (0xb) at 0x5585d4ab2117 (image + 0x60b117)
    python3 tools/host/whichfunction.py build/host/host/newton 0x60b117

A host that falls over on Linux prints the faulting instruction out of the
signal's context and the C stack (glibc's `backtrace()` through the signal
frame) as image offsets, as on Windows; so do `NEWTON_TRACE_EXCEPTIONS=2`
and `NEWTON_HEAPCHECK` (`src/host/HostCStack.h`).

**How it works.** The linked executable has no symbol table, and the PDB is
not worth parsing here, so the name comes from the object files:

  * `.pdata` - the table Windows unwinds through - gives the bounds of the
    function containing the offset;
  * the object files still have their COFF symbol tables, so every function
    in them has a name and a place;
  * a function's bytes in the executable are its bytes in the object file,
    except where the linker patched a relocation - and each object records
    where its own relocation sites are.

So it takes the function's bytes out of the executable and looks for an
object-file symbol whose bytes match everywhere the object has no relocation.
A whole function matching to the byte is not a coincidence.

It needs nothing but the Python standard library, and works on any of the
host executables (`newton.exe`, `newtonscript.exe`, one of the tests). It is
Windows/COFF only, which is what the crash handler it serves is.

## stacksample.py - what a locked-up host is doing

(On Linux, see "On Linux: the process samples itself" below.)

When a host program stops answering while one of its threads eats a whole
CPU, it is looping somewhere.  With no debugger installed this looks inside
it without stopping it:

    python tools/host/stacksample.py <pid> [--thread TID] [--samples N] [--depth BYTES]
    python tools/host/stacksample.py 73092 --samples 6

It suspends the thread (the busiest one unless `--thread` names another),
reads its registers and the top of its stack, lets it go again, and names
what it found with `whichfunction.py`'s machinery: the instruction pointer,
and every word on the stack that points into the executable's code just
after a call instruction - the return addresses, outermost last.  It is a
scan rather than an unwind, so a stale return address can turn up; the
functions that recur from sample to sample are the real frames.

**Inputs:** the process id (`Get-Process newton`); the executable must be
the build the process is running, so do not rebuild before looking (a
running `newton.exe` cannot be relinked anyway).  **Output:** per sample,
image offsets with function names.  Nothing is written and the process
carries on.  Windows only; standard library only (ctypes).

It is how a "hang" in the Time Zones application was told apart from a
stuck loop: every sample was inside `TNotebook::Idle` -> `TRootView::Update`
repainting, and `NEWTON_TRACE_UPDATE=1` (below) then showed which view.

## NEWTON_TRACE_UPDATE

Set in the environment of a host program, it prints every update region
`TRootView::Update` repaints (its bounding box and the tick), which is how
to tell a machine that is busy repainting the same thing over and over from
one that is stuck:

    NEWTON_TRACE_UPDATE=1 build/host/host/newton ... 2> updates.log
    grep "\[update\]" updates.log | cut -d' ' -f2 | sort | uniq -c | sort -rn | head

## NEWTON_TRACE_ARBITER

Set in the environment of `newton`, it prints each arbitration the
recognition arbiter makes (`src/recognition/Arbiter.cpp`): the area's
recognition case, every unit gathered over the same strokes with its best
interpretation's score (nought is best) and the units that won:

    [arbiter] case 1: gathered 'XRWR'/330 'SCRB'/0; won 'SCRB'/0

That is how to tell why writing that was read still went down as ink: a
word unit that loses leaves its strokes unclaimed, and they expire as ink
(here a cursive "mum" lost to a scrub gesture).

## NEWTON_HEAPCHECK

Heap damage shows up long after it is done - a lock-up in compaction, a
crash in an unrelated allocation.  `NEWTON_HEAPCHECK=N` makes `newton`
walk the newt task's heap after every Nth allocation (1: every one, which
is slow - a boot takes minutes) and before every `DisposPtr`, and stop at
the first damaged block, printing what is wrong and the C stack as image
offsets:

    NEWTON_HEAPCHECK=1 build/host/host/newton --rom build/MP2x00US/rom.bin --headless 20
    python tools/host/whichfunction.py build/host/host/newton.exe 0x5b6753

The damage is then between the allocation named and the one before it.
`NEWTON_HEAPDUMP=1` as well lists the free list and every block at each
walk.  The code is `src/host/HostHeapCheck.cpp`; it is how the cursive
lock-up was traced to `CreateTrigramHeader` asking for a ROM size.

## samescreen.py - two boots, one screen

    python tools/host/samescreen.py --newton build/host/host/newton \
        --script src/host/demo/newton.ns --snapshot build/newton-demo.pgm \
        --run "--rom build/MP2x00US/rom.bin" --run "--objects <object file>"

- **Purpose:** boots `newton` once for each `--run` (its arguments; `--headless 5 --script <script>` added) and compares the
  screen snapshots the script writes, byte for byte.
- **How:** each boot runs in a fresh temporary directory, so its snapshot, written at `--snapshot` relative to the working
  directory, cannot collide with another boot's or another test's.
- **Output:** "the N boots drew the same screen" and exit 0; or, for each boot that differs from the first, how many bytes
  differ and where the first is, and exit 1; exit 2 when a boot wrote no snapshot. `--keep DIR` copies the snapshots out.
- **Used by:** ctest `host.NewtonNoROMSameScreen`, the proof that the OS booted on the object file built from the ROM source
  tree draws what it draws booted on the ROM image (`docs/rom-free/README.md`).

## fidelity.py - the ROM-free boot, step by step

    python tools/host/fidelity.py build/host/host/newton --rom build/MP2x00US/rom.bin \
        --original <romsrc.py build --original -o file> --default <build>/romsrc-objects.bin [--out DIR] [--no-noise]

- **Purpose:** carries samescreen.py's check past the Setup screen. `src/host/demo/fidelity.ns` opens the ROM's
  applications in turn, the keyboard, the Action menu, three Prefs panels, the Dates month and the Names new card and
  the Extras drawer, snapshotting each step (FIDELITY_DIR/NN-step.pgm). The walk runs in four boots, compared with
  `tools/imaging/pgmdiff.py`:
  - `--rom`, the ROM image;
  - `--original`, the committed tree built with `romsrc.py build --original` (less the NIE). It must match the ROM at
    every step, so any difference there is a builder or romsrc/ bug.
  - `--default`, the object file newton boots by default. It may differ only at the steps `EXPECTED` lists, each with its
    reason (Newton Devices' AppleTalk entry in the Prefs list). An unlisted difference fails, and so does a listed one
    that has gone.
  - a second `--rom` boot, as pgmdiff's `--noise` run, so a pixel two boots of one image disagree on is not counted.
- **Output:** each comparison and "fidelity: every difference accounted for" (exit 0), or what is not explained (exit 1);
  the snapshots and marked PNGs of the differences in `--out` (default `tmp/fidelity`).
- **Used by:** ctest `host.NewtonROMFreeFidelity` (after `host.ROMSourceOriginal` builds the `--original` file, about
  four minutes).

## stress.py - host tests under load

A test that waits a fixed time for something asynchronous passes on an
idle machine and fails now and then in a full parallel `ctest`, when
something else is using the CPU. Examples are a script that waits a few
seconds for the packages `newton --package` queues, or for the NIE's
procrastinated setup. This tool makes that happen every time instead of
now and then:

    python tools/host/stress.py build/host --test host.NewtonInetSetup --copies 10 --hogs 8
    python tools/host/stress.py build/host --suite --rounds 3 --hogs 8 [-j 8] [-R regex]

- **`--test`** runs copies of one ctest test at the same time.
  - Each copy runs in its own directory, `tmp/stress/<test>/<run>/<n>/`
    (`<run>` is a time stamp and the process id, so two stress runs never
    share or clear each other's copies), with its own store. Output a
    script writes under `tmp/` lands there too.
  - A test that needs a fixture gets it. Its setups that keep a store
    (`--store`) run in each copy's directory first, each logged to
    `<setup>.txt` there - a restart test's first run, a package put on a
    fresh store. A setup that keeps none (an extraction into the build
    directory) runs once, before the copies, where ctest runs it. An
    argument naming one of the copy's stores (a checker reading the store
    file) is pointed at the copy's.
  - A test given `--port 0` or `--tcp-echo 0` takes a free port, so its
    copies run at once rather than one at a time.
  - The command line, environment, pass and fail expressions and
    timeout are read from `ctest --show-only=json-v1`.
  - It prints how many copies passed, and the tail of each failed copy's
    output. The full output is in `output.txt` in that copy's directory.
- **`--suite`** runs the whole `ctest -j` (or the tests matching `-R`)
  `--rounds` times. It prints each round's wall time and the tests that
  failed.
- **`--hogs N`** keeps N processes spinning on the CPU for as long as the
  run lasts.

A test that holds a fixed port - `--tcp-echo`, a `--port` for a server it
runs, or any `RESOURCE_LOCK` in its ctest properties (the ctests that
share a port declare one, so `ctest -j` keeps them apart too) - runs its
copies one after another instead of at once.

Not every one-off failure is a fixed wait.  `host.NewtonBigStore.write`
crashed in `TUPort::Receive` in 2 of 30 copies beside 8 hogs; the cause was
heap damage from a world task copied short (a missing `GetSizeOf`, see
`docs/work-log.md`), and after the fix it passed 36 of 36.

The three races of 2026-09-30 were found this way; `docs/work-log.md`
records them. Before the fix, 10 copies of `host.NewtonInetSetup` beside 8
hogs passed 0 of 10; after it they pass 10 of 10. The cure is always the
same: wait on a condition, and end the run with `HostQuit()`.

## soak.py - an hour of use, watched

Real use is long use. This runs newton for as long as asked and watches
what grows:

    python tools/host/soak.py build/host/host/newton --minutes 60 [--window-pen | --window]
        [--heapcheck N] [--no-beam] [--no-print] [--interval 30] [--hang 600] [--taps 20]

- **What newton does.** Each newton runs `src/host/demo/soak.ns` round
  after round. A round has five steps:
  - one application, from the Extras drawer and the root, opened, used at
    random for `--taps` actions and closed (anything it left open, an
    error slip included, is closed too);
  - a word written on a new note and read;
  - a memory card put in, a soup entry written on its store, the card
    taken out;
  - the note printed to an IPP printer that `soak.py` serves itself
    (`tools/print/ippprinter.py`'s);
  - the note beamed to a second newton, B, which receives automatically.

  Each round ends with a `soak: round N ... ptrFree ... handleFree ...
  framesFree ... systemFree ...` line, which is `GetHeapStats` after a
  collection. Every fifth round the old notes, In Box entries and the
  card's soup are thrown away, so what grows is the machine and not what
  it was asked to keep. `SOAK_STEPS` (a comma-separated list) leaves steps
  out when the script is run on its own.
- **What is watched.** Every `--interval` seconds the host process is
  sampled: its handle count, thread count, private bytes and working set,
  read through the Windows API with ctypes, or from `/proc` on Linux.
- **What counts as a problem.**
  - A crash: the process ends early or with a non-zero status.
  - A hang: no new round for `--hang` seconds. On Windows `stacksample.py`
    is run on the hung process first, into `<name>.hang.txt`.
  - A step that never finished: each one says "waited in vain" and leaves
    a screen snapshot beside its store.
- **The heap is checked while it runs.** Both newtons run under
  `NEWTON_HEAPCHECK` at a sparse rate (`--heapcheck`, default every
  50000 allocations).

**Output.** Everything goes in `--out` (default `tmp/soak`):

- each newton's log, `A.log` and `B.log`;
- `ipp.log`, the printer's log;
- `A.csv` and `B.csv`, one row per sample;
- `jobs/`, the documents printed;
- `summary.txt`, which is also printed at the end. For each measure it
  gives the first, last, minimum and maximum, and the growth per hour as
  a least-squares slope over the second half of the run (the first half
  is the machine settling).

The exit status is 1 if a newton crashed or hung.

**What it found** (2026-10-01):

- **A deleted task's thread was never ended.** It stayed parked for good,
  so thread counts grew by one or two a minute (`docs/host-runtime.md`).
- **`--headless` overran.** It counted its sleeps rather than reading the
  clock, so a busy machine ran a 4-minute run on to 5½ minutes.
- **`--limit` overflowed.** It went through a `TTimeout` of seconds,
  which overflows beyond 582 seconds.
- **Rosetta overflowed on a long stroke.** After 47 minutes, a random
  stroke drawn on the Notepad made the sum of two `FixedMultiply` squares
  in `SegmentStrokeMinDistance` overflow, and the host's sanitiser stopped
  both newtons. The ARM's add simply wraps, so the reconstruction now
  wraps too.

Over the 47 minutes up to that crash, 39 rounds (78 print jobs and the
beams), nothing else grew:

- threads held at about 22;
- handles held at about 145;
- the Newton's pointer, handle and frames heaps were level;
- private bytes grew by less than 10 MB an hour.

## profile.py - where a running host's time goes

(On Linux, see "On Linux: the process samples itself" below.)

A sampling profiler for a host build that is busy but not stuck:

    python tools/host/profile.py <pid> [--seconds N] [--interval MS] [--top N]

At each interval it suspends every thread of the process in turn. A
thread whose instruction pointer is inside the executable, rather than
waiting in the system, counts as one sample:

- its function once as *self*;
- every function with a return address on its stack once as *inclusive*.
  The stack is scanned the same way `stacksample.py` does it.

Functions are found by bisecting the image's `.pdata` table and named
with `whichfunction.py`'s object-file matching. The output lists the
functions by self time and by inclusive time, each as a percentage of the
samples.

Inclusive figures include stale words found on the stack, so read them as
upper bounds. The drawing work was measured this way
(`docs/qd/README.md`, "Drawing speed"): run
`src/host/demo/drawbench.ns` with more rounds and profile the process
while it runs.

## httpserve.py - a web server for a host Newton to browse

A host Newton browsing the web (NetHopper over the Newton Internet Enabler,
which reaches the host's own TCP/IP stack) needs a web server to ask.  This
serves a directory on 127.0.0.1 with Python's own `http.server`, runs a
program given after `--`, and stops serving when the program ends:

    python tools/host/httpserve.py --dir src/host/demo/www --port 0 -- \
        build/host/host/newton ... --script src/host/demo/nethopper.ns

**Inputs:** the directory to serve and the port - 0 takes a free one, so
two runs at once (two build directories, `stress.py` copies) never meet;
the program is told it in the environment variable `NEWTON_HTTP_PORT`,
which a script reads with `HostGetEnv("NEWTON_HTTP_PORT")`; `--file NAME=PATH`
(any number of them) serves one more file, PATH, as `/NAME` - a package out
of `fixtures/packages` without a copy of it in the directory (ctest
`host.NewtonAppNewtsCape`); `--type .EXT=MIME` serves files ending .EXT as
MIME where the Python library's guess is not what a server of the day sent
(a WAV as `audio/x-wav`, which Newt's Cape's audio helper asks for, not
`audio/wav`; ctest `host.NewtonAppNewtsCapeHelpers`).  **Output:** each request
as `[http] GET /path 200`, the program's output (stdout and stderr merged)
and `[http] the program answered N`; it exits with the program's status (1
when the port cannot be had).  ctest `host.NewtonNetHopper` runs NetHopper
3.2 under it (`src/host/demo/nethopper.ns`).  Standard library only.

## tonewav.py, palmdoc.py, tinymod.py - files for a host Newton to download

    python tools/host/tonewav.py OUT.wav [--hz 880] [--ms 200] [--rate 11025] [--bits 8]
    python tools/host/palmdoc.py IN.txt OUT.pdb [--name NAME] [--plain] [--check]
    python tools/host/tinymod.py OUT.mod [--title TITLE]

`tonewav.py` writes a mono PCM WAV of one sine tone; `palmdoc.py` writes a
text file as a PalmDoc e-text (a Palm database of type TEXt, creator REAd:
the database header, the record list, the PalmDoc header record and the
text in 4096-byte records, each compressed with PalmDoc's own LZ77 coding -
or stored as it is with `--plain`; `--check` reads the file back and says
whether it decodes to the text).  `src/host/demo/www/beep.wav` and
`story.pdb` are their output with the defaults (`--name "Host Story"`),
which Newt's Cape's audio and PalmDoc helpers take in
`src/host/demo/apps-newtscape-helpers.ns`.  `tinymod.py` writes a
four-channel ProTracker "M.K." module (one pattern of a rising arpeggio
on a looped square-wave sample), `src/host/demo/www/tune.mod`, which
Newt's Cape's MOD helper saves as a package.  Standard library only.

## objectsstamp.py - an object file newton was not built for is refused

The object file newton boots from (`romsrc-objects.bin`, written by
`tools/newton-rom/analysis/romsrc.py build`) carries the *builder's stamp*:
sixteen hex digits of a SHA-256 of `romsrc.py` and the modules it builds with
(`builder_stamp`; version 4 of the file format).  The build compiles the
same stamp into newton and newtonscript (`romsrc.py stamp --header
<build>/generated/ObjectsStamp.h`), and both refuse an object file with
another stamp, or with none:

    newton: build/host/romsrc-objects.bin was built for a different newton (rebuild with cmake --build build/host)

This is the case of a build that rewrote the object file with newer tools
but could not relink a newton that was running: the old program would
otherwise boot objects laid out in a way it does not expect, and hang.
An edit to `romsrc/` does not change the stamp, so object files built from
edited trees (the edit tests) boot as before; a ROM image (`--rom`) is not
checked.

    python tools/host/objectsstamp.py --objects build/host/romsrc-objects.bin \
        --program build/host/host/newton --program build/host/host/newtonscript \
        --work build/host/objectsstamp

**Inputs:** an object file the programs were built for, the programs, and a
directory for two copies of the file - one with another stamp, one made a
version 3 file with no stamp. **Output:** a line per program and copy, then
`objectsstamp: done` when each program refused each copy with the message
and a non-zero exit (ctest `host.NewtonObjectsStamp`).

## includecase.py - includes spelt as their files are

Windows (and WSL's view of a Windows drive) finds `#include "Objects.h"`
when the file is `objects.h`; a Linux build stops there.  This checks every
quoted include under a source tree against the files in it:

    python tools/host/includecase.py src

**Output:** `file:line: "Objects.h" is objects.h` for each include whose
only matches differ in case, and exit status 1 if there are any.  An
include that names no file in the tree (a system or generated header) is
left alone.  ctest `tools.IncludeCase` runs it on every host, so the
mistake is caught where it is made.

## On Linux: the process samples itself (linuxsample.py)

Linux will not let one process read another's registers without ptrace,
and WSL and most distributions allow ptrace only to a process's parent
(`kernel.yama.ptrace_scope = 1`), so on Linux `stacksample.py` and
`profile.py` ask newton for its own stacks instead (`tools/host/linuxsample.py`,
which they run when started on Linux - same options; `--depth` is not
used).  newton installs a handler for `SIGRTMIN+3` (`src/host/newton.cpp`'s
`HostInstallSampler`): a thread sent it (tgkill) appends a line to the
sample file - its thread id, the interrupted instruction and its return
addresses, unwound by glibc's `backtrace()` through the signal frame, as
image offsets - and carries on.  The file is `NEWTON_SAMPLE_FILE` in
newton's environment, else `/tmp/newton-sample-<pid>.txt`.

    python3 tools/host/stacksample.py <pid> [--thread TID] [--samples N]
    python3 tools/host/profile.py <pid> [--seconds N] [--interval MS] [--top N]

stacksample's thread is the busiest over half a second unless `--thread`
names one; profile samples every thread that is running (state R in
`/proc/<pid>/task/<tid>/stat`) each round.  The names come from the
executable's own symbol table (whichfunction.py).  A gdb for a hang that
needs one, without root: `apt-get download gdb` and its libraries, `dpkg -x`
each into a directory, run it with `LD_LIBRARY_PATH` there - and start
newton under it, since it cannot attach (docs/host-lp64.md).

## xdnddrop - a file dropped onto newton's window (X11)

A drag source for testing the X11 window's XDND drop target
(`src/host/x11/HostWindow.cpp`), built on an X11 host as
`build/host/host/xdnddrop` from `src/host/x11/xdnddrop.cpp`:

    xdnddrop [--name NAME] [--timeout S] FILE [-- PROGRAM ARGS...]

It runs the program (if any), waits for a window of that name ("Newton")
that takes drops, drags FILE onto its middle as a file manager would
(XdndEnter offering text/uri-list, XdndPosition, XdndDrop, the file's URI
handed over as the selection) and waits for XdndFinished.  **Output:**
`xdnddrop: dropped <file> onto "Newton" (<window>): accepted`, then the
program's exit status; non-zero after stopping the program when there is no
such window or the drop is refused, and 77 (ctest's skip) with no X display.
ctest `host.NewtonWindowDrop` (`src/host/demo/windowdrop.ns`): a package
dropped onto the window is installed.
