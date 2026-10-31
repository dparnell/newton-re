# Newton Devices (newtdev.pkg), as source

Apple's Newton Devices (the network card driver and its name server interface) - one of the three packages of the Newton
Internet Enabler 2.0 that `romsrc/` adds to the ROM extension
(`romsrc/README.md`, "The Newton Internet Enabler, built in") - as text:
`package.txt` (the directory: `tools/armpart/newtonpkg.py`), each frames
part a NewtonScript source tree (`partN/`, as the ROM's own packages'
parts are), each protocol part ARM source (`partN.s`, `tools/armpart/`).
The builder builds the package file from it and puts that into the form
the ROM keeps its packages in (`rex.tsv`: `rom-form`).

It was made from Apple's package (`fixtures/packages/apple/NIE2/ENETSUP/newtdev.pkg`,
the same bytes) with

    python tools/newton-rom/analysis/romsrc.py package unpack fixtures/packages/apple/NIE2/ENETSUP/newtdev.pkg romsrc/rex/newtdev --rom build/MP2x00US --newtonscript build/host/host/newtonscript

and `romsrc.py package pack romsrc/rex/newtdev X.pkg --newtonscript ...`
builds the package file alone.

## What was changed, and why

DEVIATION: no AppleTalk panel in the Prefs list.  Its install script
(`part[0].devInstallScript` in `part7/objects/part.ns`) registered one
(`RegPrefs('AppleTalk, ...)`: "Connect using" a card), but the host has
no AppleTalk (`docs/next-steps.md`, declined), so the call is taken out
under a `// DEVIATION` comment; the link registry it sets up beside it is
left as it was.  The Prefs list's developer tools are the host's own
Developer panel's (`docs/host-settings.md`, "The Developer panel").

Otherwise it is Apple's as it is.  Its two native modules (NTK native
code: `part1/` and `part6/`'s `resources/nativeModule/*.s`) are ARM source
whose `.rword` lines are the words the package's relocation chunk names;
the builder makes the chunk from them.
