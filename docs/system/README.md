# The machine itself (`src/system/`, `src/hal/Power.h`)

What a script may ask of the machine it is running on: its serial
number, what it is made of (Gestalt), how much memory is left, how the
batteries are doing, the backlight, and the power switch.  These are
frames code - they answer Refs - over hardware a host has not got, so
the hardware sits behind `hal/`, and `system/` is the script's view of
it.

The ROM keeps them together at 0x00201700-0x00204200, between the
gestalt selectors and the flash driver, and they all reach the same two
places: the **power manager**, a task the system talks to over an RPC,
and the **screen driver**, through QuickDraw's `SetGrafInfo`.

## Asking the power manager

`GetBatteryStatus` 0x002037bc is the whole of the battery side.  It
sends a `'newt`/`'pg&e` RPC to the port `GetPowerPort` answers, with a
command word in the event:

| command | what it asks for |
|---|---|
| 4 | the reading the manager already has |
| 5 | a reading taken now |
| 6 | how many batteries there are (`FBatteryCount` 0x00203510) |
| 7 | the kind of cells a battery holds (`SetBatteryType` 0x002035d4) |

The reply carries a `PowerPlantStatus` - 0x34 bytes, thirteen long
words, the analogue ones Fixed - which `BlockMove` copies out.  A field
the machine cannot measure reads -1, and that is what the natives above
it test for.

DEVIATION: the power manager is NOT YET RECONSTRUCTED, so the
reconstruction asks `hal/Power.h` instead (`GetPowerPlantStatus`,
`GetPowerPlantCount`, `SetPowerPlantBatteryType`).  The host's answer
(`hal/host/Power.cpp`) is a plain machine on fresh alkaline cells: a
full charge, nothing being drawn, no mains, at room temperature.  It
remembers the cell type it is told, because that is the one thing a
script can change and read back.

## What a script sees

`BatteryStatusHelper` 0x002038a0 builds the frame both status natives
answer.  It clones `canonicalBatteryStatus` and fills in every reading
that is not -1, so a slot for something the machine cannot measure stays
nil rather than reading -1.  The two enumerations come out as symbols -
`'alkaline`, `'nicd`, `'nimh`, `'lithium`; `'discharging`,
`'trickleCharging`, `'fastCharging`, `'fullyCharged`,
`'preliminaryCharge`, `'trickleChargeContinuous`, `'deepToast` - and a
value outside them is passed through as the integer it was.  Both
natives are two instructions and a tail call into it:

| native | ROM | |
|---|---|---|
| `BatteryStatus(which)` | 0x00203db8 | the reading already held |
| `BatteryRawStatus(which)` | 0x002017d4 | a reading taken now |

`BatteryLevel(what)` 0x00201804 is the same reading with one number
taken out of it, which is what a gauge watches rather than building a
whole frame each time.  `what` is 0 for the main battery's capacity, 1
for the second battery's, 2 for the temperature and 3 for the main
battery's capacity again.  0 and 3 differ only in their fallback: 0
starts from `gLastBatteryLevel` 0x0c104c48 - the level last read - so
that a reading which cannot be taken answers the one before it, where 3
answers nil.  On the mains the main battery reads 100 whatever the cells
say, which is what keeps the gauge full while the machine is plugged in.

The temperature is answered as the Fixed the power manager sent, made an
integer without being scaled - so `BatteryLevel(2)` is degrees times
65536, not degrees.  The ROM's own scripts never ask for it.

`SetBatteryType(which, type)` 0x00203684 takes one of the four symbols,
nil for "not known", or the number behind them; anything else is refused
with nil rather than an error.

`MinimumBatteryCheck` 0x002019a0 has no name in the ROM's native table,
so only the ROM's own scripts reach it: while the machine is off the
mains and the battery has fallen to the level called dead, it sleeps.

## The backlight

`BackLightStatus()` 0x00201a0c and `BackLight(on)` 0x00201a3c are the
switch.  Both go through QuickDraw - `GetGrafInfo`/`SetGrafInfo`
selector 5, which is the screen driver's feature 2 - and both answer
whether the light *was* on, as a boolean.

Switching it on sends the root view `:EventPause(true)` first.  That is
the tickle which says the machine has just been used
(`docs/newt/README.md`), and without it the machine would count as
having been left alone the moment the light came on and start counting
towards powering itself off again.

They live in `system/SystemNatives.cpp` rather than with the screen's
own natives because that is where the ROM keeps them, and because
`FBackLight` sends a message to the root view, which the QuickDraw
library is below.

## Going to sleep

`SleepUntilNextWakeup` 0x002018f8 is the sleep itself: the backlight
off, `CyclePower`, and then - if anything came back from it - the hard
keymap cleared, so a key held down through the sleep is not read as a
keypress, and the contrast set again from the preference.  What woke the
machine comes back as a power event word, which
`TVoyagerPlatform::TranslatePowerEvent` 0x0026ca40 turns into one of the
reasons in `hal/Power.h`.

`PowerOff()` 0x00201b00 is the script-facing one and is in
`newt/NewtWorld.cpp`, because its last act is to note the time in
`gLastWakeupTime` 0x0c104c4c - the neighbouring word to
`gLastBatteryLevel`, and the one that holds the automatic power-off off
until the machine has been left alone again.

DEVIATION: a host cannot power itself down, so `CyclePower` does not
sleep and comes straight back with nothing to report.

## Memory

`GetHeapStats(options)` 0x00202ff4 walks the pointer and handle heaps a
block at a time (`GetActualHeapInfo` 0x00202f08) and asks the frames
heap for its free space; `docs/memory/README.md` has the rest.

## Tested by

`newt.Newt` boots the whole system and drives these through the 'newt
port: the battery frame and its raw twin, the three `BatteryLevel`
questions, the cell type set and read back, the backlight switched on
and off, and the seeded random generator repeating itself.

## NOT YET

The power manager itself and the platform driver under it
(`TVoyagerPlatform`, which implements the `TPlatformDriver` protocol in
eighteen dispatch slots and is almost all raw register writes);
`VersionString` 0x00146cb8 for the gestalt's `romVersionString`;
`GetSystemReleasable` 0x0014312c for `GetHeapStats`'s
`includeSystemReleasable`.
