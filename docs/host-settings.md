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

## What it offers

| setting | what it does | where | before the panel |
|---|---|---|---|
| Beam over the network (`lanBeam`) | the built-in IR port's medium the LAN group (239.255.78.119:3681): beam to any newton on the network with it on - off, the port has nobody in front of it (or the `--ir-peer` newton was started with) | every host | `--ir-lan` |
| Ink in the pen waveform (`penInk`) | the ink sent in AppLoad's *ufast* mode, most likely xochitl's own pen waveform - quicker, with more ghosting (AppLoad applies it to the whole window) | reMarkable | `NEWTON_RM_INK_MODE=ufast` |
| A finger is the pen (`touch`) | one finger taps and drags as the pen, with the palm rejection of `docs/host-remarkable.md` | reMarkable | `NEWTON_RM_TOUCH=off` to turn off |
| Clear ghosts (a button) | one flashing redraw of the whole panel, as AppLoad's five-finger tap does | reMarkable | - |

A setting changed on the panel takes effect at once and is kept; one never
touched stays as newton was started.

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
  label, kind, value}]`, `kind` `'check` (a `protoCheckbox`, `@164`) or
  `'button` (a `protoTextButton`, `@226`).  The panel's children are made
  by `Start` from that list - the Prefs roll lays out and builds a panel
  from its template, so children added in its own `viewSetupFormScript`
  came too late and it opened empty - and each checkbox reads its value as
  it opens (`viewSetupFormScript`).  A checkbox's `valueChanged` (the
  ROM's protoCheckbox calls it from `toggleCheck` and `SetCheck`,
  `romsrc/functions/obj_63fcdd.*`) calls `Change`.
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
- On the reMarkable, `HostSettingsList()` was checked over SSH: Beam over
  the network off, Ink in the pen waveform off, A finger is the pen on,
  Clear ghosts.
