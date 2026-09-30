#!/usr/bin/env python3
"""Tests of newtonsfnt.py over the ROM's 13 fonts in the ROM source tree.

    python tools/fonts/test_newtonsfnt.py [romsrc/resources/sfnt]

Each font there (a directory of the text form, or a .sfnt file kept whole)
is packed to its bytes, unpacked again and packed again: the bytes must
come back identical, with no table left as hex, and a committed directory
must be exactly what the unpacker writes (so the committed form is the
canonical one).  Then the edits a person makes are tried on a copy: a
glyph's pixel, a glyph's advance, a 'cmap' mapping and a 'hsty' value each
change only what they should.  The bytes being the ROM's is
host.ROMSourceCommitted's test; this is the text form's.
"""
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import newtonsfnt as ns		# noqa: E402

FONTS = os.path.join(HERE, "..", "..", "romsrc", "resources", "sfnt")


def font_bytes(path):
	if os.path.isdir(path):
		return ns.pack(path)
	with open(path, "rb") as f:
		return f.read()


def fonts(folder):
	return sorted(os.path.join(folder, n) for n in os.listdir(folder)
				  if n.endswith(".sfnt") or os.path.isdir(os.path.join(folder, n)))


class RoundTrip(unittest.TestCase):
	def setUp(self):
		self.work = tempfile.mkdtemp(prefix="newtonsfnt")

	def tearDown(self):
		shutil.rmtree(self.work, ignore_errors=True)

	def test_all_thirteen(self):
		paths = fonts(FONTS)
		self.assertEqual(len(paths), 13, paths)
		bitmap = 0
		for path in paths:
			data = font_bytes(path)
			d = os.path.join(self.work, os.path.basename(path))
			hexed = ns.unpack(data, d)
			self.assertEqual(hexed, [], "%s: tables left as hex" % path)
			self.assertEqual(ns.pack(d), data, path)
			if os.path.exists(os.path.join(d, "bloc.txt")):
				bitmap += 1
			if os.path.isdir(path):
				self.assertEqual(sorted(os.listdir(path)), sorted(os.listdir(d)), path)
				for n in os.listdir(d):
					with open(os.path.join(path, n), encoding="utf-8") as f, open(os.path.join(d, n), encoding="utf-8") as g:
						self.assertEqual(f.read(), g.read(), "%s/%s is not what the unpacker writes" % (path, n))
		self.assertEqual(bitmap, 5)

	def unpacked_bitmap_font(self):
		for path in fonts(FONTS):
			data = font_bytes(path)
			if b"bloc" in data[:12 + 16 * 12]:
				d = os.path.join(self.work, "font")
				ns.unpack(data, d)
				return data, d
		self.fail("no bitmap font")

	def glyph_A(self, d):
		"""The A of the smallest strike: (the file's text, where its glyph
		starts and ends)."""
		path = os.path.join(d, "strike0-9.bdf")
		with open(path, encoding="utf-8") as f:
			text = f.read()
		at = text.index("ENCODING 65\n")
		return path, text, at, text.index("ENDCHAR\n", at)

	def test_edit_a_pixel(self):
		data, d = self.unpacked_bitmap_font()
		path, text, at, end = self.glyph_A(d)
		bitmap = text.index("BITMAP\n", at) + len("BITMAP\n")
		row = text[bitmap:text.index("\n", bitmap)]
		flipped = "%0*X" % (len(row), int(row, 16) ^ (1 << (len(row) * 4 - 1)))
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(text[:bitmap] + flipped + text[bitmap + len(row):])
		edited = ns.pack(d)
		self.assertEqual(len(edited), len(data))
		version, directory = ns.read_directory(data)
		where = {t: (o, n) for t, _, o, n in directory}
		bdat_at, bdat_len = where["bdat"]
		inside = [i for i in range(bdat_at, bdat_at + bdat_len) if data[i] != edited[i]]
		self.assertEqual(len(inside), 1)
		self.assertEqual(data[inside[0]] ^ edited[inside[0]], 0x80)
		# anything else that changed: the 'bdat' checksum in the directory
		# and the head's checkSumAdjustment
		bdat_entry = 12 + 16 * [t for t, _, _, _ in directory].index("bdat") + 4
		adjust = where["head"][0] + 8
		for i in range(len(data)):
			if data[i] != edited[i] and i != inside[0]:
				self.assertTrue(bdat_entry <= i < bdat_entry + 4 or adjust <= i < adjust + 4, i)
		self.assertEqual(ns.checksum(edited), ns.HEAD_ADJUST_MAGIC)

	def test_edit_advance_and_width(self):
		data, d = self.unpacked_bitmap_font()
		path, text, at, end = self.glyph_A(d)
		lines = text[at:end].split("\n")
		b = lines.index("BITMAP")
		bbx = [l for l in lines if l.startswith("BBX ")][0].split()
		width, height = int(bbx[1]) + 8, int(bbx[2])
		out = []
		for l in lines[:b + 1]:
			if l.startswith("BBX "):
				l = "BBX %d %s %s %s" % (width, bbx[2], bbx[3], bbx[4])
			elif l.startswith("DWIDTH "):
				l = "DWIDTH 15 0"
			out.append(l)
		out += [r + "00" for r in lines[b + 1:] if r] + [""]
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(text[:at] + "\n".join(out) + text[end:])
		edited = ns.pack(d)
		# a byte more on each of its rows: 'bdat' that much longer
		length = lambda font: [n for t, _, _, n in ns.read_directory(font)[1] if t == "bdat"][0]
		self.assertEqual(length(edited), length(data) + height)
		again = os.path.join(self.work, "again")
		ns.unpack(edited, again)
		_, back, at2, end2 = self.glyph_A(again)
		self.assertIn("DWIDTH 15 0", back[at2:end2])
		self.assertIn("BBX %d " % width, back[at2:end2])
		self.assertEqual(ns.pack(again), edited)

	def test_edit_cmap_and_hsty(self):
		data, d = self.unpacked_bitmap_font()
		with open(os.path.join(d, "cmap.tsv"), encoding="utf-8") as f:
			text = f.read()
		text = text + "0x2603\t36\n"		# SNOWMAN drawn as the A's glyph
		with open(os.path.join(d, "cmap.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write(text)
		with open(os.path.join(d, "hsty.txt"), encoding="utf-8") as f:
			text = f.read()
		with open(os.path.join(d, "hsty.txt"), "w", encoding="utf-8", newline="\n") as f:
			f.write(text.replace("\nbold                   169", "\nbold                   200"))
		edited = ns.pack(d)
		version, directory = ns.read_directory(edited)
		tables = {t: edited[o:o + n] for t, _, o, n in directory}
		cmap = tables["cmap"]
		sub = cmap[struct.unpack_from(">I", cmap, 8)[0]:]
		_, mapping = ns.cmap4_decode(sub)
		self.assertIn((0x2603, 36), mapping)
		self.assertEqual(struct.unpack_from(">h", tables["hsty"], 6)[0], 200)
		# the directory's checksums and the head's adjustment are right
		for tag, ck, o, n in directory:
			self.assertEqual(ck, ns.table_checksum(tag, edited[o:o + n]), tag)
		self.assertEqual(ns.checksum(edited), ns.HEAD_ADJUST_MAGIC)


if __name__ == "__main__":
	if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
		FONTS = sys.argv.pop(1)
	unittest.main()
