#!/usr/bin/env python3
"""The Unicode, collation and locale tables of the ROM as text, and back.

The 'unicode frame (Runicode: frames/UnicodeTables.cpp's InitUnicode) and
the locale bundles carry their tables as binaries.  Each kind has a text
form whose first line after any comments is `format <kind>`, and
`pack(path)` makes the binary's bytes out of it - the ROM's own bytes for
every committed file:

  to-unicode      an encoding's mapToUnicode (TEncodingMap kind 0): the
                  UniChar each of the 256 bytes stands for
                  ("0x41 U+0041").
  from-unicode    its mapFromUnicode (kind 4): segments of Unicode
                  characters, each line a character and the byte it
                  becomes ("U+00C4 0x80"); a character the encoding has no
                  byte for is left out of every segment (it becomes 0x1a).
  char-classes    charClass: the class of each Mac Roman character - what
                  the case tables are indexed by ("0x41 2").
  class-types     typelist: each class's type flags ("2 0x01").
  class-deltas    upperList, lowerList, upperNoMarkList, noMarkList: what
                  is added to a character of each class to change its case
                  or take its marks off ("2 32").
  sort-table      a sorting table (frames/SortTables.h): the projection
                  (primary weight) and second-order weight of every
                  character in its ranges and its single characters, its
                  ligatures (a character that sorts as two) and the
                  lowest-sort table.
  break-table     a locale's wordBreakTable/lineBreakTable (the Macintosh
                  Script Manager's kind: qd/Text.cpp's FindWordBreaks): a
                  class per Mac Roman character and the backward and
                  forward state machines over the classes.

Usage:
    newtontables.py unpack FORMAT TABLE.bin OUT.txt
    newtontables.py pack TABLE.txt OUT.bin
    newtontables.py check FORMAT TABLE.bin...     (unpacked and packed back: the same?)

As a module: `unpack(data, format, path)`, `pack(path)` -> bytes.
`romsrc.py` writes them as `texttable(class, "resources/....txt")`.
"""

from __future__ import annotations

import os
import struct
import sys


def _h(d, o):
	return struct.unpack(">H", d[o:o + 2])[0]


def _char_comment(code: int) -> str:
	if code < 0x20 or 0x7f <= code < 0xa0 or code in (0x2028, 0x2029) or 0xd800 <= code <= 0xdfff or code >= 0xfff0:
		return ""
	return "\t# " + chr(code)


def _mac_comment(byte: int) -> str:
	if byte < 0x20 or byte == 0x7f:
		return ""
	return "\t# " + bytes([byte]).decode("mac_roman")


def _lines(path):
	"""The lines that are not comments, the format line first taken off."""
	with open(path, encoding="utf-8") as f:
		raw = f.read().split("\n")
	out = []
	for line in raw:
		line = line.split("#", 1)[0].strip()
		if line:
			out.append(line.split())
	if not out or out[0][0] != "format":
		raise ValueError("%s: no format line" % path)
	return out[0][1], out[1:]


def _u(word: str) -> int:
	if not word.upper().startswith("U+"):
		raise ValueError("%s is not U+XXXX" % word)
	return int(word[2:], 16)


# --- the encodings -----------------------------------------------------------

def _to_unicode_text(d):
	kind, size, flags, segs = _h(d, 0), _h(d, 2), _h(d, 4), _h(d, 6)
	if kind != 0 or size != 256 or len(d) != 8 + 512:
		raise ValueError("not a 256-entry mapToUnicode")
	out = ["format to-unicode",
		   "# an encoding's mapToUnicode: the Unicode character each byte stands for",
		   "header 0x%04x 0x%04x 0x%04x 0x%04x" % (kind, size, flags, segs)]
	for b in range(256):
		c = _h(d, 8 + 2 * b)
		out.append("0x%02x U+%04X%s" % (b, c, _char_comment(c)))
	return out


def _to_unicode_bytes(rows):
	head = [int(x, 0) for x in rows[0][1:]]
	table = [None] * 256
	for r in rows[1:]:
		table[int(r[0], 0)] = _u(r[1])
	if None in table:
		raise ValueError("every byte 0x00-0xff must have a line")
	return struct.pack(">4H", *head) + b"".join(struct.pack(">H", c) for c in table)


def _from_unicode_text(d):
	kind, size, flags, n = _h(d, 0), _h(d, 2), _h(d, 4), _h(d, 6)
	if kind != 4:
		raise ValueError("not a mapFromUnicode")
	ends = [_h(d, 8 + 2 * i) for i in range(n)]
	starts = [_h(d, 8 + 2 * n + 2 * i) for i in range(n)]
	offsets = [_h(d, 8 + 4 * n + 2 * i) for i in range(n)]
	table = d[8 + 6 * n:]
	out = ["format from-unicode",
		   "# an encoding's mapFromUnicode: segments of Unicode characters, each",
		   "# character and the byte it becomes; the segments' bytes lie one after",
		   "# another in the order written, and a character in no segment becomes",
		   "# 0x1a",
		   "header 0x%04x 0x%04x 0x%04x" % (kind, size, flags)]
	at = 0
	for s, e, o in zip(starts, ends, offsets):
		if (s + o) & 0xffff != at:
			raise ValueError("the segments' bytes do not lie one after another")
		out.append("segment U+%04X U+%04X" % (s, e))
		for c in range(s, e + 1):
			b = table[at]
			at += 1
			out.append("U+%04X 0x%02x%s" % (c, b, _char_comment(c)))
	if at != len(table):
		raise ValueError("bytes after the last segment")
	return out


def _from_unicode_bytes(rows):
	head = [int(x, 0) for x in rows[0][1:]]
	segs = []
	for r in rows[1:]:
		if r[0] == "segment":
			segs.append([_u(r[1]), _u(r[2]), []])
		else:
			c, b = _u(r[0]), int(r[1], 0)
			s = segs[-1]
			if c != s[0] + len(s[2]):
				raise ValueError("U+%04X is not the next character of its segment" % c)
			s[2].append(b)
	ends, starts, offsets, table = [], [], [], b""
	for s, e, bs in segs:
		if len(bs) != e - s + 1:
			raise ValueError("segment U+%04X-U+%04X has %d characters" % (s, e, len(bs)))
		ends.append(e)
		starts.append(s)
		offsets.append((len(table) - s) & 0xffff)
		table += bytes(bs)
	n = len(segs)
	return (struct.pack(">4H", head[0], head[1], head[2], n) + struct.pack(">%dH" % n, *ends)
			+ struct.pack(">%dH" % n, *starts) + struct.pack(">%dH" % n, *offsets) + table)


# --- the character classes ----------------------------------------------------

def _classes_text(d):
	if len(d) != 256:
		raise ValueError("not 256 classes")
	out = ["format char-classes",
		   "# the class of each Mac Roman character (charClass): what typelist and",
		   "# the case tables are indexed by"]
	for b in range(256):
		out.append("0x%02x %d%s" % (b, d[b], _mac_comment(b)))
	return out


def _classes_bytes(rows):
	table = [None] * 256
	for r in rows:
		table[int(r[0], 0)] = int(r[1], 0)
	if None in table:
		raise ValueError("every character 0x00-0xff must have a line")
	return bytes(table)


def _types_text(d):
	out = ["format class-types",
		   "# each character class's type flags (typelist), class 0 first"]
	for i, b in enumerate(d):
		out.append("%d 0x%02x" % (i, b))
	return out


def _types_bytes(rows):
	return bytes(int(r[1], 0) for r in sorted(rows, key=lambda r: int(r[0], 0)))


def _deltas_text(d):
	out = ["format class-deltas",
		   "# what is added to a character of each class (a case table: upperList,",
		   "# lowerList, upperNoMarkList or noMarkList), class 0 first"]
	for i, b in enumerate(d):
		out.append("%d %d" % (i, b - 256 if b > 127 else b))
	return out


def _deltas_bytes(rows):
	return bytes(int(r[1], 0) & 0xff for r in sorted(rows, key=lambda r: int(r[0], 0)))


# --- the sorting tables -----------------------------------------------------------

_SORT_KNOWN = {0x00, 0x06, 0x20, 0x24, 0x26, 0x28, 0x2a} | set(range(0x08, 0x20, 2))


def _sort_text(d):
	n_ranges = _h(d, 6)
	out = ["format sort-table",
		   "# a sorting table (frames/SortTables.h): each character's projection - its",
		   "# primary weight, 0 if it is ignored, 0xffff if it is really two (a",
		   "# ligature) - and its second-order weight",
		   "id %d" % _h(d, 0)]
	for o in range(0, 0x44, 2):
		if o not in _SORT_KNOWN and _h(d, o) != 0:
			out.append("header-word 0x%02x 0x%04x" % (o, _h(d, o)))
	at = 0x44
	for i in range(n_ranges):
		first, last = _h(d, 8 + 4 * i), _h(d, 10 + 4 * i)
		out.append("range U+%04X U+%04X" % (first, last))
		for c in range(first, last + 1):
			out.append("U+%04X 0x%04x 0x%04x%s" % (c, _h(d, at), _h(d, at + 2), _char_comment(c)))
			at += 4
	out.append("singles")
	for i in range(_h(d, 0x20)):
		c = _h(d, at)
		out.append("U+%04X 0x%04x 0x%04x%s" % (c, _h(d, at + 2), _h(d, at + 4), _char_comment(c)))
		at += 6
	if at != 0x44 + _h(d, 0x24):
		raise ValueError("the ligatures are not after the single characters")
	out.append("ligatures")
	out.append("# the character, the two it sorts as, the least character sorting the same")
	for i in range(_h(d, 0x26)):
		c, a, b, low = (_h(d, at + k) for k in (0, 2, 4, 6))
		out.append("U+%04X U+%04X U+%04X U+%04X%s" % (c, a, b, low, _char_comment(c)))
		at += 8
	if at != 0x44 + _h(d, 0x28):
		raise ValueError("the lowest-sort table is not after the ligatures")
	out.append("lowest")
	out.append("# for each primary weight, the least character having it")
	for i in range(_h(d, 0x2a)):
		c = _h(d, at)
		out.append("%d U+%04X%s" % (i, c, _char_comment(c)))
		at += 2
	if at != len(d):
		raise ValueError("bytes after the lowest-sort table")
	return out


def _sort_bytes(rows):
	header = bytearray(0x44)
	ranges, singles, ligatures, lowest = [], [], [], []
	part = None
	for r in rows:
		if r[0] == "id":
			struct.pack_into(">H", header, 0, int(r[1], 0) & 0xffff)
		elif r[0] == "header-word":
			struct.pack_into(">H", header, int(r[1], 0), int(r[2], 0))
		elif r[0] == "range":
			ranges.append([_u(r[1]), _u(r[2]), []])
			part = "range"
		elif r[0] in ("singles", "ligatures", "lowest"):
			part = r[0]
		elif part == "range":
			c = _u(r[0])
			rng = ranges[-1]
			if c != rng[0] + len(rng[2]):
				raise ValueError("U+%04X is not the next character of its range" % c)
			rng[2].append((int(r[1], 0), int(r[2], 0)))
		elif part == "singles":
			singles.append((_u(r[0]), int(r[1], 0), int(r[2], 0)))
		elif part == "ligatures":
			ligatures.append(tuple(_u(x) for x in r[:4]))
		elif part == "lowest":
			lowest.append(_u(r[1]))
		else:
			raise ValueError("what is %s?" % " ".join(r))
	if len(ranges) > 6:
		raise ValueError("a sorting table has at most six ranges")
	struct.pack_into(">H", header, 6, len(ranges))
	data = b""
	for i, (first, last, entries) in enumerate(ranges):
		if len(entries) != last - first + 1:
			raise ValueError("range U+%04X-U+%04X has %d characters" % (first, last, len(entries)))
		struct.pack_into(">HH", header, 8 + 4 * i, first, last)
		data += b"".join(struct.pack(">HH", p, s) for p, s in entries)
	singles.sort()
	struct.pack_into(">H", header, 0x20, len(singles))
	data += b"".join(struct.pack(">HHH", *e) for e in singles)
	struct.pack_into(">HH", header, 0x24, len(data), len(ligatures))
	data += b"".join(struct.pack(">4H", *e) for e in ligatures)
	struct.pack_into(">HH", header, 0x28, len(data), len(lowest))
	data += b"".join(struct.pack(">H", c) for c in lowest)
	return bytes(header) + data


# --- the break tables ------------------------------------------------------------

def _machine(d, start, end):
	"""A state machine: its row offsets (halfwords, from its start), then
	the rows, one after another to the end."""
	offsets = []
	at = start
	first = None
	while first is None or at < start + first:
		v = _h(d, at)
		offsets.append(v)
		if v and (first is None or v < first):
			first = v
		at += 2
		if first is None and at >= end:
			raise ValueError("a machine with no rows")
	rows = sorted({v for v in offsets if v})
	if rows[0] != at - start:
		raise ValueError("the rows do not follow the row offsets")
	width = rows[1] - rows[0] if len(rows) > 1 else end - start - rows[0]
	for a, b in zip(rows, rows[1:] + [end - start]):
		if b - a != width:
			raise ValueError("the rows are not all as wide")
	return offsets, rows, width


def _cell(b):
	return ("%d*" % (b & 0x7f)) if b & 0x80 else str(b)


def _break_text(d):
	h = [_h(d, o) for o in range(0, 0x10, 2)]
	ct, aux, back, fwd, length = h[2], h[3], h[4], h[5], h[7]
	if ct != 0x10 or aux != ct + 256 or length != len(d) or not (aux < back < fwd < length):
		raise ValueError("not a break table laid out as expected")
	out = ["format break-table",
		   "# a word- or line-break table (the Macintosh Script Manager's kind, read by",
		   "# qd/Text.cpp's FindWordBreaks): the header's flags and its backup",
		   "# distance, the class of each Mac Roman character, the auxiliary table,",
		   "# then the backward and forward state machines.  A state is named by",
		   "# where it is in its machine's list, two to an entry (state 2 is the",
		   "# second); `states` gives each entry's row by number (0: none), and a row",
		   "# is the next state for each class, a * marking where a word may begin",
		   "# or end (the scan starts in state 2 and stops in state 0)",
		   "flags 0x%04x 0x%04x" % (h[0], h[1]),
		   "backup %d" % h[6],
		   "classes"]
	for b in range(256):
		out.append("0x%02x %d%s" % (b, d[ct + b], _mac_comment(b)))
	out.append("aux " + d[aux:back].hex(" "))
	for name, s, e in (("backward", back, fwd), ("forward", fwd, length)):
		offsets, rows, width = _machine(d, s, e)
		index = {v: i for i, v in enumerate(rows)}
		out.append("%s %d" % (name, width))
		out.append("states " + " ".join(str(index[v] + 1) if v else "0" for v in offsets))
		for v in rows:
			out.append("row " + " ".join(_cell(b) for b in d[s + v:s + v + width]))
	return out


def _break_bytes(rows):
	flags = backup = None
	classes = [None] * 256
	aux = b""
	machines = []
	part = None
	for r in rows:
		if r[0] == "flags":
			flags = (int(r[1], 0), int(r[2], 0))
		elif r[0] == "backup":
			backup = int(r[1], 0)
		elif r[0] == "classes":
			part = "classes"
		elif r[0] == "aux":
			aux = bytes(int(x, 16) for x in r[1:])
		elif r[0] in ("backward", "forward"):
			machines.append([int(r[1], 0), [], []])
			part = None
		elif r[0] == "states":
			machines[-1][1] = [int(x) for x in r[1:]]
		elif r[0] == "row":
			machines[-1][2].append(bytes((int(x[:-1]) | 0x80) if x.endswith("*") else int(x) for x in r[1:]))
		elif part == "classes":
			classes[int(r[0], 0)] = int(r[1], 0) & 0xff
		else:
			raise ValueError("what is %s?" % " ".join(r))
	if None in classes:
		raise ValueError("every character 0x00-0xff must have a class")
	blobs = []
	for width, states, rws in machines:
		base = 2 * len(states)
		blob = b"".join(struct.pack(">H", base + (s - 1) * width if s else 0) for s in states)
		for row in rws:
			if len(row) != width:
				raise ValueError("a row of %d cells in a machine %d wide" % (len(row), width))
			blob += row
		blobs.append(blob)
	ct = 0x10
	aux_at = ct + 256
	back = aux_at + len(aux)
	fwd = back + len(blobs[0])
	length = fwd + len(blobs[1])
	header = struct.pack(">8H", flags[0], flags[1], ct, aux_at, back, fwd, backup, length)
	return header + bytes(classes) + aux + blobs[0] + blobs[1]


# --- the files ---------------------------------------------------------------------

_TEXT = {"to-unicode": _to_unicode_text, "from-unicode": _from_unicode_text, "char-classes": _classes_text,
		 "class-types": _types_text, "class-deltas": _deltas_text, "sort-table": _sort_text,
		 "break-table": _break_text}
_BYTES = {"to-unicode": _to_unicode_bytes, "from-unicode": _from_unicode_bytes, "char-classes": _classes_bytes,
		  "class-types": _types_bytes, "class-deltas": _deltas_bytes, "sort-table": _sort_bytes,
		  "break-table": _break_bytes}


def unpack(data: bytes, format: str, path: str) -> None:
	lines = _TEXT[format](data)
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines) + "\n")


def pack(path: str) -> bytes:
	format, rows = _lines(path)
	return _BYTES[format](rows)


def main(argv=None) -> int:
	argv = sys.argv[1:] if argv is None else argv
	if len(argv) == 4 and argv[0] == "unpack":
		with open(argv[2], "rb") as f:
			unpack(f.read(), argv[1], argv[3])
		return 0
	if len(argv) == 3 and argv[0] == "pack":
		with open(argv[2], "wb") as f:
			f.write(pack(argv[1]))
		return 0
	if len(argv) >= 3 and argv[0] == "check":
		import tempfile
		bad = 0
		with tempfile.TemporaryDirectory() as tmp:
			for p in argv[2:]:
				with open(p, "rb") as f:
					data = f.read()
				out = os.path.join(tmp, "t.txt")
				try:
					unpack(data, argv[1], out)
					same = pack(out) == data
				except ValueError as e:
					print("%s: %s" % (p, e))
					same = False
				print("%s: %s" % (os.path.basename(p), "the same" if same else "DIFFERS"))
				bad += not same
		return 1 if bad else 0
	print(__doc__)
	return 2


if __name__ == "__main__":
	sys.exit(main())
