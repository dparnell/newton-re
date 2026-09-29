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
  - Each copy runs in its own directory, `tmp/stress/<test>/<n>/`, with its
    own store. Output a script writes under `tmp/` lands there too.
  - The command line, environment, pass and fail expressions and
    timeout are read from `ctest --show-only=json-v1`.
  - It prints how many copies passed, and the tail of each failed copy's
    output. The full output is in `output.txt` in that copy's directory.
- **`--suite`** runs the whole `ctest -j` (or the tests matching `-R`)
  `--rounds` times. It prints each round's wall time and the tests that
  failed.
- **`--hogs N`** keeps N processes spinning on the CPU for as long as the
  run lasts.

A test that listens on a fixed port (`--tcp-echo`) cannot run as copies.

The three races of 2026-09-30 were found this way; `docs/work-log.md`
records them. Before the fix, 10 copies of `host.NewtonInetSetup` beside 8
hogs passed 0 of 10; after it they pass 10 of 10. The cure is always the
same: wait on a condition, and end the run with `HostQuit()`.

## profile.py - where a running host's time goes

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
