#!/usr/bin/env python3
"""Check the pages a fax sent from the Newton came to, and make PNGs of them.

    python tools/modem/faxcheck.py build/fax-sent.pbm [--pages 2]
        [--width 1728] [--inked PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]]...

fakemodem.py's answering fax machine (--fax-answer OUT.pbm) writes the
first page to OUT.pbm and the ones after it to OUT-2.pbm, OUT-3.pbm, ...
This reads them all, checks that there are as many as --pages says, that
each is --width pixels wide (1728 for an A4/letter fax), that no page is
blank, and that each --inked rectangle of a page (in the page's pixels) has
at least MIN black pixels (default 200) - which is how ctest
host.NewtonFaxSend knows the note's own text reached the paper, not only
the header and the cover page.  Each page is written beside it as a PNG
(OUT.png, OUT-2.png, ...; tools/imaging/pgm2png.py does the writing).

Prints one line per page and "faxcheck: passed", or the first thing wrong
and exits 1.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imaging"))
import pgm2png  # noqa: E402


def page_paths(first):
    stem, ext = os.path.splitext(first)
    paths = [first]
    n = 2
    while os.path.exists("%s-%d%s" % (stem, n, ext)):
        paths.append("%s-%d%s" % (stem, n, ext))
        n += 1
    return paths


def black_in(width, gray, left, top, right, bottom):
    count = 0
    for y in range(top, bottom):
        row = gray[y * width:(y + 1) * width]
        count += row[left:right].count(0)
    return count


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pbm")
    parser.add_argument("--pages", type=int)
    parser.add_argument("--width", type=int, default=1728)
    parser.add_argument("--inked", action="append", default=[],
                        help="PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]")
    args = parser.parse_args(argv[1:])

    if not os.path.exists(args.pbm):
        print("faxcheck: no page at %s" % args.pbm)
        return 1
    paths = page_paths(args.pbm)
    if args.pages is not None and len(paths) != args.pages:
        print("faxcheck: %d page(s), %d expected" % (len(paths), args.pages))
        return 1
    pages = []
    for number, path in enumerate(paths, 1):
        width, height, gray = pgm2png.read_pnm(path)
        black = gray.count(0)
        png = os.path.splitext(path)[0] + ".png"
        pgm2png.write_png(png, width, height, gray)
        print("faxcheck: page %d: %d x %d, %d black pixels -> %s" % (number, width, height, black, png))
        if width != args.width:
            print("faxcheck: page %d is %d pixels wide, %d expected" % (number, width, args.width))
            return 1
        if black == 0:
            print("faxcheck: page %d is blank" % number)
            return 1
        pages.append((width, height, gray))
    for spec in args.inked:
        parts = spec.split(":")
        number = int(parts[0])
        left, top, right, bottom = (int(v) for v in parts[1].split(","))
        least = int(parts[2]) if len(parts) > 2 else 200
        if number > len(pages):
            print("faxcheck: no page %d" % number)
            return 1
        width, height, gray = pages[number - 1]
        count = black_in(width, gray, left, top, min(right, width), min(bottom, height))
        print("faxcheck: page %d [%d,%d,%d,%d]: %d black pixels" % (number, left, top, right, bottom, count))
        if count < least:
            print("faxcheck: page %d [%d,%d,%d,%d] has %d black pixels, at least %d expected"
                  % (number, left, top, right, bottom, count, least))
            return 1
    print("faxcheck: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
