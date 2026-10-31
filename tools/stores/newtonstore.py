#!/usr/bin/env python3
"""A package store - a Newton package's 'soup part - as text, and back.

A package can carry a read-only store of soups: a part of type 'soup,
kind raw, which the package manager mounts as a TPackageStore
(src/stores/PackageStore.cpp; docs/stores/README.md).  The ROM extension's
WorldData package is one: the countries and cities Setup's "where are
you" page and Time Zones query.  `unpack` turns the part's bytes into a
directory a person can read and edit - above all the soup entries, one
NewtonScript frame to a line - and `pack` makes the part's bytes back out
of it: the ROM's own bytes, byte for byte, for WorldData.

How it can: a package store is a store as the MessagePad's own soup code
wrote it (on a flash store, then copied out with its objects renumbered
in the order they were made), and nothing in it but the soups, their
entries and the order they were made in is free.  So `pack` does what the
MessagePad did: it formats an empty store, creates the soups and adds the
entries in that order, through a re-expression in Python of the ROM's own
code - the store object writer (TStoreObjectWriter: the tagged stream, the
precedents, the map and symbol tables, the compressed text object, the
word hints), the soup indexes' B-trees (TSoupIndex: key fields, node splits,
duplicate nodes) and the order the objects are made in, which is their ids.
Everything else - the root object, the map and symbol tables, the index
infos, every B-tree node, every entry's header, hint chunks and text - is
derived, so an entry edited, added or removed brings its indexes, hints,
text and every id after it along.

The directory:

  store.ns     the store's persistent frame (`store := {...};`) and each
               soup's (`soup("Name", {...});`), in the order the soups were
               created; `indexobject()` stands where the frames hold the id
               of an index's B-tree (its IndexInfo object), which pack
               assigns
  entries.ns   the history, in the order it happened: `soup("Name");`
               creates that soup (the first time) and makes it the one
               the entries after it go to; each `{...};` is an entry, one
               to a line, `_uniqueID`, `_modTime` and `name` first, then
               the other slots by name (any order may be written: pack
               puts them in the store's order, by symbol hash)
  overrides.txt  only when the derivation does not give an object's bytes
               back: `<id>\\t<label>\\t<digest>\\t<patch>` - the object's id,
               what it is ("Cities's index on 'country node 1"), the first
               16 hex digits of the SHA-1 of the bytes pack derives for it
               and the bytes the part has where they differ (`offset:hex`
               runs, or `=hex` for the whole object); pack applies it only
               while it derives those very bytes for that object, so an
               edit that changes the object drops it (with a warning).
               WorldData has one: a B-tree node's parent id (below)

The value notation is romsrc's (tools/newton-rom/analysis/romsrc.py):
12, nil, true, $a, $\\u00e9, @12 (a magic pointer), #1a3 (another
immediate), 'name, '|odd name|, "text" (a string of class 'string, its
terminator implied), string('cls, "text"), bytes('cls, "0a1b") (a binary),
stringbytes('cls, "0041") (a string's raw UniChar bytes, when they are not
text and a terminator), {tag: value, ...}, [value, ...] (class 'array),
[cls: value, ...], array(cls, value, ...); `//` and `/* */` comments.
Within an object, a symbol met twice is the same object (the store keeps
the precedent); every other object is written once, as the MessagePad
would have it for a frame built from the text.

What unpack refuses (no store this tool has met has them): a non-symbol
object shared within one store object, large binaries (tag 12), index
types other than string, int, symbol, char, real and multi-slot ones of
those, 'tags indexes, index descriptions with a sort table (string keys
are compared as the ROM does with none: folded to upper case without
diacritics through the Mac Roman class tables - OldCompareText), and a
history that is not "format, then create soups and add entries" - an
entry changed or removed on the MessagePad leaves objects this cannot
make, whose bytes go to overrides.txt; a store whose object count
differs cannot be unpacked at all.

WorldData's one override: the first node of the Cities soup's country
index (object 41) holds parent id 0x84 where pack derives 0.  A node's
parent field is whatever ReadANode last set it to when the node was
last written - meaningless, as every read sets it again - and pack gets
it right for every other node of the store (111 of them hold a stale
one); this one the MessagePad wrote once more after the index's root
split, unchanged but for that field, through something the history of
adds does not show (an entry changed, a key deleted and added back).

Usage:
    newtonstore.py unpack PART DIR [--package]
    newtonstore.py pack DIR OUT
    newtonstore.py check PART... [--package]    (unpacked and packed back: the same?)
    newtonstore.py chartables ROMSRC            (the character tables below, from a romsrc tree)

PART is the soup part's bytes; with --package it is a package file whose
first 'soup part is taken (romsrc/rex/WorldData.pkg).  As a module:
`unpack(data, path)`, `pack(path)` -> bytes, `soup_part(package)` ->
(offset, size), `replace_soup_part(package, part)` -> the package with
that part in place of its 'soup part (the sizes in its directory fixed).

Pure Python 3, no dependencies.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import struct
import sys


# --- the character tables ---------------------------------------------------
#
# What the ROM folds and splits text with, as the 'unicode frame and the
# ASCII break table give it: generated by `newtonstore.py chartables romsrc`
# from romsrc's Mac Roman mapToUnicode, charClass and upperNoMarkList and
# the break table (magic pointer @6), and checked against them by
# test_newtonstore.py.
#   MAC_ROMAN   the UniChar of each Mac Roman byte (its mapFromUnicode is
#               the inverse; a character not in it becomes 0x1a)
#   FOLD        each byte upper-cased without diacritics: byte +
#               upperNoMarkList[charClass[byte]], as a byte
#   DELIMITERS  1 for a byte the break table calls a delimiter

MAC_ROMAN = bytes.fromhex(
	"0000000100020003000400050006000700080009000a000b000c000d000e000f"
	"0010001100120013001400150016001700180019001a001b001c001d001e001f"
	"0020002100220023002400250026002700280029002a002b002c002d002e002f"
	"0030003100320033003400350036003700380039003a003b003c003d003e003f"
	"0040004100420043004400450046004700480049004a004b004c004d004e004f"
	"0050005100520053005400550056005700580059005a005b005c005d005e005f"
	"0060006100620063006400650066006700680069006a006b006c006d006e006f"
	"0070007100720073007400750076007700780079007a007b007c007d007e007f"
	"00c400c500c700c900d100d600dc00e100e000e200e400e300e500e700e900e8"
	"00ea00eb00ed00ec00ee00ef00f100f300f200f400f600f500fa00f900fb00fc"
	"202000b000a200a300a7202200b600df00ae00a9212200b400a8226000c600d8"
	"221e00b12264226500a500b522022211220f03c0222b00aa00ba212600e600f8"
	"00bf00a100ac221a01922248220600ab00bb202600a000c000c300d501520153"
	"20132014201c201d2018201900f725ca00ff0178204400a42039203afb01fb02"
	"202100b7201a201e203000c200ca00c100cb00c800cd00ce00cf00cc00d300d4"
	"f7ff00d200da00db00d9013102c602dc00af02d802d902da00b802dd02db02c7")
FOLD = bytes.fromhex(
	"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
	"202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
	"404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
	"604142434445464748494a4b4c4d4e4f505152535455565758595a7b7c7d7e7f"
	"414143454e4f554141414141414345454545494949494e4f4f4f4f4f55555555"
	"a0a1a2a3a4a5a6a7a8a9aaabacadae4fb0b1b2b3b4b5b6b7b8b9babbbcbdae4f"
	"c0c1c2c3c4c5c6c7c8c9ca41414fceced0d1d2d3d4d5d6d75959dadbdcdddedf"
	"e0e1e2e3e44145414545494949494f4ff04f55555549f6f7f8f9fafbfcfdfeff")
DELIMITERS = (
	"1111111111111111111111111111111111111111111111110000000000111111"
	"1000000000000000000000000000000000000000000000000000000000000000"
	"0000000000000000000000000000000011111111111111111111111111111100"
	"1111111111100000111111110000000000000000000000000000000000000000")


def _mac_roman_inverse():
	inv = {}
	for b in range(256):
		inv[struct.unpack_from(">H", MAC_ROMAN, 2 * b)[0]] = b
	return inv


_FROM_UNICODE = None


def mac_roman_byte(c: int) -> int:
	"""A_CONST_CHAR: the character's Mac Roman byte (ASCII as itself), 0x1a
	for one the encoding has none for."""
	global _FROM_UNICODE
	if c < 0x80:
		return c
	if _FROM_UNICODE is None:
		_FROM_UNICODE = _mac_roman_inverse()
	return _FROM_UNICODE.get(c, 0x1a)


def is_delimiter(c: int) -> bool:
	"""IsDelimiter: the ASCII break table's byte for the Mac Roman character."""
	return DELIMITERS[mac_roman_byte(c)] == "1"


def canonical_character(c: int) -> int:
	"""CanonicalCharacter: upper case, no diacritics, as a Mac Roman byte."""
	if c == 0:
		return 0
	a = mac_roman_byte(c)
	if a == 0x1a:
		return 0x1a
	return FOLD[a]


def folded_for_compare(c: int) -> int:
	"""What OldCompareText compares a character as (not exact): its folded
	Mac Roman byte, or - one with no Mac Roman form - its low byte (the
	ROM's behaviour, which made the stores; the host's ROM BUG fix compares
	such a character whole)."""
	a = mac_roman_byte(c)
	if a == 0x1a:
		return c & 0xff
	return FOLD[a]


# --- the Unicode text coder (compression/UnicodeCompression.cpp) -------------

# the blocks (high bytes) whose characters are run-coded: gUnicodeLookupTable
UNICODE_RUN_BLOCKS = bytes.fromhex("be7e80" + "00" * 29)


def _run_coded(high: int) -> bool:
	return (UNICODE_RUN_BLOCKS[high >> 3] & (0x80 >> (high & 7))) != 0


def unicode_compress(text: bytes) -> bytes:
	"""TUnicodeCompressor over big-endian UniChars: a run of characters of
	one run-coded block as its high byte, a count (at most 255) and the low
	bytes; any other character as its two bytes."""
	out = bytearray()
	run_high = None
	run = bytearray()
	for i in range(0, len(text) - 1, 2):
		high, low = text[i], text[i + 1]
		if run:
			if run_high == high:
				run.append(low)
				if len(run) > 0xfe:
					out += bytes((run_high, len(run))) + run
					run = bytearray()
				continue
			out += bytes((run_high, len(run))) + run
			run = bytearray()
		if _run_coded(high):
			run = bytearray((low,))
			run_high = high
		else:
			out += bytes((high, low))
	if run:
		out += bytes((run_high, len(run))) + run
	return bytes(out)


def unicode_decompress(data: bytes) -> bytes:
	out = bytearray()
	i = 0
	while i + 1 < len(data):
		high, count = data[i], data[i + 1]
		i += 2
		if _run_coded(high):
			for low in data[i:i + count]:
				out += bytes((high, low))
			i += count
		else:
			out += bytes((high, count))
	return bytes(out)


# --- values ----------------------------------------------------------------

class Sym:
	"""A symbol; symbols are the same object when their names are the same
	but for the case of ASCII letters (as Intern makes them)."""
	__slots__ = ("name",)

	def __init__(self, name: str):
		self.name = name

	def key(self):
		return self.name.encode("latin-1").upper()

	def __eq__(self, other):
		return isinstance(other, Sym) and self.key() == other.key()

	def __hash__(self):
		return hash(("sym", self.key()))

	def __repr__(self):
		return "'" + self.name


class Imm:
	"""An immediate Ref that is not an integer, nil, true or a character:
	the 32-bit ref itself."""
	__slots__ = ("ref",)

	def __init__(self, ref: int):
		self.ref = ref & 0xffffffff

	def __eq__(self, other):
		return isinstance(other, Imm) and self.ref == other.ref

	def __hash__(self):
		return hash(("imm", self.ref))


class Char:
	__slots__ = ("code",)

	def __init__(self, code: int):
		self.code = code

	def __eq__(self, other):
		return isinstance(other, Char) and self.code == other.code

	def __hash__(self):
		return hash(("char", self.code))


class _True:
	def __repr__(self):
		return "true"


TRUE = _True()
TRUE_REF = 0x1a


class Binary:
	"""A binary object: its class and its bytes.  A string (class 'string
	or a subclass) has big-endian UniChars."""
	__slots__ = ("cls", "data")

	def __init__(self, cls, data: bytes):
		self.cls = cls
		self.data = bytes(data)


class Array:
	__slots__ = ("cls", "items")

	def __init__(self, cls, items):
		self.cls = cls
		self.items = list(items)


class Frame:
	"""A frame: its slots in order, (Sym, value) pairs."""
	__slots__ = ("slots",)

	def __init__(self, slots=None):
		self.slots = list(slots or [])

	def get(self, name, default=None):
		key = Sym(name)
		for tag, value in self.slots:
			if tag == key:
				return value
		return default

	def has(self, name) -> bool:
		key = Sym(name)
		return any(tag == key for tag, _ in self.slots)

	def set(self, name, value):
		key = Sym(name)
		for i, (tag, _) in enumerate(self.slots):
			if tag == key:
				self.slots[i] = (tag, value)
				return
		self.slots.append((Sym(name), value))


class IndexObject:
	"""Where a frame holds the id of an index's IndexInfo object."""
	pass


SYM_STRING = Sym("string")
SYM_ARRAY = Sym("array")


def is_string_class(cls) -> bool:
	"""IsInstance(x, 'string) for a binary's class: 'string or a dotted
	subclass of it."""
	if not isinstance(cls, Sym):
		return False
	k = cls.key()
	return k == b"STRING" or k.startswith(b"STRING.")


def make_string(text: str, cls=None) -> Binary:
	data = text.encode("utf-16-be", "surrogatepass") + b"\0\0"
	return Binary(cls if cls is not None else Sym("string"), data)


def string_text_of(b: Binary):
	"""The text of a string that is UniChars and one terminator, else None."""
	d = b.data
	if len(d) < 2 or len(d) & 1 or d[-2:] != b"\0\0":
		return None
	body = d[:-2]
	for i in range(0, len(body), 2):
		if body[i] == 0 and body[i + 1] == 0:
			return None
	return body.decode("utf-16-be", "surrogatepass")


# --- the notation: printing ----------------------------------------------------

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
RESERVED = {"and", "begin", "break", "by", "call", "constant", "div", "do", "else", "end", "exists",
			"for", "foreach", "func", "global", "if", "in", "inherited", "local", "loop", "mod",
			"native", "not", "onexception", "or", "repeat", "return", "self", "then", "to", "try",
			"until", "while", "with", "nil", "true"}


def quote_name(name: str) -> str:
	if IDENT.match(name) and name.lower() not in RESERVED:
		return name
	return "|" + name.replace("\\", "\\\\").replace("|", "\\|") + "|"


def string_literal(text: str) -> str:
	out = ['"']
	for ch in text:
		c = ord(ch)
		if ch == '"':
			out.append('\\"')
		elif ch == "\\":
			out.append("\\\\")
		elif 0x20 <= c < 0x7f or (c >= 0xa0 and not 0xd800 <= c < 0xe000 and c not in (0xfffe, 0xffff)):
			out.append(ch)
		else:
			out.append("\\u%04x" % c)
	out.append('"')
	return "".join(out)


def format_value(v) -> str:
	if v is None:
		return "nil"
	if v is TRUE:
		return "true"
	if isinstance(v, bool):
		raise TypeError("a bool is not a value")
	if isinstance(v, int):
		return str(v)
	if isinstance(v, Char):
		c = v.code
		if 0x21 <= c < 0x7f and chr(c) != "\\":
			return "$" + chr(c)
		return "$\\u%04x" % c
	if isinstance(v, Imm):
		if v.ref & 3 == 3 and v.ref < 0x80000000:
			return "@%d" % (v.ref >> 2)
		return "#%x" % v.ref
	if isinstance(v, Sym):
		return "'" + quote_name(v.name)
	if isinstance(v, IndexObject):
		return "indexobject()"
	if isinstance(v, Binary):
		if is_string_class(v.cls):
			text = string_text_of(v)
			if text is None:
				return "stringbytes(%s, \"%s\")" % (format_value(v.cls), v.data.hex())
			if v.cls == SYM_STRING and v.cls.name == "string":
				return string_literal(text)
			return "string(%s, %s)" % (format_value(v.cls), string_literal(text))
		return "bytes(%s, \"%s\")" % (format_value(v.cls), v.data.hex())
	if isinstance(v, Array):
		inner = ", ".join(format_value(x) for x in v.items)
		if isinstance(v.cls, Sym):
			if v.cls == SYM_ARRAY and v.cls.name == "array":
				return "[" + inner + "]"
			return "[" + quote_name(v.cls.name) + ":" + (" " + inner if inner else "") + "]"
		return "array(" + ", ".join([format_value(v.cls)] + [format_value(x) for x in v.items]) + ")"
	if isinstance(v, Frame):
		return "{" + ", ".join("%s: %s" % (quote_name(t.name), format_value(x)) for t, x in v.slots) + "}"
	raise TypeError("cannot write %r" % (v,))


# --- the notation: reading ----------------------------------------------------

_TOKEN = re.compile(r"""
	(?P<ws>[ \t\r\n]+)
	| (?P<comment>//[^\n]*|/\*.*?\*/)
	| (?P<string>"(?:[^"\\]|\\.)*")
	| (?P<symbol>'(?:\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*))
	| (?P<char>\$(?:\\u[0-9A-Fa-f]{4}|\\.|[^\\]))
	| (?P<magic>@[0-9]+)
	| (?P<imm>\#[0-9A-Fa-f]+)
	| (?P<number>-?(?:0x[0-9A-Fa-f]+|[0-9]+))
	| (?P<quoted>\|(?:[^|\\]|\\.)*\|)
	| (?P<name>[A-Za-z_][A-Za-z0-9_]*)
	| (?P<punct>:=|[{}\[\](),:;])
	""", re.VERBOSE | re.DOTALL)


def _unquote_bar(text: str) -> str:
	body = text[1:-1]
	return re.sub(r"\\(.)", r"\1", body)


def _string_value(literal: str) -> str:
	out = []
	body = literal[1:-1]
	i = 0
	while i < len(body):
		ch = body[i]
		if ch == "\\":
			nxt = body[i + 1]
			if nxt == "u":
				out.append(chr(int(body[i + 2:i + 6], 16)))
				i += 6
				continue
			out.append({"n": "\n", "t": "\t", "r": "\r"}.get(nxt, nxt))
			i += 2
			continue
		out.append(ch)
		i += 1
	return "".join(out)


class Parser:
	def __init__(self, text: str, where: str):
		self.where = where
		self.tokens = []
		pos = 0
		line = 1
		while pos < len(text):
			m = _TOKEN.match(text, pos)
			if m is None:
				raise SyntaxError("%s:%d: cannot read %r" % (where, line, text[pos:pos + 20]))
			kind = m.lastgroup
			if kind not in ("ws", "comment"):
				self.tokens.append((kind, m.group(), line))
			line += m.group().count("\n")
			pos = m.end()
		self.i = 0

	def at_end(self):
		return self.i >= len(self.tokens)

	def peek(self):
		return self.tokens[self.i] if self.i < len(self.tokens) else (None, None, 0)

	def take(self, expect=None):
		tok = self.peek()
		if tok[0] is None:
			raise SyntaxError("%s: unexpected end" % self.where)
		if expect is not None and tok[1] != expect:
			raise SyntaxError("%s:%d: %r expected, found %r" % (self.where, tok[2], expect, tok[1]))
		self.i += 1
		return tok

	def error(self, message):
		return SyntaxError("%s:%d: %s" % (self.where, self.peek()[2], message))

	def name(self) -> str:
		kind, text, _ = self.take()
		if kind == "name":
			return text
		if kind == "quoted":
			return _unquote_bar(text)
		self.i -= 1
		raise self.error("a name expected, found %r" % text)

	def value(self):
		kind, text, line = self.take()
		if kind == "number":
			return int(text, 0)
		if kind == "string":
			return make_string(_string_value(text))
		if kind == "symbol":
			body = text[1:]
			return Sym(_unquote_bar(body) if body.startswith("|") else body)
		if kind == "char":
			body = text[1:]
			if body.startswith("\\u"):
				return Char(int(body[2:], 16))
			if body.startswith("\\"):
				return Char(ord(body[1]))
			return Char(ord(body))
		if kind == "magic":
			return Imm((int(text[1:]) << 2) | 3)
		if kind == "imm":
			return Imm(int(text[1:], 16))
		if kind == "punct" and text == "{":
			slots = []
			while self.peek()[1] != "}":
				tag = self.name()
				self.take(":")
				slots.append((Sym(tag), self.value()))
				if self.peek()[1] == ",":
					self.take(",")
				elif self.peek()[1] != "}":
					raise self.error("',' or '}' expected")
			self.take("}")
			return Frame(slots)
		if kind == "punct" and text == "[":
			cls = Sym("array")
			# [cls: ...] - a name followed by a colon
			k2, t2, _ = self.peek()
			if k2 in ("name", "quoted") and self.i + 1 < len(self.tokens) and self.tokens[self.i + 1][1] == ":":
				cls = Sym(self.name())
				self.take(":")
			items = []
			while self.peek()[1] != "]":
				items.append(self.value())
				if self.peek()[1] == ",":
					self.take(",")
				elif self.peek()[1] != "]":
					raise self.error("',' or ']' expected")
			self.take("]")
			return Array(cls, items)
		if kind == "name":
			low = text.lower()
			if low == "nil":
				return None
			if low == "true":
				return TRUE
			if self.peek()[1] == "(":
				self.take("(")
				args = []
				while self.peek()[1] != ")":
					args.append(self.value())
					if self.peek()[1] == ",":
						self.take(",")
				self.take(")")
				return self.call(low, args, line)
		raise SyntaxError("%s:%d: a value expected, found %r" % (self.where, line, text))

	def call(self, fn, args, line):
		def text_arg(a):
			t = string_text_of(a) if isinstance(a, Binary) else None
			if t is None:
				raise SyntaxError("%s:%d: %s wants a string" % (self.where, line, fn))
			return t
		if fn == "indexobject" and not args:
			return IndexObject()
		if fn == "string" and len(args) == 2:
			return make_string(text_arg(args[1]), args[0])
		if fn == "bytes" and len(args) == 2:
			return Binary(args[0], bytes.fromhex(text_arg(args[1])))
		if fn == "stringbytes" and len(args) == 2:
			return Binary(args[0], bytes.fromhex(text_arg(args[1])))
		if fn == "array" and args:
			return Array(args[0], args[1:])
		raise SyntaxError("%s:%d: unknown constructor %s/%d" % (self.where, line, fn, len(args)))


# --- symbols and maps (frames/Symbols.cpp, stores/StoreWrapper.cpp) ----------

def symbol_hash(name: bytes) -> int:
	"""SymbolHashFunction: the upper-cased (ASCII) characters' sum times the
	golden ratio's 32 bits."""
	total = 0
	for b in name:
		if 0x61 <= b <= 0x7a:
			b -= 0x20
		total += b
	return (total * 0x9E3779B9) & 0xffffffff


def _sym_bytes(sym: Sym) -> bytes:
	return sym.name.encode("latin-1")


def _symcmp_key(name: bytes) -> bytes:
	return bytes(b - 0x20 if 0x61 <= b <= 0x7a else b for b in name)


def _rotr(x: int, n: int) -> int:
	n &= 0x1f
	x &= 0xffffffff
	return x if n == 0 else ((x >> n) | (x << (32 - n))) & 0xffffffff


class HashTable:
	"""TStoreHashTable: 64 buckets, each an object of [2-byte length][bytes]
	entries; a reference is bucket << 16 | offset."""

	def __init__(self, store, table_id: int, label: str):
		self.store = store
		self.id = table_id
		self.label = label
		self.buckets = [0] * 64

	def insert(self, hash_value: int, data: bytes) -> int:
		bucket = hash_value & 63
		bucket_id = self.buckets[bucket]
		entry = struct.pack(">H", len(data)) + data
		if bucket_id == 0:
			bucket_id = self.store.new_object(b"", "%s bucket %d" % (self.label, bucket))
			self.buckets[bucket] = bucket_id
			table = bytearray(self.store.objects[self.id])
			struct.pack_into(">I", table, bucket * 4, bucket_id)
			self.store.objects[self.id] = bytes(table)
			offset = 0
		else:
			content = self.store.objects[bucket_id]
			offset = 0
			while offset < len(content):
				length = struct.unpack_from(">H", content, offset)[0]
				if length == len(data) and content[offset + 2:offset + 2 + length] == data:
					return (bucket << 16) + offset
				offset += length + 2
		self.store.objects[bucket_id] = self.store.objects[bucket_id][:offset] + entry
		return (bucket << 16) + offset


def read_hash_entry(objects, table_id: int, reference: int) -> bytes:
	bucket_id = struct.unpack_from(">I", objects[table_id], (reference >> 16) * 4)[0]
	content = objects[bucket_id]
	offset = reference & 0xffff
	length = struct.unpack_from(">H", content, offset)[0]
	return content[offset + 2:offset + 2 + length]


# --- the word hints (stores/StoreObject.cpp "The word hints") ----------------

def hash_quadgram(quad: int, position: int) -> int:
	return (_rotr(quad, position & 0x1f) * 0x9e3779b9) & 0xffffffff


def _set_hint_bit(chunk, h):
	bit = (h & 0xffffffff) >> 26
	chunk[bit >> 5] |= 1 << (31 - (bit & 0x1f))


def set_word_hints(chunk, word) -> None:
	"""TWordHintsHandler::SetHints (handler 1)."""
	quad = canonical_character(word[1]) | (canonical_character(word[0]) << 8) | 0x200000
	for i in range(2, len(word)):
		quad = (canonical_character(word[i]) | (quad << 8)) & 0xffffffff
		h = hash_quadgram(quad, i)
		_set_hint_bit(chunk, h)
		if i < 4:
			h = hash_quadgram(h, i)
			_set_hint_bit(chunk, h)
			h = hash_quadgram(h, i)
			_set_hint_bit(chunk, h)
			if i == 2:
				h = hash_quadgram(h, 2)
				_set_hint_bit(chunk, h)


def num_hint_chunks(text_chars: int) -> int:
	if text_chars < 0x1fe0:
		return ((text_chars >> 5) + 1) & 0xff
	return 1


def find_hint_word(text, pos, remaining):
	"""TWordHintsHandler::FindHintWord: (start, length, remaining) of the
	next word of three characters or more, None when there is none."""
	while True:
		while True:
			if remaining == 0:
				return None
			if not is_delimiter(text[pos]):
				break
			if text[pos] == 0:
				return None
			pos += 1
			remaining -= 1
		end = pos
		while remaining != 0 and not is_delimiter(text[end]):
			end += 1
			remaining -= 1
		length = end - pos
		if length > 2:
			return pos, length, remaining
		pos += length


# --- the store object writer (TStoreObjectWriter) ----------------------------

def _long_bytes(v: int) -> bytes:
	if 0 <= v < 0xff:
		return bytes((v,))
	return b"\xff" + struct.pack(">I", v & 0xffffffff)


def _frame_tags_sorted(frame: Frame):
	"""GetFrameMapTags sorted: (index, tag) by symbol hash, then name -
	unless the frame keeps its order (an _implementor slot)."""
	tagged = list(enumerate(frame.slots))
	if frame.has("_implementor"):
		return [(i, t) for i, (t, _) in tagged]
	tagged.sort(key=lambda it: (symbol_hash(_sym_bytes(it[1][0])), _symcmp_key(_sym_bytes(it[1][0]))))
	return [(i, t) for i, (t, _) in tagged]


_HIDDEN = {b"_PROTO"}
_ROOT_HIDDEN = {b"_PROTO", b"_UNIQUEID", b"_MODTIME"}


def _immediate_ref(v) -> int:
	if isinstance(v, int):
		if not -(1 << 29) <= v < (1 << 29):
			raise ValueError("%d is too big for a 30-bit integer" % v)
		return (v << 2) & 0xffffffff
	if v is TRUE:
		return TRUE_REF
	if isinstance(v, Imm):
		return v.ref
	raise TypeError(v)


def _is_small_rect(frame: Frame):
	if len(frame.slots) != 4:
		return None
	sides = []
	for name in ("top", "left", "bottom", "right"):
		v = frame.get(name, None)
		if not isinstance(v, int) or isinstance(v, bool) or not 0 <= v <= 0xff:
			return None
		sides.append(v)
	return bytes(sides)


class ObjectWriter:
	"""One store object written as StorePermObject writes it: the stream
	(maps and symbols entered in the tables as met), the strings' text,
	the hint chunks, the header."""

	def __init__(self, store, root, label):
		self.store = store
		self.root = root
		self.label = label
		self.stream = bytearray()
		self.text = bytearray()
		self.precedents = {}
		self.count = 0
		# the hints
		self.text_size = self._prescan_text(root, True)
		self.num_hints = num_hint_chunks(self.text_size >> 1)
		self.hints = [[0, 0] for _ in range(self.num_hints)]
		self.chunk = 0
		self.hint_words = 0

	def _prescan_text(self, v, is_root):
		"""Prescan's text size: the strings' lengths (each object once)."""
		seen = set()
		total = 0
		stack = [(v, is_root)]
		while stack:
			x, root = stack.pop()
			if isinstance(x, (Binary, Array, Frame)):
				if id(x) in seen:
					continue
				seen.add(id(x))
			if isinstance(x, Binary):
				if is_string_class(x.cls):
					total += len(x.data)
			elif isinstance(x, Array):
				stack.extend((y, False) for y in x.items)
			elif isinstance(x, Frame):
				hidden = _ROOT_HIDDEN if root else _HIDDEN
				stack.extend((y, False) for t, y in x.slots if _sym_bytes(t).upper() not in hidden)
		return total

	def _precedent(self, v):
		key = ("sym", v.key()) if isinstance(v, Sym) else ("obj", id(v))
		return key

	def scan(self, v, is_root=False):
		s = self.stream
		if v is None:
			s.append(10)
			return
		if isinstance(v, Char):
			if v.code < 0x100:
				s += bytes((1, v.code))
			else:
				s += bytes((2, v.code >> 8, v.code & 0xff))
			return
		if isinstance(v, IndexObject):
			raise ValueError("indexobject() outside a store or soup frame")
		if isinstance(v, (int, Imm)) or v is TRUE:
			ref = _immediate_ref(v)
			s.append(0)
			s += _long_bytes(ref if ref < 0x80000000 else ref - 0x100000000)
			return
		key = self._precedent(v)
		if key in self.precedents:
			s.append(9)
			s += _long_bytes(self.precedents[key])
			return
		self.precedents[key] = self.count
		self.count += 1
		if isinstance(v, Frame):
			packed = _is_small_rect(v)
			if packed is not None:
				s.append(11)
				s += packed
				return
			s.append(6)
			hidden = _ROOT_HIDDEN if is_root else _HIDDEN
			kept = [(i, t) for i, t in _frame_tags_sorted(v) if _sym_bytes(t).upper() not in hidden]
			names = struct.pack(">H", len(kept))
			h = 0
			for n, (i, t) in enumerate(kept):
				h = _rotr(h ^ symbol_hash(_sym_bytes(t)), n)
				names += _sym_bytes(t) + b"\0"
			ref = self.store.maps.insert(h, names)
			s += struct.pack(">I", ref)[1:]
			for i, t in kept:
				self.scan(v.slots[i][1])
			return
		if isinstance(v, Array):
			plain = v.cls == SYM_ARRAY
			s.append(5 if plain else 4)
			s += _long_bytes(len(v.items))
			if not plain:
				self.scan(v.cls)
			for x in v.items:
				self.scan(x)
			return
		if isinstance(v, Sym):
			name = _sym_bytes(v)
			s.append(7)
			ref = self.store.symbols.insert(symbol_hash(name), name)
			s += struct.pack(">I", ref)[1:]
			return
		if isinstance(v, Binary):
			is_string = is_string_class(v.cls)
			s.append(8 if is_string else 3)
			s += _long_bytes(len(v.data))
			self.scan(v.cls)
			if not is_string:
				s += v.data
				return
			self.text += v.data
			if not (isinstance(v.cls, Sym) and v.cls.key() == b"STRING.NOHINT"):
				chars = [struct.unpack_from(">H", v.data, i)[0] for i in range(0, len(v.data) - 1, 2)]
				chars.append(0)		# (the ROM reads no further than remaining allows)
				start = 0
				word = 0
				remaining = (len(v.data) >> 1) - 1
				while True:
					found = find_hint_word(chars, word, remaining)
					if found is None:
						break
					word, length, remaining = found
					set_word_hints(self.hints[self.chunk], chars[word:word + length])
					word += length
					n = self.hint_words + (word - start)
					self.hint_words = n
					start = word
					while 32 < n:
						self._next_chunk()
						n -= 32
			return
		raise TypeError("cannot store %r" % (v,))

	def _next_chunk(self):
		if self.chunk < self.num_hints - 1:
			self.chunk += 1
			self.hints[self.chunk] = [0, 0]
		else:
			self.hints[self.chunk] = [0xffffffff, 0xffffffff]
		self.hint_words = 0

	def write(self, object_id=None):
		"""==> the object's id (a new one unless object_id is given: then
		it is rewritten, its text object kept)."""
		self.scan(self.root, True)
		while self.chunk < self.num_hints - 1:
			self._next_chunk()
		text_id = 0
		old_text = 0
		if object_id is not None:
			old_text = struct.unpack_from(">I", self.store.objects[object_id], 8)[0]
		if self.text:
			compressed = unicode_compress(bytes(self.text))
			if old_text:
				text_id = old_text
				self.store.objects[text_id] = compressed
			else:
				text_id = self.store.new_object(compressed, "the text of " + self.label)
		uid, mod = -1, 0xffffffff
		if isinstance(self.root, Frame):
			u = self.root.get("_uniqueID")
			if u is not None:
				uid = u
			m = self.root.get("_modTime")
			if m is not None:
				mod = m & 0x3fffffff
		header = struct.pack(">iII", uid, mod, text_id)
		header += bytes((self.num_hints, 2, (len(self.text) >> 8) & 0xff, len(self.text) & 0xff))
		for c in self.hints:
			header += struct.pack(">II", c[0] & 0xffffffff, c[1] & 0xffffffff)
		data = header + bytes(self.stream)
		if object_id is None:
			return self.store.new_object(data, self.label)
		self.store.objects[object_id] = data
		return object_id


# --- the store object reader (TStoreObjectReader) ----------------------------

class Unsupported(Exception):
	pass


class ObjectReader:
	def __init__(self, objects, map_table, symbol_table, object_id):
		self.objects = objects
		self.maps = map_table
		self.symbols = symbol_table
		d = objects[object_id]
		self.data = d
		self.uid, self.mod, self.text_id, self.num_hints, self.flags, ths, tls = struct.unpack_from(">iIIBBBB", d, 0)
		self.text_size = (ths << 8) | tls
		self.pos = 16 + 8 * self.num_hints
		self.text = unicode_decompress(objects[self.text_id]) if self.text_id else b""
		self.text_pos = 0
		self.precedents = []

	def byte(self):
		v = self.data[self.pos]
		self.pos += 1
		return v

	def long(self):
		v = self.byte()
		if v == 0xff:
			v = struct.unpack_from(">i", self.data, self.pos)[0]
			self.pos += 4
		return v

	def ref3(self):
		v = int.from_bytes(self.data[self.pos:self.pos + 3], "big")
		self.pos += 3
		return v

	def read(self):
		v = self.scan()
		if isinstance(v, Frame):
			slots = []
			if self.uid != -1:
				slots.append((Sym("_uniqueID"), self.uid))
			if self.mod != 0xffffffff:
				m = self.mod if self.mod < 0x80000000 else self.mod - 0x100000000
				slots.append((Sym("_modTime"), m))
			v.slots = slots + v.slots
		return v

	def scan(self):
		tag = self.byte()
		if tag == 0:
			ref = self.long() & 0xffffffff
			if ref & 3 == 0:
				return (ref - (1 << 32) if ref & 0x80000000 else ref) >> 2
			if ref == TRUE_REF:
				return TRUE
			if ref == 2:
				raise Unsupported("nil written as an immediate")
			if ref & 0xf == 6:
				raise Unsupported("a character written as an immediate")
			return Imm(ref)
		if tag == 1:
			return Char(self.byte())
		if tag == 2:
			hi = self.byte()
			return Char((hi << 8) | self.byte())
		if tag in (3, 8):
			length = self.long()
			obj = Binary(None, b"")
			self.precedents.append(obj)
			obj.cls = self.scan()
			if tag == 8:
				if not is_string_class(obj.cls):
					raise Unsupported("a string whose class is not 'string")
				obj.data = self.text[self.text_pos:self.text_pos + length]
				self.text_pos += length
			else:
				if is_string_class(obj.cls):
					raise Unsupported("a binary of a string class")
				obj.data = self.data[self.pos:self.pos + length]
				self.pos += length
			return obj
		if tag in (4, 5):
			length = self.long()
			obj = Array(SYM_ARRAY, [])
			self.precedents.append(obj)
			if tag == 4:
				obj.cls = self.scan()
				if obj.cls == SYM_ARRAY:
					raise Unsupported("an array of class 'array written with its class")
			obj.items = [self.scan() for _ in range(length)]
			return obj
		if tag == 6:
			names = read_hash_entry(self.objects, self.maps, self.ref3())
			count = struct.unpack_from(">H", names, 0)[0]
			tags = names[2:].split(b"\0")[:count]
			obj = Frame()
			self.precedents.append(obj)
			obj.slots = [(Sym(t.decode("latin-1")), self.scan()) for t in tags]
			return obj
		if tag == 7:
			obj = Sym(read_hash_entry(self.objects, self.symbols, self.ref3()).decode("latin-1"))
			self.precedents.append(obj)
			return obj
		if tag == 9:
			obj = self.precedents[self.long()]
			if not isinstance(obj, Sym):
				raise Unsupported("an object other than a symbol shared within a store object")
			return obj
		if tag == 10:
			return None
		if tag == 11:
			b = self.data[self.pos:self.pos + 4]
			self.pos += 4
			obj = Frame([(Sym("top"), b[0]), (Sym("left"), b[1]), (Sym("bottom"), b[2]), (Sym("right"), b[3])])
			self.precedents.append(obj)
			return obj
		if tag == 12:
			raise Unsupported("a large binary")
		raise Unsupported("tag %d" % tag)


# --- soup indexes (stores/SoupIndex.cpp) -------------------------------------

KEY_STRING, KEY_LONG, KEY_CHAR, KEY_DOUBLE, KEY_ASCII, KEY_RAW, KEY_MULTI = range(7)
KEY_SIZES = (0, 4, 2, 8, 0, 0, 0)
NODE_SIZE = 0x200
NODE_HEADER = 0x0e
DUP_HEADER = 0x10
KF_BUFFER = 100


class BuildError(Exception):
	"""What an index refuses: a key of the wrong type or too big, a
	duplicate _uniqueID."""
	pass


def _h(b, o):
	return (b[o] << 8) | b[o + 1]


def _w(b, o):
	return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def _ph(b, o, v):
	b[o] = (v >> 8) & 0xff
	b[o + 1] = v & 0xff


def _pw(b, o, v):
	b[o:o + 4] = struct.pack(">I", v & 0xffffffff)


def _pad(n):
	return n + 1 if n & 1 else n


def _s32(v):
	v &= 0xffffffff
	return v - (1 << 32) if v & 0x80000000 else v


class IndexInfo:
	"""The 0x1c bytes of an index's info object."""

	def __init__(self, key_type, data_type=KEY_LONG, duplicates=2, multi_types=0xffffffff,
				 multi_ascending=0xff, descending=0):
		self.root = 0
		self.node_size = NODE_SIZE
		self.key_type = key_type
		self.data_type = data_type
		self.duplicates = duplicates
		self.multi_types = multi_types
		self.multi_ascending = multi_ascending
		self.descending = descending

	def to_bytes(self):
		return struct.pack(">IiiiiIBBBB", self.root, self.node_size, self.key_type, self.data_type,
						   self.duplicates, self.multi_types, self.multi_ascending, self.descending, 0, 0)


class SoupIndex:
	"""TSoupIndex: the B-tree in 512-byte nodes, written as the ROM writes
	it.  The nodes stay in memory (self.store.nodes, expanded as the node
	cache holds them); each operation ends by writing the nodes it dirtied
	in their compact form, as TNodeCache::Commit does."""

	def __init__(self, store, info_id, info: IndexInfo, label):
		self.store = store
		self.info_id = info_id
		self.info = info
		self.label = label
		self.fixed_key = KEY_SIZES[info.key_type]
		self.fixed_data = KEY_SIZES[info.data_type]
		self.nodes_made = 0

	# comparisons
	def compare_type(self, t, a, ao, b, bo):
		if t == KEY_STRING:
			na, nb = a[ao + 1] >> 1, b[bo + 1] >> 1
			for i in range(min(na, nb)):
				ca = folded_for_compare(_h(a, ao + 2 + 2 * i))
				cb = folded_for_compare(_h(b, bo + 2 + 2 * i))
				if ca != cb:
					return ca - cb
			return na - nb
		if t == KEY_LONG:
			return _s32(_w(a, ao) - _w(b, bo))
		if t == KEY_CHAR:
			ca, cb = _h(a, ao), _h(b, bo)
			return -1 if ca < cb else (1 if ca > cb else 0)
		if t == KEY_DOUBLE:
			da = struct.unpack(">d", bytes(a[ao:ao + 8]))[0]
			db = struct.unpack(">d", bytes(b[bo:bo + 8]))[0]
			return -1 if da < db else (1 if da > db else 0)
		if t == KEY_ASCII:
			sa, sb = a[ao + 1], b[bo + 1]
			for i in range(min(sa, sb)):
				ca, cb = a[ao + 2 + i], b[bo + 2 + i]
				if 0x41 <= ca <= 0x5a:
					ca += 0x20
				if 0x41 <= cb <= 0x5a:
					cb += 0x20
				if ca != cb:
					return -1 if ca < cb else 1
			if sa > sb:
				return 1
			if sa < sb:
				return -1
			return 0
		if t == KEY_RAW:
			sa, sb = a[ao + 1], b[bo + 1]
			n = min(sa, sb)
			x, y = bytes(a[ao + 2:ao + 2 + n]), bytes(b[bo + 2:bo + 2 + n])
			if x != y:
				return -1 if x < y else 1
			return (sa > sb) - (sa < sb)
		if t == KEY_MULTI:
			return self.compare_multi(a, ao, b, bo)
		raise BuildError("key type %d" % t)

	def compare_multi(self, a, ao, b, bo):
		types = self.info.multi_types
		ascending = self.info.multi_ascending
		pa, a_end = ao + 2, ao + 2 + a[ao + 1]
		pb, b_end = bo + 2, bo + 2 + b[bo + 1]
		a_missing, b_missing = a[ao], b[bo]
		while True:
			if pa == a_end:
				if pb == b_end:
					return 0
				return 1 if a[ao] & 0x80 else -1
			if pb == b_end:
				return -1 if b[bo] & 0x80 else 1
			if ((a_missing | b_missing) & 1) == 0:
				t = types & 0x0f
				result = self.compare_type(t, a, pa, b, pb)
				if result == 0:
					size = KEY_SIZES[t]
					if size == 0:
						pa += _pad(a[pa + 1] + 2)
						pb += _pad(b[pb + 1] + 2)
					else:
						pa += size
						pb += size
			elif (a_missing & 1) == 0:
				result = 1
			elif (b_missing & 1) == 0:
				result = -1
			else:
				result = 0
			if result != 0:
				return result if ascending & 1 else -result
			types >>= 4
			ascending >>= 1
			a_missing >>= 1
			b_missing >>= 1

	def compare_keys(self, a, ao, b, bo):
		r = self.compare_type(self.info.key_type, a, ao, b, bo)
		return -r if self.info.descending else r

	def compare_data(self, a, ao, b, bo):
		return self.compare_type(self.info.data_type, a, ao, b, bo)

	# key fields: (buffer, offset)
	def size_of_key(self, b, o):
		return self.fixed_key or b[o + 1] + 2

	def size_of_data(self, b, o):
		return self.fixed_data or b[o + 1] + 2

	@staticmethod
	def kf_size(b, o):
		return _h(b, o) & 0x3fff

	@staticmethod
	def kf_flags(b, o):
		return _h(b, o) >> 14

	@staticmethod
	def set_kf_size(b, o, size):
		_ph(b, o, (_h(b, o) & 0xc000) | (size & 0x3fff))

	@staticmethod
	def set_kf_flags(b, o, flags):
		_ph(b, o, (flags << 14) | (_h(b, o) & 0x3fff))

	def first_data(self, b, o):
		return o + 2 + _pad(self.size_of_key(b, o + 2))

	def next_data(self, b, o, data):
		if data is None:
			return self.first_data(b, o), True
		p = data + _pad(self.size_of_data(b, data))
		return p, p < o + self.kf_size(b, o) - 6

	def dup_count(self, b, o):
		if self.kf_flags(b, o) != 1:
			return 1
		return _s32(_h(b, o + self.kf_size(b, o) - 6) << 16) >> 16

	def next_dup_id(self, b, o):
		if self.kf_flags(b, o) != 1:
			return 0
		return _w(b, o + self.kf_size(b, o) - 4)

	def set_dup_count(self, b, o, n):
		if self.kf_flags(b, o) == 1:
			_ph(b, o + self.kf_size(b, o) - 6, n & 0xffff)

	def set_next_dup_id(self, b, o, i):
		if self.kf_flags(b, o) == 1:
			_pw(b, o + self.kf_size(b, o) - 4, i)

	def copy_datum(self, to, to_o, data, size):
		to[to_o:to_o + size] = data[:size]
		if size & 1:
			to[to_o + size] = 0

	def kf_insert_data(self, b, o, where, data):
		size = self.kf_size(b, o)
		dsize = self.size_of_data(data, 0)
		padded = _pad(dsize)
		if self.kf_flags(b, o) != 1:
			return
		b[where + padded:o + size + padded] = b[where:o + size]
		self.copy_datum(b, where, data, dsize)
		self.set_kf_size(b, o, size + padded)
		self.set_dup_count(b, o, self.dup_count(b, o) + 1)

	def kf_find_data(self, b, o, data):
		p, more = self.next_data(b, o, None)
		while more:
			if self.compare_data(data, 0, b, p) == 0:
				return p
			p, more = self.next_data(b, o, p)
		return None

	def assemble_key_field(self, kf, key, data):
		key_size = self.size_of_key(key, 0)
		data_offset = _pad(key_size + 2)
		data_size = self.size_of_data(data, 0) if data is not None else 0
		if key_size + _pad(data_size) + 4 > KF_BUFFER:
			raise BuildError("key too big")
		self.set_kf_flags(kf, 0, 0)
		self.set_kf_size(kf, 0, data_offset + _pad(data_size))
		kf[2:2 + key_size] = key[:key_size]
		if key_size & 1:
			kf[2 + key_size] = 0
		if data_size:
			self.copy_datum(kf, data_offset, data, data_size)

	def convert_key_field(self, kf):
		if self.kf_flags(kf, 0) != 0:
			return
		self.set_kf_flags(kf, 0, 1)
		self.set_kf_size(kf, 0, self.kf_size(kf, 0) + 6)
		self.set_dup_count(kf, 0, 1)
		self.set_next_dup_id(kf, 0, 0)

	# nodes
	def read_node(self, node_id, parent_id):
		node = self.store.nodes.get(node_id)
		if node is None:
			raise BuildError("node %d is not in memory" % node_id)
		_pw(node, 0, node_id)
		_pw(node, 4, parent_id)
		return node

	def read_dup_node(self, node_id):
		node = self.store.nodes.get(node_id)
		if node is None:
			raise BuildError("dup node %d is not in memory" % node_id)
		_pw(node, 0, node_id)
		return node

	def new_node(self):
		self.nodes_made += 1
		node_id = self.store.new_object(b"", "%s node %d" % (self.label, self.nodes_made))
		node = bytearray(NODE_SIZE)
		_pw(node, 0, node_id)
		_pw(node, 4, 0)
		_ph(node, 8, NODE_SIZE - 0x14)
		_ph(node, 10, 0)
		_ph(node, 12, NODE_SIZE - 2)
		kf = NODE_SIZE - 2
		node[kf] = 0
		node[kf + 1] = 0
		_pw(node, kf - 4, 0)
		self.store.nodes[node_id] = node
		self.store.dup_nodes.discard(node_id)
		self.store.dirty.add(node_id)
		return node

	def new_dup_node(self):
		self.nodes_made += 1
		node_id = self.store.new_object(b"", "%s duplicates node %d" % (self.label, self.nodes_made))
		node = bytearray(NODE_SIZE)
		_pw(node, 0, node_id)
		_pw(node, 4, 0)
		_ph(node, 8, NODE_SIZE - DUP_HEADER)
		_ph(node, 10, 0)
		_ph(node, 12, DUP_HEADER)
		_ph(node, 14, 0)
		self.store.nodes[node_id] = node
		self.store.dup_nodes.add(node_id)
		self.store.dirty.add(node_id)
		return node

	def commit(self):
		for node_id in sorted(self.store.dirty):
			node = self.store.nodes[node_id]
			size = NODE_SIZE - _h(node, 8)
			if node_id in self.store.dup_nodes:
				self.store.objects[node_id] = bytes(node[:size])
			else:
				header = NODE_HEADER + _h(node, 10) * 2
				self.store.objects[node_id] = bytes(node[:header]) + bytes(node[header + _h(node, 8):header + _h(node, 8) + size - header])
		self.store.dirty.clear()

	def write_info(self):
		self.store.objects[self.info_id] = self.info.to_bytes()

	@staticmethod
	def num_keys(node):
		return _h(node, 10)

	@staticmethod
	def bytes_remaining(node):
		return _h(node, 8)

	@staticmethod
	def offset(node, slot):
		return _h(node, 12 + 2 * slot)

	def left_node(self, node, slot):
		return _w(node, self.offset(node, slot) - 4)

	def right_node(self, node, slot):
		return _w(node, self.offset(node, slot + 1) - 4)

	def set_node_no(self, node, slot, node_id):
		_pw(node, self.offset(node, slot) - 4, node_id)

	def key_field_base(self, node):
		return self.bytes_remaining(node) + self.num_keys(node) * 2 + NODE_HEADER

	def room_in_node(self, node, kb, ko):
		return self.kf_size(kb, ko) + 6 < self.bytes_remaining(node)

	def node_underflow(self, node):
		return self.bytes_remaining(node) > self.info.node_size // 2

	def put_key_into_node(self, kb, ko, right_id, node, slot):
		if not self.room_in_node(node, kb, ko):
			raise BuildError("key too big for the node")
		size = self.kf_size(kb, ko)
		field = bytes(kb[ko:ko + size])
		where = self.key_field_base(node) - size
		node[where:where + size] = field
		n = self.num_keys(node)
		start = 12 + (slot + 1) * 2
		node[start:start + (n - slot + 1) * 2] = node[12 + slot * 2:12 + slot * 2 + (n - slot + 1) * 2]
		_ph(node, 12 + slot * 2, where)
		left = self.right_node(node, slot)
		self.set_node_no(node, slot, left)
		self.set_node_no(node, slot + 1, right_id)
		_ph(node, 10, n + 1)
		_ph(node, 8, self.bytes_remaining(node) - (size + 6))

	def delete_key_from_node(self, node, slot):
		n = self.num_keys(node)
		if slot < 0 or slot >= n:
			raise BuildError("no key at slot %d" % slot)
		self.set_node_no(node, slot + 1, self.left_node(node, slot))
		kf = self.offset(node, slot)
		removed = self.kf_size(node, kf) + 4
		base = self.key_field_base(node)
		deleted = kf
		node[base + removed:kf - 4 + removed] = node[base:kf - 4]
		node[12 + slot * 2:12 + slot * 2 + (n - slot) * 2] = node[12 + (slot + 1) * 2:12 + (slot + 1) * 2 + (n - slot) * 2]
		n -= 1
		_ph(node, 10, n)
		_ph(node, 8, self.bytes_remaining(node) + removed + 2)
		for i in range(n):
			o = self.offset(node, i)
			if o < deleted + removed:
				_ph(node, 12 + i * 2, o + removed)

	def key_in_node(self, kb, ko, node):
		lo, hi = 0, self.num_keys(node) - 1
		mid = 0
		cmp = 0
		while lo <= hi:
			mid = lo + (hi - lo) // 2
			cmp = self.compare_keys(kb, ko + 2, node, self.offset(node, mid) + 2)
			if cmp == 0:
				return True, self.left_node(node, mid), mid
			if cmp < 0:
				hi = mid - 1
			else:
				lo = mid + 1
		if cmp > 0:
			mid += 1
		return False, self.left_node(node, mid), mid

	def move_key(self, fb, fo, tb, to):
		size = self.kf_size(fb, fo)
		if size:
			tb[to:to + size] = bytes(fb[fo:fo + size])
			return True
		self.set_kf_size(tb, to, 0)
		return False

	# duplicates
	def append_dup_data(self, node, data):
		size = self.size_of_data(data, 0)
		padded = _pad(size)
		if padded > self.bytes_remaining(node):
			return False
		used = _h(node, 12)
		self.copy_datum(node, used, data, size)
		_ph(node, 8, self.bytes_remaining(node) - padded)
		_ph(node, 12, used + padded)
		_ph(node, 10, _h(node, 10) + 1)
		return True

	def check_for_dup_data(self, b, o, data):
		if self.info.duplicates == 0:
			raise BuildError("a duplicate key in an index of unique keys")
		if self.kf_find_data(b, o, data) is not None:
			raise BuildError("the datum is there already")
		if self.kf_flags(b, o) != 0:
			dup = self.next_dup_id(b, o)
			while dup:
				node = self.read_dup_node(dup)
				p = DUP_HEADER
				while p < _h(node, 12):
					if self.compare_data(data, 0, node, p) == 0:
						raise BuildError("the datum is there already")
					p += _pad(self.size_of_data(node, p))
				dup = _w(node, 4)

	def store_dup_data(self, kf, data):
		dup = self.next_dup_id(kf, 0)
		if dup:
			while True:
				node = self.read_dup_node(dup)
				dup = _w(node, 4)
				if not dup:
					break
			if self.size_of_data(data, 0) > self.bytes_remaining(node):
				new = self.new_dup_node()
				self.append_dup_data(new, data)
				_pw(node, 4, _w(new, 0))
			else:
				self.append_dup_data(node, data)
			self.store.dirty.add(_w(node, 0))
			return
		if self.size_of_data(data, 0) + self.kf_size(kf, 0) < KF_BUFFER:
			end, more = self.next_data(kf, 0, None)
			while more:
				end, more = self.next_data(kf, 0, end)
			self.kf_insert_data(kf, 0, end, data)
		else:
			new = self.new_dup_node()
			self.append_dup_data(new, data)
			self.set_next_dup_id(kf, 0, _w(new, 0))

	def insert_dup_data(self, kf, node, slot):
		"""==> (rightId, split)."""
		first = self.first_data(kf, 0)
		data = bytes(kf[first:first + self.size_of_data(kf, first)])
		here = self.offset(node, slot)
		self.check_for_dup_data(node, here, data)
		if self.info.duplicates == 1:
			raise BuildError("a duplicate key")
		right = self.right_node(node, slot)
		self.move_key(node, here, kf, 0)
		self.delete_key_from_node(node, slot)
		self.convert_key_field(kf)
		self.store_dup_data(kf, data)
		if not self.room_in_node(node, kf, 0):
			right = self.split_node(kf, right, node, slot)
			return right, True
		self.put_key_into_node(kf, 0, right, node, slot)
		self.store.dirty.add(_w(node, 0))
		return right, False

	def insert_key(self, kf, node):
		"""==> (rightId, split)."""
		found, child, slot = self.key_in_node(kf, 0, node)
		if found:
			return self.insert_dup_data(kf, node, slot)
		if child == 0:
			split, right = True, 0
		else:
			right, split = self.insert_key(kf, self.read_node(child, _w(node, 0)))
		if split:
			_, child, slot = self.key_in_node(kf, 0, node)
			if not self.room_in_node(node, kf, 0):
				right = self.split_node(kf, right, node, slot)
				return right, True
			self.put_key_into_node(kf, 0, right, node, slot)
			self.store.dirty.add(_w(node, 0))
			return right, False
		return right, split

	def split_node(self, kf, right, node, slot):
		"""SplitANode: ==> the new node's id (the key pushed up is in kf)."""
		new = self.new_node()
		n = self.num_keys(node)
		if slot == n:
			self.put_key_into_node(kf, 0, right, new, 0)
		last = n - 1
		while last > 1 and self.node_underflow(new):
			self.put_key_into_node(node, self.offset(node, last), self.right_node(node, last), new, 0)
			self.delete_key_from_node(node, last)
			if slot == last:
				self.put_key_into_node(kf, 0, right, new, 0)
			last -= 1
		if slot <= last:
			self.put_key_into_node(kf, 0, right, node, slot)
			last += 1
		self.set_node_no(new, 0, self.right_node(node, last))
		self.move_key(node, self.offset(node, last), kf, 0)
		self.delete_key_from_node(node, last)
		self.store.dirty.add(_w(node, 0))
		return _w(new, 0)

	def create_new_root(self, kf, right):
		node = self.new_node()
		self.put_key_into_node(kf, 0, right, node, 0)
		self.set_node_no(node, 0, self.info.root)
		self.info.root = _w(node, 0)
		self.write_info()

	def add(self, key: bytes, data: bytes):
		"""TSoupIndex::Add: the key with its datum; the nodes it changed
		written."""
		kf = bytearray(256)
		self.assemble_key_field(kf, bytearray(key) + bytes(0x50), bytearray(data) + bytes(0x50))
		if self.info.root == 0:
			node = self.new_node()
			self.info.root = _w(node, 0)
			self.write_info()
		root = self.read_node(self.info.root, 0)
		right, split = self.insert_key(kf, root)
		if split:
			self.create_new_root(kf, right)
		self.commit()


def _expand_node(stored: bytes) -> bytearray:
	"""ReadANode: a compact node as the cache holds it."""
	node = bytearray(NODE_SIZE)
	node[:len(stored)] = stored
	n = _h(node, 10)
	key_data = NODE_HEADER + n * 2
	gap = _h(node, 8)
	moved = bytes(node[key_data:NODE_SIZE - gap])
	node[key_data + gap:key_data + gap + len(moved)] = moved
	node[key_data:key_data + gap] = bytes(gap)
	return node


def walk_index(objects, info_id):
	"""(key bytes as an SKey, [data bytes]) of every key of an index, in
	order (the key's datum in its field first, then its duplicate nodes')."""
	info = struct.unpack_from(">IiiiiIBB", objects[info_id], 0)
	root, key_type, data_type = info[0], info[2], info[3]
	fixed_key, fixed_data = KEY_SIZES[key_type], KEY_SIZES[data_type]
	out = []

	def key_size(b, o):
		return fixed_key or b[o + 1] + 2

	def data_size(b, o):
		return fixed_data or b[o + 1] + 2

	def walk(node_id):
		node = _expand_node(objects[node_id])
		n = _h(node, 10)
		for slot in range(n + 1):
			o = _h(node, 12 + 2 * slot)
			child = _w(node, o - 4)
			if child:
				walk(child)
			if slot == n:
				break
			size = _h(node, o) & 0x3fff
			flags = _h(node, o) >> 14
			key = bytes(node[o + 2:o + 2 + key_size(node, o + 2)])
			end = o + size - (6 if flags == 1 else 0)
			p = o + 2 + _pad(len(key))
			data = []
			while p < end:
				data.append(bytes(node[p:p + data_size(node, p)]))
				p += _pad(data_size(node, p))
			if flags == 1:
				dup = _w(node, o + size - 4)
				while dup:
					d = objects[dup]
					q = DUP_HEADER
					while q < _h(d, 12):
						data.append(bytes(d[q:q + data_size(d, q)]))
						q += _pad(data_size(d, q))
					dup = _w(d, 4)
			out.append((key, data))

	if root:
		walk(root)
	return out


# --- keys from entries (Soups.cpp's KeyToSKey, MultiKeyToSKey, GetEntryKey) ---

def _type_key(t):
	"""IndexDescToIndexInfo's key type of an index type symbol."""
	if not isinstance(t, Sym):
		raise Unsupported("an index type that is not a symbol")
	k = t.key()
	table = {b"STRING": KEY_STRING, b"INT": KEY_LONG, b"REAL": KEY_DOUBLE, b"CHAR": KEY_CHAR, b"SYMBOL": KEY_ASCII}
	if k not in table:
		raise Unsupported("index type %s" % t.name)
	return table[k]


def index_info_of(desc: Frame) -> IndexInfo:
	"""IndexDescToIndexInfo."""
	if desc.has("sortID"):
		raise Unsupported("an index with a sort table")
	path = desc.get("path")
	t = desc.get("type")
	order = desc.get("order")
	info = IndexInfo(0)
	info.duplicates = 0 if path == Sym("_uniqueID") else 2
	if isinstance(t, Array):
		info.key_type = KEY_MULTI
		for i in range(len(t.items) - 1, -1, -1):
			info.multi_types = ((info.multi_types << 4) | _type_key(t.items[i])) & 0xffffffff
			if order is not None:
				info.multi_ascending = (info.multi_ascending << 1) & 0xff
				if order.items[i] == Sym("ascending"):
					info.multi_ascending |= 1
	else:
		if isinstance(t, Sym) and t.key() == b"TAGS":
			raise Unsupported("a tags index")
		info.key_type = _type_key(t)
		if order is not None and order != Sym("ascending"):
			info.descending = 1
	return info


def _slot_path_value(entry, path):
	"""GetEntryKey for one path: (present, value)."""
	if isinstance(path, Sym):
		if not isinstance(entry, Frame) or not entry.has(path.name):
			return False, None
		return True, entry.get(path.name)
	if isinstance(path, Array) and isinstance(path.cls, Sym) and path.cls.key() == b"PATHEXPR":
		v = entry
		for p in path.items:
			if not isinstance(p, Sym) or not isinstance(v, Frame) or not v.has(p.name):
				return False, None
			v = v.get(p.name)
		return True, v
	raise Unsupported("an index path of that kind")


def entry_key(entry, path):
	if isinstance(path, Array) and path.cls == SYM_ARRAY:
		keys = []
		for p in path.items:
			keys.append(entry_key(entry, p))
		return keys if any(k is not None for k in keys) else None
	present, value = _slot_path_value(entry, path)
	return value if present else None


def _key_skey(key, t):
	"""KeyToSKey for one type: (skey bytes, size in the field, variable)."""
	k = t.key()
	if k == b"STRING":
		if not (isinstance(key, Binary) and is_string_class(key.cls)):
			raise BuildError("a string key expected")
		d = key.data
		n = 0
		while 2 * n + 1 < len(d) and (d[2 * n] or d[2 * n + 1]):
			n += 1
		n = min(n, 0x4e // 2)
		body = d[:2 * n]
		return bytes((0, len(body))) + body, len(body) + 2, True
	if k == b"INT":
		if not isinstance(key, int) or isinstance(key, bool):
			raise BuildError("an integer key expected")
		return struct.pack(">I", key & 0xffffffff), 4, False
	if k == b"REAL":
		if isinstance(key, int):
			value = float(key)
		elif isinstance(key, Binary) and isinstance(key.cls, Sym) and key.cls.key() == b"REAL":
			value = struct.unpack(">d", key.data)[0]
		else:
			raise BuildError("a real key expected")
		return struct.pack(">d", value), 8, False
	if k == b"CHAR":
		if not isinstance(key, Char):
			raise BuildError("a character key expected")
		return struct.pack(">H", key.code), 2, False
	if k == b"SYMBOL":
		if not isinstance(key, Sym):
			raise BuildError("a symbol key expected")
		name = _sym_bytes(key)[:0x4e]
		return bytes((0, len(name))) + name, len(name) + 2, True
	raise Unsupported("index type %s" % t.name)


def entry_skey(entry, desc: Frame):
	"""GetEntrySKey: the entry's key for the index, or None."""
	key = entry_key(entry, desc.get("path"))
	if key is None:
		return None
	t = desc.get("type")
	if isinstance(t, Array):
		flags = 0
		out = bytearray()
		items = key if isinstance(key, list) else [key]
		for i, sub in enumerate(items):
			if sub is None:
				flags |= 1 << i
				continue
			sk, size, variable = _key_skey(sub, t.items[i])
			padded = (size + 1) & ~1
			if len(out) + padded > 0x4e:
				if not variable:
					break
				room = 0x4e - len(out)
				if room < 3:
					break
				size = room
				sk = bytes((sk[0], room - 2)) + sk[2:]
				padded = room
			piece = sk[:size]
			piece += bytes(padded - len(piece))
			out += piece
		return bytes((flags, len(out))) + bytes(out)
	sk, size, variable = _key_skey(key, t)
	return sk


# --- the store, as the MessagePad makes it -----------------------------------

class Store:
	"""A store being made: its objects (bytes by id, ids in the order
	made), the tables, the index nodes in memory."""

	def __init__(self):
		self.objects = []
		self.labels = []
		self.nodes = {}
		self.dup_nodes = set()
		self.dirty = set()
		self.maps = None
		self.symbols = None

	def new_object(self, data: bytes, label: str) -> int:
		self.objects.append(bytes(data))
		self.labels.append(label)
		return len(self.objects) - 1


def _resolve_index_objects(frame: Frame, ids):
	"""A copy of a store or soup frame with each indexobject() replaced by
	the next of ids."""
	it = iter(ids)

	def walk(v):
		if isinstance(v, IndexObject):
			return next(it)
		if isinstance(v, Frame):
			return Frame([(t, walk(x)) for t, x in v.slots])
		if isinstance(v, Array):
			return Array(v.cls, [walk(x) for x in v.items])
		return v
	return walk(frame)


def _soup_name_key(name: str) -> bytes:
	data = name.encode("utf-16-be", "surrogatepass")[:0x4e]
	return bytes((0, len(data))) + data


def build(store_frame: Frame, soups, script):
	"""The store made: formatted with store_frame as its persistent frame,
	then the script run - ("soup", name) creates the soup (soups[name] its
	persistent frame) the first time and makes it current, ("entry",
	frame) adds an entry to the current soup.  Soups the script never
	names are created at the end.  ==> the Store."""
	s = Store()
	root_id = s.new_object(b"", "the root")
	name_info = IndexInfo(KEY_STRING, KEY_LONG, 0)
	name_info_id = s.new_object(name_info.to_bytes(), "the name index's info")
	map_id = s.new_object(bytes(256), "the map table")
	s.maps = HashTable(s, map_id, "the map table")
	sym_id = s.new_object(bytes(256), "the symbol table")
	s.symbols = HashTable(s, sym_id, "the symbol table")
	resolved = _resolve_index_objects(store_frame, [name_info_id])
	frame_id = ObjectWriter(s, resolved, "the store's frame").write()
	s.objects[root_id] = b"WALY" + struct.pack(">iIII", 4, map_id, sym_id, frame_id)
	name_index = SoupIndex(s, name_info_id, name_info, "the name index")
	made = {}
	current = None

	def create(name):
		frame = soups[name]
		descs = frame.get("indexes")
		if not isinstance(descs, Array):
			raise ValueError("soup %s: no indexes array" % name)
		infos = []
		for desc in descs.items:
			if not isinstance(desc, Frame) or not isinstance(desc.get("index"), IndexObject):
				raise ValueError("soup %s: each index needs index: indexobject()" % name)
			info = index_info_of(desc)
			label = "%s's index on %s" % (name, format_value(desc.get("path")))
			info_id = s.new_object(info.to_bytes(), label + " (info)")
			infos.append((desc, SoupIndex(s, info_id, info, label)))
		resolved = _resolve_index_objects(frame, [ix.info_id for _, ix in infos])
		soup_id = ObjectWriter(s, resolved, "soup %s" % name).write()
		name_index.add(_soup_name_key(name), struct.pack(">I", soup_id))
		made[name] = (soup_id, infos, resolved)

	for kind, arg in script:
		if kind == "soup":
			if arg not in soups:
				raise ValueError("soup(%s): store.ns has no such soup" % string_literal(arg))
			if arg not in made:
				create(arg)
			current = arg
			continue
		if current is None:
			raise ValueError("an entry before any soup(...)")
		soup_id, infos, _ = made[current]
		uid = arg.get("_uniqueID")
		label = "entry %s %s" % (current, uid if uid is not None else "?")
		entry_id = ObjectWriter(s, arg, label).write()
		for desc, index in reversed(infos):
			try:
				key = entry_skey(arg, desc)
				if key is not None:
					index.add(key, struct.pack(">I", entry_id))
			except BuildError as e:
				raise ValueError("%s: %s (index on %s)" % (label, e, format_value(desc.get("path"))))
	for name in soups:
		if name not in made:
			create(name)
	return s


# --- the package store container (stores/PackageStore.cpp) -------------------

def parse_package_store(data: bytes):
	"""The objects of a package store part: [bytes] by id, and its root id."""
	root, count = struct.unpack_from(">II", data, 0)
	offsets = struct.unpack_from(">%dI" % (count + 1), data, 8)
	if offsets[-1] != len(data):
		raise ValueError("the directory's last offset is not the part's size")
	return [data[offsets[i]:offsets[i + 1]] for i in range(count)], root


def package_store_bytes(objects, root=0) -> bytes:
	count = len(objects)
	offset = 8 + 4 * (count + 1)
	offsets = []
	for o in objects:
		offsets.append(offset)
		offset += len(o)
	offsets.append(offset)
	return struct.pack(">II", root, count) + struct.pack(">%dI" % (count + 1), *offsets) + b"".join(objects)


def soup_part(package: bytes):
	"""(offset, size) of a package's first part of type 'soup."""
	if package[:7] != b"package":
		raise ValueError("not a package")
	dir_size, num_parts = struct.unpack_from(">II", package, 8 + 44 - 8)
	for i in range(num_parts):
		offset, size, _, ptype = struct.unpack_from(">IIII", package, 52 + 32 * i)
		if ptype == struct.unpack(">I", b"soup")[0]:
			return dir_size + offset, size
	raise ValueError("the package has no 'soup part")


def replace_soup_part(package: bytes, part: bytes) -> bytes:
	"""The package with its first 'soup part's bytes replaced by part: the
	part's two sizes and the package's size (its last part's end) made to
	fit.  The part must be the package's last."""
	offset, size = soup_part(package)
	if offset + size != len(package):
		raise ValueError("the 'soup part is not the package's last")
	new = bytearray(package[:offset]) + part
	struct.pack_into(">I", new, 0x1c, len(new))
	num_parts = struct.unpack_from(">I", new, 48)[0]
	for i in range(num_parts):
		entry = 52 + 32 * i
		if new[entry + 12:entry + 16] == b"soup":
			struct.pack_into(">II", new, entry + 4, len(part), len(part))
			break
	return bytes(new)


# --- unpacking ----------------------------------------------------------------

def _to_index_objects(v, info_ids):
	"""The frame with every ints that is an index's info id (a store
	frame's nameIndex, an index description's index) as indexobject()."""
	if isinstance(v, Frame):
		slots = []
		for t, x in v.slots:
			if t.key() in (b"NAMEINDEX", b"INDEX") and isinstance(x, int) and x in info_ids:
				slots.append((t, IndexObject()))
			else:
				slots.append((t, _to_index_objects(x, info_ids)))
		return Frame(slots)
	if isinstance(v, Array):
		return Array(v.cls, [_to_index_objects(x, info_ids) for x in v.items])
	return v


def decode(data: bytes):
	"""The part read back into what build takes: (store frame, soups by
	name in creation order, script)."""
	objects, root_id = parse_package_store(data)
	root = objects[root_id]
	if root[:4] != b"WALY":
		raise Unsupported("the root object is not a store root ('WALY')")
	version, map_id, sym_id, frame_id = struct.unpack_from(">iIII", root, 4)

	def read(object_id):
		r = ObjectReader(objects, map_id, sym_id, object_id)
		v = r.read()
		if r.pos != len(r.data):
			raise Unsupported("object %d: bytes after the stream" % object_id)
		if r.text_pos != len(r.text) or r.text_size != len(r.text) & 0xffff:
			raise Unsupported("object %d: text not all used" % object_id)
		if r.flags & ~0x0a:
			raise Unsupported("object %d: flags %#x" % (object_id, r.flags))
		return v

	store_frame = read(frame_id)
	name_info = store_frame.get("nameIndex")
	soups = []
	events = []
	info_ids = {name_info}
	for key, data_list in walk_index(objects, name_info):
		name = key[2:2 + key[1]].decode("utf-16-be", "surrogatepass")
		soup_id = struct.unpack(">I", data_list[0])[0]
		frame = read(soup_id)
		descs = frame.get("indexes")
		uid_index = None
		for desc in descs.items:
			info_ids.add(desc.get("index"))
			if desc.get("path") == Sym("_uniqueID"):
				uid_index = desc.get("index")
		soups.append((soup_id, name, frame))
		events.append((soup_id, "soup", name))
		if uid_index is not None:
			for key2, ids in walk_index(objects, uid_index):
				for d in ids:
					events.append((struct.unpack(">I", d)[0], "entry", name))
	events.sort()
	script = []
	current = None
	for object_id, kind, name in events:
		if kind == "soup" or name != current:
			script.append(("soup", name))
			current = name
		if kind == "entry":
			script.append(("entry", read(object_id)))
	soups.sort()
	store_frame = _to_index_objects(store_frame, info_ids)
	soup_frames = {}
	for _, name, frame in soups:
		soup_frames[name] = _to_index_objects(frame, info_ids)
	return store_frame, soup_frames, script


HEADER_STORE = """\
// A package store (a 'soup part) as tools/stores/newtonstore.py unpacks it:
// the store's persistent frame and each soup's, in the order the soups were
// created.  indexobject() is where a frame holds an index's IndexInfo
// object id, which `newtonstore.py pack` assigns.  The entries are in
// entries.ns.
"""

HEADER_ENTRIES = """\
// The store's history, as tools/stores/newtonstore.py unpacks it: soup("X")
// creates soup X (the first time) and makes it the one the entries after it
// are added to, one entry to a line, in the order they were added - the
// order of their store objects, so an entry's place decides its id.  An
// entry's _uniqueID and _modTime go in its header; its slots are written
// _uniqueID, _modTime, name, then the rest by name (pack puts them in the
// store's own order, whatever order they are written in).
"""

HEADER_OVERRIDES = """\
# Objects whose bytes tools/stores/newtonstore.py does not derive: the
# object's id, what it is, the first 16 hex digits of the SHA-1 of the bytes
# pack derives for it, and the bytes the part has where they differ
# (offset:hex ...).  An override is applied only while pack derives those
# same bytes for that object; an edit that changes the object drops it.
"""


def _display_frame(frame: Frame, entry=False) -> Frame:
	"""The frame with its slots in the order the text gives them: by name
	(case-insensitively), an entry's _uniqueID, _modTime and name first;
	a frame whose order matters (an _implementor slot) as it is."""
	def walk(v, top):
		if isinstance(v, Frame):
			slots = [(t, walk(x, False)) for t, x in v.slots]
			if v.has("_implementor"):
				return Frame(slots)
			first = [b"_UNIQUEID", b"_MODTIME", b"NAME"] if top else []

			def key(slot):
				k = slot[0].key()
				return (first.index(k) if k in first else len(first), k)
			return Frame(sorted(slots, key=key))
		if isinstance(v, Array):
			return Array(v.cls, [walk(x, False) for x in v.items])
		return v
	return walk(frame, entry)


def _patch(derived: bytes, actual: bytes) -> str:
	"""The bytes where actual differs from derived, as offset:hex runs
	(or =hex, the whole object, when their sizes differ)."""
	if len(derived) != len(actual):
		return "=" + actual.hex()
	runs = []
	i = 0
	while i < len(actual):
		if derived[i] == actual[i]:
			i += 1
			continue
		j = i
		while j < len(actual) and derived[j] != actual[j]:
			j += 1
		runs.append("%d:%s" % (i, actual[i:j].hex()))
		i = j
	return " ".join(runs)


def _apply_patch(derived: bytes, patch: str) -> bytes:
	if patch.startswith("="):
		return bytes.fromhex(patch[1:])
	out = bytearray(derived)
	for run in patch.split():
		offset, data = run.split(":")
		data = bytes.fromhex(data)
		out[int(offset):int(offset) + len(data)] = data
	return bytes(out)


def _digest(data: bytes) -> str:
	return hashlib.sha1(data).hexdigest()[:16]


def write_directory(path, store_frame, soups, script, overrides=()):
	"""The directory written: store.ns, entries.ns and (when there are
	any) overrides.txt from (id, label, derived bytes, actual bytes)."""
	os.makedirs(path, exist_ok=True)
	with open(os.path.join(path, "store.ns"), "w", encoding="utf-8", newline="\n") as f:
		f.write(HEADER_STORE)
		f.write("\nstore := %s;\n" % format_value(_display_frame(store_frame)))
		for name, frame in soups.items():
			f.write("\nsoup(%s, %s);\n" % (string_literal(name), format_value(_display_frame(frame))))
	with open(os.path.join(path, "entries.ns"), "w", encoding="utf-8", newline="\n") as f:
		f.write(HEADER_ENTRIES)
		for kind, arg in script:
			if kind == "soup":
				f.write("\nsoup(%s);\n" % string_literal(arg))
			else:
				f.write(format_value(_display_frame(arg, True)) + ";\n")
	opath = os.path.join(path, "overrides.txt")
	if overrides:
		with open(opath, "w", encoding="utf-8", newline="\n") as f:
			f.write(HEADER_OVERRIDES)
			for object_id, label, derived, actual in overrides:
				f.write("%d\t%s\t%s\t%s\n" % (object_id, label, _digest(derived), _patch(derived, actual)))
	elif os.path.exists(opath):
		os.remove(opath)


def read_directory(path):
	"""(store frame, soups by name, script, overrides) from a directory;
	an override is (id, label, digest, patch)."""
	with open(os.path.join(path, "store.ns"), encoding="utf-8") as f:
		p = Parser(f.read(), "store.ns")
	store_frame = None
	soups = {}
	while not p.at_end():
		name = p.name()
		if name == "store":
			p.take(":=")
			store_frame = p.value()
		elif name == "soup":
			p.take("(")
			soup_name = string_text_of(p.value())
			p.take(",")
			soups[soup_name] = p.value()
			p.take(")")
		else:
			raise p.error("store or soup expected")
		p.take(";")
	if not isinstance(store_frame, Frame):
		raise ValueError("store.ns: no store := {...};")
	with open(os.path.join(path, "entries.ns"), encoding="utf-8") as f:
		p = Parser(f.read(), "entries.ns")
	script = []
	while not p.at_end():
		if p.peek()[0] == "name" and p.peek()[1] == "soup":
			p.take()
			p.take("(")
			script.append(("soup", string_text_of(p.value())))
			p.take(")")
		else:
			v = p.value()
			if not isinstance(v, Frame):
				raise p.error("an entry must be a frame")
			script.append(("entry", v))
		p.take(";")
	overrides = []
	opath = os.path.join(path, "overrides.txt")
	if os.path.exists(opath):
		with open(opath, encoding="utf-8") as f:
			for line in f:
				line = line.rstrip("\n")
				if not line or line.startswith("#"):
					continue
				object_id, label, digest, patch = line.split("\t")
				overrides.append((int(object_id), label, digest, patch))
	return store_frame, soups, script, overrides


def pack(path, warn=sys.stderr) -> bytes:
	"""The part's bytes made from an unpacked directory."""
	store_frame, soups, script, overrides = read_directory(path)
	s = build(store_frame, soups, script)
	for object_id, label, digest, patch in overrides:
		if object_id < len(s.objects) and s.labels[object_id] == label and _digest(s.objects[object_id]) == digest:
			s.objects[object_id] = _apply_patch(s.objects[object_id], patch)
		elif warn is not None:
			if object_id >= len(s.labels):
				why = "there is no such object"
			elif s.labels[object_id] != label:
				why = "it is now " + s.labels[object_id]
			else:
				why = "the bytes it derives to have changed"
			print("newtonstore: the override of object %d (%s) is dropped: %s" % (object_id, label, why), file=warn)
	return package_store_bytes(s.objects)


def unpack(data: bytes, path) -> None:
	"""The part's bytes unpacked into the directory path, and packed back to
	check.  Raises Unsupported for a store this cannot express."""
	store_frame, soups, script = decode(data)
	s = build(store_frame, soups, script)
	objects, _ = parse_package_store(data)
	if len(s.objects) != len(objects):
		raise Unsupported("the store's history made %d objects, the part has %d" % (len(s.objects), len(objects)))
	overrides = [(i, s.labels[i], s.objects[i], objects[i]) for i in range(len(objects)) if s.objects[i] != objects[i]]
	write_directory(path, store_frame, soups, script, overrides)
	if pack(path) != bytes(data):
		raise RuntimeError("newtonstore: %s does not pack back to the part's bytes" % path)


# --- the character tables from a romsrc tree ---------------------------------

def tables_from_romsrc(romsrc):
	"""(MAC_ROMAN, FOLD, DELIMITERS) read out of romsrc's text tables: the
	'unicode frame's encoding 1 (Mac Roman) mapToUnicode, charClass and
	upperNoMarkList, and the break table (magic pointer @6)."""
	text = ""
	for top, dirs, files in sorted(os.walk(os.path.join(romsrc, "objects"))):
		for name in sorted(files):
			with open(os.path.join(top, name), encoding="utf-8") as f:
				text += f.read()

	def table_path(pattern):
		m = re.search(pattern, text)
		if m is None:
			raise ValueError("romsrc: %s not found" % pattern)
		return os.path.join(romsrc, m.group(1))

	# (a frame's slots a line each, or all on one)
	to_unicode = table_path(r"encodingID: 1,\s*mapFromUnicode: texttable\('UniC, \"[^\"]*\"\),\s*mapToUnicode: texttable\('UniC, \"([^\"]*)\"\)")
	char_class = table_path(r"charClass: texttable\('UniC, \"([^\"]*)\"\)")
	upper_no_mark = table_path(r"upperNoMarkList: texttable\('UniC, \"([^\"]*)\"\)")
	with open(os.path.join(romsrc, "magic.tsv"), encoding="utf-8") as f:
		magic = dict(line.rstrip("\n").split("\t")[:2] for line in f if line[:1].isdigit())
	breaks = table_path(r"\b" + re.escape(magic["6"]) + r" := hexfile\('table, \"([^\"]*)\"\)")

	def rows(p):
		with open(p, encoding="utf-8") as f:
			return [line.split("#", 1)[0].split() for line in f if line.split("#", 1)[0].strip()]
	mac = [0] * 256
	for r in rows(to_unicode)[2:]:
		mac[int(r[0], 16)] = int(r[1][2:], 16)
	classes = [0] * 256
	for r in rows(char_class)[1:]:
		classes[int(r[0], 16)] = int(r[1])
	deltas = {}
	for r in rows(upper_no_mark)[1:]:
		deltas[int(r[0])] = int(r[1])
	fold = bytes((b + deltas[classes[b]]) & 0xff for b in range(256))
	delims = []
	for r in rows(breaks):
		delims += [int(x, 16) for x in r]
	return (b"".join(struct.pack(">H", c) for c in mac), fold,
			"".join("1" if d else "0" for d in delims))


def _hex_lines(data: bytes, per=32):
	h = data.hex()
	return "\n".join('\t"%s"' % h[i:i + 2 * per] for i in range(0, len(h), 2 * per))


def chartables_source(romsrc) -> str:
	mac, fold, delims = tables_from_romsrc(romsrc)
	out = ["MAC_ROMAN = bytes.fromhex(\n" + _hex_lines(mac) + ")",
		   "FOLD = bytes.fromhex(\n" + _hex_lines(fold) + ")",
		   "DELIMITERS = (\n" + "\n".join('\t"%s"' % delims[i:i + 64] for i in range(0, 256, 64)) + ")"]
	return "\n".join(out) + "\n"


# --- the command line ----------------------------------------------------------

def _part_bytes(path, package):
	with open(path, "rb") as f:
		data = f.read()
	if package:
		offset, size = soup_part(data)
		data = data[offset:offset + size]
	return data


def main(argv=None) -> int:
	ap = argparse.ArgumentParser(description="A Newton package store ('soup part) as text, and back.")
	sub = ap.add_subparsers(dest="cmd", required=True)
	u = sub.add_parser("unpack", help="the part's bytes into a directory")
	u.add_argument("part")
	u.add_argument("dir")
	u.add_argument("--package", action="store_true", help="PART is a package file: its 'soup part")
	p = sub.add_parser("pack", help="a directory back into the part's bytes")
	p.add_argument("dir")
	p.add_argument("out")
	c = sub.add_parser("check", help="unpacked and packed back: the same bytes?")
	c.add_argument("part", nargs="+")
	c.add_argument("--package", action="store_true")
	t = sub.add_parser("chartables", help="the character tables' source from a romsrc tree")
	t.add_argument("romsrc")
	args = ap.parse_args(argv)
	if args.cmd == "unpack":
		unpack(_part_bytes(args.part, args.package), args.dir)
		print("%s: unpacked into %s" % (args.part, args.dir))
		return 0
	if args.cmd == "pack":
		data = pack(args.dir)
		with open(args.out, "wb") as f:
			f.write(data)
		print("%s: %d bytes" % (args.out, len(data)))
		return 0
	if args.cmd == "check":
		import tempfile
		bad = 0
		for path in args.part:
			data = _part_bytes(path, args.package)
			with tempfile.TemporaryDirectory() as d:
				unpack(data, d)
				same = pack(d) == data
				overrides = os.path.exists(os.path.join(d, "overrides.txt"))
			print("%s: %s%s" % (path, "same" if same else "DIFFERENT",
								 " (with overrides)" if overrides else ""))
			bad += not same
		return 1 if bad else 0
	if args.cmd == "chartables":
		sys.stdout.write(chartables_source(args.romsrc))
		return 0
	return 2


if __name__ == "__main__":
	sys.exit(main())
