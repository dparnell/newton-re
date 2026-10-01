# Running the reconstruction on Linux: what a wider `long` changes

The reconstruction was written and checked on Windows, where `long` is 32
bits - the same width as the ARM's word.  On Linux and macOS (LP64) `long`
is 64 bits, and everything that quietly relied on the ARM's width stops
being the ARM's arithmetic.  This page records what that brought out, so
that the next piece of work spells such values the way the ones below now
are, and so that a similar failure is recognised rather than re-diagnosed.

The port is otherwise small: the kernel, the object system, QuickDraw, the
recognisers and the comms all build and run unchanged.  `ctest` on Linux
passes all 394 (2026-10-01: Ubuntu 22.04 under WSL 2, clang 14 and
libstdc++, X11 and OpenSSL; no ALSA headers, so newton runs silent there -
see "Building under WSL" below).

## The rule

A value that is the ARM's 32-bit word - because it is laid out in the
ROM's data, read from a package or a store, or because its arithmetic has
to wrap where the ARM's did - is spelt with a fixed-width type, never a
bare `long`:

* `Long32` / `ULong32` (`src/host/host_compat.h`) for a word in the
  reconstruction's own code.
* `Fixed` and `Fract` are pinned to 32 bits for a host whose `long` is
  wider (`hostLongIsWiderThanARMWord`, applied to `NewtonTypes.h` by
  `tools/newton-rom/sync_ddk_headers.py`).  Windows' `long` is the ARM's
  width already, so nothing is said there and a `Fixed` stays `long`,
  which keeps every overload it takes part in where it was.

`ULong` and `Long` are deliberately **not** the ARM's word: they are
pointer-sized on every host (`hostLongIsPointerSized`), because the OS
passes pointers through them.  Casting a 32-bit constant or a negative
value through `ULong` is therefore the usual way to get this wrong.

`tools/newton-rom/analysis/romsizes.py --lp64` finds ROM byte counts used
as the size of something that is wider on an LP64 host.

## What it found

Each of these behaved correctly on Windows and wrongly on Linux.  All the
fixes are no-ops on Windows.

**A negative constant written as a 32-bit word.**  `(long) 0xffedf02e` is
-18.0618 dB in 16.16 on Windows and +4293849134 on Linux, so every volume
comparison went the wrong way (`sound/SoundSettings.cpp`, `SoundChannel.cpp`,
`SoundServer.cpp`; `test_SoundVolume`).  Spelt `(Long32) 0xffedf02e`.
Note that `(long) 0x80000000` used as a *bit flag* - `kStoreNoSlopCheck`,
a `fFontFace` bit - is right as it is on both: it sets bit 31 and nothing
above it.

**Arithmetic that has to wrap in 32 bits.**  The ROM's `rand`
(0x003503d0) brings a negative remainder back above nought with
`SUBLT r0, r0, #0x80000001` - in an ARM register, subtracting 0x80000001
wraps to adding 0x7fffffff.  Done in a 64-bit `long` it does not wrap at
all, the state stays negative for ever and every number after it is
negative too (`utility/Random.cpp`).  On Linux that dealt Mahjongg a board
with a tile index of -6: the game seeds the machine's generator with
`TimeInSeconds() mod 5 + 1`, which is negative now that `TimeInSeconds`
has outgrown a NewtonScript integer, and nothing brought it back
(`host.NewtonThirdPartyApps`).  `test_Random` now writes a negative seed.

**A shift that has to be arithmetic.**  `TNSDebugAPI`'s frame base was
`(ULong) ref >> 8` where the ROM's `Locals` (0x002d2688) is
`MOV r0, r8, ASR #2; MOV r8, r0, ASR #6` - two arithmetic shifts, because
a frame's base on the value stack may be negative.  Shifted unsigned it
answered 0x00fffffe on the device and something far larger on a host whose
Ref is 64 bits, which took the stack inspection off the end of the value
stack (`frames/DebugAPI.cpp`, `test_Printer`).

**An index or count written through the wrong pointer.**  The shape
solver's coefficient rows are handles of 4-byte `Fixed`
(`MakeHandle((n+1)*4)`, zeroed as `Fixed` by `NewCoeffs`), but
`AddBilinears` and sixteen places in `ShapeEquations.cpp` wrote them
through a `long*` - eight bytes each on Linux, so every write past the
middle of the row corrupted the heap (`test_ShapeDomain` crashed in
`DisposHandle`).

**A ROM byte count over host-sized structs.**  `GlobalTrends`' sides block
was `MakeHandle(0x1a4 + 3 * sizeof(SideMap))`, mixing the ROM's count for
the first fifteen entries with the host's size for three more; a `SideMap`
holds five `long`s.  Sized from the host's own types instead.

**A ROM sentinel that only narrowing makes negative.**  `TDictChain::Make(0,
0xffffffff)` is the ROM's "nowhere"; `fPosition = (long) position` with a
64-bit `ULong` kept it as 4294967295 (`recognition/Dictionaries.cpp`).

**A time gone by, read back as a huge wait.**  `TTime::ConvertTo` gives
the ARM's word (`ULong32`, so that a real-time-clock alarm wraps as the
ROM's does), and the newt world's idle arithmetic read it as
`(long) ... ConvertTo(kMilliseconds)`: a delayed action already due is a
negative number of milliseconds, which on Windows is negative and becomes
the one millisecond the ROM's code makes it, and on Linux is four thousand
million.  The world's idle timer was primed for fifty days, and every
delayed action after the boot sound - every demo's `AddDelayedCall` -
never ran (135 ctests).  Spelt `(Long32)` (`newt/NewtWorld.cpp`, four
places).

## Not a width problem, but Linux-only

**Case-sensitive includes.**  `#include "Objects.h"` for `objects.h`, and
`Longtime.h`/`sharedTypes.h` in the DDK's own headers, only pass on a
case-insensitive file system.  The DDK ones are patched by
`sync_ddk_headers.py`, next to the two that were already there.  The
mistake kept coming back (four files on 2026-10-01), so ctest
`tools.IncludeCase` (`tools/host/includecase.py`) now catches it on
Windows too.

**The host's MIME table.**  `tools/host/httpserve.py` let Python guess a
file's type, and Linux's `/etc/mime.types` makes `.pkg` an Apple
installer's XML (Windows has no entry), so Newt's Cape read a downloaded
package as text and never asked to install it.  A `.pkg` now always goes
out as `application/x-newton-compatible-pkg`; and a status prints as its
number (`HTTPStatus.OK` did before Python 3.11, which the tests' patterns
did not match).

**A condition variable destroyed with waiters on it.**  The task runtime
ends a run by leaving each task's thread parked (see
`docs/host-runtime.md`), so the static `std::mutex` and
`std::condition_variable` the baton is handed over on are now made once
and never destroyed.  Destroying a condition variable somebody is waiting
on is undefined; glibc's `pthread_cond_destroy` waits for its waiters to
leave, so every test that booted the OS printed that all its checks had
passed and then hung for ever on the way out of `main`.  Windows' own
destructor happens not to wait, which is why it was never seen there.

**A pointer into a buffer that was freed and allocated again.**  The
window holds the display's pixels for as long as it runs, and
`THostScreenDriver::ScreenSetup` freed that buffer and allocated another
of the same size.  Windows' allocator handed the same block straight back
and nothing came of it; a buffer that size is mapped on its own by glibc,
so on Linux the window read an unmapped page as soon as the screen was set
up.  `ScreenSetup` now keeps the buffer and clears it.

**GNU ld's single pass over archives.**  The libraries are mutually
dependent by design, so `src/CMakeLists.txt` brackets them in
`--start-group` when the linker is GNU's.  lld (the zig toolchain's, and
Apple's ld64) resolves them without it - and zig 0.16's linker falls over
if it is given one, so the group goes on only where it is needed.

## The LZ round trip, and the oracle that settled it

`compression.LZ` failed on Linux at 1022 and 1023 bytes, and the answer
turned out to be neither a host difference nor a fault in the
reconstruction.  Two things were wrong at once.

The test was not the same test on two hosts: its data came from the C
library's `rand()`, which is a different sequence on glibc and on Windows,
so whether any size landed on an incompressible block differed between
them.  It now has the C standard's sample generator spelt out, so every
host compresses the same bytes.

And what it asked for was more than the coder gives.  The LZ coder gives
back the bytes it was given, but a *last* block that is not a whole
`kLZBlockSize` can come back a few bytes long, in two ways:

* a **stored** last block of 1021 to 1023 bytes comes back as a whole
  0x400 bytes, because the length it is worked out from counts the block's
  own four-byte header (`DecompressBlock`, 0x000ffa60:
  `CMP r0,#0x400; SUBLS r3,r0,#4; MOVHI r3,#0x400`);
* a **coded** last block can come back up to a few bytes long, because the
  decoder reads codewords until the input runs out and the bits that pad
  the last byte can make one more.

Both are the ROM's own.  `compression.LZOracle`
(`src/compression/tests/test_LZOracle.cpp`) settles it: it runs the ROM's
`TLZCompressor` and `TLZDecompressor` on the ARM interpreter over the ROM
image and holds the reconstruction to the ROM's answer - the same
compressed bytes, and the same restored bytes *and length* - at every size
from 0 to 0x900 and around the stored path's boundary.  All 2305 sizes
agree, the 76 that do not round-trip exactly included.  Changing the
reconstruction to "fix" the stored-block length makes the oracle fail at
1021-1023, which is what it is for.

Neither fault is reachable through the only thing that uses the coder: the
store compander hands it fixed 0x400-byte blocks
(`stores/StoreCompander.h`), where the last block is full and the padding
has nothing after it to decode.  `RoundTrip` in `compression.LZ` therefore
asks for the bytes back always, and for the length back whenever the
source is a whole number of blocks.

Running the ROM's code needs three things beside the image: the patchable
jump table aliased at 0x01A00000 (virtual page *p* is a plain alias of ROM
page `0x2000 + (p/32)*0x1000`, and the branch offsets are relative to the
virtual address - `tools/newton-rom/newtonrom/jumptable.py`), a host trap
for the `malloc` and `operator new` the compressor's `New` calls out to,
and nothing else: `TLZDecompressor::New` is `MOV pc, lr` and both `Init`s
answer `noErr`.  `build/<ROM>/symbols.json` is worth generating on a fresh
checkout (`tools/newton-rom/dump_symbols.py`) - five more ctests run when
it is there.

## Found on Linux, not Linux-only

**A driver's timed wait taken for a stopped machine.**  With a card
pulled out while its store is mounted, ATA Support restarts the machine
from its card server task - but if the ATA slip asks the store for its
sizes first, the `Tmux` task (priority 20) polls the card that has gone
until ATA Support's own timeout, over ten seconds, and nothing else runs
meanwhile.  That is the ROM's and the driver's behaviour, and the store
answers -1001029 at the end of it; but the host's watchdog (no handover
for ten seconds) took it for a stalled machine and printed the
NewtonScript stack from its own thread while the task was still running,
and the allocations that printing makes damaged the heap under it
(`host.NewtonATASupport.pull`, one run in four on Linux, where the slip's
update came first more often).  armcpu's safe points (`ShortTimerDelay`,
`TimedOut`) now tell the runtime the task is busy on purpose
(`HostTaskBusy`, `os600/kernel/host/TaskRuntime.h`), which the watchdog
counts as progress.

## Building under WSL

The Linux results are from WSL 2 on the development machine, built on
WSL's own file system: a Windows drive seen from WSL (`/mnt/f`) is case
insensitive, which hides exactly the include mistakes a Linux build is
for, and is several times slower.  The commands (no root needed - cmake
and ninja from pip, into the repository's `tmp/`):

    python3 -m pip install --target tmp/wsl-pip cmake ninja
    export PATH=$PWD/tmp/wsl-pip/cmake/data/bin:$PWD/tmp/wsl-pip/bin:$PATH
    rsync -a --delete --exclude .git <checkout>/ ~/newton-lp64/tree/
    cmake -G Ninja -S ~/newton-lp64/tree/src -B ~/newton-lp64/build         -DCMAKE_CXX_COMPILER=clang++ -DNEWTON_ROM_BUILD=<checkout>/build/MP2x00US
    ninja -C ~/newton-lp64/build && ctest --test-dir ~/newton-lp64/build -j16

(`ninja` from the wheel may need `chmod +x`.)  gdb, for a hang, can be
unpacked the same way: `apt-get download gdb` and its libraries,
`dpkg -x` each into a directory, and run it with `LD_LIBRARY_PATH`
pointing there; WSL's `ptrace_scope` keeps it from attaching to a running
newton, so start newton under it and interrupt it with `pkill -INT`.
