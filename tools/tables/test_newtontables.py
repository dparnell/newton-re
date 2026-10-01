#!/usr/bin/env python3
"""Tests of newtontables.py (ctests tools.NewtonTables, tools.NewtonTablesROM).

    python tools/tables/test_newtontables.py [--rom build/MP2x00US]

Every `texttable(...)` of romsrc/objects is packed, unpacked and packed
again to the same bytes and the same text; with --rom, the bytes must be
the ROM's own - the object at the address the file is named after (its
data after the 12-byte object header).  Then edits are read back: a
character mapped to another byte, a class's case delta, a sorting weight,
a ligature added, a break table's class.
"""
import os
import re
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import newtontables				# noqa: E402

ROMSRC = os.path.join(HERE, "..", "..", "romsrc")
ROM_BUILD = None


def committed():
	seen = []
	for name in sorted(os.listdir(os.path.join(ROMSRC, "objects"))):
		with open(os.path.join(ROMSRC, "objects", name), encoding="utf-8") as f:
			for m in re.finditer(r'texttable\(\'\w+, "([^"]+)"\)', f.read()):
				seen.append(os.path.join(ROMSRC, m.group(1)))
	return seen


def edited(src, tmp, change):
	with open(src, encoding="utf-8") as f:
		text = f.read()
	new = change(text)
	assert new != text, "the edit changed nothing"
	path = os.path.join(tmp, os.path.basename(src))
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(new)
	return newtontables.pack(path)


class RoundTrip(unittest.TestCase):
	def test_committed(self):
		files = committed()
		self.assertEqual(len(files), 16)
		rom = None
		if ROM_BUILD:
			with open(os.path.join(ROM_BUILD, "rom.bin"), "rb") as f:
				rom = f.read()
		with tempfile.TemporaryDirectory() as tmp:
			for path in files:
				with self.subTest(os.path.basename(path)):
					data = newtontables.pack(path)
					with open(path, encoding="utf-8") as f:
						format = re.search(r"^format (\S+)", f.read(), re.M).group(1)
					again = os.path.join(tmp, "t.txt")
					newtontables.unpack(data, format, again)
					self.assertEqual(newtontables.pack(again), data)
					with open(path, encoding="utf-8") as a, open(again, encoding="utf-8") as b:
						self.assertEqual(a.read(), b.read())
					if rom is not None:
						address = int(os.path.splitext(os.path.basename(path))[0], 16) - 1
						size = struct.unpack(">I", rom[address:address + 4])[0] >> 8
						self.assertEqual(data, rom[address + 12:address + size])


class Editing(unittest.TestCase):
	def test_encoding(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "resources", "UniC", "62b835.txt")		# Mac Roman to Unicode
			data = edited(src, tmp, lambda t: t.replace("0x80 U+00C4", "0x80 U+00C5"))
			self.assertEqual(struct.unpack(">H", data[8 + 2 * 0x80:8 + 2 * 0x81])[0], 0xc5)

	def test_case_delta(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "resources", "UniC", "62c415.txt")
			data = edited(src, tmp, lambda t: re.sub(r"^3 -?\d+", "3 -7", t, flags=re.M))
			self.assertEqual(data[3], 0xf9)

	def test_sort(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "resources", "Sort", "62cbb9.txt")

			def change(t):
				t = t.replace("U+0061 0x0041 0x0007", "U+0061 0x0042 0x0007")
				return t.replace("ligatures\n", "ligatures\nU+0132 U+0049 U+004A U+0049\n")
			data = edited(src, tmp, change)
			self.assertEqual(struct.unpack(">H", data[0x44 + 4 * 0x61:0x44 + 4 * 0x61 + 2])[0], 0x42)
			self.assertEqual(struct.unpack(">H", data[0x26:0x28])[0], 8)
			lig = 0x44 + struct.unpack(">H", data[0x24:0x26])[0]
			self.assertEqual(struct.unpack(">H", data[lig:lig + 2])[0], 0x132)
			low = 0x44 + struct.unpack(">H", data[0x28:0x2a])[0]
			self.assertEqual(low, lig + 8 * 8)

	def test_break_table(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "resources", "Intl", "4a3fbd.txt")
			data = edited(src, tmp, lambda t: re.sub(r"^0x5f \d+", "0x5f 2", t, flags=re.M))
			self.assertEqual(data[0x10 + 0x5f], 2)


if __name__ == "__main__":
	if "--rom" in sys.argv:
		i = sys.argv.index("--rom")
		ROM_BUILD = sys.argv[i + 1]
		del sys.argv[i:i + 2]
	unittest.main()
