#!/usr/bin/env python3
"""The test that a font in the ROM source tree is editable: one pixel of a
glyph changed in a BDF file reaches the screen, and nothing else does.

    python tools/fonts/glyph_edit_test.py --tree romsrc -o <dir> \\
        --newton <newton> --newtonscript <newtonscript> \\
        --script src/host/demo/fontedit.ns [--original <objects file>]

It copies the tree into <dir>/tree, finds the System font (the family
whose screenSym is 'espyFont) and its plain face's directory (a font still
kept as a .sfnt file is unpacked there first, with tools/fonts/
newtonsfnt.py, and its reference rewritten), and in the smallest strike's
BDF file flips the pixel in the middle of the capital A's ink.  It builds
the copy (tools/newton-rom/analysis/romsrc.py build --relayout; the
original tree too unless --original gives its object file), boots the OS
on each (newton --objects, headless) with the script, which draws an A in
that font and size and writes build/fontedit.pgm, and compares the two
pictures: exactly one pixel must differ, where the flipped one falls
relative to the A's ink on the screen, and the other way round.  Exit 0
when it does (ctest host.ROMSourceFontEdit).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import newtonsfnt				# noqa: E402

ROMSRC = os.path.join(HERE, "..", "newton-rom", "analysis", "romsrc.py")
SNAPSHOT = os.path.join("build", "fontedit.pgm")


def read_pgm(path):
	with open(path, "rb") as f:
		data = f.read()
	fields = []
	at = 0
	while len(fields) < 4:
		while data[at:at + 1].isspace():
			at += 1
		if data[at:at + 1] == b"#":
			at = data.index(b"\n", at) + 1
			continue
		start = at
		while not data[at:at + 1].isspace():
			at += 1
		fields.append(data[start:at])
	if fields[0] != b"P5" or int(fields[3]) > 255:
		raise ValueError("%s: not an 8-bit PGM" % path)
	width, height = int(fields[1]), int(fields[2])
	pixels = data[at + 1:at + 1 + width * height]
	return width, height, pixels


def find_font_dir(tree):
	"""The System font's plain face: its directory in the tree (unpacking a
	.sfnt file kept whole first)."""
	pattern = re.compile(r"screenSym: 'espyFont[^\n]*?plainData: (sfnt|binary)\('sfnt, \"([^\"]+)\"\)")
	for name in sorted(os.listdir(os.path.join(tree, "objects"))):
		path = os.path.join(tree, "objects", name)
		with open(path, encoding="utf-8") as f:
			text = f.read()
		m = pattern.search(text)
		if not m:
			continue
		rel = m.group(2)
		if m.group(1) == "binary":
			with open(os.path.join(tree, rel), "rb") as f:
				data = f.read()
			new_rel = os.path.splitext(rel)[0]
			newtonsfnt.unpack(data, os.path.join(tree, new_rel))
			text = text[:m.start(1)] + "sfnt('sfnt, \"%s\")" % new_rel + text[m.end():]
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write(text)
			print("the System font was %s: unpacked into %s" % (rel, new_rel))
			rel = new_rel
		return os.path.join(tree, rel)
	raise SystemExit("no font family with screenSym 'espyFont and a plainData font in %s" % tree)


def smallest_strike(font_dir):
	best = None
	with open(os.path.join(font_dir, "bloc.txt"), encoding="utf-8") as f:
		for line in f:
			w = newtonsfnt.strip_comment(line).split()
			if len(w) == 2 and w[0] == "strike":
				with open(os.path.join(font_dir, w[1]), encoding="utf-8") as g:
					ppem = int(re.search(r"^BLOC_PPEM_Y (\d+)", g.read(), re.M).group(1))
				if best is None or ppem < best[0]:
					best = (ppem, w[1])
	return best


def flip_pixel_of_A(bdf_path):
	"""Flips the pixel in the middle of the A's ink.  ==> (row, col) in the
	glyph's box, the ink's top-left (row, col), whether it is now inked."""
	with open(bdf_path, encoding="utf-8") as f:
		lines = f.read().split("\n")
	i = next(k for k, l in enumerate(lines) if l.strip() == "ENCODING 65")
	bbx = next(k for k in range(i, len(lines)) if lines[k].startswith("BBX "))
	width, height = int(lines[bbx].split()[1]), int(lines[bbx].split()[2])
	first = next(k for k in range(bbx, len(lines)) if lines[k].strip() == "BITMAP") + 1
	rows = [int(lines[first + r], 16) for r in range(height)]
	nbits = ((width + 7) >> 3) * 8
	ink = [(r, c) for r in range(height) for c in range(width) if rows[r] >> (nbits - 1 - c) & 1]
	top, bottom = min(r for r, _ in ink), max(r for r, _ in ink)
	left, right = min(c for _, c in ink), max(c for _, c in ink)
	row, col = (top + bottom) // 2, (left + right) // 2
	# keep the ink's bounding box as it was: a pixel on its edge must stay
	rows[row] ^= 1 << (nbits - 1 - col)
	inked = bool(rows[row] >> (nbits - 1 - col) & 1)
	lines[first + row] = "%0*X" % (nbits // 4, rows[row])
	with open(bdf_path, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines))
	return (row, col), (top, left), inked


def build(tree, out, newtonscript):
	subprocess.run([sys.executable, ROMSRC, "build", tree, "--relayout", "-o", out, "--newtonscript", newtonscript],
				   check=True, stdout=subprocess.DEVNULL)


def boot(newton, objects, script, work):
	shutil.rmtree(work, ignore_errors=True)
	os.makedirs(os.path.join(work, "build"))
	env = dict(os.environ)
	env["NEWTON_ROM"] = os.path.join(work, "no-rom-image")
	result = subprocess.run([newton, "--objects", objects, "--headless", "5", "--script", script], cwd=work,
							stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", env=env)
	path = os.path.join(work, SNAPSHOT)
	if not os.path.exists(path):
		raise SystemExit("the boot on %s wrote no snapshot; its output ended:\n%s" % (objects, result.stdout[-2000:]))
	return read_pgm(path)


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--tree", required=True, help="the ROM source tree (romsrc/)")
	ap.add_argument("-o", "--output", required=True, help="the working directory (emptied first)")
	ap.add_argument("--newton", required=True)
	ap.add_argument("--newtonscript", required=True)
	ap.add_argument("--script", required=True, help="src/host/demo/fontedit.ns")
	ap.add_argument("--original", help="the object file built from the unedited tree (else it is built)")
	a = ap.parse_args(argv)
	newton = os.path.abspath(a.newton)
	if not os.path.exists(newton) and os.path.exists(newton + ".exe"):
		newton += ".exe"
	out = os.path.abspath(a.output)
	shutil.rmtree(out, ignore_errors=True)
	tree = os.path.join(out, "tree")
	shutil.copytree(a.tree, tree)
	font_dir = find_font_dir(tree)
	ppem, bdf = smallest_strike(font_dir)
	(row, col), (ink_top, ink_left), inked = flip_pixel_of_A(os.path.join(font_dir, bdf))
	print("flipped row %d, column %d of the A in %s (%d pixels per em): now %s"
		  % (row, col, os.path.relpath(os.path.join(font_dir, bdf), out), ppem, "inked" if inked else "clear"))
	edited = os.path.join(out, "edited.bin")
	build(tree, edited, a.newtonscript)
	original = a.original and os.path.abspath(a.original)
	if not original:
		original = os.path.join(out, "original.bin")
		build(a.tree, original, a.newtonscript)
	script = os.path.abspath(a.script)
	w0, h0, before = boot(newton, original, script, os.path.join(out, "boot-original"))
	w1, h1, after = boot(newton, edited, script, os.path.join(out, "boot-edited"))
	if (w0, h0) != (w1, h1):
		print("the two screens are not the same size")
		return 1
	diffs = [i for i in range(len(before)) if before[i] != after[i]]
	# the A's ink on the original screen, inside the text view fontedit.ns draws
	ink = [(y, x) for y in range(108, 124) for x in range(110, 130) if before[y * w0 + x] < 128]
	if not ink:
		print("no A drawn in the text view at (110, 108)")
		return 1
	top, left = min(y for y, _ in ink), min(x for _, x in ink)
	want = (top + row - ink_top, left + col - ink_left)
	print("%d pixel(s) differ%s; the flipped one should be at x %d, y %d"
		  % (len(diffs), "".join(" (x %d, y %d: %d -> %d)" % (i % w0, i // w0, before[i], after[i]) for i in diffs[:5]),
			 want[1], want[0]))
	if len(diffs) != 1 or divmod(diffs[0], w0) != want:
		return 1
	i = diffs[0]
	if (after[i] < 128) != inked or (before[i] < 128) == inked:
		print("the pixel did not change the way the edit says")
		return 1
	print("the glyph edit reached the screen: one pixel of the A, as edited")
	return 0


if __name__ == "__main__":
	sys.exit(main())
