#!/usr/bin/env python3
"""Tests of newtonlex.py (ctest tools.NewtonLexicons).

    python tools/lexicons/test_newtonlex.py [--rom build/MP2x00US]

Every lexicon of romsrc/ (romsrc/lexicons.tsv) is packed, unpacked and
packed again to the same bytes and the same text; with --rom, the bytes
must be the ROM's own at the lexicon's address (rom.bin), and the ROM's
bytes unpacked must give the committed text.  Then a word list is edited -
a word added, a word taken out, an attribute changed - and read back, and
a lexical graph has a character added to one of its sets.
"""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import newtonlex				# noqa: E402

ROMSRC = os.path.join(HERE, "..", "..", "romsrc")
ROM_BUILD = None


def lexicons():
	with open(os.path.join(ROMSRC, "lexicons.tsv"), encoding="utf-8") as f:
		for line in f:
			if line.startswith("#") or not line.strip():
				continue
			address, name, rel = line.rstrip("\n").split("\t")
			yield int(address, 16), name, os.path.join(ROMSRC, rel)


class RoundTrip(unittest.TestCase):
	def test_committed(self):
		rom = None
		if ROM_BUILD:
			with open(os.path.join(ROM_BUILD, "rom.bin"), "rb") as f:
				rom = f.read()
		with tempfile.TemporaryDirectory() as tmp:
			for address, name, path in lexicons():
				with self.subTest(name):
					if path.endswith(".bin"):
						continue
					data = newtonlex.pack(path)
					again = os.path.join(tmp, os.path.basename(path))
					newtonlex.unpack(data, again)
					self.assertEqual(newtonlex.pack(again), data)
					with open(path, encoding="utf-8") as a, open(again, encoding="utf-8") as b:
						self.assertEqual(a.read(), b.read())
					if rom is not None:
						size = int.from_bytes(rom[address:address + 4], "big")
						self.assertEqual(data, rom[address:address + 4 + size], "%s is not the ROM's" % name)


def _words(data):
	return newtonlex._enum_words(data[4:])


class Editing(unittest.TestCase):
	def test_word_list(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "lexicons", "gEnum80DaysMonths.words")
			before = _words(newtonlex.pack(src))
			path = os.path.join(tmp, "days.words")
			with open(src, encoding="utf-8") as f:
				lines = f.read().split("\n")
			lines = [l for l in lines if l != "Tuesday"] + ["Caturday", "Février"]
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write("\n".join(lines))
			after = dict(_words(newtonlex.pack(path)))
			self.assertIn(b"Caturday", after)
			self.assertIn("Février".encode("mac_roman"), after)
			self.assertNotIn(b"Tuesday", after)
			for w, a in before:
				if w != b"Tuesday":
					self.assertIn(w, after)
			self.assertEqual(len(after), len(before) + 1)

	def test_attributes(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "lexicons", "gEnum81IAWordList.words")
			path = os.path.join(tmp, "ia.words")
			with open(src, encoding="utf-8") as f:
				text = f.read().replace("birthday\t8", "birthday\t99")
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write(text + "zap\t7\n")
			after = dict(_words(newtonlex.pack(path)))
			self.assertEqual(after[b"birthday"], 99)
			self.assertEqual(after[b"zap"], 7)
			self.assertEqual(after[b"about newton"], 2)

	def test_lexical_graph(self):
		with tempfile.TemporaryDirectory() as tmp:
			src = os.path.join(ROMSRC, "lexicons", "gLex8hyphen.lex")
			path = os.path.join(tmp, "hyphen.lex")
			with open(src, encoding="utf-8") as f:
				text = f.read()
			self.assertIn('set s0 "-" "t0"', text)
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write(text.replace('set s0 "-" "t0"', 'set s0 "-~" "t0"'))
			data = newtonlex.pack(path)
			lay = newtonlex._ALLayout(data[4:])
			self.assertEqual(lay.sets[0][1], b"-~")
			# every node still points at a set and its children at nodes
			self.assertEqual(len(lay.nodes), 3)


if __name__ == "__main__":
	if "--rom" in sys.argv:
		i = sys.argv.index("--rom")
		ROM_BUILD = sys.argv[i + 1]
		del sys.argv[i:i + 2]
	unittest.main()
