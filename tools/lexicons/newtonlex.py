#!/usr/bin/env python3
"""The recognisers' lexicons as text, and back to the ROM's bytes.

The ROM carries its word lists and its lexical grammars (dates, times,
numbers, phone numbers, money...) as Airus dictionaries: a big-endian size
word, then the dictionary - 'a', a type byte, then the data
(docs/recognition/README.md, "The Airus engine" and "The ROM's word data").
There are two shapes, and each has a text form:

  * the enumerated kind (type & 7 = 3, 5 or 7: the gEnum8*/gSymb8* word
    lists) is a trie of characters; its text form, `.words`, is the words,
    one to a line, with an attribute after a tab when the dictionary gives
    words one.  The trie is a function of the set of words - each row's
    nodes in character order, each sibling offset in as few nibbles as hold
    it - so the order of the lines does not matter, and a word added or
    taken out is simply a line added or taken out.

  * the ROM lexicon kind (type & 7 = 1 or 2: the gLex8* grammars) is a
    graph, not a list - a node stands for a *set* of characters that all
    lead to the same place, the sets are shared and so are the nodes below
    them, which is how a date or a phone number is described in a few
    hundred bytes.  Its text form, `.lex`, is the sets and the nodes in the
    order they lie in the data, the nodes named and their children named
    by node, so a set can be changed, a node added or a child pointed
    elsewhere and the whole laid out again.

Characters are bytes in the Newton's 8-bit encoding, Mac Roman, written as
UTF-8 text; `\\xNN` stands for a control character, `\\\\` for a backslash,
and a space at either end of a word is written `\\x20` so that an editor
cannot lose it.

Usage:
    newtonlex.py unpack LEXICON.bin OUT.words|OUT.lex   (the size word first, as romsrc/lexicons keeps them)
    newtonlex.py pack TEXT OUT.bin
    newtonlex.py check LEXICON.bin...                    (each unpacked and packed back: byte for byte?)

`unpack` chooses the text form by the dictionary's type and answers the
path it wrote.  As a module: `unpack(data, path)`, `pack(path)` -> bytes
(with the size word), `text_suffix(data)`.
"""

from __future__ import annotations

import os
import re
import struct
import sys


# --- characters --------------------------------------------------------------

def _escape(chars: bytes, sixteen: bool = False, quote: bool = False) -> str:
	out = []
	if sixteen:
		codes = [int.from_bytes(chars[i:i + 2], "big") for i in range(0, len(chars), 2)]
		text = [chr(c) for c in codes]
		raw = codes
	else:
		text = list(chars.decode("mac_roman"))
		raw = list(chars)
	for i, (c, code) in enumerate(zip(text, raw)):
		if c == "\\":
			out.append("\\\\")
		elif quote and c == '"':
			out.append('\\"')
		elif code < 0x20 or code == 0x7f or (code > 0xff and not c.isprintable()):
			out.append("\\x%02x" % code if code <= 0xff else "\\u%04x" % code)
		elif c == " " and not quote and (i == 0 or i == len(text) - 1):
			out.append("\\x20")
		else:
			out.append(c)
	return "".join(out)


def _unescape(text: str, sixteen: bool = False) -> bytes:
	codes = []
	i = 0
	while i < len(text):
		c = text[i]
		if c == "\\":
			n = text[i + 1]
			if n == "x":
				codes.append(int(text[i + 2:i + 4], 16))
				i += 4
				continue
			if n == "u":
				codes.append(int(text[i + 2:i + 6], 16))
				i += 6
				continue
			codes.append(ord(n))
			i += 2
			continue
		codes.append(ord(c) if sixteen else c.encode("mac_roman")[0])
		i += 1
	if sixteen:
		return b"".join(struct.pack(">H", c) for c in codes)
	return bytes(codes)


# --- the enumerated kind -------------------------------------------------------

_NIBBLES = {0: 0, 1: 1, 2: 3, 3: 7}
_RP_BYTES = {0: 0, 1: 0, 2: 1, 3: 3}


def _enum_words(d: bytes):
	kind, attr = d[1] & 7, d[1] >> 4
	cs = 2 if kind in (2, 5) else 1
	words = []

	def walk(p, prefix):
		while True:
			ch = d[p:p + cs]
			fl = d[p + cs]
			cls = fl >> 6
			rp = 0
			if _NIBBLES[cls]:
				rp = fl & 0xf
				for i in range(_RP_BYTES[cls]):
					rp = (rp << 8) | d[p + cs + 1 + i]
			q = p + cs + 1 + _RP_BYTES[cls]
			w = prefix + ch
			if fl & 0x10:
				words.append((w, int.from_bytes(d[q:q + attr], "big") if attr else None))
				q += attr
			if not fl & 0x20:
				walk(q, w)
			if cls == 0:
				return
			p = q + rp

	if len(d) > 2:
		walk(2, b"")
	return words


def _enum_build(type_byte: int, words) -> bytes:
	kind, attr = type_byte & 7, type_byte >> 4
	cs = 2 if kind in (2, 5) else 1
	root = {}
	for w, a in words:
		node = root
		for i in range(0, len(w), cs):
			node = node.setdefault(w[i:i + cs], {})
		node[None] = a

	def row(level):
		out = []
		keys = sorted(k for k in level if k is not None)
		for i, k in enumerate(keys):
			child = level[k]
			sub = row(child) if any(c is not None for c in child) else b""
			fl = 0 if sub else 0x20
			abytes = b""
			if None in child:
				fl |= 0x10
				if attr:
					abytes = (child[None] or 0).to_bytes(attr, "big")
			rp = len(sub)
			if i == len(keys) - 1:
				cls, rpb = 0, b""
			elif rp < 0x10:
				cls, rpb = 1, b""
				fl |= rp
			elif rp < 0x1000:
				cls, rpb = 2, bytes([rp & 0xff])
				fl |= rp >> 8
			else:
				cls, rpb = 3, (rp & 0xffffff).to_bytes(3, "big")
				fl |= (rp >> 24) & 0xf
			out.append(k + bytes([fl | (cls << 6)]) + rpb + abytes + sub)
		return b"".join(out)

	return b"a" + bytes([type_byte]) + row(root)


# --- the ROM lexicon kind ------------------------------------------------------

class _ALLayout:
	"""The nodes and sets of an AL lexicon, in the order they lie."""

	def __init__(self, d: bytes):
		self.type = d[1]
		attr = d[1] >> 4
		self.attr = attr
		self.nodes = []				# (offset, set offset, flags, child offset or None, attribute)
		p = 2
		first_set = len(d)
		while p < first_set:
			sym = (d[p] << 8) | d[p + 1]
			fl = d[p + 2]
			child = None if fl & 2 else (d[p + 3] << 8) | d[p + 4]
			q = p + (3 if fl & 2 else 5)
			a = int.from_bytes(d[q:q + attr], "big") if attr else None
			self.nodes.append((p, sym, fl, child, a))
			first_set = min(first_set, sym)
			p = q + attr
		if p != first_set:
			raise ValueError("the nodes do not end where the first set begins (%#x, %#x)" % (p, first_set))
		self.sets = []				# (offset, characters, tag)
		while p < len(d):
			e = d.index(b"\0", p)
			f = d.index(b"\0", e + 1)
			self.sets.append((p, d[p:e], d[e + 1:f]))
			p = f + 1
		offsets = {o for o, *_ in self.sets}
		for n in self.nodes:
			if n[1] not in offsets:
				raise ValueError("a node's set at %#x is not where a set begins" % n[1])
		nodes = {o for o, *_ in self.nodes}
		for n in self.nodes:
			if n[3] is not None and n[3] not in nodes:
				raise ValueError("a node's child at %#x is not a node" % n[3])


def _al_text(d: bytes, name: str) -> str:
	lay = _ALLayout(d)
	sixteen = (d[1] & 7) == 2
	setname = {o: "s%d" % i for i, (o, _, _) in enumerate(lay.sets)}
	nodename = {n[0]: "n%d" % i for i, n in enumerate(lay.nodes)}
	out = ["# %s: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each" % name,
		   "# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;",
		   "# the format is in that file and in tools/lexicons/README.md.",
		   "type 0x%02x" % lay.type, ""]
	out.append("# the character sets, each with the tag the recogniser is told when a")
	out.append("# character of it matched")
	for o, chars, tag in lay.sets:
		out.append('set %s "%s" "%s"' % (setname[o], _escape(chars, sixteen, True), _escape(tag, False, True)))
	out.append("")
	out.append("# the nodes, in the order they lie (the first row is the root's): the")
	out.append("# set, 'word' if a word may end here, 'last' on the last of a row, the")
	out.append("# first child after '->'")
	for o, sym, fl, child, a in lay.nodes:
		parts = ["node", nodename[o], setname[sym]]
		if fl & 1:
			parts.append("word")
		if fl & 4:
			parts.append("last")
		extra = fl & ~7
		if extra:
			parts.append("flags=0x%02x" % extra)
		if a is not None:
			parts.append("attr=%d" % a)
		if child is not None:
			parts += ["->", nodename[child]]
		out.append(" ".join(parts))
	return "\n".join(out) + "\n"


_SET = re.compile(r'set\s+(\S+)\s+"((?:[^"\\]|\\.)*)"\s+"((?:[^"\\]|\\.)*)"\s*$')


def _al_build(lines) -> bytes:
	type_byte = None
	sets, nodes = [], []
	for line in lines:
		line = line.strip()
		if not line or line.startswith("#"):
			continue
		if line.startswith("type "):
			type_byte = int(line.split()[1], 0)
		elif line.startswith("set "):
			m = _SET.match(line)
			if not m:
				raise ValueError("bad set line: %s" % line)
			sets.append((m.group(1), m.group(2), m.group(3)))
		elif line.startswith("node "):
			nodes.append(line.split()[1:])
		else:
			raise ValueError("what is this? %s" % line)
	sixteen = (type_byte & 7) == 2
	attr = type_byte >> 4
	# the nodes from offset 2, then the sets
	node_at, p = {}, 2
	parsed = []
	for parts in nodes:
		name, setn, rest = parts[0], parts[1], parts[2:]
		fl, child, a = 0, None, 0
		i = 0
		while i < len(rest):
			r = rest[i]
			if r == "word":
				fl |= 1
			elif r == "last":
				fl |= 4
			elif r.startswith("flags="):
				fl |= int(r[6:], 0)
			elif r.startswith("attr="):
				a = int(r[5:], 0)
			elif r == "->":
				child = rest[i + 1]
				i += 1
			else:
				raise ValueError("what is %s in node %s?" % (r, name))
			i += 1
		if child is None:
			fl |= 2
		node_at[name] = p
		parsed.append((setn, fl, child, a))
		p += (3 if child is None else 5) + attr
	set_at, data = {}, b""
	for name, chars, tag in sets:
		set_at[name] = p + len(data)
		data += _unescape(chars, sixteen) + (b"\0\0" if sixteen else b"\0") + _unescape(tag) + b"\0"
	out = bytearray(b"a" + bytes([type_byte]))
	for setn, fl, child, a in parsed:
		out += struct.pack(">HB", set_at[setn], fl)
		if child is not None:
			out += struct.pack(">H", node_at[child])
		if attr:
			out += a.to_bytes(attr, "big")
	out += data
	if len(out) > 0x10000:
		raise ValueError("an AL lexicon's offsets are sixteen bits: %d bytes is too big" % len(out))
	return bytes(out)


# --- the files -------------------------------------------------------------------

def text_suffix(data: bytes) -> str:
	"""The text form a lexicon (with its size word) takes."""
	return ".lex" if (data[5] & 7) in (1, 2) else ".words"


def unpack(data: bytes, path: str) -> None:
	"""A lexicon (its size word, then the dictionary) written as text."""
	size = struct.unpack(">I", data[:4])[0]
	d = data[4:4 + size]
	name = os.path.splitext(os.path.basename(path))[0]
	if (d[1] & 7) in (1, 2):
		text = _al_text(d, name)
	else:
		sixteen = (d[1] & 7) in (2, 5)
		lines = ["# %s: an Airus word list (an enumerated dictionary): one word to a" % name,
				 "# line, in any order, with its attribute after a tab when it has one.",
				 "# Built by tools/lexicons/newtonlex.py; tools/lexicons/README.md.",
				 "type 0x%02x" % d[1]]
		for w, a in _enum_words(d):
			t = _escape(w, sixteen)
			if t.startswith("#") or t.startswith("type "):
				t = "\\x%02x" % ord(t[0]) + t[1:]
			lines.append(t if a is None else "%s\t%d" % (t, a))
		text = "\n".join(lines) + "\n"
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)


def pack(path: str) -> bytes:
	"""A text lexicon built: its size word, then the dictionary."""
	with open(path, encoding="utf-8") as f:
		lines = f.read().split("\n")
	if path.endswith(".lex"):
		d = _al_build(lines)
	else:
		type_byte = None
		words = []
		for line in lines:
			if not line or line.startswith("#"):
				continue
			if line.startswith("type ") and type_byte is None:
				type_byte = int(line.split()[1], 0)
				continue
			sixteen = (type_byte & 7) in (2, 5)
			if "\t" in line:
				w, a = line.split("\t", 1)
				words.append((_unescape(w, sixteen), int(a, 0)))
			else:
				words.append((_unescape(line, sixteen), None))
		d = _enum_build(type_byte, words)
	return struct.pack(">I", len(d)) + d


def main(argv=None) -> int:
	argv = sys.argv[1:] if argv is None else argv
	if len(argv) >= 1 and argv[0] == "unpack" and len(argv) == 3:
		with open(argv[1], "rb") as f:
			unpack(f.read(), argv[2])
		return 0
	if len(argv) >= 1 and argv[0] == "pack" and len(argv) == 3:
		data = pack(argv[1])
		with open(argv[2], "wb") as f:
			f.write(data)
		return 0
	if len(argv) >= 2 and argv[0] == "check":
		import tempfile
		bad = 0
		with tempfile.TemporaryDirectory() as tmp:
			for path in argv[1:]:
				with open(path, "rb") as f:
					data = f.read()
				out = os.path.join(tmp, os.path.splitext(os.path.basename(path))[0] + text_suffix(data))
				try:
					unpack(data, out)
					same = pack(out) == data
				except ValueError as e:
					same = False
					print("%s: %s" % (path, e))
				print("%s: %s" % (os.path.basename(path), "the same" if same else "DIFFERS"))
				bad += not same
		return 1 if bad else 0
	print(__doc__)
	return 2


if __name__ == "__main__":
	sys.exit(main())
