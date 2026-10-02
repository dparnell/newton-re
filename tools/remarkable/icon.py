#!/usr/bin/env python3
"""Draw the Newton's icon for AppLoad's launcher on a reMarkable
(docs/host-remarkable.md):

    python tools/remarkable/icon.py [-o icon.png] [--size 150]

A MessagePad 2x00 seen from the front - its body, the screen with a line of
handwriting on it, the row of silkscreened buttons under the screen - and a
stylus across it, drawn in grays (the panel is e-ink) and anti-aliased by
sampling each pixel 4 x 4 times.  AppLoad shows an application's icon.png
at 150 x 150 (its appload.qml), so that is the default size; the drawing is
in a 150-unit square and scales to any size.  No Apple logo or lightbulb:
only the shape of the machine.

tools/remarkable/package.py calls `newton_icon` for every Newton application
it makes.  Written with tools/imaging/png.py, so it needs nothing but Python.
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "imaging"))
import png  # noqa: E402


# Signed distances (negative inside) in the 150-unit drawing.

def rounded_rect(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - r


def segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def polyline(px, py, points):
    return min(segment(px, py, *points[i], *points[i + 1]) for i in range(len(points) - 1))


def handwriting():
    """A line of cursive: loops (a prolate cycloid), rising a little."""
    points = []
    steps = 160
    for i in range(steps + 1):
        t = 3.2 * 2 * math.pi * i / steps
        x = 36.0 + 2.25 * t - 4.6 * math.sin(t)
        y = 82.0 + 6.0 * math.cos(t) - 0.15 * t
        points.append((x, y))
    return points


INK = handwriting()
# a line written before it, higher up
UNDERLINE = [(38.0 + 2.2 * i, 52.0 + 1.8 * math.sin(i * 0.55)) for i in range(20)]
# the stylus, held as a pencil is: its top end out past the body's top
# right, its tip where the handwriting ends
STYLUS_TIP = INK[-1]
STYLUS_TOP = (140.0, 14.0)
STYLUS_GRIP = (STYLUS_TIP[0] + 0.14 * (STYLUS_TOP[0] - STYLUS_TIP[0]),
               STYLUS_TIP[1] + 0.14 * (STYLUS_TOP[1] - STYLUS_TIP[1]))


def shade(x, y):
    """The gray at one sample point: 255 white .. 0 black."""
    v = 255.0
    # the body: a light gray with a dark rim
    body = rounded_rect(x, y, 66, 75, 45, 71, 11)
    if body < 0:
        v = 40.0 if body > -3.2 else 214.0
    # the screen: a bezel line round a near-white panel
    screen = rounded_rect(x, y, 66, 64, 35, 50, 3)
    if screen < 0:
        v = 70.0 if screen > -1.6 else 248.0
    # the silkscreened buttons under it
    for i in range(5):
        if rounded_rect(x, y, 38 + i * 14, 126, 4.2, 4.2, 1.5) < 0:
            v = 120.0
    # the speaker slots at the foot
    for i in range(4):
        if rounded_rect(x, y, 56 + i * 7, 139, 1.3, 2.6, 1.2) < 0:
            v = 150.0
    # the handwriting
    if screen < -2 and (polyline(x, y, INK) < 1.35 or polyline(x, y, UNDERLINE) < 1.0):
        v = 0.0
    # the stylus: a shadow, its barrel, its tip
    if segment(x, y, STYLUS_TOP[0] + 2.5, STYLUS_TOP[1] + 2.5, STYLUS_GRIP[0] + 2.5, STYLUS_GRIP[1] + 2.5) < 3.6 and body < 0:
        v = min(v, 150.0)
    barrel = segment(x, y, *STYLUS_TOP, *STYLUS_GRIP)
    if barrel < 3.8:
        v = 30.0 if barrel > 2.6 else 95.0
    tip = segment(x, y, *STYLUS_GRIP, *STYLUS_TIP)
    if tip < 2.6 - 1.8 * min(1.0, math.hypot(x - STYLUS_GRIP[0], y - STYLUS_GRIP[1]) / 14.0):
        v = 20.0
    return v


def newton_icon(path, size=150):
    samples = 4
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            total = 0.0
            for sy in range(samples):
                for sx in range(samples):
                    x = (px + (sx + 0.5) / samples) * 150.0 / size
                    y = (py + (sy + 0.5) / samples) * 150.0 / size
                    total += shade(x, y)
            row.append(int(round(total / (samples * samples))))
        rows.append(row)
    png.write_gray(path, size, size, rows, 8)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", default="icon.png")
    parser.add_argument("--size", type=int, default=150)
    args = parser.parse_args()
    newton_icon(args.out, args.size)
    print("icon: %s (%d x %d)" % (args.out, args.size, args.size))


if __name__ == "__main__":
    main()
