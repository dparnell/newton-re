Newton OS on your computer
=========================

A reconstruction of the Apple Newton MessagePad 2x00's operating system
(NewtonOS 2.1), built from https://github.com/dparnell/newton-re.

  newton               the OS in a window: the mouse is the pen, the keyboard
                       the Newton's keyboard
  newtonscript         NewtonScript on the command line
  romsrc-objects.bin   what both boot from - keep it beside them

Run it:

  newton                          a MessagePad's 320 x 480 screen
  newton --display 480x640        another size
  newton --store newton.store     keep what you do in a file between runs
  newton --package app.pkg        install a Newton package (or drop one on the window)

The first start walks you through the Setup assistant, as a new MessagePad
does.  The Host page in the Newton's Preferences (Extras, Prefs, Host) has
the settings of this computer that can change while it runs, such as
beaming to another newton on the network.

On Linux newton needs X11 for its window; sound uses ALSA (libasound2) and
printing to network printers OpenSSL (libssl), when they are installed.

More: the repository's README.md and docs/.
