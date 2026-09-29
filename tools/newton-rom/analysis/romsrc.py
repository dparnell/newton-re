#!/usr/bin/env python3
"""The ROM's object area as source files, and back.

The ROM-free track's step 2 (docs/rom-free/README.md).  `extract` writes
the whole object area - 46538 frames, arrays, symbols and binaries - as a
tree of text files a person can read and edit, and `build` makes the
object area back out of such a tree.  With the tree's layout manifest the
build is byte for byte the ROM's (`--check` compares, and the ctest
host.ROMSourceRoundTrip runs both); that is the proof the extraction lost
nothing.

    python romsrc.py extract build/MP2x00US -o build/MP2x00US/romsrc
    python romsrc.py build build/MP2x00US/romsrc -o build/MP2x00US/objects.bin --check build/MP2x00US
    python romsrc.py roundtrip build/MP2x00US -o <dir>     # both, as the ctest runs them

The tree (this is stage 1: every binary but strings and reals is kept as
its bytes; functions are frames with their instructions as a binary):

    objects/NNN.ns   the objects, as definitions `name := value;`
    maps.ns          the frame maps, `map_<addr> := map(class, supermap, 'tag, ...);`
    resources/<class>/<addr>.bin   the binaries' bytes
    layout.tsv       the manifest: every object's address, path and header flags,
                     and each frame's map

The notation of a value (a subset of NewtonScript's literals, plus a few
constructors of its own):

    12  -3          an integer
    nil  true       themselves
    $a  $\\u00e9     a character
    @123            a magic pointer (ROM table 0, entry 123)
    #1a3            any other immediate Ref, in hex
    'name  '|a b|   a symbol
    "text"          a string (class 'string), UTF-8; \\uXXXX for a UTF-16
                    unit that is not printable, \\" and \\\\ escaped
    string('cls, "text")   a string of another class
    real(1.5)  real('Real, 1.5)   a real, of class 'real or another
    binary('cls, "resources/cls/addr.bin")   any other binary
    {tag: value, ...}      a frame (its map is the manifest's)
    [cls: value, ...]      an array whose class is the symbol cls
    array(value, value, ...)   an array whose class is not a symbol (the first)
    map(value, value, 'tag, ...)   a frame map: its class (the map's flags),
                    its supermap (nil or a map), its tags
    name            the object defined by that name

An object referenced from exactly one slot is written inline where it is
used; one referenced from more than one, or from none (a root: the
magic-pointer table and the ROM's C code reach those), is a definition of
its own, named after the ROM's R-constant that holds it (Rfoo), else
after its address (obj_<addr>).  A path names an inline object in the
manifest: the definition's name, then .tag for a frame's slot, [i] for an
array's element, ^ for an object's class.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import nsfunctions as nf			# noqa: E402

PAD = 0xba						# the bytes between objects
PER_FILE = 400					# definitions in one objects/NNN.ns
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
RESERVED = {"nil", "true", "real", "string", "binary", "array", "map"}


def symbol_hash(name: bytes) -> int:
	"""SymbolHashFunction (ROM 0x0032dab8): the sum of the upper-cased
	bytes times the golden ratio."""
	total = 0
	for b in name:
		total += ord(chr(b).upper()) if b < 128 else b
	return (total * 0x9E3779B9) & 0xFFFFFFFF


def quote_name(name: str) -> str:
	if IDENT.match(name) and name.lower() not in RESERVED:
		return name
	return "|" + name.replace("\\", "\\\\").replace("|", "\\|") + "|"


def string_text(text: str) -> str:
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


# ---- extraction

class Extractor:
	def __init__(self, rom: nf.ROM, out: str):
		self.rom = rom
		self.out = out
		self.objs = list(nf.objects(rom))
		self.inside = set(self.objs)
		self.maps = {rom.cls(o) for o in self.objs if rom.flags(o) & 3 == 3}
		more = set(self.maps)
		while more:
			supers = {rom.slots(m)[0] for m in more if rom.slots(m)[0] in self.inside} - self.maps
			self.maps |= supers
			more = supers
		self.refcount = collections.Counter()
		for o in self.objs:
			for r in self.refs_of(o):
				self.refcount[r] += 1
		self.rnames = self.r_names()
		self.names = {}					# object -> definition name
		self.paths = {}					# object -> path
		self.layout_extra = {}			# object -> extra manifest fields

	def refs_of(self, o):
		rom = self.rom
		if rom.symname(o) is not None:
			return []
		refs = [rom.cls(o)] if rom.cls(o) in self.inside else []
		if rom.flags(o) & 1:
			refs += [s for s in rom.slots(o) if s in self.inside]
		return refs

	def r_names(self):
		"""The ROM's object constants: R<name>, whose word holds the ref (with
		RS<name>, the RefStruct, the word after it)."""
		rom = self.rom
		names = {}
		for name, addr in rom.by_name.items():
			if name.startswith("RS") and ("R" + name[2:]) in rom.by_name and rom.by_name["R" + name[2:]] == addr - 4 \
					and not name.startswith("RSSYM"):
				ref = rom.word(addr - 4)
				if ref in self.inside and ref not in names and IDENT.match("R" + name[2:]):
					names[ref] = "R" + name[2:]
		return names

	def is_symbol(self, o):
		return self.rom.symname(o) is not None

	def named(self, o):
		return o in self.maps or self.refcount[o] != 1

	def name_of(self, o):
		if o not in self.names:
			if o in self.maps:
				self.names[o] = "map_%x" % o
			else:
				self.names[o] = self.rnames.get(o, "obj_%x" % o)
		return self.names[o]

	# the value notation

	def value(self, ref, path, inline=True):
		rom = self.rom
		if ref & 3 == 0:
			v = ref if ref < 0x80000000 else ref - 0x100000000
			return str(v >> 2)
		if ref & 3 == 3:
			return "@%d" % (ref >> 2) if ref < 0x80000000 else "#%x" % ref
		if ref & 3 == 2:
			if ref == 2:
				return "nil"
			if ref == 0x1a:
				return "true"
			if ref & 0xf == 6 and ref >> 4 <= 0xffff:
				c = ref >> 4
				if 0x21 <= c < 0x7f and chr(c) not in "\\":
					return "$" + chr(c)
				return "$\\u%04x" % c
			return "#%x" % ref
		if ref not in self.inside:
			raise ValueError("a pointer outside the object area: %#x" % ref)
		if self.is_symbol(ref):
			return "'" + quote_name(rom.symname(ref))
		if not inline or self.named(ref):
			return self.name_of(ref)
		return self.object(ref, path)

	def object(self, o, path):
		"""An object's notation; its path (and those of what it holds
		inline) recorded for the manifest."""
		rom = self.rom
		self.paths[o] = path
		f = rom.flags(o)
		cls = rom.cls(o)
		if o in self.maps:
			s = rom.slots(o)
			parts = [self.value(cls, path + "^"), self.value(s[0], path + "[0]")]
			parts += [self.value(t, path + "[%d]" % (i + 1)) for i, t in enumerate(s[1:])]
			return "map(" + ", ".join(parts) + ")"
		if f & 3 == 3:
			self.layout_extra[o] = "map=" + self.name_of(cls)
			parts = []
			for tag, value in zip(rom.map_tags(cls), rom.slots(o)):
				name = rom.symname(tag)
				parts.append("%s: %s" % (quote_name(name), self.value(value, path + "." + quote_name(name))))
			return "{" + ", ".join(parts) + "}"
		if f & 1:
			items = [self.value(v, path + "[%d]" % i) for i, v in enumerate(rom.slots(o))]
			if self.is_symbol(cls):
				return "[" + quote_name(rom.symname(cls)) + ": " + ", ".join(items) + "]"
			return "array(" + ", ".join([self.value(cls, path + "^")] + items) + ")"
		return self.binary(o, path)

	def binary(self, o, path):
		rom = self.rom
		cls = rom.cls(o)
		data = rom.data(o)
		cname = rom.symname(cls)
		if cname is not None and (cname.lower() == "string" or cname.lower().startswith("string.")) \
				and len(data) % 2 == 0 and data.endswith(b"\0\0"):
			try:
				text = data[:-2].decode("utf-16-be", "surrogatepass")
			except UnicodeDecodeError:
				text = None
			if text is not None and text.encode("utf-16-be", "surrogatepass") + b"\0\0" == data:
				s = string_text(text)
				return s if cname == "string" else "string('%s, %s)" % (quote_name(cname), s)
		if cname is not None and cname.lower() == "real" and len(data) == 8:
			v = struct.unpack(">d", data)[0]
			text = repr(v)
			if struct.pack(">d", float(text)) == data and "nan" not in text and "inf" not in text:
				return "real(%s)" % text if cname == "real" else "real('%s, %s)" % (quote_name(cname), text)
		folder = re.sub(r"[^A-Za-z0-9_.-]", "_", cname or "class_%x" % cls)
		rel = "resources/%s/%x.bin" % (folder, o)
		os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
		with open(os.path.join(self.out, rel), "wb") as f:
			f.write(data)
		return "binary(%s, \"%s\")" % (self.value(cls, path + "^"), rel)

	def run(self):
		rom = self.rom
		defs = []
		mapdefs = []
		# the definitions: every object that is named, in address order; an
		# object left unwritten afterwards (a cycle of objects referenced
		# once each) is named too, and written
		pending = [o for o in self.objs if not self.is_symbol(o) and self.named(o)]
		written = set()
		while pending:
			for o in pending:
				name = self.name_of(o)
				text = "%s := %s;" % (name, self.object(o, name))
				(mapdefs if o in self.maps else defs).append((o, text))
				written.add(o)
			missing = [o for o in self.objs if not self.is_symbol(o) and o not in self.paths]
			for o in missing:
				self.refcount[o] = 0		# (named from now on)
			pending = missing
		for o in self.objs:
			if self.is_symbol(o):
				self.paths[o] = "'" + quote_name(rom.symname(o))
		os.makedirs(os.path.join(self.out, "objects"), exist_ok=True)
		defs.sort()
		for i in range(0, len(defs), PER_FILE):
			with open(os.path.join(self.out, "objects", "%03d.ns" % (i // PER_FILE)), "w", encoding="utf-8", newline="\n") as f:
				for _, text in defs[i:i + PER_FILE]:
					f.write(text + "\n")
		with open(os.path.join(self.out, "maps.ns"), "w", encoding="utf-8", newline="\n") as f:
			for _, text in sorted(mapdefs):
				f.write(text + "\n")
		with open(os.path.join(self.out, "layout.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write("# the ROM's object area: %#x, %#x bytes\n" % (rom.soup, rom.soup_size))
			f.write("area\t%x\t%x\n" % (rom.soup, rom.soup_size))
			for o in self.objs:
				fields = ["%x" % (o - 1), self.paths[o], "%x" % rom.flags(o)]
				if o in self.layout_extra:
					fields.append(self.layout_extra[o])
				f.write("\t".join(fields) + "\n")
		return len(defs), len(mapdefs)


# ---- reading the notation

class Name:
	def __init__(self, name):
		self.name = name


class Sym:
	def __init__(self, name):
		self.name = name


class Imm:
	def __init__(self, ref):
		self.ref = ref


class Obj:
	"""A composite or binary object: kind is frame, array, map or binary."""
	def __init__(self, kind, cls=None, items=None, tags=None, data=None):
		self.kind, self.cls, self.items, self.tags, self.data = kind, cls, items or [], tags or [], data
		self.path = None


class Reader:
	TOKEN = re.compile(r"""
		(?P<space>\s+|//[^\n]*)
		|(?P<string>"(?:[^"\\]|\\.)*")
		|(?P<quoted>\|(?:[^|\\]|\\.)*\|)
		|(?P<char>\$\\u[0-9a-fA-F]{4}|\$.)
		|(?P<hex>\#[0-9a-fA-F]+)
		|(?P<magic>@\d+)
		|(?P<number>-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)
		|(?P<name>[A-Za-z_][A-Za-z0-9_]*)
		|(?P<punct>:=|[{}\[\](),:;'])
		""", re.X | re.S)

	def __init__(self, text, where, root):
		self.toks = []
		self.root = root
		pos = 0
		while pos < len(text):
			m = self.TOKEN.match(text, pos)
			if not m:
				raise SyntaxError("%s: cannot read at %r" % (where, text[pos:pos + 30]))
			pos = m.end()
			if m.lastgroup != "space":
				self.toks.append((m.lastgroup, m.group()))
		self.i = 0
		self.where = where

	def peek(self):
		return self.toks[self.i] if self.i < len(self.toks) else (None, None)

	def take(self, text=None):
		t = self.peek()
		if text is not None and t[1] != text:
			raise SyntaxError("%s: expected %s, found %s" % (self.where, text, t[1]))
		self.i += 1
		return t

	def name_text(self):
		kind, text = self.take()
		if kind == "quoted":
			return re.sub(r"\\(.)", r"\1", text[1:-1])
		if kind == "name":
			return text
		raise SyntaxError("%s: a name expected, found %s" % (self.where, text))

	def definitions(self):
		while self.peek()[0] is not None:
			name = self.name_text()
			self.take(":=")
			value = self.value()
			self.take(";")
			yield name, value

	def value(self):
		kind, text = self.peek()
		if kind == "number":
			self.take()
			return Imm((int(text) << 2) & 0xffffffff)
		if kind == "char":
			self.take()
			c = int(text[3:], 16) if text.startswith("$\\u") else ord(text[1])
			return Imm((c << 4) | 6)
		if kind == "hex":
			self.take()
			return Imm(int(text[1:], 16))
		if kind == "magic":
			self.take()
			return Imm((int(text[1:]) << 2) | 3)
		if kind == "string":
			self.take()
			return Obj("binary", Sym("string"), data=string_bytes(text))
		if text == "'":
			self.take()
			return Sym(self.name_text())
		if text == "{":
			self.take()
			o = Obj("frame")
			while self.peek()[1] != "}":
				o.tags.append(self.name_text())
				self.take(":")
				o.items.append(self.value())
				if self.peek()[1] == ",":
					self.take()
			self.take("}")
			return o
		if text == "[":
			self.take()
			cls = Sym(self.name_text())
			self.take(":")
			o = Obj("array", cls)
			while self.peek()[1] != "]":
				o.items.append(self.value())
				if self.peek()[1] == ",":
					self.take()
			self.take("]")
			return o
		if kind == "name" and text in ("nil", "true"):
			self.take()
			return Imm(2 if text == "nil" else 0x1a)
		if kind == "name" and text in ("real", "string", "binary", "array", "map") and self.toks[self.i + 1][1] == "(":
			self.take()
			self.take("(")
			args = []
			while self.peek()[1] != ")":
				if text in ("real",) and self.peek()[0] == "number":
					args.append(float(self.take()[1]))
				elif text in ("string", "binary") and self.peek()[0] == "string":
					args.append(self.take()[1])
				else:
					args.append(self.value())
				if self.peek()[1] == ",":
					self.take()
			self.take(")")
			if text == "real":
				cls, v = (Sym("real"), args[0]) if len(args) == 1 else args
				return Obj("binary", cls, data=struct.pack(">d", v))
			if text == "string":
				return Obj("binary", args[0], data=string_bytes(args[1]))
			if text == "binary":
				with open(os.path.join(self.root, args[1][1:-1]), "rb") as f:
					return Obj("binary", args[0], data=f.read())
			if text == "array":
				return Obj("array", args[0], items=args[1:])
			return Obj("map", args[0], items=args[1:])
		if kind in ("name", "quoted"):
			return Name(self.name_text())
		raise SyntaxError("%s: a value expected, found %s" % (self.where, text))


def string_bytes(literal):
	text = []
	body = literal[1:-1]
	i = 0
	while i < len(body):
		ch = body[i]
		if ch == "\\":
			nxt = body[i + 1]
			if nxt == "u":
				text.append(int(body[i + 2:i + 6], 16).to_bytes(2, "big"))
				i += 6
				continue
			text.append(nxt.encode("utf-16-be"))
			i += 2
			continue
		text.append(ch.encode("utf-16-be", "surrogatepass"))
		i += 1
	return b"".join(text) + b"\0\0"


# ---- building

class Builder:
	def __init__(self, src):
		self.src = src
		self.defs = {}
		files = [os.path.join(src, "maps.ns")]
		objdir = os.path.join(src, "objects")
		files += [os.path.join(objdir, f) for f in sorted(os.listdir(objdir)) if f.endswith(".ns")]
		for path in files:
			with open(path, encoding="utf-8") as f:
				for name, value in Reader(f.read(), path, src).definitions():
					if name in self.defs:
						raise SyntaxError("%s defined twice" % name)
					self.defs[name] = value
		# every object by its path
		self.by_path = {}
		for name, value in self.defs.items():
			self.walk(value, name)

	def walk(self, v, path):
		if not isinstance(v, Obj):
			return
		v.path = path
		self.by_path[path] = v
		if v.kind == "frame":
			for tag, item in zip(v.tags, v.items):
				self.walk(item, path + "." + quote_name(tag))
		elif v.kind in ("array", "map"):
			if isinstance(v.cls, Obj):
				self.walk(v.cls, path + "^")
			if v.kind == "map":
				self.walk(v.items[0], path + "[0]")
				for i, item in enumerate(v.items[1:]):
					self.walk(item, path + "[%d]" % (i + 1))
			else:
				for i, item in enumerate(v.items):
					self.walk(item, path + "[%d]" % i)
		elif isinstance(v.cls, Obj):
			self.walk(v.cls, path + "^")

	def build(self):
		area_base = area_size = None
		entries = []
		with open(os.path.join(self.src, "layout.tsv"), encoding="utf-8") as f:
			for line in f:
				if line.startswith("#"):
					continue
				fields = line.rstrip("\n").split("\t")
				if fields[0] == "area":
					area_base, area_size = int(fields[1], 16), int(fields[2], 16)
					continue
				extra = dict(x.split("=", 1) for x in fields[3:])
				entries.append((int(fields[0], 16), fields[1], int(fields[2], 16), extra))
		addr = {}					# path -> ref
		for a, path, _, _ in entries:
			addr[path] = a + 1
		missing = [p for p in self.by_path if p not in addr]
		if missing:
			raise ValueError("%d objects the layout does not place, e.g. %s" % (len(missing), missing[:3]))

		def ref(v):
			if isinstance(v, Imm):
				return v.ref
			if isinstance(v, Sym):
				return addr["'" + quote_name(v.name)]
			if isinstance(v, Name):
				return addr[v.name]
			return addr[v.path]

		out = bytearray([PAD]) * area_size
		for a, path, flags, extra in entries:
			if path.startswith("'"):
				name = Reader(path, "layout", self.src)
				name.take("'")
				text = name.name_text().encode("latin-1")
				body = struct.pack(">I", symbol_hash(text)) + text + b"\0"
				words = struct.pack(">III", ((12 + len(body)) << 8) | flags, 0, nf.SYMBOL_CLASS) + body
			else:
				v = self.by_path[path]
				if v.kind == "binary":
					body = v.data
					cls = ref(v.cls)
				elif v.kind == "frame":
					body = b"".join(struct.pack(">I", ref(x)) for x in v.items)
					cls = ref(Name(extra["map"]))
				else:
					body = b"".join(struct.pack(">I", ref(x)) for x in v.items)
					cls = ref(v.cls)
				words = struct.pack(">III", ((12 + len(body)) << 8) | flags, 0, cls) + body
			o = a - area_base
			out[o:o + len(words)] = words
		return area_base, bytes(out)


def check(area_base, built, rom, entries_src):
	original = rom.rom[area_base:area_base + len(built)]
	if original == built:
		return 0
	bad = 0
	objs = list(nf.objects(rom))
	for o in objs:
		a = o - 1 - area_base
		size = (rom.size(o) + 3) & ~3
		if original[a:a + size] != built[a:a + size]:
			if bad < 10:
				print("differs: object %#x (%s)" % (o, rom.describe(o)), file=sys.stderr)
			bad += 1
	print("%d of %d objects differ" % (bad, len(objs)), file=sys.stderr)
	return 1


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = ap.add_subparsers(dest="command", required=True)
	e = sub.add_parser("extract", help="write the object area as source")
	e.add_argument("build_dir")
	e.add_argument("-o", "--output", required=True)
	b = sub.add_parser("build", help="make the object area from source")
	b.add_argument("source")
	b.add_argument("-o", "--output")
	b.add_argument("--check", metavar="BUILD_DIR", help="compare with that ROM's object area, byte for byte")
	r = sub.add_parser("roundtrip", help="extract, build and compare")
	r.add_argument("build_dir")
	r.add_argument("-o", "--output", required=True, help="where the source tree is written (emptied first)")
	a = ap.parse_args(argv)
	if a.command == "roundtrip":
		import shutil
		shutil.rmtree(a.output, ignore_errors=True)
		if main(["extract", a.build_dir, "-o", a.output]) != 0:
			return 1
		return main(["build", a.output, "--check", a.build_dir])
	if a.command == "extract":
		rom = nf.ROM(a.build_dir)
		n, m = Extractor(rom, a.output).run()
		print("%d definitions and %d maps written to %s" % (n, m, a.output))
		return 0
	builder = Builder(a.source)
	base, area = builder.build()
	if a.output:
		with open(a.output, "wb") as f:
			f.write(area)
	print("built %d objects, %#x bytes at %#x" % (len(builder.by_path), len(area), base))
	if a.check:
		rom = nf.ROM(a.check)
		result = check(base, area, rom, None)
		print("the object area is %s" % ("identical to the ROM's" if result == 0 else "NOT the ROM's"))
		return result
	return 0


if __name__ == "__main__":
	sys.exit(main())
