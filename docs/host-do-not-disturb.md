# Do not disturb

*Host addition (2026-11-27, the owner's ask): a DEVIATION - the ROM has
nothing like it.  Code: `src/host/HostQuickSheet.ns` ("Do not disturb"),
its users `src/comms/host/HostPhone.ns`, `HostMessages.ns`, `HostMesh.ns`.*

**The switch** is the first line of the quick sheet (a finger pulled down
from the top of the screen - `docs/host-lock-screen.md`), "Do not disturb
on", on every host.  While it is on:

- **A call** from anyone not let through is declined at once - the caller
  goes to voicemail - without a beep (the Phone transport's ring beep is
  skipped for it: its `dismissCall`) and without Phone opening.  It is
  listed in Recents as missed, counted on the Recents tab, and recorded.
  On the T-Deck Max the module's own ring may be heard for the moment
  before the decline reaches it.
- **A text** is filed in Messages unread, with no beep and no alert slip
  (nor a lock-screen notification), and recorded.
- **A mesh message** likewise in Mesh.  A mesh sender has no phone number,
  so no card can let one through.

**Repeated callers** (2026-11-27): a call from a number whose call was
held in the last 3 minutes (`kRepeatMinutes`, by the number's last eight
digits) rings - someone calling twice is likely to matter.  A withheld
number never does.  The Options slip's first switch, "Let a second call
within 3 minutes ring", turns it off (`hostDNDRepeatOff`); on by default.
Only calls: texts are never let through for repeating.

**The schedule** (2026-11-27): the sheet's **Options** button, beside the
switch, opens a slip - repeated callers' switch, then "On every day, as below", From and Until with - and
+ a quarter of an hour apart, each change kept at once.  Every day at
From it turns Do not disturb on, and at Until off again (the summary
shown; the screen locked, a notification on the lock screen that shows it
at the unlock) - but only if the schedule turned it on.  It acts each time
a time is *crossed*, not on whether it is within the hours: turned off by
hand at night, it stays off until the next night; turned on by hand in the
day, the morning's Until does not turn it off.  The schedule is followed
by one delayed call for the next of the two times, and checked again at
each unlock and each opening of the sheet, so a machine that slept through
a time catches up.  Kept in the user configuration: `hostDNDSchedule`
`{on, start, stop}` (minutes of the day), `hostDNDLastEdge` (the time last
acted on), `hostDNDBySchedule`.

**Let through**: a Names card whose slot `hostDNDAllowed` is true.  Set:
- in **Names**: its status bar has a **DND** button (the ROM's
  `RegAuxButton` - Names lists a package's buttons in its status bar), a
  picker for the card shown, "Allow during Do not disturb", ticked when it
  is;
- in **Phone's Contacts**, a long press on the card, "Allow during Do not
  disturb" (again: "Don't allow ..."); the list says "DND allowed" beside
  its number.  A call or text from any of that card's numbers (matched by
the last eight digits) rings or beeps as ever.

**The summary**: turned off, a slip lists what came in meanwhile - "Since
9:29 pm: 1 missed call, 1 text.", then each, newest first, its time, what
it was in bold and its words below, as many as fit and "and N more".  It is
shown even when nothing came ("Nothing came in.").

**Kept** in the user configuration, so a restart in the middle loses
neither the setting nor the record: `hostDoNotDisturb`, `hostDNDSince`
(when it was turned on, minutes as `Time()`), `hostDNDLog` (at most 100
events, the oldest dropped).

## The interface

Global functions, defined at boot by the quick sheet's `Start`; an
application that may run without them checks `GetGlobalFn('DoNotDisturbHolds)`
first.

| function | returns | what it does |
|---|---|---|
| `DoNotDisturbIsOn()` | `true` or `nil` | whether it is on |
| `DoNotDisturbSet(on)` | whether it is on now | turns it on (the record begun afresh) or off (the summary shown) |
| `DoNotDisturbHolds(number)` | `true` or `nil` | whether news from this number (a string; `nil` when there is none) is to be kept quiet: on, and no card let through has the number |
| `DoNotDisturbHoldsCall(number)` | `true` or `nil` | the same for a call, a repeated caller let through |
| `DoNotDisturbRecord(appSymbol, event)` | `true` | records something kept quiet: `event` is `{kind, title, text, number}` (`number` optional: a held call's, for repeated callers) - `kind` `'missedCall`, `'text` or `'mesh` (any other symbol is counted as "notifications"), `title` in bold in the summary, `text` below it |
| `DoNotDisturbHeld()` | an array, oldest first | what has been recorded: `{appSymbol, kind, title, text, time}` (copies) |
| `DoNotDisturbAllow(card, allow)` | whether it is now | a Names card let through (`hostDNDAllowed`) or not, the entry saved |
| `DoNotDisturbSchedule()` | `{on, start, stop}` | the schedule (minutes of the day; 10 pm to 7 am, off, when none was set) |
| `DoNotDisturbSetSchedule(schedule)` | the schedule kept | sets it, and acts on the latest time crossed at once |

The pattern (Messages' `Received`):

```
local held := GetGlobalFn('DoNotDisturbHolds) and call GetGlobalFn('DoNotDisturbHolds) with (number);
if held then
    call GetGlobalFn('DoNotDisturbRecord) with (appSymbol, {kind: 'text, title: "Text from " & who, text: text})
else
    ... beep, slip, lock-screen notification as before
```

Don't treat what was held as seen: Messages and Mesh leave it unread.

ctest: `host.NewtonPhone.<size>` (`src/host/demo/phone.ns`): a held
stranger calling again at once ringing, and held with the switch off; the
schedule's start turning it on and its stop off (the summary), a hand's
off kept through a check; Names' DND button letting Bob through; on, a
stranger's call declined unseen (Phone not opened, missed in Recents) and
a text with no alert, both recorded; Alice's card let through and her call
ringing; off, the summary saying "1 missed call, 1 text." with two lines,
and the record cleared.

## Cards and attributes of one's own

A Names card is a soup entry - a frame - and any application may add
slots of its own to it: Names' editors write the slot they show, so a slot
it does not know stays on the card, and goes with the soup entry in a
backup (a card sent as a vCard keeps only what that format holds; not
checked: beaming).  This is how
`hostDNDAllowed` is kept.  A package's slot should have a name no other
will take (Apple's advice: the package's signature in it,
`'|allowed:MySig|`); the `host` prefix is the host's.  What NewtonOS 2.1's
Names offers beyond that, for a package to show or edit its slots:
`RegAuxButton(symbol, {destApp: 'cardfile, ...})` (a button in its status
bar, as here), a layout of its own in the Show picker (`AddLayout`,
`RegisterViewDef` on the card's data class), and the user's own Custom
fields (their labels in the Names soup's `'customFields` info).

NOT YET: held texts
from a card let through by its name rather than its number.
