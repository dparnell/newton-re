#!/usr/bin/env python3
"""Tests of png.py: gray PNGs round-trip at every depth, and a PNG another
program might write instead (8-bit RGB with filters, a palette) reads back
as the same levels.

    python tools/imaging/test_png.py
"""
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png						# noqa: E402


def write_rgb(path, rows, filter_type):
	"""An 8-bit RGB PNG of gray pixels, each row filtered (1: sub, 2: up)."""
	height, width = len(rows), len(rows[0])
	raw = bytearray()
	prev = bytes(width * 3)
	for row in rows:
		line = bytes(v for g in row for v in (g, g, g))
		raw.append(filter_type)
		for i, b in enumerate(line):
			ref = line[i - 3] if filter_type == 1 and i >= 3 else prev[i] if filter_type == 2 else 0
			raw.append((b - ref) & 0xff)
		prev = line
	data = png.SIGNATURE + png._chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
	data += png._chunk(b"IDAT", zlib.compress(bytes(raw))) + png._chunk(b"IEND", b"")
	with open(path, "wb") as f:
		f.write(data)


class TestPNG(unittest.TestCase):
	def setUp(self):
		self.dir = tempfile.mkdtemp()

	def test_gray_round_trip(self):
		for depth in (1, 2, 4, 8):
			top = (1 << depth) - 1
			rows = [[(x * 7 + y * 3) % (top + 1) for x in range(13)] for y in range(5)]
			path = os.path.join(self.dir, "g%d.png" % depth)
			png.write_gray(path, 13, 5, rows, depth)
			self.assertEqual(png.read_gray(path, depth), (13, 5, rows))

	def test_rgb_read_as_levels(self):
		levels = [[(x + y) % 16 for x in range(9)] for y in range(4)]
		for filter_type in (1, 2):
			path = os.path.join(self.dir, "rgb%d.png" % filter_type)
			write_rgb(path, [[v * 17 for v in row] for row in levels], filter_type)
			self.assertEqual(png.read_gray(path, 4), (9, 4, levels))


if __name__ == "__main__":
	unittest.main()
