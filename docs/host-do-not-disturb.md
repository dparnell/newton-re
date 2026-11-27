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

**Let through**: a Names card whose slot `hostDNDAllowed` is true - set in
Phone's Contacts with a long press on the card, "Allow during Do not
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
| `DoNotDisturbRecord(appSymbol, event)` | `true` | records something kept quiet: `event` is `{kind, title, text}` - `kind` `'missedCall`, `'text` or `'mesh` (any other symbol is counted as "notifications"), `title` in bold in the summary, `text` below it |
| `DoNotDisturbHeld()` | an array, oldest first | what has been recorded: `{appSymbol, kind, title, text, time}` (copies) |

The pattern (Messages' `Received`):

```
local held := GetGlobalFn('DoNotDisturbHolds) and call GetGlobalFn('DoNotDisturbHolds) with (number);
if held then
    call GetGlobalFn('DoNotDisturbRecord) with (appSymbol, {kind: 'text, title: "Text from " & who, text: text})
else
    ... beep, slip, lock-screen notification as before
```

Don't treat what was held as seen: Messages and Mesh leave it unread.

ctest: `host.NewtonPhone.<size>` (`src/host/demo/phone.ns`): on, a
stranger's call declined unseen (Phone not opened, missed in Recents) and
a text with no alert, both recorded; Alice's card let through and her call
ringing; off, the summary saying "1 missed call, 1 text." with two lines,
and the record cleared.

NOT YET: a schedule (on at night); repeated calls let through; held texts
from a card let through by its name rather than its number.
