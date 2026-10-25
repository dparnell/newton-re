Newton OS on your Mac
=====================

A reconstruction of the Apple Newton MessagePad 2x00's operating system
(NewtonOS 2.1), built from https://github.com/dparnell/newton-re.

  Newton.app           the OS in a window: the mouse is the pen, the keyboard
                       the Newton's keyboard.  One application for Apple
                       silicon and Intel Macs, macOS 11 or later.
  newtonscript         NewtonScript on the command line
  romsrc-objects.bin   what newtonscript boots from - keep it beside it
                       (Newton.app has its own copy inside)

Opening it the first time: Newton.app is signed ad hoc, not by an Apple
developer ID, so macOS will not open a downloaded copy straight away.
Either open it once, then go to System Settings > Privacy & Security and
click "Open Anyway" beside its name; or, in Terminal:

  xattr -dr com.apple.quarantine Newton.app

Newton.app keeps the internal store - everything you do on the Newton - in
~/.local/newton/Internal.store (as newton run from a shell does), and
its log in ~/Library/Logs/com.matthiasm.newton-re/newton.log.  The first
start walks you through the Setup assistant, as a new MessagePad does.

Packages install by dropping them onto the window, by File > Install
Package (Cmd-O), or by opening a .nspkg file with Newton.app.  F12 is the
power switch, F11 the backlight.

Options, as on the other hosts (Newton.app/Contents/MacOS/newton --help):

  open -a Newton.app --args --display 480x640    another screen size

More: the repository's README.md and docs/host-macos.md.
