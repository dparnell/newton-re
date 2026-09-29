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

The output (-o) is what the host loads in place of a ROM image
(`newton --objects`, frames/ROMImport.h's ImportBuiltObjects): the header
- the 8 bytes "NewtObjs", then as big-endian words the version (1), the
area's base address and size, the magic-pointer table's address and its
count - then the area, then the magic pointers as big-endian words, then
(version 2) the count of other blocks of ROM data and each one's address,
length and bytes (rounded to a word): the lexicons, and the ROM extension;
then (version 3) the count of objects that are not where the ROM has them
and, for each, the ROM's ref and the ref now (build --relayout).
    python romsrc.py roundtrip build/MP2x00US -o <dir> --newtonscript <exe>   # both, as the ctest runs them
    python romsrc.py edit-test <tree> -o <dir> --objects <file> --newtonscript <exe>

edit-test is the test of editability: it copies a tree, adds a slot
holding a new frame to Rcanonicalinkshape (so the builder must make maps
and symbols), lengthens a string in the first package's part (so every
package after it moves), lengthens one string in the object area (the plain string of 12 characters or more with the lowest
address - near the area's start, so that nearly every object after it
moves), and builds the copy with --relayout into an object
file, which the host must boot as it boots the ROM's (ctest
host.NewtonEditedSameScreen).

`build` needs the host's newtonscript (--newtonscript) to compile the
functions; it runs it with no ROM image, so nothing of the ROM's is read
but the tree.

The tree (stage 2: the ROM's NewtonScript functions are source, every
binary but strings and reals is kept as its bytes):

    objects/NNN.ns   the objects, as definitions `name := value;`
    functions/<addr>.ns   each top-level function as the decompiler writes it
                     (tools/newton-rom/analysis/nsdecompile.py: its constants,
                     then the function), compiled by the builder with the
                     host's compiler (newtonscript --compile-records)
    maps.ns          the frame maps, `map_<addr> := map(class, supermap, 'tag, ...);`
    resources/<class>/<addr>.bin   the binaries' bytes
    lexicons/<name>.bin, lexicons.tsv   the recognisers' lexicons (the Airus
                     tries InitROMDictionaryData points gROMDictionaryData at:
                     C data outside the object area, some of it in the ROM
                     extension), each its size word then the trie; the .tsv
                     their ROM addresses (tools/newton-rom/analysis/romdicts.py)
    rex/, rex.tsv    the ROM extension (the "high" file: its header, config
                     entries and the ten built-in packages), cut into pieces
                     in address order - each package a .pkg file as the ROM
                     holds it (its frames parts' refs ROM addresses: see
                     tools/newton-rom/analysis/packages.py), each config
                     entry a .bin - which the builder puts back together
    magic.tsv        the magic-pointer table (gROMMagicPointerTable): each
                     entry's index and what it is - an object's path in
                     the layout, or a value
    layout.tsv       the manifest: every object's address, path and header flags,
                     and each frame's map; `alias` lines say which named
                     object a path inside a compiled function is (an object
                     the ROM shares, which the compiler makes afresh)

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
    bytes('cls, "0a1b...")   a binary given in hex (what the host's compiler writes)
    bitmap('cls, "resources/cls/addr.png", "header", depth)   a 'bits, 'mask
                    or 'cbits bitmap: its rows a grayscale PNG (black the
                    Newton's set pixels), its 16-byte header (a FramBitmap:
                    qd/Pictures.h) in hex, its bits per pixel
    sound('samples, "resources/samples/addr.wav")   the samples of a
                    simple sound (8-bit, uncompressed: offset binary, as a
                    WAV file's 8-bit samples are); the sampling rate stays
                    in the sound frame
    pict('picture, "resources/picture/addr.pict")   a QuickDraw picture: a
                    PICT file (512 bytes of nought, then the picture), as a
                    Macintosh drawing program reads and writes it
    (a font, 'sfnt, is binary('sfnt, "resources/sfnt/addr.ttf"): the
    binary is the font file, which font tools read)
    function("functions/addr.ns")   a function, compiled from that source
    same("path")    the object a compiled function holds at that path: a
                    shared object only compiled functions use (bytecode two
                    functions have in common, a function literal of two),
                    whose source is theirs
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
import nsdecompile as nd			# noqa: E402
import romdicts						# noqa: E402
import packages as rexpackages		# noqa: E402
import json							# noqa: E402
import subprocess					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "imaging"))
import png							# noqa: E402

import wave							# noqa: E402

BITMAP_CLASSES = ("bits", "mask", "cbits")
PICT_HEADER = 512					# a PICT file's header: nought, before the picture

PAD = 0xba						# the bytes between objects
PER_FILE = 400					# definitions in one file of objects/
GROUP_MIN = 8					# fewer definitions than this belonging to one root go into a misc file
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
RESERVED = {"nil", "true", "real", "string", "binary", "array", "map"}


def same_object(rom, v, ref, where):
	"""Where a compiled value (the notation read back) differs from the ROM's
	object at ref; None when it is the same."""
	if isinstance(v, Imm):
		return None if v.ref == ref else "%s: %#x, the ROM's %#x" % (where, v.ref, ref)
	if isinstance(v, Sym):
		name = rom.symname(ref) if rom.is_ptr(ref) else None
		return None if name is not None and name.lower() == v.name.lower() else "%s: '%s" % (where, v.name)
	if not isinstance(v, Obj) or not rom.is_ptr(ref):
		return "%s: a different kind of value" % where
	f = rom.flags(ref)
	if v.kind == "binary":
		if f & 1:
			return "%s: a binary, the ROM's is not" % where
		why = same_object(rom, v.cls, rom.cls(ref), where + "^")
		if why:
			return why
		return None if v.data == rom.data(ref) else "%s: the bytes differ" % where
	if v.kind == "frame":
		if f & 3 != 3:
			return "%s: a frame, the ROM's is not" % where
		slots = rom.frame_slots(ref)
		if [t.lower() for t in v.tags] != [(t or "").lower() for t, _ in slots]:
			return "%s: the tags differ" % where
		for tag, item, (_, value) in zip(v.tags, v.items, slots):
			why = same_object(rom, item, value, where + "." + tag)
			if why:
				return why
		return None
	if v.kind == "array":
		if f & 3 != 1:
			return "%s: an array, the ROM's is not" % where
		why = same_object(rom, v.cls, rom.cls(ref), where + "^")
		if why:
			return why
		slots = rom.slots(ref)
		if len(slots) != len(v.items):
			return "%s: %d slots, the ROM's %d" % (where, len(v.items), len(slots))
		for i, (item, value) in enumerate(zip(v.items, slots)):
			why = same_object(rom, item, value, "%s[%d]" % (where, i))
			if why:
				return why
		return None
	return "%s: a %s" % (where, v.kind)


def lengthen_first_string(tree):
	"""edit-test's edit: the plain string of 12 characters or more with the
	lowest address in a tree's objects gains " (edited)"."""
	address = {}
	with open(os.path.join(tree, "layout.tsv"), encoding="utf-8") as f:
		for line in f:
			fields = line.split("\t")
			if len(fields) > 2 and not line.startswith(("#", "area", "alias")):
				address[fields[1]] = int(fields[0], 16)
	best = None
	folder = os.path.join(tree, "objects")
	for name in sorted(os.listdir(folder)):
		with open(os.path.join(folder, name), encoding="utf-8") as f:
			for i, line in enumerate(f.read().split("\n")):
				m = re.match(r'([A-Za-z_][A-Za-z0-9_]*) := "([^"\\]{12,})";$', line)
				if m and m.group(1) in address and (best is None or address[m.group(1)] < best[0]):
					best = (address[m.group(1)], name, i, m.group(1), m.group(2))
	if best is None:
		print("no string to edit in %s" % folder)
		return 1
	_, name, i, what, text = best
	path = os.path.join(folder, name)
	with open(path, encoding="utf-8") as f:
		lines = f.read().split("\n")
	lines[i] = '%s := "%s (edited)";' % (what, text)
	print("edited %s in %s: \"%s\" is now \"%s (edited)\"" % (what, os.path.relpath(path, tree), text, text))
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines))
	return 0


def file_name(path, ref):
	"""A file's name made of an object's path (what holds it, so that a
	person finds it): its letters, digits and dots, the rest made _, cut
	to a length every file system takes, and the object's address added
	when it had to be cut or said nothing."""
	name = re.sub(r"[^A-Za-z0-9_.]+", "_", path).strip("._")
	if len(name) > 80 or not name:
		name = name[:60].rstrip("._") + "_%x" % ref
	return name


def part_objects(rom, first, last, align):
	"""A package part's objects, walked from its start (aligned to `align`
	bytes), the byte the gaps are filled with, and the gaps that are not all
	that byte (object -> their bytes: a version 0 package fills some with a
	word of 0xbeacebad): None unless the part is exactly a run of objects
	whose pointers all stay within it."""
	objs = []
	gaps = {}
	counts = collections.Counter()
	a = first
	while a < last:
		size = rom.size(a + 1)
		if size < 12:
			return None
		objs.append(a + 1)
		nxt = first + ((a - first + size + align - 1) & ~(align - 1))		# (aligned from the part's start)
		gaps[a + 1] = bytes(rom.rom[a + size:nxt])
		counts.update(gaps[a + 1])
		a = nxt
	if a != last:
		return None
	inside = set(objs)
	for o in objs:
		refs = [rom.cls(o)] + (rom.slots(o) if rom.flags(o) & 1 else [])
		if any(r & 3 == 1 and r not in inside for r in refs):
			return None
	pad = counts.most_common(1)[0][0] if counts else PAD
	odd = {o: g for o, g in gaps.items() if g and g != bytes([pad]) * len(g)}
	return objs, pad, odd


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
	def __init__(self, rom: nf.ROM, out: str, build_dir: str = None, objects=None, area=None, pad=PAD,
				 newtonscript=None, align=4):
		"""The ROM's object area; or, given `objects` and `area` (its base and
		size) and the byte between objects, a package part's objects."""
		self.rom = rom
		self.build_dir = build_dir
		self.out = out
		self.main = objects is None
		self.area = (rom.soup, rom.soup_size) if area is None else area
		self.pad = pad
		self.align = align
		self.objs = list(nf.objects(rom)) if objects is None else list(objects)
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
		self.functions = set(nd.rom_functions(rom, None if self.main else self.objs))
		self.simple_sounds = self.find_simple_sounds()
		self.undecompiled = []			# (function, why): the ones kept as bytecode
		self.odd_gaps = {}				# object -> the bytes after it, when they are not the pad byte
		self.newtonscript = newtonscript
		self.sources = self.function_sources()
		self.in_function = 0			# inside a function written as source
		self.aliases = []				# (path, name): a named object inside a compiled function
		self.named_refs = []			# (path, name, the definition being written): every named reference
		self.current_def = None

	def refs_of(self, o):
		rom = self.rom
		if rom.symname(o) is not None:
			return []
		refs = [rom.cls(o)] if rom.cls(o) in self.inside else []
		if rom.flags(o) & 1:
			refs += [s for s in rom.slots(o) if s in self.inside]
		return refs

	def group_files(self, defs):
		"""The definitions cut into files by what they belong to: each goes
		with the root that dominates it - the one top-level object (reached
		from the magic pointers, an R constant or no object at all) every
		path to it passes through - in a file named after that root; the
		groups of fewer than GROUP_MIN definitions, and what more than one
		root shares, go into misc-NNN.ns files by address."""
		names = {o: self.names[o] for o, _ in defs}
		node = {n: o for o, n in names.items()}
		edges = collections.defaultdict(set)

		def owner_of(path):
			m = re.match(r"\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*", path)
			n = m.group(0) if m else None
			if n and n.startswith("|"):
				n = re.sub(r"\\(.)", r"\1", n[1:-1])
			return n
		for path, name, owner in self.named_refs:
			if owner in node and name in node and owner != name:
				edges[owner].add(name)
		for path, name in self.aliases:
			owner = owner_of(path)
			if owner in node and name in node and owner != name:
				edges[owner].add(name)
		incoming = collections.Counter(n for targets in edges.values() for n in targets)
		magic_targets = set()
		magic_index = {}
		count = self.rom.word(self.rom.mp_table) if self.main else 0
		for i in range(count):
			ref = self.rom.word(self.rom.mp_table + 4 + 4 * i)
			if ref in self.names:
				magic_targets.add(self.names[ref])
				magic_index.setdefault(self.names[ref], i)
		roots = [n for n in node if incoming[n] == 0 or n in magic_targets or n.startswith("R")]
		# dominators (Cooper, Harvey and Kennedy), from a root above the roots
		top = "(the root above the roots)"
		succ = dict(edges)
		succ[top] = set(roots)
		order = []
		seen = {top}
		stack = [(top, iter(sorted(succ[top])))]
		while stack:
			n, it = stack[-1]
			nxt = next(it, None)
			if nxt is None:
				order.append(n)
				stack.pop()
			elif nxt not in seen:
				seen.add(nxt)
				stack.append((nxt, iter(sorted(succ.get(nxt, ())))))
		order.reverse()								# reverse postorder, the root first
		index = {n: i for i, n in enumerate(order)}
		preds = collections.defaultdict(list)
		for n, targets in succ.items():
			for t in targets:
				preds[t].append(n)
		idom = {top: top}
		changed = True
		while changed:
			changed = False
			for n in order[1:]:
				new = None
				for p in preds[n]:
					if p in idom:
						if new is None:
							new = p
						else:
							a, b = p, new
							while a != b:
								while index[a] > index[b]:
									a = idom[a]
								while index[b] > index[a]:
									b = idom[b]
							new = a
				if new is not None and idom.get(n) != new:
					idom[n] = new
					changed = True
		groups = collections.defaultdict(list)
		misc = []
		for o, text in defs:
			n = names[o]
			while n in idom and idom[n] is not top:
				n = idom[n]
			(groups[n] if n in idom else misc).append((o, text))
		files = []
		for n, group in sorted(groups.items(), key=lambda g: min(o for o, _ in g[1])):
			if len(group) < GROUP_MIN:
				misc += group
				continue
			base = file_name(self.root_label(n, node[n], magic_index.get(n)), node[n])
			for i in range(0, len(group), PER_FILE):
				files.append(("%s%s.ns" % (base, "" if i == 0 else "-%d" % (i // PER_FILE + 1)), group[i:i + PER_FILE]))
		misc.sort()
		for i in range(0, len(misc), PER_FILE):
			files.append(("misc-%03d.ns" % (i // PER_FILE), misc[i:i + PER_FILE]))
		return files

	def root_label(self, name, ref, magic):
		"""What a file of a root's definitions is called: its R constant's
		name; else what the root says it is (a template's debug name, an
		application's symbol, a title), with its magic-pointer index or its
		address; else the definition's own name."""
		rom = self.rom
		if name.startswith("R"):
			return name
		said = None
		if rom.flags(ref) & 3 == 3:
			slots = {(t or "").lower(): v for t, v in rom.frame_slots(ref)}
			for tag in ("debug", "appsymbol", "title", "name", "symbol"):
				v = slots.get(tag)
				if v is None or not rom.is_ptr(v):
					continue
				if rom.symname(v) is not None:
					said = rom.symname(v)
				elif rom.symname(rom.cls(v)) == "string" and not rom.flags(v) & 1:
					said = rom.data(v)[:-2].decode("utf-16-be", "replace")
				if said:
					break
		where = "mp%d" % magic if magic is not None else "%x" % ref
		return "%s_%s" % (said, where) if said else ("mp%d" % magic if magic is not None else name)

	def function_sources(self):
		"""Each function's source, as the decompiler writes it - only those
		that compile back to the same function (checked by compiling them
		all with the host's newtonscript, when there is one, and comparing
		what comes back with the ROM's objects); the rest are kept as their
		bytecode, with the reason in self.undecompiled."""
		sources = {}
		for o in sorted(self.functions):
			try:
				sources[o] = nd.record(self.rom, o)
			except (nd.DecompileError, IndexError, KeyError, AttributeError, TypeError, ValueError) as e:
				self.undecompiled.append((o, "decompile: %s" % e))
		if self.newtonscript is None or not sources:
			return sources
		os.makedirs(self.out, exist_ok=True)
		records = os.path.join(self.out, ".verify-records.txt")
		compiled = os.path.join(self.out, ".verify-compiled.txt")
		order = sorted(sources)
		with open(records, "w", encoding="utf-8", newline="\n") as f:
			for n, o in enumerate(order):
				head, body = sources[o].split("\n", 1)
				f.write("@@ %x%s\n%s" % (n, " names" if head.endswith(" names") else "", body))
		env = dict(os.environ, NEWTON_ROM=os.path.join(self.out, "no-rom-image"))
		subprocess.run([nd.newtonscript_path(self.newtonscript), "--compile-records", records, compiled], env=env,
					   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		with open(compiled, encoding="utf-8") as f:
			lines = f.read().split("\n")
		done = set()
		for i, line in enumerate(lines):
			if not line.startswith("@@ "):
				continue
			fields = line.split()
			n = int(fields[1], 16)
			o = order[n]
			done.add(o)
			if len(fields) > 2:
				why = "compile: " + " ".join(fields[3:])
			else:
				why = same_object(self.rom, Reader(lines[i + 1], "compiled", self.out).value(), o, "")
			if why:
				self.undecompiled.append((o, why))
				del sources[o]
		for o in order:
			if o not in done and o in sources:
				self.undecompiled.append((o, "not compiled"))
				del sources[o]
		os.remove(records)
		os.remove(compiled)
		return sources

	def find_simple_sounds(self):
		"""The samples of the simple sounds (8-bit, uncompressed), with their
		frame's sampling rate: what a WAV file holds exactly."""
		rom = self.rom
		found = {}
		for o in self.objs:
			if rom.flags(o) & 3 != 3:
				continue
			slots = dict(rom.frame_slots(o))
			kind = slots.get("sndFrameType")
			if kind is None or rom.symname(kind) != "simpleSound" or slots.get("dataType") != 8 << 2 \
					or slots.get("compressionType") != 0 or "samples" not in slots:
				continue
			rate = slots.get("samplingRate")
			hz = 22026
			if rom.is_ptr(rate) and rom.symname(rom.cls(rate)) in ("Real", "real"):
				hz = struct.unpack(">d", rom.data(rate))[0]
			elif rom.is_ptr(rate) and rom.symname(rom.cls(rate)) == "fixed":
				hz = struct.unpack(">i", rom.data(rate))[0] / 65536
			elif rate is not None and rate & 3 == 0:
				hz = rate >> 2
			found[slots["samples"]] = max(1, int(round(hz)))
		return found

	def write_rex(self):
		"""The ROM extension as pieces: every config entry and every package
		of the package list a file, what lies between them a file too."""
		layout_path = os.path.join(self.build_dir, "layout.json")
		with open(layout_path, encoding="utf-8") as f:
			rex = json.load(f).get("rex")
		if rex is None:
			return
		start, end = rex["start"], rex["start"] + rex["length"]
		names = {start: "header"}
		for e in rex["entries"]:
			names.setdefault(e["address"], "%s_%x" % (re.sub(r"[^A-Za-z0-9]", "_", e["tag"].strip()), e["address"]))
		_, pkgs = rexpackages.rex_packages(self.build_dir)
		cuts = {start, end}
		for e in rex["entries"]:
			cuts |= {e["address"], e["address"] + e["size"]}
		parts = {}				# a frames part's start -> (its end, its tree's directory)
		aligns = {}
		for p in pkgs:
			cuts |= {p["base"], p["base"] + p["size"]}
			name = re.sub(r"[^A-Za-z0-9_]", "_", p["name"])
			names[p["base"]] = name + ".pkg"
			for part in p["parts"]:
				if part["flags"] & 3 != 1:
					continue
				first = p["base"] + p["directory_size"] + part["offset"]
				last = first + part["size"]
				align = 8 if p["signature"].endswith("0") else 4
				found = part_objects(self.rom, first, last, align)
				if found is None:
					continue			# (a part not a plain run of objects: kept in the package's bytes)
				objs, pad, odd = found
				aligns[first] = align
				cuts |= {first, last}
				names[p["base"]] = name + ".head.bin"
				names[last] = name + ".tail.bin"
				names[first] = name + "/"
				parts[first] = (last, objs, pad, odd)
		cuts = sorted(c for c in cuts if start <= c <= end)
		os.makedirs(os.path.join(self.out, "rex"), exist_ok=True)
		rom = self.rom.rom
		with open(os.path.join(self.out, "rex.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write("# the ROM extension: its pieces in address order (address, file)\n")
			f.write("rex\t%x\t%x\n" % (start, end - start))
			for a, b in zip(cuts, cuts[1:]):
				name = names.get(a, "bytes_%x" % a)
				if name.endswith("/"):
					# a package's frames part: its objects as a tree of their own
					last, objs, pad, odd = parts[a]
					rel = "rex/" + name
					sub = Extractor(self.rom, os.path.join(self.out, rel), self.build_dir, objs, (a, last - a), pad,
									self.newtonscript, aligns[a])
					sub.odd_gaps = odd
					sub.run()
					print("  %s: %d objects, %d functions as source, %d as bytecode"
						  % (rel, len(objs), len(sub.sources), len(sub.undecompiled)))
					f.write("%x\t%s\n" % (a, rel))
					continue
				rel = "rex/" + (name if "." in name else name + ".bin")
				with open(os.path.join(self.out, rel), "wb") as out:
					out.write(rom[a:b])
				f.write("%x\t%s\n" % (a, rel))

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
			if self.in_function:
				self.aliases.append((path, self.name_of(ref)))
			else:
				self.named_refs.append((path, self.name_of(ref), self.current_def))
			return self.name_of(ref)
		return self.object(ref, path)

	def object(self, o, path):
		"""An object's notation; its path (and those of what it holds
		inline) recorded for the manifest."""
		rom = self.rom
		self.paths[o] = path
		f = rom.flags(o)
		cls = rom.cls(o)
		source = self.sources.get(o) if not self.in_function else None
		if source is not None:
			# the function's source; its objects walked for their paths and
			# maps (what the builder lays the compiled function out by)
			rel = "functions/%s.ns" % file_name(path, o)
			os.makedirs(os.path.join(self.out, "functions"), exist_ok=True)
			with open(os.path.join(self.out, rel), "w", encoding="utf-8", newline="\n") as out:
				out.write(source)
			self.in_function += 1
			try:
				self.frame_text(o, path)
			finally:
				self.in_function -= 1
			return 'function("%s")' % rel
		if o in self.maps:
			s = rom.slots(o)
			parts = [self.value(cls, path + "^"), self.value(s[0], path + "[0]")]
			parts += [self.value(t, path + "[%d]" % (i + 1)) for i, t in enumerate(s[1:])]
			return "map(" + ", ".join(parts) + ")"
		if f & 3 == 3:
			return self.frame_text(o, path)
		if f & 1:
			items = [self.value(v, path + "[%d]" % i) for i, v in enumerate(rom.slots(o))]
			if self.is_symbol(cls):
				return "[" + quote_name(rom.symname(cls)) + ": " + ", ".join(items) + "]"
			return "array(" + ", ".join([self.value(cls, path + "^")] + items) + ")"
		return self.binary(o, path)

	def frame_text(self, o, path):
		rom = self.rom
		cls = rom.cls(o)
		self.layout_extra[o] = "map=" + self.name_of(cls)
		parts = []
		for tag, value in zip(rom.map_tags(cls), rom.slots(o)):
			name = rom.symname(tag)
			parts.append("%s: %s" % (quote_name(name), self.value(value, path + "." + quote_name(name))))
		return "{" + ", ".join(parts) + "}"

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
		if cname in BITMAP_CLASSES and not self.in_function:
			made = bitmap_rows(data, cname)
			if made is not None:
				header, depth, width, height, rows = made
				rel = "resources/%s/%x.png" % (folder, o)
				os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
				top = (1 << depth) - 1
				png.write_gray(os.path.join(self.out, rel), width, height, [[top - v for v in row] for row in rows], depth)
				return "bitmap(%s, \"%s\", \"%s\", %d)" % (self.value(cls, path + "^"), rel, header.hex(), depth)
		if o in self.simple_sounds and not self.in_function:
			rel = "resources/%s/%x.wav" % (folder, o)
			os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
			with wave.open(os.path.join(self.out, rel), "wb") as w:
				w.setnchannels(1)
				w.setsampwidth(1)
				w.setframerate(self.simple_sounds[o])
				w.writeframes(data)
			return "sound(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
		if cname == "picture" and not self.in_function:
			rel = "resources/%s/%x.pict" % (folder, o)
			os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
			with open(os.path.join(self.out, rel), "wb") as f:
				f.write(bytes(PICT_HEADER) + data)
			return "pict(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
		rel = "resources/%s/%x.%s" % (folder, o, "ttf" if cname == "sfnt" else "bin")
		if self.in_function:
			return "binary(%s, \"%s\")" % (self.value(cls, path + "^"), rel)	# (compiled, not written)
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
				self.current_def = name
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
		# a named object every reference to which is inside a compiled
		# function is taken from the compiled function (same): its source is
		# the functions', and an edit to them is not overridden by a copy
		inside = collections.defaultdict(list)
		for path, name in self.aliases:
			inside[name].append(path)
		by_name = {n: o for o, n in self.names.items()}
		same = {}
		for name, paths in inside.items():
			o = by_name[name]
			if self.refcount[o] == len(paths):
				same[name] = paths[0]
		defs = [(o, "%s := same(\"%s\");" % (self.names[o], same[self.names[o]])) if self.names.get(o) in same else (o, t)
				for o, t in defs]
		mapdefs = [(o, "%s := same(\"%s\");" % (self.names[o], same[self.names[o]])) if self.names.get(o) in same else (o, t)
				   for o, t in mapdefs]
		# (the named objects inside such a definition are named where the
		# compiled function has them inline too)
		self.aliases += [(path, name) for path, name, owner in self.named_refs if owner in same]
		# the resources nothing refers to any more (those of the definitions
		# now taken from the compiled functions)
		used = set()
		for _, text in defs + mapdefs:
			used.update(re.findall(r'"(resources/[^"]+)"', text))
		for folder, _, files in os.walk(os.path.join(self.out, "resources")):
			for f in files:
				rel = os.path.relpath(os.path.join(folder, f), self.out).replace(os.sep, "/")
				if rel not in used:
					os.remove(os.path.join(folder, f))
		self.same_count = len(same)
		os.makedirs(os.path.join(self.out, "objects"), exist_ok=True)
		defs.sort()
		for rel, group in self.group_files(defs):
			with open(os.path.join(self.out, "objects", rel), "w", encoding="utf-8", newline="\n") as f:
				for _, text in group:
					f.write(text + "\n")
		with open(os.path.join(self.out, "maps.ns"), "w", encoding="utf-8", newline="\n") as f:
			for _, text in sorted(mapdefs):
				f.write(text + "\n")
		with open(os.path.join(self.out, "layout.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write("# the objects' area: %#x, %#x bytes; the byte between objects; the objects' alignment\n" % self.area)
			f.write("area\t%x\t%x\t%x\t%x\n" % (self.area + (self.pad, self.align)))
			for o in self.objs:
				fields = ["%x" % (o - 1), self.paths[o], "%x" % rom.flags(o)]
				if o in self.layout_extra:
					fields.append(self.layout_extra[o])
				if o in self.odd_gaps:
					fields.append("gap=%s" % self.odd_gaps[o].hex())
				if rom.word(o - 1 + 4):
					# the header's second word, nought but for a part's first object
					fields.append("gc=%x" % rom.word(o - 1 + 4))
				f.write("\t".join(fields) + "\n")
			for path, name in self.aliases:
				f.write("alias\t%s\t%s\n" % (path, name))
		if self.undecompiled:
			with open(os.path.join(self.out, "bytecode.tsv"), "w", encoding="utf-8", newline="\n") as f:
				f.write("# the functions kept as bytecode: they do not decompile, or do not compile back the same\n")
				for o, why in sorted(self.undecompiled):
					f.write("%x\t%s\n" % (o, why))
		if not self.main:
			return len(defs), len(mapdefs)
		# the lexicons
		os.makedirs(os.path.join(self.out, "lexicons"), exist_ok=True)
		_, writes = romdicts.decode(rom.rom, rom.by_name[romdicts.INIT_FUNCTION])
		with open(os.path.join(self.out, "lexicons.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write("# the recognisers' lexicons: ROM address, name, file (its size word, then the trie)\n")
			for address in sorted({a for a in writes.values() if a}):
				name = re.sub(r"[^A-Za-z0-9_]", "_", rom.symbols.get(address, "lexicon_%x" % address))
				size = rom.word(address)
				rel = "lexicons/%s.bin" % name
				with open(os.path.join(self.out, rel), "wb") as out:
					out.write(rom.rom[address:address + 4 + size])
				f.write("%x\t%s\t%s\n" % (address, name, rel))
		self.write_rex()
		with open(os.path.join(self.out, "magic.tsv"), "w", encoding="utf-8", newline="\n") as f:
			count = rom.word(rom.mp_table)
			f.write("# the magic-pointer table: @index, then the object (its path) or value\n")
			f.write("table\t%x\n" % rom.mp_table)
			for i in range(count):
				ref = rom.word(rom.mp_table + 4 + 4 * i)
				what = self.paths[ref] if ref in self.inside else self.value(ref, "@%d" % i)
				f.write("%d\t%s\n" % (i, what))
		return len(defs), len(mapdefs)


# ---- bitmaps

def bitmap_depths(data, cname):
	"""The depths a bitmap's rows could be: one for 'bits and 'mask; for
	'cbits, those whose rows fit its row bytes with less than a word over."""
	if cname != "cbits":
		return [1]
	row_bytes, width = struct.unpack(">H", data[4:6])[0], bitmap_bounds(data)[1]
	return [d for d in (1, 2, 4, 8) if (width * d + 7) // 8 <= row_bytes < (width * d + 7) // 8 + 4]


def bitmap_bounds(data):
	top, left, bottom, right = struct.unpack(">4h", data[8:16])
	return bottom - top, right - left


def bitmap_rows(data, cname):
	"""(header, depth, width, height, rows of pixel values) of a bitmap the
	PNG form holds exactly - one depth it can be, rows that fill its bytes
	and nothing in the padding - or None."""
	if len(data) < 16:
		return None
	row_bytes = struct.unpack(">H", data[4:6])[0]
	height, width = bitmap_bounds(data)
	depths = bitmap_depths(data, cname)
	if len(depths) != 1 or height <= 0 or width <= 0 or len(data) != 16 + row_bytes * height:
		return None
	depth = depths[0]
	rows = []
	for r in range(height):
		line = data[16 + r * row_bytes:16 + (r + 1) * row_bytes]
		rows.append([(line[(x * depth) // 8] >> (8 - depth - (x * depth) % 8)) & ((1 << depth) - 1) for x in range(width)])
	header = data[:16]
	if bitmap_bytes(header, depth, rows) != data:
		return None
	return header, depth, width, height, rows


def bitmap_bytes(header, depth, rows):
	"""A bitmap's bytes: the header, then each row packed from its high bit
	and padded with zeros to the header's row bytes."""
	row_bytes = struct.unpack(">H", header[4:6])[0]
	out = bytearray(header)
	for row in rows:
		line = bytearray(row_bytes)
		for x, v in enumerate(row):
			line[(x * depth) // 8] |= v << (8 - depth - (x * depth) % 8)
		out += line
	return bytes(out)


def wav_samples(path):
	"""A WAV file's samples as a simple sound's: 8-bit offset binary, one
	channel.  The extractor's own files are read as they are; another
	(16-bit, stereo: a sound edited elsewhere) is brought to that by
	averaging the channels and keeping each sample's high byte."""
	with wave.open(path, "rb") as w:
		channels, width, count = w.getnchannels(), w.getsampwidth(), w.getnframes()
		frames = w.readframes(count)
	if channels == 1 and width == 1:
		return frames
	out = bytearray()
	step = channels * width
	for i in range(0, len(frames) - step + 1, step):
		total = 0
		for c in range(channels):
			sample = frames[i + c * width:i + (c + 1) * width]
			if width == 1:
				total += sample[0] - 128
			else:
				total += int.from_bytes(sample[-1:], "little", signed=True)		# (the high byte of a little-endian sample)
		out.append((total // channels + 128) & 0xff)
	return bytes(out)


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
		if kind == "name" and text in ("real", "string", "binary", "array", "map", "bytes", "function", "bitmap", "sound", "pict", "same") \
				and self.toks[self.i + 1][1] == "(":
			self.take()
			self.take("(")
			args = []
			while self.peek()[1] != ")":
				if text in ("real",) and self.peek()[0] == "number":
					args.append(float(self.take()[1]))
				elif text in ("string", "binary", "bytes", "function", "bitmap", "sound", "pict", "same") and self.peek()[0] == "string":
					args.append(self.take()[1])
				elif text == "bitmap" and self.peek()[0] == "number":
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
			if text == "same":
				return Obj("same", data=args[0][1:-1])
			if text == "pict":
				with open(os.path.join(self.root, args[1][1:-1]), "rb") as f:
					return Obj("binary", args[0], data=f.read()[PICT_HEADER:])
			if text == "sound":
				return Obj("binary", args[0], data=wav_samples(os.path.join(self.root, args[1][1:-1])))
			if text == "bitmap":
				cls, rel, header, depth = args[0], args[1][1:-1], bytes.fromhex(args[2][1:-1]), int(args[3])
				width, height, levels = png.read_gray(os.path.join(self.root, rel), depth)
				if (height, width) != bitmap_bounds(header):
					raise ValueError("%s is %d x %d, its header's bounds %d x %d: change the header too"
									 % (rel, width, height, bitmap_bounds(header)[1], bitmap_bounds(header)[0]))
				top = (1 << depth) - 1
				return Obj("binary", cls, data=bitmap_bytes(header, depth, [[top - v for v in row] for row in levels]))
			if text == "bytes":
				return Obj("binary", args[0], data=bytes.fromhex(args[1][1:-1]))
			if text == "function":
				return Obj("function", data=args[0][1:-1])
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
	def __init__(self, src, newtonscript=None, relayout=False, base=None):
		"""relayout: the objects laid out afresh, one after another in the
		layout's order at the sizes they now have (an edit that grows or
		shrinks one moves every one after it; an object the layout does not
		know goes at the end), rather than at the layout's addresses."""
		self.src = src
		self.newtonscript = newtonscript
		self.relayout = relayout
		self.new_base = base				# (relayout: where the area now starts - a package part that has moved)
		self.relocations = []			# (the ROM's ref, the ref now) for every object that moved
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
		self.read_layout()
		self.compile_functions()
		for name in list(self.defs):
			self.resolve_same(name, set())
		# every object by its path (a path the layout says is a named object
		# - one the ROM shares, which a compiled function made afresh - is
		# that object)
		self.by_path = {}
		for name in list(self.defs):
			self.defs[name] = self.walk(self.defs[name], name)

	def read_layout(self):
		self.area_base = self.area_size = None
		self.entries = []
		self.aliases = {}
		with open(os.path.join(self.src, "layout.tsv"), encoding="utf-8") as f:
			for line in f:
				if line.startswith("#"):
					continue
				fields = line.rstrip("\n").split("\t")
				if fields[0] == "area":
					self.area_base, self.area_size = int(fields[1], 16), int(fields[2], 16)
					self.pad = int(fields[3], 16) if len(fields) > 3 else PAD
					self.align = int(fields[4], 16) if len(fields) > 4 else 4
					continue
				if fields[0] == "alias":
					self.aliases[fields[1].lower()] = fields[2]
					continue
				extra = dict(x.split("=", 1) for x in fields[3:])
				self.entries.append((int(fields[0], 16), fields[1], int(fields[2], 16), extra))

	def compile_functions(self):
		"""Every function(...) compiled by the host's compiler, in one run,
		and put in its place."""
		found = []

		def find(v):
			if isinstance(v, Obj):
				if v.kind == "function":
					found.append(v)
				for x in v.items:
					find(x)
				find(v.cls)
		for v in self.defs.values():
			find(v)
		if not found:
			return
		if self.newtonscript is None:
			raise ValueError("the tree has functions: the builder needs --newtonscript to compile them")
		# (the working files in a directory of their own: the tree may be the
		# committed one, which a build must leave as it is)
		import tempfile
		work = tempfile.mkdtemp(prefix="romsrc-build")
		records = os.path.join(work, "records.txt")
		compiled = os.path.join(work, "compiled.txt")
		by_id = {}
		with open(records, "w", encoding="utf-8", newline="\n") as out:
			for n, v in enumerate(found):
				with open(os.path.join(self.src, v.data), encoding="utf-8") as f:
					text = f.read()
				# (each record numbered afresh: the file's own @@ line names
				# the ROM's function, which an edited tree need not have)
				head, body = text.split("\n", 1)
				out.write("@@ %x%s\n%s" % (n, " names" if head.endswith(" names") else "", body))
				by_id[n] = v
		env = dict(os.environ, NEWTON_ROM=os.path.join(work, "no-rom-image"))
		result = subprocess.run([nd.newtonscript_path(self.newtonscript), "--compile-records", records, compiled], env=env,
								stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		if result.returncode != 0:
			raise ValueError("the functions did not all compile: %s" % result.stderr.strip()[-500:])
		with open(compiled, encoding="utf-8") as f:
			lines = f.read().split("\n")
		i = 0
		while i < len(lines):
			if lines[i].startswith("@@ "):
				n = int(lines[i][3:], 16)
				value = Reader(lines[i + 1], "compiled %x" % n, self.src).value()
				target = by_id.pop(n)
				target.kind, target.cls, target.items, target.tags, target.data = \
					value.kind, value.cls, value.items, value.tags, value.data
				i += 3
			else:
				i += 1
		import shutil
		shutil.rmtree(work, ignore_errors=True)
		if by_id:
			raise ValueError("%d functions came back uncompiled" % len(by_id))

	def resolve_same(self, name, seen):
		"""A same(path) definition replaced by the object at that path."""
		v = self.defs[name]
		if not (isinstance(v, Obj) and v.kind == "same"):
			return v
		if name in seen:
			raise ValueError("same() goes round in a circle at %s" % name)
		seen.add(name)
		self.defs[name] = self.at_path(v.data, seen)
		return self.defs[name]

	def at_path(self, path, seen):
		m = re.match(r"(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)", path)
		root = m.group(1)
		if root.startswith("|"):
			root = re.sub(r"\\(.)", r"\1", root[1:-1])
		v = self.resolve_same(root, seen)
		rest = path[m.end():]
		for step in re.findall(r"\.(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)|\[(\d+)\]|(\^)", rest):
			tag, index, cls = step
			if tag:
				if tag.startswith("|"):
					tag = re.sub(r"\\(.)", r"\1", tag[1:-1])
				v = v.items[v.tags.index(tag)]
			elif index:
				v = v.items[int(index)]
			else:
				v = v.cls
			if isinstance(v, Name):
				v = self.resolve_same(v.name, seen)
		return v

	def walk(self, v, path):
		if not isinstance(v, Obj):
			return v
		# (paths are matched whatever their case: a compiled frame's tags are
		# spelt as the host first interned the symbols)
		if path.lower() in self.aliases:
			name = self.aliases[path.lower()]
			if name not in self.defs:
				raise ValueError("%s: the alias %s is not defined" % (path, name))
			return Name(name)
		v.path = path
		self.by_path[path.lower()] = v
		if v.kind == "frame":
			v.items = [self.walk(item, path + "." + quote_name(tag)) for tag, item in zip(v.tags, v.items)]
		elif v.kind in ("array", "map"):
			if isinstance(v.cls, Obj):
				v.cls = self.walk(v.cls, path + "^")
			if v.kind == "map":
				v.items = [self.walk(item, path + "[%d]" % i) for i, item in enumerate(v.items)]
			else:
				v.items = [self.walk(item, path + "[%d]" % i) for i, item in enumerate(v.items)]
		elif isinstance(v.cls, Obj):
			v.cls = self.walk(v.cls, path + "^")
		return v

	def object_size(self, path):
		"""The size an object's header and body come to now (None: gone)."""
		if path.startswith("'"):
			r = Reader(path, "layout", self.src)
			r.take("'")
			return 12 + 4 + len(r.name_text().encode("latin-1")) + 1
		v = self.by_path.get(path.lower())
		if v is None:
			return None
		if v.kind == "binary":
			return 12 + len(v.data)
		return 12 + 4 * len(v.items)

	def rex_relaid_out(self, rex):
		"""The ROM extension put back together with its frames parts laid out
		afresh, each at wherever it now falls: a part that grew or shrank
		moves everything after it.  What records where things are is made to
		agree: each package's directory (its size, its part's size), the
		extension's header (its length; each config entry's offset, and the
		package list's size) and the frame export table 'fexp (the refs of
		the objects the parts export, each moved as its object was).  The
		header's checksum is left as it was: nothing in the ROM reads it
		(TestForREx 0x003137dc takes a block on its signatures and id alone;
		docs/rom-free/README.md)."""
		pieces = []
		with open(rex, encoding="utf-8") as f:
			for line in f:
				if line.startswith("#"):
					continue
				fields = line.rstrip("\n").split("\t")
				if fields[0] == "rex":
					base, length = int(fields[1], 16), int(fields[2], 16)
				else:
					pieces.append((int(fields[0], 16), fields[1]))
		data = bytearray()
		moved = {}					# a piece's old address -> its new one
		relocations = {}			# an exported object's old ref -> its new one
		head = None					# the offset in data of the last package directory
		fexp_at = None
		for old, rel in pieces:
			new = base + len(data)
			moved[old] = new
			if rel.endswith("/"):
				sub = Builder(os.path.join(self.src, rel), self.newtonscript, relayout=True, base=new)
				_, part = sub.build()
				relocations.update(dict(sub.relocations))
				delta = len(part) - sub.original_size
				if head is not None and delta:
					# the package directory: its size (+28), and the entry of the
					# part at this offset (+52 on, 32 bytes each: offset, size, size again)
					size_at = head + 28
					struct.pack_into(">I", data, size_at, struct.unpack_from(">I", data, size_at)[0] + delta)
					dir_size = struct.unpack_from(">I", data, head + 44)[0]
					for i in range(struct.unpack_from(">I", data, head + 48)[0]):
						entry = head + 52 + 32 * i
						if head + dir_size + struct.unpack_from(">I", data, entry)[0] == len(data):
							for k in (4, 8):
								struct.pack_into(">I", data, entry + k, struct.unpack_from(">I", data, entry + k)[0] + delta)
				data += part
				continue
			with open(os.path.join(self.src, rel), "rb") as piece:
				blob = piece.read()
			if blob[:7] == b"package":
				head = len(data)
			data += blob
		moved[base + length] = base + len(data)
		# the header: its length, and each config entry's offset and size
		struct.pack_into(">I", data, 0x18, len(data))
		for i in range(struct.unpack_from(">I", data, 0x24)[0]):
			entry = 0x28 + 12 * i
			tag, offset, size = struct.unpack_from(">4sII", data, entry)
			start, end = base + offset, base + offset + size
			new_start, new_end = moved.get(start, start), moved.get(end, end)
			struct.pack_into(">II", data, entry + 4, new_start - base, new_end - new_start)
			if tag == b"fexp":
				fexp_at, fexp_size = new_start - base, new_end - new_start
		if fexp_at is not None:
			for k in range(fexp_at, fexp_at + fexp_size - 3, 4):
				ref = struct.unpack_from(">I", data, k)[0]
				struct.pack_into(">I", data, k, relocations.get(ref, ref))		# (the entries are refs)
		return base, bytes(data)

	def map_tags(self, name):
		"""A map definition's tags, its supermap's first, in lower case (None:
		not a map)."""
		v = self.defs.get(name)
		if not (isinstance(v, Obj) and v.kind in ("map", "array")) or not v.items:
			return None
		sup = v.items[0]
		tags = []
		if isinstance(sup, Name):
			above = self.map_tags(sup.name)
			if above is None:
				return None
			tags = above
		elif not (isinstance(sup, Imm) and sup.ref == 2):
			return None
		for t in v.items[1:]:
			if not isinstance(t, Sym):
				return None
			tags = tags + [t.name.lower()]
		return tags

	def make_maps_and_symbols(self, entries, missing, symbols):
		"""What an edit needs that the layout does not have: a map for each
		frame whose slots are no longer its map's (a slot added, taken away
		or renamed) or which is new - a map already there with those very
		tags and no supermap, or a new one - and a symbol object for each
		name the tree now uses that the area has no symbol for.  ==> the
		paths of the new objects, the ones missing from the layout added."""
		frame_map = {}
		for a, path, flags, extra in entries:
			if "map" in extra:
				frame_map[path.lower()] = extra["map"]
		by_tags = {}
		for name in self.defs:
			tags = self.map_tags(name)
			v = self.defs[name]
			if tags is not None and isinstance(v.items[0], Imm):
				by_tags.setdefault(tuple(tags), name)
		self.frame_maps = {}
		made = 0
		for path, v in list(self.by_path.items()):
			if v.kind != "frame":
				continue
			tags = [t.lower() for t in v.tags]
			current = frame_map.get(path)
			if current is not None and self.map_tags(current) == tags:
				continue
			name = by_tags.get(tuple(tags))
			if name is None:
				made += 1
				name = "romsrc_map_%d" % made
				m = Obj("map", Imm(0), items=[Imm(2)] + [Sym(t) for t in v.tags])
				m.path = name
				self.defs[name] = m
				self.by_path[name.lower()] = m
				missing.append(name.lower())
				by_tags[tuple(tags)] = name
			self.frame_maps[path] = name
		# the symbols
		new_symbols = set()

		def names_in(v):
			if isinstance(v, Sym):
				if v.name.lower() not in symbols:
					new_symbols.add(v.name)
			elif isinstance(v, Obj):
				for x in v.items:
					names_in(x)
				names_in(v.cls)
				for t in v.tags:
					if t.lower() not in symbols:
						new_symbols.add(t)
		for v in self.defs.values():
			names_in(v)
		for s in sorted(new_symbols, key=str.lower):
			if s.lower() not in {x.lower() for x in missing if x.startswith("'")}:
				missing.append("'" + quote_name(s))
		return missing

	def lay_out_afresh(self, entries, new_paths):
		"""The objects one after another from the area's base, each on a word:
		the layout's in its order, then the new ones.  Every object's old and
		new refs are kept (self.relocations) for the object file, so that the
		host's constants, which name the ROM's addresses, find them."""
		out = []
		at = self.area_base if self.new_base is None else self.new_base
		start = at
		for a, path, flags, extra in entries:
			size = self.object_size(path)
			if size is None:
				continue
			out.append((at, path, flags, extra))
			if at != a:
				self.relocations.append((a + 1, at + 1))
			at = start + ((at - start + size + self.align - 1) & ~(self.align - 1))
		for path in sorted(new_paths):
			if path.startswith("'"):
				out.append((at, path, 0x40, {}))				# a symbol
				at = start + ((at - start + self.object_size(path) + self.align - 1) & ~(self.align - 1))
				continue
			v = self.by_path[path]
			flags = {"binary": 0x40, "frame": 0x43}.get(v.kind, 0x41)
			out.append((at, v.path, flags, {}))
			at = start + ((at - start + self.object_size(v.path) + self.align - 1) & ~(self.align - 1))
		return out, at - start

	def build(self):
		area_base, area_size, entries = self.area_base, self.area_size, self.entries
		addr = {}					# path -> ref
		symbols = {}				# a symbol's name, in lower case -> ref
		for a, path, _, _ in entries:
			addr[path.lower()] = a + 1
			if path.startswith("'"):
				r = Reader(path, "layout", self.src)
				r.take("'")
				symbols[r.name_text().lower()] = a + 1
		missing = [p for p in self.by_path if p not in addr]
		if missing and not self.relayout:
			raise ValueError("%d objects the layout does not place, e.g. %s" % (len(missing), missing[:3]))
		self.original_size = area_size
		if self.relayout:
			missing = self.make_maps_and_symbols(entries, missing, symbols)
			entries, area_size = self.lay_out_afresh(entries, missing)
			if self.new_base is not None:
				area_base = self.new_base
			addr = {}
			symbols = {}
			for a, path, _, _ in entries:
				addr[path.lower()] = a + 1
				if path.startswith("'"):
					r = Reader(path, "layout", self.src)
					r.take("'")
					symbols[r.name_text().lower()] = a + 1
			self.entries = entries

		def ref(v):
			if isinstance(v, Imm):
				return v.ref
			if isinstance(v, Sym):
				# (symbols are one whatever their case: the host's compiler
				# spells one as the host first interned it)
				return symbols[v.name.lower()]
			if isinstance(v, Name):
				return addr[v.name.lower()]
			return addr[v.path.lower()]

		# the magic-pointer table
		self.magic_base = None
		self.magic = []
		magic = os.path.join(self.src, "magic.tsv")
		with open(magic if os.path.exists(magic) else os.devnull, encoding="utf-8") as f:
			for line in f:
				if line.startswith("#"):
					continue
				index, what = line.rstrip("\n").split("\t", 1)
				if index == "table":
					self.magic_base = int(what, 16)
					continue
				if int(index) != len(self.magic):
					raise ValueError("magic.tsv: entry %s out of order" % index)
				if what.lower() in addr:
					self.magic.append(addr[what.lower()])
				elif what.startswith("'"):
					self.magic.append(ref(Reader(what, "magic.tsv", self.src).value()))
				else:
					self.magic.append(ref(Reader(what, "magic.tsv", self.src).value()))

		# the other ROM data: the lexicons
		self.blocks = []
		lexicons = os.path.join(self.src, "lexicons.tsv")
		if os.path.exists(lexicons):
			with open(lexicons, encoding="utf-8") as f:
				for line in f:
					if line.startswith("#"):
						continue
					address, _, rel = line.rstrip("\n").split("\t")
					with open(os.path.join(self.src, rel), "rb") as blob:
						self.blocks.append((int(address, 16), blob.read()))

		# the ROM extension, put back together
		rex = os.path.join(self.src, "rex.tsv")
		if os.path.exists(rex) and self.relayout:
			self.blocks.append(self.rex_relaid_out(rex))
		elif os.path.exists(rex):
			with open(rex, encoding="utf-8") as f:
				data = bytearray()
				base = None
				for line in f:
					if line.startswith("#"):
						continue
					fields = line.rstrip("\n").split("\t")
					if fields[0] == "rex":
						base, length = int(fields[1], 16), int(fields[2], 16)
						continue
					if int(fields[0], 16) != base + len(data):
						raise ValueError("rex.tsv: %s is not where the piece before it ends" % fields[1])
					if fields[1].endswith("/"):
						# a package's frames part, built from its own tree
						part_base, part = Builder(os.path.join(self.src, fields[1]), self.newtonscript).build()
						if part_base != base + len(data):
							raise ValueError("rex.tsv: %s's objects are laid out at %#x" % (fields[1], part_base))
						data += part
						continue
					with open(os.path.join(self.src, fields[1]), "rb") as piece:
						data += piece.read()
				if len(data) != length:
					raise ValueError("rex.tsv: the pieces make %#x bytes, not %#x" % (len(data), length))
				self.blocks.append((base, bytes(data)))

		out = bytearray([self.pad]) * area_size
		for a, path, flags, extra in entries:
			if path.startswith("'"):
				name = Reader(path, "layout", self.src)
				name.take("'")
				text = name.name_text().encode("latin-1")
				body = struct.pack(">I", symbol_hash(text)) + text + b"\0"
				words = struct.pack(">III", ((12 + len(body)) << 8) | flags, int(extra.get("gc", "0"), 16), nf.SYMBOL_CLASS) + body
			else:
				v = self.by_path[path.lower()]
				if v.kind == "binary":
					body = v.data
					cls = ref(v.cls)
				elif v.kind == "frame":
					body = b"".join(struct.pack(">I", ref(x)) for x in v.items)
					cls = ref(Name(getattr(self, "frame_maps", {}).get(path.lower(), extra.get("map"))))
				else:
					body = b"".join(struct.pack(">I", ref(x)) for x in v.items)
					cls = ref(v.cls)
				words = struct.pack(">III", ((12 + len(body)) << 8) | flags, int(extra.get("gc", "0"), 16), cls) + body
			o = a - area_base
			out[o:o + len(words)] = words
			if "gap" in extra:
				gap = bytes.fromhex(extra["gap"])
				out[o + len(words):o + len(words) + len(gap)] = gap
		return area_base, bytes(out)


def container(builder, area_base, area):
	"""The file the host loads: header, area, magic pointers."""
	header = b"NewtObjs" + struct.pack(">5I", 3, area_base, len(area), builder.magic_base, len(builder.magic))
	blocks = struct.pack(">I", len(builder.blocks))
	for address, data in builder.blocks:
		blocks += struct.pack(">II", address, len(data)) + data + bytes(-len(data) % 4)
	# (version 3) where each object that moved now is: the ROM's ref, then the
	# ref now, sorted by the first
	moved = struct.pack(">I", len(builder.relocations))
	moved += b"".join(struct.pack(">II", old, new) for old, new in sorted(builder.relocations))
	return header + area + b"".join(struct.pack(">I", r) for r in builder.magic) + blocks + moved


def check_magic(builder, rom):
	count = rom.word(rom.mp_table)
	original = [rom.word(rom.mp_table + 4 + 4 * i) for i in range(count)]
	if builder.magic_base != rom.mp_table or builder.magic != original:
		bad = [i for i in range(max(len(original), len(builder.magic)))
			   if i >= len(original) or i >= len(builder.magic) or original[i] != builder.magic[i]]
		print("the magic-pointer table differs at %d entries, e.g. @%s" % (len(bad), bad[:5]), file=sys.stderr)
		return 1
	return 0


def check_blocks(builder, rom):
	bad = [a for a, data in builder.blocks if rom.rom[a:a + len(data)] != data]
	if bad:
		print("%d blocks of ROM data (lexicons, the extension) differ from the ROM's, e.g. %#x" % (len(bad), bad[0]), file=sys.stderr)
		return 1
	return 0


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
	e.add_argument("--newtonscript", help="the host's newtonscript: only functions that compile back the same are written as source")
	b = sub.add_parser("build", help="make the object area from source")
	b.add_argument("source")
	b.add_argument("-o", "--output")
	b.add_argument("--check", metavar="BUILD_DIR", help="compare with that ROM's object area, byte for byte")
	b.add_argument("--newtonscript", help="the host's newtonscript, which compiles the functions")
	b.add_argument("--relayout", action="store_true",
				   help="lay the objects out afresh at the sizes they now have (what an edit needs), not at the layout's addresses")
	t = sub.add_parser("edit-test", help="copy a tree, lengthen one string, build it laid out afresh")
	t.add_argument("source")
	t.add_argument("-o", "--output", required=True, help="where the edited copy goes (emptied first)")
	t.add_argument("--objects", required=True, help="the object file to write")
	t.add_argument("--newtonscript", required=True)
	r = sub.add_parser("roundtrip", help="extract, build and compare")
	r.add_argument("build_dir")
	r.add_argument("-o", "--output", required=True, help="where the source tree is written (emptied first)")
	r.add_argument("--newtonscript", required=True, help="the host's newtonscript, which compiles the functions")
	r.add_argument("--objects", help="also write the object file the host loads (newton --objects)")
	a = ap.parse_args(argv)
	if a.command == "edit-test":
		import shutil
		shutil.rmtree(a.output, ignore_errors=True)
		shutil.copytree(a.source, a.output)
		# the lowest string in the object area, and in the first package's
		# part (every package after it moves)
		trees = [a.output] + sorted(os.path.join(a.output, "rex", t) for t in os.listdir(os.path.join(a.output, "rex"))
									if os.path.isdir(os.path.join(a.output, "rex", t)))[:1]
		for tree in trees:
			if lengthen_first_string(tree) != 0:
				return 1
		folder = os.path.join(a.output, "objects")
		# and a slot added to a frame, holding a new frame: a map made for
		# each, and symbols for the new names
		for name in sorted(os.listdir(folder)):
			path = os.path.join(folder, name)
			with open(path, encoding="utf-8") as f:
				text = f.read()
			marker = "\nRcanonicalinkshape := {"
			if marker in text:
				text = text.replace(marker, marker + 'romsrcEdited: {romsrcNote: "added by edit-test"}, ', 1)
				with open(path, "w", encoding="utf-8", newline="\n") as f:
					f.write(text)
				print("edited Rcanonicalinkshape in %s: a slot romsrcEdited added, holding a new frame" % name)
				break
		result = main(["build", a.output, "-o", a.objects, "--newtonscript", a.newtonscript, "--relayout"])
		return result
	if a.command == "roundtrip":
		import shutil
		shutil.rmtree(a.output, ignore_errors=True)
		if main(["extract", a.build_dir, "-o", a.output, "--newtonscript", a.newtonscript]) != 0:
			return 1
		return main(["build", a.output, "--check", a.build_dir, "--newtonscript", a.newtonscript]
					+ (["-o", a.objects] if a.objects else []))
	if a.command == "extract":
		rom = nf.ROM(a.build_dir)
		e = Extractor(rom, a.output, a.build_dir, newtonscript=a.newtonscript)
		n, m = e.run()
		print("%d definitions (%d taken from compiled functions) and %d maps written to %s"
			  % (n, e.same_count, m, a.output))
		return 0
	builder = Builder(a.source, a.newtonscript, a.relayout)
	base, area = builder.build()
	if a.output:
		with open(a.output, "wb") as f:
			f.write(container(builder, base, area))
	print("built %d objects, %#x bytes at %#x%s" % (len(builder.by_path), len(area), base,
		  ", %d of them moved" % len(builder.relocations) if a.relayout else ""))
	if a.check:
		rom = nf.ROM(a.check)
		result = check(base, area, rom, None) | check_magic(builder, rom) | check_blocks(builder, rom)
		print("the object area, magic pointers and %d blocks of ROM data (the lexicons, the extension) are %s"
			  % (len(builder.blocks), "identical to the ROM's" if result == 0 else "NOT the ROM's"))
		return result
	return 0


if __name__ == "__main__":
	sys.exit(main())
