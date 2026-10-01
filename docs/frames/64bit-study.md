# A 64-bit NewtonScript: feasibility study and plan

Status: **study only** (branch `ns64-study`; the spike that measured it is
on branch `ns64-spike`, neither merged).  Written 2026-10-01 against
`main` at 080c4449.

The owner asked what it would take to move the NewtonScript side of the
reconstruction to a "fully 64-bit state", expecting a large, breaking
change to be done on a branch.  This page says what that phrase can mean,
measures how far each meaning reaches into `src/`, reports a spike that
actually widened the integers and ran the suite, and proposes a staged
plan.

**The short answer.**  Much less of the system cares than one would
expect.  Refs, object headers and immediates are already pointer-sized
on the host; the 30-bit integer is held in place by *one* macro
(`MAKEINT`) and *one* helper (`WordRef`, 7 uses).  Widening those, with
`RINT` answering a pointer-sized value, compiles with no errors, and
after one host-side fix (a place that used "does not fit in 30 bits" to
tell a pointer from a number) the suite goes from 394/394 to 382/394,
and of the remaining failures eight are tests that deliberately pin 30-bit
behaviour or a wide time crossing into a store, and four are not yet
diagnosed (probably the same boundary).  The real work is not in the
interpreter but at the **boundaries** (stores, soup keys, NSOF, docking,
beaming, the ARM interpreter), which must stay 32-bit for compatibility,
and in a **policy** for what happens to an integer that does not fit when
it crosses one.  A compile-time flavour, `NEWTON_NS64`, beside the
faithful default, is about **3-5 weeks** of focused work for the
recommended scope (below); widening the persistent formats themselves is
a separate, larger and incompatible project that I do not recommend.

---

## 1. What "fully 64-bit NewtonScript" could mean

A Ref is the ARM's word with a two-bit tag (`docs/frames/README.md`,
"Refs").  On the host it is already pointer-sized; the question is which
of the *values* it carries become wider.  There are five separable
meanings.

### (a) 62-bit integers

`MAKEINT` today shifts in 32 bits and sign-extends (patched in by
`sync_ddk_headers.py`), and the interpreter's `+`, `-`, `*`, `div` and
`for` increment cut their result back to a word (`WordRef`), so a
NewtonScript integer is -536870912..536870911 and wraps where the ARM
wraps (`docs/frames/README.md`, "An integer is thirty bits, however wide
the host's word is").  Widening means an integer runs to ±2^61 and wraps
(or overflows into something else - a design choice, see 4.3) only there.

Consequences scripts can see:

* sums that wrapped now do not (`536870911 + 1` is 536870912, not
  -536870912; `test_Strings`' `TestThirtyBitIntegers` pins the old
  answer);
* literals: `0x20000000`..`0x3fffffff` are negative today (the lexer
  accepts up to 0x3fffffff and the value wraps; NTK sources use this for
  flag words with bit 29 set, e.g. `vjBottomRatio`) and would become
  positive.  The ROM's own compiled objects keep their (negative) values,
  so a script-computed flag word could stop being `=` to an imported one;
* `TimeInSeconds()` is the true count (about 1.06e9 in 2026) instead of
  the value that wrapped in January 2010;
* `ExtractLong` no longer needs to throw `kNSErrLongOutOfRange` on a word
  whose top bits disagree;
* `Ticks()` wraps at 2^32 (the C value) rather than 2^30;
* the ROM's NewtonScript uses 536870911 / -536870912 as "infinity" in 34
  function files (`romsrc/functions/*protoCursor.FetchPage.ns`,
  `UpdateDateRange`, ...).  Those are dates in *minutes* (about 6.5e7
  today), so they still bound everything they are compared with - but a
  new script can now produce integers beyond the ROM's "infinity".

### (b) Objects larger than the 24-bit size field

The size word is `size << 8 | flags`, and `kObjMaxSize = 0xffffff`
(`frames/ObjectHeap.h`) caps an object at 16 MB.  On the host the header
word is *already* a 64-bit `ULong`, so the field could hold 56 bits; the
cap is one constant and four checks (`ObjectHeap.cpp:474, 505, 605,
614`).  Worth knowing: because a host slot is 8 bytes, the host's
`kMaxArrayLength` is already **half the ROM's** - an array of 2,097,148
slots against the device's 4,194,300.  Lifting the cap removes that
host-only deviation as a side effect.  The persistent formats carry
lengths as signed 32-bit xlongs (2 GB), so they need no change below
that; a real Newton cannot receive an object over 16 MB in any case.
Heap sizes and allocation lengths are `long` in the object heap's API,
32 bits on Windows, which matters only above 2 GB.

### (c) 64-bit addresses wherever a Ref or header is stored

**Done.**  Refs, both header words, RefHandles, slots and the RefVar
machinery are pointer-sized (`ObjHeader.h`, `ObjectHeap.h`), and the ROM,
packages and the romsrc object file are imported from their 32-bit
layout into the host's (`ObjectAreaImport.cpp`, `ROMImport.cpp`,
`FramesPart.cpp`).  Two host-side places carry a pointer in an integer
Ref (`AddressToRef`/`RefToAddress`, 42 callers, and a command's
`parameter`, `views/Commands.cpp`); under (a) those become ordinary
integers, which is cleaner than today.

### (d) 64-bit-clean persistent formats

Every persistent or wire format holds a Ref as a 32-bit big-endian word
or xlong: NSOF, the store object format, 4-byte soup 'int keys, the
store object header's `_uniqueID`/`_modTime`, packages (ARM layout), the
romsrc object file, the dock protocol, the endpoint data forms, and the
ARM interpreter's view of a Ref.  There are two ways to go:

* **keep them 32-bit and convert at the boundary** - an integer that fits
  in 30 bits is written exactly as today, byte for byte; one that does
  not is handled by a single policy (4.3).  Stores, packages and NSOF
  stay interchangeable with real Newtons, NCU/NTK, Einstein and the
  faithful build;
* **widen them** - a new NSOF tag (a real Newton throws `kNSErrBadStream`
  on any tag above 12), a new soup key type recorded in the index info, a
  new object-file version.  Every one of these breaks interchange with
  real devices and the desktop tools, and the reconstruction would have to
  carry both forms for old data.  I recommend against it (section 3).

### (e) What NewtonScript can observe, and the faithful-port rule

The owner's standing rule is that `src/` is a faithful port, bugs and
all.  A 64-bit NewtonScript is by definition *not* the ROM's behaviour,
so it cannot replace the default: it has to be a separate, opt-in
**flavour**, in the same spirit as `NEWTON_ROM_2010_BUG` (which selects
between the ROM's arithmetic and the year-2010 fix).  The faithful build
stays the default and stays the oracle; the 64-bit flavour is documented
as one large DEVIATION with a list of exactly what a script can see
differently (the bullets under (a)).

---

## 2. The reach, measured

Counts are over `src/` (1385 `.cpp`/`.h` files, 596k lines) unless said
otherwise.  Class: **M** mechanical, **D** needs design, **B** must stay
32-bit at a boundary (or blocks a naive change).

### 2.1 The integer choke points (the core change)

| What | Where | Count | Class |
|---|---|---|---|
| `MAKEINT` truncating to 32 bits | `ddk/objects.h:58` (via `sync_ddk_headers.py`) | 1 definition, 1741 uses in 186 files | M (one line) |
| `RINT` returns `long` (32 bits on Windows) | `ddk/objects.h:87` | 1 definition, 2216 uses (≈1308 outside tests) | M to change, D to audit (2.2) |
| `WordRef` (interpreter `+ - * div`, `for` step) | `frames/Interpreter.cpp:857` | 1 + 7 uses | M |
| Multiply/divide fast path in `ULong32`/`Long32` | `Interpreter.cpp:2101, 2111` | 2 | M |
| Explicit 30-bit range constants and masks (non-test) | lexer 2, `ExtractLong` 1, Dates 3, store `& 0x1fffffff`/`0x3fffffff` 13, Docker 1, sentinels 5, `TestNatives` 1, armcpu 2, NSOF/store immediates 4, soup int key 3 | ≈45 | mixed, itemised below |
| Characters (`MAKECHAR`/`RCHAR`, 64+67 uses) | `objects.h:59-60, 89` | - | none: 16-bit value in a pointer-wide immediate |
| Magic pointers (`MAKEMAGICPTR`, 37 uses) | `objects.h:62`, `ResolveMagicPtr` | - | none: 12-bit index fixed by the formats, table field unbounded |
| Booleans, `NILREF`, `kSymbolClass`, `kFuncClass` | `objects.h` | - | none |
| `kRefValueBits = 30` | `objects.h:41` | unused elsewhere | M |

`MAKEIMMED`, `MAKEMAGICPTR` and `RVALUE` are already pointer-wide, so
nothing about the tag scheme itself needs to change.

### 2.2 Where `RINT`'s 32-bit `long` reaches (Windows' LLP64)

Making `RINT` answer a pointer-sized value is a one-line change and, in
the spike, compiled without a single error.  That is the danger as much
as the comfort: on Windows every `long x = RINT(...)` then narrows
*silently*.  Nearly all of them are coordinates, indices, counts and
selectors that are small by contract and do not care; the job is to find
the few that carry times, ids, hashes or masks.

| `src/` area | `RINT(` | typed receivers | of which `long x =` | narrowing casts |
|---|---|---|---|---|
| views | 443 | 166 | 142 | 93 |
| frames | 175 | 72 | 70 | 14 |
| recognition | 134 | 61 | 44 | 33 |
| comms | 89 | 14 | 9 | 7 |
| stores | 82 | 18 | 11 | 34 |
| intl | 77 | 36 | 20 | 8 |
| books | 71 | 19 | 18 | 5 |
| qd | 50 | 19 | 15 | 14 |
| text | 30 | 13 | 12 | 5 |
| testing | 29 | 15 | 9 | 11 |
| packages | 22 | 5 | 5 | 7 |
| everything else | 106 | 14 | 10 | 25 |
| **total (non-test)** | **1308** | **≈452** | **≈365** | **≈256** |

About 100 of the casts narrow to `short`/`UShort`/`UniChar`/`UByte`/
`char` (intended), and about 925 `RINT(` calls are passed straight as
arguments, which narrow at the callee's parameter type.  The audit is
mechanical but long: build the 64-bit flavour with clang's
`-Wshorten-64-to-32` and keep only the warnings on lines that mention
`RINT`/`RVALUE`/`CoerceToInt` (Linux's LP64 hides most of them, so the
audit has to be done on the Windows toolchain).  **M**, the largest item
by volume.

Related small items, all **M**: `IntegerString(long)` and the printer's
`"%d"`/`"%ld"` formats (`Printer.cpp:201, 668, 774`; 28 uses);
`IntegerStringSpec(long)` (`intl/NumberFormat.cpp:396`); `for`-loop
counters held in `long` (`Interpreter.cpp:1362, 1937`); `FCeiling`,
`FFloor` and `CoerceToInt` converting a real with `(long)` and no range
check (`Builtins.cpp:198-213`, `Objects.cpp:2316`) - undefined behaviour
for a large real, which wants a range check (**D**); `FRandom`'s `long`
bounds.

### 2.3 The compiler and the tools

* `Lexer.cpp:405-408` (hex) and `485-488` (decimal): `strtol` (32 bits on
  Windows) and `value >= 0x40000000` → `kNSErrIntegerTooLarge`.  Needs
  `strtoll`, a 2^61 limit, and a decision about the negative-hex idiom
  (`0x3fffffff` is -1 today; `test_Strings.cpp:830` pins it).  **D**.
* Constant folding is only unary minus (`Compiler.cpp:2008-2025`); push
  constants carry a Ref in a sign-extended 16-bit operand when it fits
  and use the literals array otherwise (`Compiler.cpp:966-975`).  Both
  work at any width.  **M**.
* `romsrc.py:775, 1397` parses literals with `(int(text) << 2) &
  0xffffffff` (silently wrapping), `:1479` has a 30-bit sign fix,
  `nsdecompile.py:1146-1149` and `nsfunctions.py:170` read 32-bit words.
  The object file is the ROM's layout and stays so (**B**); romsrc.py
  should reject a literal it cannot hold instead of wrapping it (**M**).

### 2.4 The persistent and wire boundaries

Inbound, every reader already sign-extends a 32-bit word into a host Ref
(`ROMImport.cpp:107`, `ObjectAreaImport.cpp:91`, `ObjectStreamer.cpp:589`,
`StorePipes.cpp:512-525`, `PackageNativeCPU.cpp:1081`), and a 30-bit
integer sign-extended *is* the same value as a 62-bit one - so **no
reader changes**.  Outbound, every writer truncates silently:

| Boundary | Outbound site(s) | What happens to a wide integer today | Class |
|---|---|---|---|
| NSOF (all ~25 users: beaming, docking, endpoints, frame parts, books) | `ObjectStreamer.cpp:175, 294` (`LongToPipe`, xlong of the raw Ref) | low 32 bits written; reads back as a different integer; on Windows the `long` parameter cuts it even earlier | B + D (policy); the `long` parameter M |
| Store objects | `StoreObject.cpp:751` (`<< (long) ref`), `StorePipes.cpp:286-298` | low 32 bits | B + D |
| Store object header | `StoreObject.cpp:993` (`_uniqueID` as `Long32`), `:995` (`_modTime & 0x3fffffff`) | narrowed | B (ROM behaviour, keep) |
| Store mod-time masks | `Entries.cpp:381`, `Soups.cpp` ×6, `Tags.cpp` ×3, `Docker.cpp:4293` (`RealClock() & 0x1fffffff`) | already 29 bits | B, keep |
| Soup 'int keys | `Soups.cpp:2598-2602` (`KeyToSKey`), `:2654`, `:1304`; `SoupIndex.cpp:130-167` (4-byte BE key), `:492-496` (`LongKeyCompare`, a subtraction that overflows for full 32-bit keys) | narrowed to **32** bits, while the entry is narrowed to **30** - the mismatch behind the old To Do roll-over bug | B + D |
| Dock protocol | `Docker.cpp:1329, 2227, 3454, 3574` | truncated | B + D |
| Endpoint data forms / marshalling | `comms/Translators.cpp:298, 315, 395`; `MarshalOut.cpp:51-138, 410`; `Marshalling.cpp:189-244` | truncated to the C field; inbound a device `long` would now keep its top 2 bits (and an unsigned one is still sign-extended - D) | B (device C types), D for range errors |
| ARM interpreter (package native code) | `TNativeWorld::ToARM`, `PackageNativeCPU.cpp:1046-1049` - every Ref into ARM code passes here (≈91 call sites, 4 integer glue routines `Glue_MakeInt`/`RefToInt`/`CoerceToInt`/`RINTError`) | low 32 bits, tag kept: the ARM code sees a wrapped 30-bit integer | B + D (one check covers all paths) |
| NIE re-expressions | `thirdparty/nie/NIERuntime.cpp:100-104, 206-210` (`NIEAdd`/`NIESubtract` wrap as the NTK's compiled code does; 4 compare fast paths) | wrap at 30 bits | M (follow the interpreter's new rule); the ARM-run oracle `NEWTON_NIE_ON_CPU` then differs above 2^29 by design |
| Large binaries | `LBData::fLength` is `long` (`LargeBinaries.h:60`); roots hold BE words | 2 GB limit | only matters with (b) |
| `tools/dock/nsof.py` | `_encode`: `(obj<<2)&0xFFFFFFFF` | Python raises `struct.error` for ≥2^29 | B, desktop side |

About 40 integer- and size-specific outbound sites in all, in 7 formats.

### 2.5 Dates and the year-2010 fix

`FTimeInSeconds` is `MAKEINT((long)(Long32)(RealClockSeconds() -
0xa7693a00))`; the owner-approved DEVIATION
`ClockSecondsFromScriptSeconds` (`intl/Dates.cpp:1178-1187`) reads a
script's seconds modulo 2^30 so the wrapped values of 2010-2026 still
mean the right instant.  Under (a) `TimeInSeconds` answers the true count
and the ROM's own NewtonScript (`TimeInSecondsToTime`'s `46811520 + s div
60`) is right without help - but the fix does **not** simply go away:

1. values already wrapped (negative) live on in soups, packages and
   `lastCommunicationWithDesktop`; only the congruence reading recovers
   them;
2. a true 2026 value (≈1.06e9) does not fit in a store's 30 bits - so the
   moment the ROM's own Clock or Dates code writes one into a soup it hits
   the boundary policy (this is exactly what the spike's alarm failures
   are);
3. the `Long32` cast must become `ULong32` or it overflows in 2061.

So under (a) the time functions need a decided rule: either keep
`TimeInSeconds` answering the 30-bit wrapped value as today (the device's
definition; everything stays storable), or answer the true count and let
the store boundary's policy narrow it consistently.  **D**, and the
biggest semantic question in the whole change.  Minutes since 1904
(`Time()`, about 6.5e7) are unaffected.

---

## 3. Compatibility: what must stay 32-bit

| Thing | Verdict |
|---|---|
| Existing stores (host store files, flash images, Einstein images, cards) | Must stay readable and writable unchanged.  Keep the store object format and 4-byte 'int keys; wide integers handled by the outbound policy. |
| Third-party packages (compiled for 32-bit NewtonScript) | Their bytecode and literals are 30-bit and import correctly unchanged.  Their *behaviour* may differ only where they relied on wrap (hash/checksum code written in NewtonScript, `TimeInSeconds` arithmetic, negative-hex flag words).  Run the third-party app suite in both flavours to find out. |
| Package native code on `src/armcpu` | Must stay 32-bit: ARM code does `ASR #2` on its own.  Every Ref crosses at `ToARM`; an integer that does not fit there must be refused (throw) or narrowed, never passed as garbage. |
| NIE re-expressions | Follow the interpreter (they are re-expressions of compiled NewtonScript); accept that the ARM oracle differs above 2^29. |
| NSOF to and from real Newtons, NCU/NTK, `tools/dock`, beaming (Sharp IR, IrDA), the NTK inspector | Must stay byte-identical for values that fit.  No new tags. |
| The ROM's own bytecode and objects, and the romsrc object file | Unchanged; imported values are already correctly sign-extended.  The ROM's 30-bit sentinels (536870911 as "forever", 34 files) keep working for minute-valued dates. |
| Device-side C templates (endpoint options, marshalling) | Must stay the C type's width; a wide integer into a 4-byte field is narrowed or refused by the same policy. |

---

## 4. The spike (branch `ns64-spike`, labelled, not for merging)

### 4.1 What it changed

A compile-time switch, `-DNEWTON_NS64=1` in `CMAKE_CXX_FLAGS` (off by
default, so the same tree builds the faithful machine unchanged):

* `MAKEINT` shifts the full Ref instead of cutting to 32 bits;
* `RINT` returns `Ref` instead of `long`;
* `WordRef` returns its argument; the multiply and divide fast paths work
  in the Ref's width;
* (second commit) `views/Commands.cpp`'s `ParameterRef`/`ParameterValue`
  simply make and read an integer.

`ddk/objects.h` was edited directly for the spike; the real change goes
through `sync_ddk_headers.py`.  Five hunks, about 30 lines.

### 4.2 What it measured

Both trees built with the zig toolchain on Windows, same commit, suite
run with `ctest -j 12`:

| Build | Compile errors | ctest |
|---|---|---|
| baseline (flag off) | 0 | **394 / 394** pass |
| `NEWTON_NS64=1` | **0** (the warning count is unchanged, 441 vs 440: nothing flags the silent `long` narrowing) | **260 / 394** - 134 fail |
| `NEWTON_NS64=1` + the `Commands.cpp` fix | 0 | **382 / 394** - 12 fail |

*One* host-side fix removed 122 of the 134 failures.  The command
parameter carried either a number or a host pointer and told them apart
by whether the value fitted in a Newton word; with 62-bit integers every
host pointer fits, so `ParameterRef` made an integer and `ParameterValue`
read it as a pointer, and every tap crashed (80 tests in
`TUnitPublic::Stroke`, 21 in `TView::RealDoCommand`, the rest their
dependents).  A search for the same idiom (`value == (Ref) (int) value`,
`RVALUE(x) == x`) finds no other site.

The 12 that remain, by cause:

| Test | Cause | Expected? |
|---|---|---|
| `frames.Strings` (7 assertions) | `TestThirtyBitIntegers` pins `536870911 + 1 = -536870912` etc.; `ExtractLong` no longer throws; `0x3fffffff` is no longer -1 | yes - tests of the 30-bit rule itself |
| `stores.Soups` (3) | `TestIndexKeysSurviveTheStore` pins "MAKEINT answers the 30 bits the store will hold"; then a wide key throws `kNSErrKeySizeTooBig` (-48022) out of the index | yes - the boundary policy (4.3) |
| `intl.Dates` (2), `host.NewtonYear2010`, `.romBug` | `TimeInSeconds` is no longer negative; the ROM-bug arithmetic flavour no longer has a bug to show | yes - 2.5 |
| `host.NewtonWalkthrough2` (+ `.restart` not run), `host.NewtonAppNewtsCapeHelpers` | an alarm keyed on a true `TimeInSeconds` value reaches the store's index and throws -48022 | yes - 2.5 point 2 |
| `host.NewtonAlignPen.restart`, `host.NewtonNetHopper`, `NetHopperJPEG`, `NetHopperNewtsCape` | reproducible serially; probably the boundary (4.4) | not yet diagnosed |

Nothing failed in the compiler, the interpreter's own tests, QuickDraw,
the views' unit tests, recognition, ink, comms, the ARM interpreter's
unit tests (Mahjongg and NewtHack run their native code and pass), the
romsrc round trips or the screen comparisons.

### 4.3 What the spike says about the boundary policy

Every failure that is not a test of the 30-bit rule itself is a wide
value reaching a 32-bit boundary.  There are three possible policies:

1. **narrow consistently** - wrap to 30 bits at *every* outbound site,
   with the soup key path narrowed the same way as the entry (today the
   key is cut to 32 bits and the entry to 30, which is the To Do
   roll-over bug).  The store then holds exactly what the device would
   have computed; a store written by the 64-bit flavour is a valid store
   for the faithful one.  Silent, but compatible;
2. **refuse** - throw (a new `kNSErrIntegerTooWide`, or reuse
   `kNSErrIntegerTooLarge`).  Honest, but the ROM's own applications then
   fail where they store a time (exactly the spike's alarms);
3. **spill** - write a wide integer as a `'real` (exact to 2^53, readable
   by a real Newton as a real) or a classed 8-byte binary.  Keeps the
   value, changes its class for every other reader.

Recommendation: one helper (`NarrowRefForDevice(ref, where)`) used by all
≈40 outbound sites, implementing policy 1 by default, with a trace
(`NEWTON_TRACE_NARROW`) and a strict mode (policy 2) for development.
Policy 3 only if a concrete need appears.

### 4.4 Not yet diagnosed

Rerun serially, `host.NewtonAlignPen.restart` and the three
NetHopper/NewtsCape browsing tests fail again, so they are caused by the
flavour and not by load:

* `NewtonAlignPen.restart`: after the restart the calibration screen is
  not shown and the pen is not aligned.  The calibration is kept in the
  System soup's "Calibration" entry (and handed across the restart in
  the environment), so the likely cause is a value wider than 30 bits
  narrowed on its way to the store or the environment - the boundary
  again.
* `NewtonNetHopper`, `NewtonNetHopperJPEG`, `NewtonNetHopperNewtsCape`:
  the page never arrives; in the parallel run the host watchdog reported
  the `dnst` task holding the baton with nothing running for 10 seconds.
  NetHopper's own code runs on `src/armcpu`, and the NIE's re-expressions
  carry IPv4 addresses and similar 32-bit quantities as integers, so the
  likely cause is a wide integer crossing `ToARM` or an NIE fast path.

Both are step S1's first job.

---

## 5. A staged plan (branch `ns64`)

Each step leaves the tree building in **both** flavours and the faithful
suite at 394/394 with byte-identical behaviour; that suite is the oracle
for "nothing else changed".

| Step | What | Leaves | Size |
|---|---|---|---|
| **S0** *(could land on `main`: no behaviour change)* | `RINT` answers `Long`; `IntegerString`/printer formats, `LongToPipe`, `TStoreWritePipe::operator<<(long)` take `Long`; `NEWTON_NS64` as a CMake option (`-DNEWTON_NS64=ON`) driving `MAKEINT`/`WordRef`/the fast paths through `sync_ddk_headers.py`; a `ctest` label so a flavour's expectations can differ | faithful 394/394 unchanged; NS64 builds | 1-2 days |
| **S1** | The host's own assumptions: the `Commands.cpp` discriminator (done in the spike), diagnose the four 4.4 tests, any other "fits in a word" idiom found | NS64 ≈ 382/394, every failure explained | 1-2 days |
| **S2** | The boundary policy (4.3): `NarrowRefForDevice` at the ≈40 outbound sites (NSOF, store object, header, soup keys incl. `LongKeyCompare`, Docker, Translators/marshalling, `ToARM`), `nsof.py` and `romsrc.py` rejecting what they cannot hold; tests that a wide integer stored, streamed, docked and passed to ARM code comes back as the device would have it (or throws in strict mode) | stores/NSOF byte-identical across flavours for values that fit | 3-5 days |
| **S3** | Semantics that change on purpose: the lexer (`strtoll`, 2^61, the negative-hex idiom), `ExtractLong`, `FCeiling`/`FFloor`/`CoerceToInt` range checks, `Ticks`, `TimeInSeconds` and the year-2010 reading (2.5), NIE fast paths; per-flavour expectations in `test_Strings`, `test_Soups`, `test_Dates`, `NewtonYear2010`; new `TestSixtyTwoBitIntegers` | NS64 suite green with its own expectations | 3-5 days |
| **S4** | The Windows narrowing audit (2.2): `-Wshorten-64-to-32` filtered to `RINT`/`RVALUE`/`CoerceToInt` lines, each site either `Long`, a range-checked narrowing, or left (small by contract) with no change | no silent narrowing of anything that can be wide | 1-2 weeks, mechanical |
| **S5** *(optional)* | Objects over 16 MB: `kObjMaxSize` to the header word's width, the heap's sizes `Long`, `LBData::fLength` `Long`; restores the ROM's array-length limit on the host as a side effect | in-memory only; formats unchanged | 2-3 days |
| **S6** *(not recommended)* | Wide integers on disk/wire: a new soup key type in the index info, an NSOF spill or tag, an object-file version | breaks interchange | weeks, and a decision first |

Recommended scope: **S0-S4** (about 3-5 weeks), S5 if large objects are
wanted, S6 not at all.

### 5.1 Compile-time or run-time switch?

* **Compile-time** (`NEWTON_NS64`): no cost in the hot paths, two build
  directories, the suite run twice in CI.  The flavours cannot be mixed
  in one process, which is also the safe property.  Recommended.
* **Run-time** (an environment variable, as `NEWTON_ROM_2010_BUG` is):
  `MAKEINT` becomes a test of a global in 1741 places and the
  interpreter's add/subtract fast paths gain a branch (a few per cent in
  `drawbench`-style loops at most, to be measured with
  `tools/host/profile.py`).  One binary, one suite run per mode.  Its
  real cost is that every boundary and every test must ask the mode, and
  a value computed under one mode can meet code that assumes the other.
  Worth it only if the owner wants to switch on a running build.

With policy 1 at the boundaries, stores, packages and NSOF are
interchangeable between the two flavours either way.

### 5.2 Test strategy

* The **faithful build is the oracle**: it must stay 394/394 and its
  screens identical (`tools/host/samescreen.py`) at every step.
* The **64-bit build runs the same suite**; tests that pin the 30-bit rule
  get per-flavour expectations (a handful: `test_Strings`, `test_Soups`,
  `test_Dates`, `NewtonYear2010`), never deletions.
* **New tests** for the widened behaviour: arithmetic at and beyond
  ±2^29 and ±2^61, literals, `ExtractLong`, real↔integer conversions at
  the range ends; each boundary with a wide value (store round trip and
  index query, NSOF to `nsof.py` and back, dock, beam between two hosts,
  ARM native call, marshalling into a C long); `TimeInSeconds` across the
  2010 wrap and the 2061 `Long32` edge.
* **Cross-flavour**: a store written by the 64-bit build mounted by the
  faithful one (`host.*.restart`-style ctests) and the reverse.

---

## 6. Risks

* **Silent narrowing on Windows** (2.2): nothing warns, and the spike
  passing most of the suite is partly *because* most values are small.  A
  value that is wide only in the field (a time, an id, a hash, a
  checksum) will be cut somewhere S4 has not reached yet.
* **ROM and third-party NewtonScript that relies on wrap** - hashing,
  checksums, `TimeInSeconds` arithmetic, negative-hex flag words compared
  with `=`.  Nothing in the suite does visibly (382/394), but the suite is
  not the field.  The third-party package fixtures run in both flavours
  are the best detector available.
* **Time** (2.5): whichever rule is chosen, values written by one rule are
  read by code written for the other.
* **Divergence from the oracle tools**: the ARM-run oracles
  (`NEWTON_NIE_ON_CPU`, `compression.LZOracle`, the DES oracle) stay
  30-bit, so for the 64-bit flavour they are oracles only for values that
  fit.
* **Maintenance**: every future reconstruction must be right in both
  flavours; `MAKEINT`'s width can no longer be assumed by a reader of the
  code, and new outbound sites must go through the policy helper.

---

## Appendix: how the numbers were obtained

* Counts: `grep -rEo` over `src/` for each macro; the `RINT` receiver
  table by patterns of the form `\b(int|short|long|Long|ULong|...)\s+\w+\s*=\s*RINT\(`
  per top-level directory, excluding `tests/`.
* Boundaries: reading `stores/ObjectStreamer.cpp`, `StoreObject.cpp`,
  `StorePipes.cpp`, `SoupIndex.cpp`, `Soups.cpp`, `comms/Docker.cpp`,
  `comms/Translators.cpp`, `frames/Marshalling.cpp`, `MarshalOut.cpp`,
  `armcpu/PackageNativeCPU.cpp`, `thirdparty/nie/NIERuntime.cpp`,
  `tools/newton-rom/analysis/romsrc.py`, `tools/dock/nsof.py`.
* The spike: branch `ns64-spike`, configured as
  `cmake -G Ninja -S <spike>/src -B tmp/build-ns64 -DCMAKE_TOOLCHAIN_FILE=src/cmake/zig-toolchain.cmake -DNEWTON_ROM_BUILD=build/MP2x00US -DCMAKE_CXX_FLAGS=-DNEWTON_NS64=1`,
  and the same without the flag for the baseline; `ctest -j 12`.
* ROM sentinels: `grep -rE '\b536870911\b' romsrc --include=*.ns`.
