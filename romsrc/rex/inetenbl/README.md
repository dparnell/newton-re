# Newton Internet Enabler (inetenbl.pkg), as source

Apple's Newton Internet Enabler (the TCP/IP stack, its link and domain managers) - one of the three packages of the Newton
Internet Enabler 2.0 that `romsrc/` adds to the ROM extension
(`romsrc/README.md`, "The Newton Internet Enabler, built in") - as text:
`package.txt` (the directory: `tools/armpart/newtonpkg.py`), each frames
part a NewtonScript source tree (`partN/`, as the ROM's own packages'
parts are), each protocol part ARM source (`partN.s`, `tools/armpart/`).
The builder builds the package file from it and puts that into the form
the ROM keeps its packages in (`rex.tsv`: `rom-form`).

It was made from Apple's package (`fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg`,
the same bytes) with

    python tools/newton-rom/analysis/romsrc.py package unpack fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg romsrc/rex/inetenbl --rom build/MP2x00US --newtonscript build/host/host/newtonscript

and `romsrc.py package pack romsrc/rex/inetenbl X.pkg --newtonscript ...`
builds the package file alone.

## What was changed, and why

DEVIATION: one change, so that the NIE works from the ROM.

The NIE keeps its state in a global named "PkgVars_" & the id of its
package - GetPkgRefInfo(ObjectPkgRef('|Domain Manager:NIE|)).id - so that
its domain manager and link manager share one per package, and marks its
package busy while it is in use.  In the ROM there is no pkgRef: ObjectPkgRef
(ROM 0x00321384) answers a package's store entry, and a package in the ROM
has none, so it answers nil and GetPkgRefInfo(nil) throws type.ref.frame
(-48200) - InetStartUp, and with it InetGrabLink, stops there.
MarkPackageBusy(nil, ...)/MarkPackageNotBusy(nil) answer nil and are
harmless (a package in the ROM cannot be deleted anyway).

Each of the four functions that make the name - the domain manager's and
the link manager's acquire and release, disassembled with

    python tools/newton-rom/analysis/pkgns.py fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg --disasm 0x373c5

had the same five bytes at +5 of its instructions:

    get-var 3 (pkgRef); push 3 ('GetPkgRefInfo); call 1; push 4 ('id); get-path 1

which become push 4 ('id); dup; pop; dup; pop - so the name is
"PkgVars_id", the same for all four as the package id was.

The change was first made to Apple's bytes (`romsrc/rex/inetenbl.patches.tsv`,
in the history); it is now made to the source: the four places, two in
`part[0].UnitsInstalledScript` in `part8/objects/part.ns` (the domain
manager's acquire and release, its nested functions) and one each in
`UnitsInstalledScript_literal0.InetStartUp` and `.InetShutDown` in `part11/objects/part.ns` (the
link manager's), read `'id` where Apple's read `GetPkgRefInfo(l1).id`, each
under a `// DEVIATION` comment - and so do the copies of InetStartUp and
InetShutDown that part 11's `UnitsInstalledScript` and
`UnitsRemovedScript` hold as constants (the function table they install,
`UnitsInstalledScript_literal0`, is one object with InetStartUp's: a copy left as Apple's
would be a table of its own, and the unchanged functions installed).
