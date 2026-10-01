#!/usr/bin/env python3
"""Compare two directories of screen snapshots (binary PGM, as a host
Newton's ScreenSnapshot writes them) and say where they differ.

    python tools/imaging/pgmdiff.py BEFORE_DIR AFTER_DIR [--noise AGAIN_DIR] [--ignore-rows 0-12] [--png OUT_DIR]

For each .pgm in BEFORE_DIR with a namesake in AFTER_DIR: identical, or
how many pixels differ and the box they lie in, and the rows that hold
them grouped into runs.  --ignore-rows leaves rows out of the comparison
(the status bar's clock, which a later run shows at another minute).
--noise names a second run of the "before" snapshots: a pixel that differs
between the two before runs (the clock, a random deal, a timer) is not
counted as a difference.
--png writes, for each pair that differs, a PNG of the after image with the
differing pixels marked mid-gray, so a difference can be looked at.

Used to check that a change to drawing or layout draws what it drew before,
or to account for each pixel it does not (docs/views/README.md: the line
layout's move to the ROM's LineLoop).  Exit code: 0 when every pair is the
same, 1 otherwise.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def read_pgm(path):
	with open(path, "rb") as f:
		data = f.read()
	fields = []
	pos = 0
	while len(fields) < 4:
		while data[pos:pos + 1].isspace():
			pos += 1
		if data[pos:pos + 1] == b"#":
			while data[pos:pos + 1] not in (b"\n", b""):
				pos += 1
			continue
		start = pos
		while not data[pos:pos + 1].isspace():
			pos += 1
		fields.append(data[start:pos])
	pos += 1
	if fields[0] != b"P5":
		raise ValueError("%s: not a binary PGM" % path)
	w, h = int(fields[1]), int(fields[2])
	return w, h, data[pos:pos + w * h]


def row_runs(rows):
	runs = []
	for r in sorted(rows):
		if runs and r == runs[-1][1] + 1:
			runs[-1][1] = r
		else:
			runs.append([r, r])
	return ", ".join("%d" % a if a == b else "%d-%d" % (a, b) for a, b in runs)


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("before")
	ap.add_argument("after")
	ap.add_argument("--ignore-rows", help="first-last rows to leave out, e.g. 0-12")
	ap.add_argument("--png", help="a directory for PNGs of the differences")
	ap.add_argument("--noise", help="a second run of BEFORE: pixels that differ between the two are left out")
	a = ap.parse_args(argv)
	ignore = None
	if a.ignore_rows:
		lo, hi = a.ignore_rows.split("-")
		ignore = (int(lo), int(hi))
	same = True
	for name in sorted(os.listdir(a.before)):
		if not name.endswith(".pgm"):
			continue
		other = os.path.join(a.after, name)
		if not os.path.exists(other):
			print("%s: not in %s" % (name, a.after))
			same = False
			continue
		w, h, p = read_pgm(os.path.join(a.before, name))
		w2, h2, q = read_pgm(other)
		if (w, h) != (w2, h2):
			print("%s: %dx%d against %dx%d" % (name, w, h, w2, h2))
			same = False
			continue
		n = None
		noisy = os.path.join(a.noise, name) if a.noise else None
		if noisy and os.path.exists(noisy):
			nw, nh, n = read_pgm(noisy)
			if (nw, nh) != (w, h):
				n = None
		diffs = []
		for i in range(min(len(p), len(q))):
			if p[i] != q[i] and (n is None or n[i] == p[i]):
				y = i // w
				if ignore and ignore[0] <= y <= ignore[1]:
					continue
				diffs.append((i % w, y))
		if not diffs:
			print("%s: same" % name)
			continue
		same = False
		xs = [d[0] for d in diffs]
		ys = [d[1] for d in diffs]
		print("%s: %d pixels differ in (%d,%d)-(%d,%d); rows %s" % (name, len(diffs), min(xs), min(ys), max(xs), max(ys), row_runs(set(ys))))
		if a.png:
			import png
			os.makedirs(a.png, exist_ok=True)
			marked = bytearray(q)
			for x, y in diffs:
				marked[y * w + x] = 0x80
			rows = [list(marked[y * w:(y + 1) * w]) for y in range(h)]
			png.write_gray(os.path.join(a.png, name[:-4] + ".png"), w, h, rows, 8)
	return 0 if same else 1


if __name__ == "__main__":
	sys.exit(main())
