# Running the reconstruction on Linux: what a wider `long` changes

The reconstruction was written and checked on Windows, where `long` is 32
bits - the same width as the ARM's word.  On Linux and macOS (LP64) `long`
is 64 bits, and everything that quietly relied on the ARM's width stops
being the ARM's arithmetic.  This page records what that brought out, so
that the next piece of work spells such values the way the ones below now
are, and so that a similar failure is recognised rather than re-diagnosed.

The port is otherwise small: the kernel, the object system, QuickDraw, the
recognisers and the comms all build and run unchanged.  `ctest` on Linux
passes 226 of 227 (the exception is `compression.LZ`, below).

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

## Not a width problem, but Linux-only

**Case-sensitive includes.**  `#include "Objects.h"` for `objects.h`, and
`Longtime.h`/`sharedTypes.h` in the DDK's own headers, only pass on a
case-insensitive file system.  The DDK ones are patched by
`sync_ddk_headers.py`, next to the two that were already there.

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

## The one test that still fails on Linux

`compression.LZ` round-trips 1022 and 1023 bytes wrongly.  This is a ROM
bug faithfully reproduced, not a host difference: `TLZDecompressor::
DecompressBlock` (0x000ffa60) is `CMP r0,#0x400; SUBLS r3,r0,#4;
MOVHI r3,#0x400` over `fRemaining`, which counts the block's own four-byte
header, so a *stored* last block of 1021 to 1023 bytes comes back as a
whole 0x400 bytes.  The ROM never meets it - the store compander hands the
coder fixed 0x400-byte blocks.

The test only reaches it on Linux because its data comes from the C
library's `rand()`, which is a different sequence on each host, so whether
any size lands on an incompressible 1021-1023 byte block differs between
them.  Giving the test a generator of its own would make it the same test
everywhere; it then fails at 127 bytes as well, where the coded path
over-produces, which is a separate question about what the decompressor
guarantees for a partial final block and has not been chased.
