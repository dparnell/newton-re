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
| `TimeInSeconds` | wrapped (negative since January 2010) | the true count - "Time" below |
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

## Time

`TimeInSeconds()` counts seconds from 1993.  The ROM's passed 2^29 on 5
January 2010 and went negative (the year-2010 bug, which the faithful build
fixes on the reading side - `docs/intl/year-2010.md`).  A 62-bit integer
holds the true count (about 1.06e9 in 2026).

**The owner's decision (2026-10-02): the 64-bit flavour's time is 64-bit
aware** - `TimeInSeconds()` answers the true count, and a time is narrowed
by the boundary policy where it is stored or sent.  The device's wrapped
value stays available for comparison as `NEWTON_NS64_TIME=device` (read
once).

What that was checked against, and what it found:

* **Arithmetic on times is right without help**: `TimeInSeconds() + 3600`
  is an hour from now, `TotalSeconds(Date(Time()))` and `TimeInSeconds()`
  agree, and the ROM's own `TimeInSecondsToTime` and `DateFromSeconds` are
  right; `host.NewtonYear2010` passes and `.romBug` cannot show the ROM's
  overflow (it needs 30-bit sums that wrap).
* **A time read back from a store is the device's 30 bits.**  Whatever
  meets a live time after a store has to read it by congruence.  The
  year-2010 fix's reading does exactly that - `ClockSecondsFromScriptSeconds`
  takes any value within 2^29 seconds (17 years) of now, wrapped or not - so
  everything that goes through it is right: `SetSysAlarm`, `SetTimeInSeconds`,
  `TimeInSecondsToTime`, the values already wrapped in soups and packages
  from 2010-2026.
* **The one place the ROM compares a stored time with a live one is the
  alarm queue.**  `SetNextAlarm` (`Rbuiltinfunctions.SetNextAlarm`) walks
  the SystemAlarmSoup by its TimeInSeconds index and treats every entry
  whose key is `<= TimeInSeconds()` as due; the key is the stored 30 bits,
  so every alarm was due the moment it was added - the Clock's timer never
  rang (`host.NewtonWalkthrough2`, `.ROM`), and NewtHack's dungeon came out
  different because the alarm loop ran meanwhile (`host.NewtonAppNewtHack`).
  Under the true count the host replaces `SetNextAlarm` with the ROM's own
  logic reading the key through `HostWidenTimeInSeconds` (the congruence
  reading, a host global) - `intl/Dates.cpp`, installed with the year-2010
  fix.  The index's order needs nothing: the stored keys run in order from
  2010 until they wrap in 2044.
* **The spike's -48022** (an index throw on an alarm keyed on the true time)
  is gone: the soup key and the entry are now narrowed the same way.
* Not affected: dates, meetings and repeating meetings (minutes since 1904,
  about 6.5e7 - they fit 30 bits until the year 2924), sort keys (text),
  the walkthroughs' date checks, the Dock's `lastCommunicationWithDesktop`
  (kept in memory and compared there).
* **Left as it is, and recorded:** a third-party application that stores
  `TimeInSeconds()` in a soup and compares it with `TimeInSeconds()` later
  will see a wrapped value, as on the device after 2010 - there is no way to
  tell a stored time from any other integer.  The ListView's topic `unique`
  (`TimeInSeconds() * 4 + ...`) is narrowed when stored but used only as an
  identity among stored topics.  The Clock timer's `alarmClockTimer` in the
  user configuration reads back wrapped after a restart, so the timer slip
  shows 0 left for a timer running across a restart (the alarm itself still
  rings).  The general cure is storing wide integers, the study's S6.
* The count passes 2^31 in 2061; it is taken unsigned, so it is good to 2129.

`NEWTON_NS64_TIME=device` answers the device's wrapped value instead: every
time a script holds is then one a store can hold, but arithmetic on times no
longer wraps the way the device's did, so `TotalSeconds(Date(Time()))` and
`TimeInSeconds()` agree only modulo 2^30 (`test_Dates` checks it that way
under it).

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
