# Internet Setup (inetstup.pkg), as source

Apple's Internet Setup (the application that sets up the NIE's links) - one of the three packages of the Newton
Internet Enabler 2.0 that `romsrc/` adds to the ROM extension
(`romsrc/README.md`, "The Newton Internet Enabler, built in") - as text:
`package.txt` (the directory: `tools/armpart/newtonpkg.py`), each frames
part a NewtonScript source tree (`partN/`, as the ROM's own packages'
parts are), each protocol part ARM source (`partN.s`, `tools/armpart/`).
The builder builds the package file from it and puts that into the form
the ROM keeps its packages in (`rex.tsv`: `rom-form`).

It was made from Apple's package (`fixtures/packages/apple/NIE2/REGPKGS/inetstup.pkg`,
the same bytes) with

    python tools/newton-rom/analysis/romsrc.py package unpack fixtures/packages/apple/NIE2/REGPKGS/inetstup.pkg romsrc/rex/inetstup --rom build/MP2x00US --newtonscript build/host/host/newtonscript

and `romsrc.py package pack romsrc/rex/inetstup X.pkg --newtonscript ...`
builds the package file alone.

## What was changed, and why

DEVIATION: two changes, so that its edit slip fits a screen smaller than a MessagePad's (the T-Deck Pro's).

DEVIATION: the edit slip (the slip a setup's line opens: Configuration,
Domain Name, the host's Wi-Fi) fitted to a screen narrower than a
MessagePad's.  Its viewSetupFormScript (editSlip, 0x11125 - disassembled
with

    python tools/newton-rom/analysis/pkgns.py fixtures/packages/apple/NIE2/REGPKGS/inetstup.pkg --disasm 0x11125)

gives the slip SetBounds(0, -(appAreaHeight - 8), 304, -8), centred - 304
wide whatever the screen - but in the walk-through layout the bounds of
the function at 0xf7a1, whose width is min(appAreaWidth, 320) - 16.  On
the T-Deck Pro's 240x320 screen 304 does not fit, so the ROM's floater
(@132's viewSetupFormScript) takes the slip's bounds away and the
template's, 320x320 (editSlip.viewBounds, 0xf02d), are used: a slip
hanging 40 pixels off each side.  The script's first branch-if-false
(offset 3, at 0x11153: 6f 000e, to 14) is made to go to 6, the next
instruction, so that both layouts take 0xf7a1's bounds.  On a MessagePad
(320 wide upright, which Internet Setup asks for) that is the same width,
304; the slip's height is fitted to its contents afterwards in both
layouts (its first child's viewSetupDoneScript, 0x104a1).  Found with

    python tools/newton-rom/analysis/pkgns.py fixtures/packages/apple/NIE2/REGPKGS/inetstup.pkg --where viewJustify=144


DEVIATION: the edit slip's keyboard (0x10c79, the ROM's keyboard @375 the
Domain Name and other text lines show) as wide as the slip less 21, not
283 centred: on a MessagePad (a 304-wide slip) it is the same 283, 10 in
from the left; in the T-Deck Pro's 224-wide slip it is 203 and fits - a
keyboard view makes its keys' cell from its bounds (views/KeyboardView.cpp,
TKeyboardView::Constructor), so its keys come out smaller rather than cut
off.  viewJustify 144 (centred, at the bottom) becomes 176 (the parent's
full width, at the bottom) and viewBounds' left 0 and right 283 (0x10cd9)
become the insets 10 and -11.

The changes were first made to Apple's bytes (`romsrc/rex/inetstup.patches.tsv`,
in the history); they are now made to the source:
`part[0].theForm.editSlip.viewSetupFormScript` in `part0/objects/part.ns`
sets the walk-through layout's bounds whatever the layout (a `//
DEVIATION` comment keeps the other layout's), and the keyboard
`editFieldsItem` in `part0/objects/part.ns` has viewJustify 176 and
viewBounds' left 10 and right -11.  The function the edit slip shares
with four others (`viewSetupFormScript_literal6`, the walk-through layout's bounds) is
now `same()` as `theForm.viewSetupFormScript`'s literal, and the edit
slip's alias in `part0/layout.tsv` names its new place, literals[1].
