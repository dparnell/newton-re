#!/usr/bin/env python3
"""Check a print job a host Newton sent to a printer (tools/print/ippprinter.py).

Purpose
    The host's IPP printers send what the ROM's printer drivers make: a
    PostScript document from TPSPrinter, or HP PCL 5 raster graphics from
    ThpPCL (src/print/PSPrinter.h, src/print/HPPCL.h).  This checks that a
    document saved by ippprinter.py is what it should be - ctests
    host.NewtonIPPPostScript.check and host.NewtonIPPPCL.check.

Usage
    python tools/print/jobcheck.py JOB [--pages N] [--text TEXT]...
                                   [--inked PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]]...
                                   [--png OUT]

    JOB      a job-N.ps or job-N.pcl ippprinter.py saved (the kind is told
             by its first bytes, not its name)
    --pages  how many pages it must have (default 1)
    --text   (PostScript) text that must be shown on a page: the string the
             document shows it in, "(TEXT) show", (, ) and \\ escaped
    --inked  (PCL) a rectangle of a page, in the printer's dots (300 an
             inch), that must have at least MIN black dots (default 200)
    --png    (PCL) each page written as OUT-N.png (a one-bit gray PNG,
             tools/imaging/png.py), to look at

Inputs / outputs
    PostScript: the DSC structure - %!PS-Adobe-3.0 first, the prolog's end
    (%%EndProlog), the setup, %%Page: N N and showpage for every page, the
    trailer's %%Pages: N and %%EOF - and each --text.
    PCL: the job reset (PJL's universal exit, then ESC E), and for every
    page 300 dots an inch (ESC *t300R), raster graphics started (ESC *r0A)
    and ended (ESC *rC); every escape sequence parsed, the rows (ESC *b..W,
    PackBits when ESC *b2m) decoded and the skips (ESC *b..Y) followed into
    a bitmap of the page, which must not be blank.
    Prints what it found, then "jobcheck: passed", or what is wrong and
    exits 1.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imaging"))


def fail(message):
    print("jobcheck: FAILED: " + message)
    sys.exit(1)


def check_postscript(data, args):
    text = data.decode("latin-1").replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    if not lines[0].startswith("%!PS-Adobe-3.0"):
        fail("it does not start %!PS-Adobe-3.0")
    for needed in ("%%EndProlog", "%%BeginSetup", "%%EndSetup", "%%Trailer", "%%EOF"):
        if needed not in lines:
            fail("no %s line" % needed)
    pages = [l for l in lines if l.startswith("%%Page: ")]
    showpages = sum(1 for l in lines if l == "showpage")
    print("jobcheck: PostScript, %d bytes, %d page(s), %d showpage(s)" % (len(data), len(pages), showpages))
    if len(pages) != args.pages or showpages != args.pages:
        fail("%d pages, not %d" % (len(pages), args.pages))
    for n, line in enumerate(pages, 1):
        if line != "%%%%Page: %d %d" % (n, n):
            fail("page %d is %r" % (n, line))
    if "%%%%Pages: %d" % args.pages not in lines:
        fail("the trailer does not say %%%%Pages: %d" % args.pages)
    if lines.index("%%EndProlog") > text.count("\n") or lines.index("%%EndProlog") > lines.index(pages[0]):
        fail("the prolog does not end before the first page")
    for wanted in args.text:
        escaped = wanted.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        if ("(%s) show" % escaped) not in text and ("(%s) awidthshow" % escaped) not in text:
            fail("%r is not shown" % wanted)
        print("jobcheck: shows %r" % wanted)


def unpack_bits(data):
    """TIFF PackBits."""
    out = bytearray()
    i = 0
    while i < len(data):
        n = data[i]
        i += 1
        if n < 128:
            out += data[i:i + n + 1]
            i += n + 1
        elif n > 128:
            out += bytes([data[i]]) * (257 - n)
            i += 1
    return bytes(out)


def pcl_commands(data):
    """The PCL commands of a job, in order: (group, parameter, value, data) -
    a parameterised escape sequence (ESC & 0x21-0x2f, a group character,
    then values each ended by a parameter character, lower case when
    another follows) is one command per value, its data (a raster row)
    after the last; a two-character one (ESC E) is (b"", b"E", 0, b"");
    PJL's universal exit (ESC %-12345X) is (b"%", b"X", -12345, b"")."""
    uel = b"\x1b%-12345X"
    i = 0
    while i < len(data):
        if data.startswith(uel, i):
            yield b"%", b"X", -12345, b""
            i += len(uel)
            continue
        if data[i] != 0x1b or i + 1 >= len(data):
            fail("a byte 0x%02x outside a command at %d" % (data[i], i))
        c = data[i + 1]
        if not 0x21 <= c <= 0x2f:
            yield b"", bytes([c]), 0, b""
            i += 2
            continue
        group = bytes([c, data[i + 2]])
        i += 3
        while True:
            m = re.compile(rb"([+-]?\d*(?:\.\d*)?)([@-~])").match(data, i)
            if not m:
                fail("a bad escape sequence at %d: %r" % (i, data[i:i + 10]))
            value = int(float(m.group(1))) if m.group(1) not in (b"", b"+", b"-") else 0
            parameter = m.group(2)
            i = m.end()
            if 0x60 <= parameter[0] <= 0x7e:
                yield group, parameter.upper(), value, b""
                continue
            payload = b""
            if group + parameter == b"*bW":
                payload = data[i:i + value]
                i += value
            yield group, parameter, value, payload
            break


def check_pcl(data, args):
    uel = b"\x1b%-12345X"
    if not data.startswith(uel + b"\x1bE"):
        fail("it does not start with the universal exit and a reset")
    pages = []
    page = None
    width = 0
    compression = 0
    seen = set()
    for group, parameter, value, payload in pcl_commands(data):
        key = group + parameter
        if key in (b"%X", b"E"):
            continue
        if key == b"*tR":
            if value != 300:
                fail("%d dots an inch, not 300" % value)
            seen.add("300")
        elif key == b"*rS":
            width = value
        elif key == b"*rT":
            pass
        elif key == b"*rA":
            page = {"rows": [], "width": width}
            pages.append(page)
        elif key == b"*rC":
            if page is None:
                fail("raster graphics ended before they began")
            page = None
        elif key == b"*bM":
            compression = value
        elif key == b"*bY":
            if page is None:
                fail("rows skipped outside raster graphics")
            page["rows"] += [b""] * value
        elif key == b"*bW":
            if page is None:
                fail("a row outside raster graphics")
            if compression == 2:
                row = unpack_bits(payload)
            elif compression == 0:
                row = payload
            else:
                fail("compression %d" % compression)
            page["rows"].append(row)
        else:
            fail("an unexpected command ESC %s" % key.decode("latin-1"))
    if "300" not in seen:
        fail("no ESC *t300R")
    print("jobcheck: PCL, %d bytes, %d page(s)" % (len(data), len(pages)))
    if len(pages) != args.pages:
        fail("%d pages, not %d" % (len(pages), args.pages))
    for n, page in enumerate(pages, 1):
        rows = page["rows"]
        black = sum(bin(b).count("1") for row in rows for b in row)
        printed = sum(1 for row in rows if any(row))
        print("jobcheck: page %d: %d rows, %d with black, %d black dots, %d dots wide" % (n, len(rows), printed, black, page["width"]))
        if black == 0:
            fail("page %d is blank" % n)
        if args.png:
            import png
            bytes_wide = (page["width"] + 7) // 8
            pixels = []
            for row in rows:
                row = row.ljust(bytes_wide, b"\0")
                pixels.append([0 if (row[x >> 3] >> (7 - (x & 7))) & 1 else 1 for x in range(page["width"])])
            png.write_gray("%s-%d.png" % (args.png, n), page["width"], len(pixels), pixels, 1)
            print("jobcheck: page %d written as %s-%d.png" % (n, args.png, n))
        for spec in args.inked:
            where, _, rest = spec.partition(":")
            if int(where) != n:
                continue
            box, _, least = rest.partition(":")
            left, top, right, bottom = (int(v) for v in box.split(","))
            least = int(least) if least else 200
            count = 0
            for y in range(top, min(bottom, len(rows))):
                row = rows[y]
                for x in range(left, right):
                    if (x >> 3) < len(row) and (row[x >> 3] >> (7 - (x & 7))) & 1:
                        count += 1
            print("jobcheck: page %d, %d,%d .. %d,%d: %d black dots" % (n, left, top, right, bottom, count))
            if count < least:
                fail("page %d, %s: %d black dots, fewer than %d" % (n, box, count, least))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("job")
    ap.add_argument("--pages", type=int, default=1)
    ap.add_argument("--text", action="append", default=[])
    ap.add_argument("--inked", action="append", default=[])
    ap.add_argument("--png")
    args = ap.parse_args(argv)
    try:
        data = open(args.job, "rb").read()
    except OSError as e:
        fail(str(e))
    if data.startswith(b"%!"):
        check_postscript(data, args)
    elif data.startswith(b"\x1b"):
        check_pcl(data, args)
    else:
        fail("neither PostScript nor PCL")
    print("jobcheck: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
