# The Host preferences panel

The settings of the machine newton runs on that can change while it runs,
in the Newton's own Preferences: Extras, Prefs, **Host** (the monitor
icon).  Added 2026-10-03 at the owner's request, so that things a host had
only as a command-line flag or an environment variable - beaming over the
network, the reMarkable's pen waveform - can be switched on the machine
without restarting newton.

On a desktop newton the page is a line, "The machine newton runs on:", and
one checkbox, "Beam over the network"; a reMarkable adds its panel's
settings.

**Two pages** (2026-10-25): the screen's settings - an e-paper panel's
refreshes (`bigChange`, `cleanCount`, `cleanAfter`, `cleanArea`,
`localClean`, Clear ghosts), the reMarkable's `waveform`, `directPen`,
`touch` and `screenScale`, and `colourScreen` - are on a page of their own,
**Screen**, the rest on **Host** (`HostSettings.ns`'s `kScreenSettings`);
a page with no settings is not registered (a desktop newton has no Screen
page unless it is offered the colour screen).  And a page with more rows
than fit the application area either way up (`(shorter side - 50) div
22`) is split, "Host 2" ... after it.  The ROM's Prefs cannot open a panel
taller than the application area: the roll opens every panel through
`OpenPrefsTo`, whose slip (@809) is a protoFloatNGo with `viewBounds: nil`
set afterwards; the floater's base (@132) takes `viewBounds` out at setup
when its box is off the application area, which leaves the template's nil
and the view is never made (-8505, `kViewErrNoViewBounds`: "exception in a
unit handler" and nothing on the screen).  The T-Deck Pro's one page had
grown to 292 pixels with its e-paper settings and 314 with Auto-lock, on a
screen of 320: it did not open.  Ctest `host.NewtonHostPages` opens every
page at 240x320 and a page split from twenty pretend rows.

## What it offers

Only what applies to the machine newton is running on: the page lists what
the host says it has, so a desktop shows beaming and docking, a reMarkable
its panel's settings as well, and those only when the panel can do them
(a panel says what it can do - `HasPenInk`, `HasDirectPen`; a headless
newton has no panel settings at all).

(The LoRa mesh node's settings - its radio and its power - are the Mesh
application's own Prefs, its "i" button: `docs/host-tdeck-pro-mesh.md`.)

| setting | what it does | where | before the panel |
|---|---|---|---|
| Beam over the network (`lanBeam`) | the built-in IR port's medium the LAN group (239.255.78.119:3681): beam to any newton on the network with it on - off, the port has nobody in front of it (or the `--ir-peer` newton was started with) | every host | `--ir-lan` |
| Dock over the network (`docking`) | the serial port's TCP listener (3679, or `--serial-port`'s) open: a desktop docks with it; off, a desktop connected is cut off as by a cable pulled out | every host | `--serial-port none` |
| GPS receiver on (`gps`, a picker) | the port the GPS receiver newton was given (`--gps`, `hal/host/HostGPS.h`) is plugged into: Not plugged in; Serial port 2 (`'mdem'`, a port of its own - GPS Map's "Serial Port 2", GPS Monitor's "Internal Modem"); or Serial port, the external one, for GPS View, which never asks - there it is the port's cable and Dock over the network takes nobody ([comms/gps.md](comms/gps.md)) | a host started with `--gps` | `--gps-port 1`; serial port 2 by default |
| GPS receiver (`gpsKind`, a picker) | the receiver an NMEA one (sentences all the time) or a DeLorme Tripmate (ASTRAL first: GPS View) | a host started with `--gps` | `--gps-kind tripmate` |
| The GPS on (`gpsPower`) | the GPS receiver powered; off, its supply is switched off to save the battery and its fix forgotten - `HostGPSStatus()` has none (the Mesh application then sends and advertises no position) until it is on again and has a new one (well under a minute with a view of the sky).  The board keeps it in NVS (`gpsOff`), so a receiver switched off is not powered at the next boot | T-Deck Pro | - |
| The 4G module on (`cellPower`) | the cellular module powered; off, it is shut down in order (`AT+CPOF`) and its supply switched off to save the battery - no calls or text messages, the status "no module"; on again, it is configured as after a power cycle (about 15 s to the network).  The board also keeps it in NVS, so a module switched off is not powered at the next boot | T-Deck Pro (4G) | - |
| Ink (`waveform`, a picker) | the ink's waveform: Fast; Pen - AppLoad's *ufast*, xochitl's own pen waveform: quickest, with noticeable ghosting (AppLoad applies it to the whole window); Gray - the UI waveform for everything: slowest, no ghosts | reMarkable (qtfb) | `NEWTON_RM_WAVEFORM`, `NEWTON_RM_INK_MODE=ufast` |
| Read the Marker directly (`directPen`) | the Marker from its own input device, mapped by what AppLoad's pen events show (`remarkable/PenFit.h`) - smooth ink; off, AppLoad's own (bunched) events | reMarkable (qtfb) | `NEWTON_RM_PEN=evdev` |
| A finger is the pen (`touch`) | one finger taps and drags as the pen, with the palm rejection of `docs/host-remarkable.md` | reMarkable | `NEWTON_RM_TOUCH=off` to turn off |
| Screen (next start) (`screenScale`, a picker) | the Newton's screen the panel over 1, 2, 3 or 4 ("2x: 810 x 1080") when newton next starts - AppLoad fixes the framebuffer's size when it is asked for; kept beside the store (below), and the pen's calibration reset to the factory one when the size changes | reMarkable | `--display` |
| Colour screen (next start) (`colourScreen`) | the screen eight bits whose values a palette's, so applications' colours are drawn in colour (`docs/qd/colour.md`) when newton next starts - the depth is fixed when the screen is made; kept beside the store.  Behind `--colour`: listed only once colour has been asked for (started with `--colour`, or the setting already kept), so it can be turned off here; nothing among the usual packages draws in colour | every host with a store file, once asked for | `--colour` |
| Auto-lock (`autoLock`, a picker) | the lock screen shown once nobody has touched the pen or the keys for 1, 2, 5, 10 or 30 minutes; Never (the default) leaves it to the quick sheet or an application ([host-lock-screen.md](host-lock-screen.md)) | every host | - |
| A new page drawn in full (`bigChange`) | a change of more than an eighth of the screen (a new page) drawn by a full refresh - clean, but 3 s and flashing; off (the default), every change is a quick partial one (0.65 s) | T-Deck Pro | `DevicePanelSet("bigChange", 12)` (100 off) |
| Clean up (`cleanCount`, a picker) | after 10, 20, 30 (the default) or 50 quick updates, or never, the ghosts cleaned up by a full refresh at the first two-second pause | T-Deck Pro | `DevicePanelSet("cleanCount", n)` |
| ... and when still (`cleanAfter`, a picker) | the ghosts cleaned up once the screen has been still 5 s, 10 s, 30 s (the default), a minute, or never | T-Deck Pro | `DevicePanelSet("cleanAfter", ms)` |
| ... once (`cleanArea`, a picker) | either clean-up only once the quick updates since the last full refresh have between them redrawn any of the screen, 2 %, 5 % (the default), 10 % or a quarter of it - so a still screen whose only change is the clock's minute is not flashed every minute, sparing the battery | T-Deck Pro | `DevicePanelSet("cleanArea", percent)` |
| Small changes cleaned up (`localClean`, a picker) | less than that redrawn (a clock's digits) cleaned up over just the rectangle redrawn, after 5, 10 (the default), 20 or 50 quick updates at the first two-second pause, or never: two quick refreshes, to it inverted and back (1.3 s), the rest of the screen left alone | T-Deck Pro | `DevicePanelSet("localClean", n)` |
| Clear ghosts (a button) | one flashing redraw of the whole panel, as AppLoad's five-finger tap does (the T-Deck Pro: one full refresh) | reMarkable, T-Deck Pro | - |

A setting changed on the panel takes effect at once (the screen size at the
next start) and is kept; one never touched stays as newton was started.

## How it works

- **The panel** is NewtonScript, `src/host/HostSettings.ns`, compiled into
  newton as a string (`HostSettingsSource.cpp`, by `src/cmake/EmbedText.cmake`)
  and evaluated at boot by `HostInstallSettings` (`src/host/HostSettings.cpp`,
  after the Network Printers panel), which keeps the frame it answers in
  the global `HostSettings:host` and sends it `Start`: the settings kept
  before are given back to the host, and the panel - a
  `protoPrefsRollItem` (`@385`) - is registered with `RegPrefs`, as the
  ROM's own optional settings and `print/host/HostPrinters.ns` are.
- **What it lists** is what `HostSettingsList()` answers: `[{setting,
  label, kind, value}]`, `kind` `'check` (a `protoCheckbox`, `@164`),
  `'choice` (a `protoLabelPicker`, `@190`, with `choices`, the value the
  index of the one chosen: its `TextSetup` shows the host's, its
  `LabelActionScript(index)` calls `Change` - the pattern of the ROM's own
  Fax and Sound panels) or `'button` (a `protoTextButton`, `@226`); a
  setting for the next start has `nextStart`.  The panel's children are made
  by `Start` from that list - the Prefs roll lays out and builds a panel
  from its template, so children added in its own `viewSetupFormScript`
  came too late and it opened empty - and each checkbox reads its value as
  it opens (`viewSetupFormScript`).  A checkbox's `valueChanged` (the
  ROM's protoCheckbox calls it from `toggleCheck` and `SetCheck`,
  `protoCheckboxIcon` in `romsrc/objects/protos/protoCheckboxIcon.ns`) calls `Change`.
- **A change** goes to the host by `HostSetSetting(setting, value)` (==>
  whether it was taken) and is kept where the ROM keeps an application's
  settings: `GetAppPrefs('|HostSettings:host|)`'s `settings` frame, an entry
  of the System soup.  Only a slot that has been set is put back at boot.
- **Beaming over the network** switches the installed IR chip's medium:
  `HostIRChipSetPeer` (`hal/host/HostIRChip.h`) closes the TCP peer,
  listener or group the chip had and opens the new one, the chip staying
  registered as 'infr.  It is called from a native, so with the host's
  baton held - as the chip's interrupt source runs - and the two never
  meet.  On is the LAN spec newton was given (`--ir-lan [port]`,
  `--ir-lan-interface`), else plain `lan`; off is the `--ir-peer` newton was
  given, else nobody (`HostSettingsSetBeamPeers`, from `newton.cpp`).
- **Docking over the network** opens and closes the serial chip's TCP
  listener (`HostSerialChipSetListening`, `hal/host/HostSerialChip.h`): off
  closes the desktop's connection too (the tool sees the carrier go) and
  drops what was on its way; on listens on the port the chip had, or makes
  and registers the chip on 3679 when newton was started with
  `--serial-port none`.  With the baton held, as beaming's.
- **The screen size** is kept in `<store>.host` (`screenScale=N`, beside
  `displaySide=`, the display's longer side at the last start), read before
  the display is made (`HostSettingsReadStartup` tells the window
  `startScale`, and `HostWindowPreferredDisplay` makes the display the
  panel's size over it).  When the longer side has changed since the last
  start, the System soup's "Calibration" entry is given the factory
  calibration for the new size (and for all four orientations) just before
  it is read back (`gNewtHostBeforeCalibration`, `newt/NewtWorld.cpp`):
  the host's panel reads 8, 4, 2 or 1 raw units to the pixel by the longer
  side, so a calibration kept at another size is out by a factor of two or
  more, while the factory one is exact.  Only a window with a screen size to
  choose keeps the file; a desktop's stores are left as they were.  The reset
  is made of calls (`GetStores`, `GetSoup`, `Query`, `Entry`,
  `GetCalibration`, `EntryChange`), not compiled NewtonScript: the hook runs
  inside the newt world's MainConstructor, where compiling a block failed
  (`evt.ex.fr.comp` - the owner's first change to 4x kept the 2x
  calibration, the pen out by half).  The new side is kept only once the
  reset has run, so one that fails is tried again at the next start (or at
  boot, when Setup is still to run and calibrates afresh itself).
- **The window's settings** go through `HostWindowOption` /
  `HostWindowSetOption` (`src/host/HostWindow.h`): a window answers only
  the ones it has (a desktop window none).  The reMarkable's keeps them in
  atomics its thread reads (`gTouchIsPen`, `gPenInkAsked`, `gClearAsked` in
  `remarkable/HostWindow.cpp`); the qtfb panel switches the ink's refresh
  mode at its next update (`RemarkablePanel::SetPenInk`).

## Adding a setting

1. Make it changeable while newton runs, from a native (the baton held) or
   through an atomic another thread reads.
2. Answer it in `HostSettingsList()` and `HostSetSetting()`
   (`HostSettings.cpp`) - for a window setting, add it to
   `kWindowSettings` and to the window's `HostWindowOption`.
3. Nothing in the panel changes: it lists what the host says it has.

The magic-pointer numbers of the ROM's protos come from
`tools/newton-rom/analysis/magicpointer.py protoCheckbox ...`.

## Tests

- `host.NewtonHostSettings.set`, then `.kept` on the same store
  (`src/host/demo/hostsettings.ns`): Prefs opened as a user opens it, the
  Host row tapped, its checkbox tapped - beaming over the network on at
  once and kept - then a second boot on the store beaming from the start,
  and turned off again through the panel's `Change`.
- The Prefs list draws its rows itself and keeps the added ones in
  alphabetical order (Host before Network Printers), so demos tap a row by
  name with `tapPrefsItem` (`src/host/demo/apphelpers.ns`), which finds it
  among the list's drawn `pieces` (a `'textdata` text shape and a
  `'boundsrect`).  The printers demos used to tap a fixed position, which
  Host would have taken.
- On a reMarkable, `tools/remarkable/hostsettings-device.ns` on a scratch
  store (2026-10-03: seven settings; the Ink picker showed Fast, Pen was
  picked from its popup, the window switched to the pen waveform):

        ssh root@10.11.99.1 mkdir -p /home/root/newton-data/hstest
        scp tools/remarkable/hostsettings-device.ns src/host/demo/{common,walkhelpers,apphelpers}.ns             root@10.11.99.1:/home/root/newton-data/hstest/
        ssh root@10.11.99.1 'cd /home/root/newton-data/hstest && QTFB_KEY=4243 NEWTON_TRACE_EXCEPTIONS=1             NEWTON_RM_PEN=evdev /home/root/xovi/exthome/appload/newton/newton             --objects /home/root/xovi/exthome/appload/newton/romsrc-objects.bin --display 320x480             --store internal.store --script hostsettings-device.ns'

  (a made-up `QTFB_KEY` gives newton a framebuffer AppLoad never shows, so
  the tablet's screen is not touched; `--display 320x480` because the
  demos' Setup walk taps a MessagePad's positions).
- `host.NewtonHostSettings.kept` also turns docking over the network off and
  on again.
- On a reMarkable, `tools/remarkable/calibration-device.ns` (its header says
  how): a store calibrated at 320 x 480 (x scale 8192) started at 4x
  (404 x 540) logs the reset and reads 16384 kept and in use, and keeps it
  at the next start (2026-10-03).

## The Developer panel

Beside the Host panel, Prefs has a **Developer** page
(`src/host/HostDeveloper.ns`, `host/HostDeveloper.h`, evaluated at boot
by `HostInstallDeveloper` - on the desktops and the T-Deck Pro), for the
tools that look into the OS as it runs; Newton Devices' AppleTalk panel,
which the host had nothing behind, is gone from the list
(`romsrc/rex/newtdev/README.md`).  For now:

- **Profile NewtonScript**: the NewtonScript profiler
  (`frames/FunctionProfiler.h`, `docs/frames/README.md` "Profiling
  NewtonScript") on and off - the checkbox shows whether it runs, so a
  profile `NEWTON_NS_PROFILE` started at boot shows ticked;
- **Write profile**: its report written to the file
  `HostDeveloperReportPath()` names - `<store>.nsprofile.txt` beside the
  store file, `newton-nsprofile.txt` in the working directory with no
  store, the console on the T-Deck Pro - and a notice saying where
  (`tools/host/nsprofile.py` reads it);
- **Clear**: its counts cleared.

Nothing on the page is kept: each tool starts off at every boot.  A new
developer tool goes here (a control in `HostDeveloper.ns`'s panel, a
native in `HostDeveloper.cpp` if it needs the host), not on the Host
page, which is the machine's settings.  ctest `host.NewtonDeveloperPanel`
(`src/host/demo/developer.ns`) opens Prefs, checks Developer is there and
AppleTalk is not, starts the profiler from the checkbox, writes the
profile and stops it.
