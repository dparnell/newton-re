# The 64-bit NewtonScript flavour (`NEWTON_NS64`)

The reconstruction's NewtonScript is the ROM's: an integer is a Ref with a
two-bit tag in the ARM's 32-bit word, so it runs from -536870912 to
536870911 and wraps where the ARM wraps (`README.md`, "An integer is thirty
bits, however wide the host's word is").  That is the default build and it
stays the oracle.

`-DNEWTON_NS64=ON` builds a second flavour in which an integer is as wide as
the host's Ref less its tag - 62 bits - while **every persistent and wire
format stays 32-bit**.  It is the owner's choice (2026-10-01) after
`64bit-study.md` (branch `ns64-study`), which measured the change.  The
two flavours are built in separate directories:

```
cmake -G Ninja -S src -B build/host-ns64 -DNEWTON_NS64=ON ...
```

## What changes, and where

| | faithful (default) | `NEWTON_NS64` |
|---|---|---|
| integer range | -2^29 .. 2^29-1 | -2^61 .. 2^61-1 |
| `+ - * div` wrap at | 2^30 (the ARM's word) | 2^62 (the Ref's width) |
| a literal | at most 0x3fffffff; 0x20000000.. is negative | at most 2^61-1, as written |
| `ExtractLong` | throws `kNSErrLongOutOfRange` on a word that does not fit | any signed word |
| `Floor`/`Ceiling` of a whole real | an integer within 30 bits, else a real | within 62 bits |
| `LShift`/`RShift` past the width | (the host's shift) | nothing / the sign |
| `TimeInSeconds` | wrapped (negative since January 2010) | the open question - "Time" below |
| a host pointer as an integer (`AddressToRef`, a command's parameter) | the pointer's bits, told from a number by its width | a plain integer |

The switch is a handful of places, every one marked `NEWTON_NS64`:
`MAKEINT` (`sync_ddk_headers.py`'s `objects.h` patch); the interpreter's
`WordRef` and its multiply/divide fast paths (`Interpreter.cpp`); the lexer
(`Lexer.cpp`); `ExtractLong` (`ArrayNatives.cpp`); `WholeNumberRef`, the
shifts (`Builtins.cpp`); `AddressToRef`/`RefToAddress` (`Objects.cpp`) and
the command parameter (`views/Commands.cpp`).  In both flavours `RINT` and
`CoerceToInt` answer a `Long` (a Ref's width), and the integer-to-text
routines (`IntegerString`, `IntegerStringSpec`, the printer and the
stringer) print a `long long` - the same text for every value the faithful
flavour can hold.

## The 32-bit boundary (`frames/NarrowRef.h`)

A store, a package, an NSOF stream (beaming, docking, endpoints, frame
parts), a soup index key, package native code on `src/armcpu` and a
device-side C field all hold the ARM's word, and keep doing so: a store or a
stream is the same bytes whichever flavour wrote it, and a real MessagePad,
NCU/NTK and Einstein read them.  What crosses goes through one of three
calls:

* `NarrowRef(ref, where)` - a Ref as the ARM's word (an integer cut to 30
  bits): NSOF's immediates (`ObjectStreamer.cpp`), store objects
  (`StoreObject.cpp`), `TNativeWorld::ToARM` (`armcpu/PackageNativeCPU.cpp`);
* `NarrowInteger(value, where)` - an integer as the device's: an entry's
  `_uniqueID`, a soup 'int index key (`Soups.cpp`'s `KeyToSKey`, the alias
  lookup in `SoupNatives.cpp`);
* `NarrowToWord(value, where)` - a 32-bit device field, signed or
  unsigned: the dock protocol's words (`Docker.cpp`), an endpoint's number
  form (`comms/Translators.cpp`), a marshalled word (`MarshalOut.cpp`),
  `CoerceToInt` for ARM code.

The policy is one, the same at every site:

* **wrap** (the default): the low 30 bits, sign-extended - what the
  MessagePad itself would have computed.  A store holds what the device would
  have held, and an index key and the entry it indexes agree.  (Before the
  policy the key was cut to 32 bits and the entry to 30: the system's alarms
  and NetHopper's request queue went into a soup under one key and were
  looked for under another - `host.NewtonAlignPen.restart` and the NetHopper
  tests.)
* **strict**: `NEWTON_NS64_STRICT` in the environment makes a crossing throw
  `kNSErrLongOutOfRange` with the value - the way to find where a wide value
  meets the boundary.  `NEWTON_TRACE_NARROW` prints each narrowing and its
  site.

Inbound nothing changes: every reader already sign-extends the 32-bit word,
and a 30-bit integer sign-extended is the same value in 62 bits.  A device
`ULong` read back is sign-extended too, as the device reads it: software
declares error codes `'ulong` (the NIE answers a failed DNS lookup -60791
through one), so zero-extending it answered 4294906505 and failed
`host.NewtonInetHostSetup.restart`.

`romsrc.py` and `tools/dock/nsof.py` refuse an integer the ROM's 30 bits
cannot hold rather than wrap it.

In the faithful flavour the three are the identity, inline.

## Time - the open decision

`TimeInSeconds()` counts seconds from 1993.  The ROM's passed 2^29 on 5
January 2010 and went negative (the year-2010 bug, which the faithful build
fixes on the reading side - `docs/intl/year-2010.md`).  A 62-bit integer
holds the true count (about 1.06e9 in 2026).  Which one a script gets is the
owner's decision; both are built in, chosen at run time by
`NEWTON_NS64_TIME` (read once), so either can be tried:

**`true` - the true count (the default until it is decided).**

* What a script computes with times is right: `TimeInSeconds() + 3600` is an
  hour from now, differences and comparisons need no congruence reading, and
  the ROM's own `TimeInSecondsToTime` is right without the year-2010 fix.
* But a store holds 30 bits.  A time written to a soup comes back as the
  device's wrapped value, and a script that compares the two finds them a
  generation apart.  The ROM's own code does exactly that: the Clock's
  timer's alarm goes into the alarm soup and is compared with
  `TimeInSeconds()` when it is due, and never rings
  (`host.NewtonWalkthrough2`, `.ROM`); NewtHack seeds its generator from the
  time and plays a different dungeon (`host.NewtonAppNewtHack` - the test's
  walk is blocked).  Under this choice those are failures by design until
  times are stored wide (the study's S6, which breaks the formats).
* Values already wrapped (negative ones from 2010-2026, in soups, packages,
  `lastCommunicationWithDesktop`) are still read by the year-2010 fix's
  congruence (`ClockSecondsFromScriptSeconds` takes any value within 2^29
  seconds - 17 years - of now, wrapped or not), so they still mean the
  right instant.
* The count passes 2^31 in 2061; it is taken unsigned, so it is good to 2129.

**`device` - the MessagePad's value, wrapped to 30 bits.**

* Every time a script holds is one a store can hold, so alarms, timers and
  anything else that stores a time and compares it later behave as on the
  device and as in the faithful build (the whole suite passes).
* But arithmetic on times no longer wraps the way the device's did:
  `TotalSeconds(Date(Time()))` is computed by the ROM's NewtonScript from
  the minutes and comes out the true count, so it and `TimeInSeconds()`
  agree only modulo 2^30 (`test_Dates` checks it that way under this
  choice).  Any script mixing the two kinds of seconds sees the gap.
* It keeps the year-2010 fix necessary, exactly as in the faithful build.

Neither is fully consistent while stores are 32-bit: *true* is consistent
in memory and inconsistent with what it stores; *device* is consistent with
what it stores and inconsistent with the rest of its own arithmetic.  The
ROM's year-2010 overflow itself shows through 30-bit arithmetic that wraps,
so with either choice the 64-bit flavour dates a note right even with the
ROM's arithmetic (`NEWTON_ROM_2010_BUG=1`; `host.NewtonYear2010.romBug`'s
expectation is per flavour).

## Tests

The faithful suite is the oracle and stays 402/402 at every step.  The
64-bit build runs the same suite; the tests that pin the 30-bit rule have
per-flavour expectations rather than being dropped:
`test_Strings`' `TestThirtyBitIntegers`/`TestSixtyTwoBitIntegers` and
`ExtractLong`, `test_Compiler`'s too-large literal, `test_Soups`'
`TestIndexKeysSurviveTheStore`, `test_Dates`' year-2010 and `TotalSeconds`,
`host.NewtonYear2010.romBug`.  `test_ObjectStreamer`'s `TestWideIntegers`
checks the policy - the narrowing, NSOF's bytes for a wide integer, a
value that fits unchanged, and the strict throw.

| step | faithful | `NEWTON_NS64` (`true` time) | `NEWTON_NS64_TIME=device` |
|---|---|---|---|
| S0 the switch | 402/402 | 261/402 | - |
| S1 the host's own assumptions | 402/402 | 388/402 | - |
| S2 the boundary policy | 402/402 | 394/402 | - |
| S3 the semantics, per-flavour tests | 402/402 | 398/402 (the timer and NewtHack: time) | 402/402 |
