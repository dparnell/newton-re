#!/usr/bin/env python3
"""A streamed package: one frames part whose frame is NSOF - the kind a card
can carry in its attribute memory.

The card server loads a package that a card's CIS says is in attribute
memory through a pipe (TCardServer::LoadCardPackage over a TCardPipe, ROM
0x00050130), so the package arrives as a stream - and a frames part from a
stream is read by its handler as one flattened object, NSOF
(TFramePartHandler::Expand; docs/packages/README.md, "Packages from a
stream").  A package made for memory, its frames part in object layout,
cannot be loaded that way, on a MessagePad or here.  This makes one that
can.

--kind auto (the default) is an 'auto part:

    {text: "<text>",
     InstallScript: func(partFrame) begin
         cardPackageInstalled := partFrame.text; partFrame end,
     removeScript: func(cookie) begin
         cardPackageRemoved := cookie.text; nil end}

The ROM's InstallAutoPart sends the frame InstallScript and keeps what it
answers as the part's remove cookie; RemoveAutoPart sends the cookie
removeScript.  So the globals cardPackageInstalled/cardPackageRemoved say
the part went in and came out.  The two functions are the NewtonScript
compiler's own output, kept here as bytes:

    newtonscript --no-objects --compile-records IN OUT

over the two `func` lines above (records `@@ 0x0` ... `@@end`) gives
instructions 7b 18 91 a9 7b 02 and 7b 18 91 a9 22 02 with the literals
['text, 'cardPackageInstalled] and ['text, 'cardPackageRemoved], class
0x32 (kFuncClass) and numArgs 1.

--kind form is an application part:

    {app: '<app>, text: "<text>",
     theForm: {viewClass: 74, viewBounds: {left: 0, top: 0, right: 220,
               bottom: 120}, viewFlags: 5, viewFormat: 0x151,
               title: "<text>"}}

which the U.S. ROM cannot install off a card: its InstallFormPart (ROM
0x5579c1) writes the card socket into the new view with `find-var
'deviceNumber` - a free variable - where `a1.deviceNumber` was meant, so
every 'form part from a card throws -48807 (undefined variable) and the
user is told "An error occurred activating the package" (a ROM bug, kept;
docs/curiosities.md).

Usage:
    python tools/cards/streamedpkg.py OUT.pkg --name NAME --text TEXT [--kind auto|form] [--app SYMBOL]

Writes the package: the 0x34-byte directory header ("package1", the name
at the start of the data area as big-endian UniChars), one 0x20-byte part
entry ('auto or 'form, flags 0x81: frames, notify) and the NSOF of the
frame (tools/dock/nsof.py).  Then `linearcard.py make ... --attr-package
OUT.pkg` puts it on a card.  The layout is the one
src/packages/tests/test_PackageManager.cpp's StreamedPackage builds,
written from docs/packages/README.md's directory format.

Standard library only (and tools/dock/nsof.py).
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dock"))
from nsof import encode, Symbol, Binary, Array, Immediate  # noqa: E402

K_FUNC_CLASS = 0x32


def function(instructions, literals, num_args):
    """A compiled function as the NSOF of its code block."""
    return {
        Symbol("class"): Immediate(K_FUNC_CLASS),
        Symbol("instructions"): Binary(Symbol("instructions"), bytes.fromhex(instructions)),
        Symbol("literals"): Array(Symbol("literals"), [Symbol(name) for name in literals]),
        Symbol("argFrame"): None,
        Symbol("numArgs"): num_args,
    }


def auto_frame(text):
    return {
        Symbol("text"): text,
        Symbol("InstallScript"): function("7b1891a97b02", ["text", "cardPackageInstalled"], 1),
        Symbol("removeScript"): function("7b1891a92202", ["text", "cardPackageRemoved"], 1),
    }


def form_frame(app, text):
    return {
        Symbol("app"): Symbol(app),
        Symbol("text"): text,
        Symbol("theForm"): {
            Symbol("viewClass"): 74,
            Symbol("viewBounds"): {Symbol("left"): 0, Symbol("top"): 0, Symbol("right"): 220, Symbol("bottom"): 120},
            Symbol("viewFlags"): 5,
            Symbol("viewFormat"): 0x151,
            Symbol("title"): text,
        },
    }


def make(name, frame, part_type):
    part = encode(frame)
    header_size, entry_size = 0x34, 0x20
    name_bytes = name.encode("utf-16-be") + b"\0\0"
    directory_size = (header_size + entry_size + len(name_bytes) + 3) & ~3
    size = directory_size + len(part)
    header = b"package1" + struct.pack(">IIIIIIIIIII", 0x78787878, 0x10000000, 1, 0, len(name_bytes),
                                       size, 0, 0, 0, directory_size, 1)
    entry = struct.pack(">IIIIIIII", 0, len(part), len(part), struct.unpack(">I", part_type)[0], 0, 0x81, 0, 0)
    directory = header + entry + name_bytes
    directory += b"\0" * (directory_size - len(directory))
    return directory + part


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--name", required=True, help="the package's name")
    ap.add_argument("--text", required=True, help="the part frame's text (and a form's title)")
    ap.add_argument("--kind", choices=("auto", "form"), default="auto")
    ap.add_argument("--app", default="CardHello:Test", help="a form part's application symbol")
    args = ap.parse_args(argv)
    if args.kind == "auto":
        data = make(args.name, auto_frame(args.text), b"auto")
    else:
        data = make(args.name, form_frame(args.app, args.text), b"form")
    with open(args.out, "wb") as f:
        f.write(data)
    print("%s: \"%s\", an '%s part, %d bytes" % (args.out, args.name, args.kind, len(data)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
