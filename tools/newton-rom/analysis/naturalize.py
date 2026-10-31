#!/usr/bin/env python3
"""romsrc/ rewritten in its natural form: functions in their frames, the
objects named for what they are, constants by name, a directory per kind of
thing.

    python tools/newton-rom/analysis/naturalize.py romsrc [-o <out>] [--stats]

The tree romsrc.py extract wrote (romsrc/README.md) says everything about
the ROM's objects, but in the extractor's terms: a definition named after
its address (obj_4f901d), files named after a magic pointer's number
(mp287.ns) or none (misc-003.ns), each function in a file of its own
(functions/obj_4f901d.DateChanged.ns) with a function("...") where it is
used, magic pointers by number (_proto: @387) and the views' flags as
numbers (viewFlags: 47649).  This rewrites such a tree - in place, or into
-o - so that it reads as NewtonScript a person would write:

  - a function is written in its frame, `DateChanged: func(a1) begin ...
    end` (one with constants of its own, or options for the compiler, or
    that is a value made when the ROM was built rather than a func, is
    compile([constants: {...},] [options: "...",] source));
  - a definition takes a name: the magic pointer's name for the object an
    entry is (the ROM's R-constants that are magic pointers,
    src/frames/ROMConstants.cpp, spelt as the ROM's symbols spell them:
    protoFloatNGo), the R-constant's for one the C code names
    (canonicalInkShape), else one made from what it is or what holds it;
    renames.tsv lists every old name with its new one;
  - a magic pointer is written by its name, @protoFloatNGo, in the objects
    and in the functions (magic.tsv gives each entry's object and its other
    names);
  - viewFlags, viewJustify, viewFormat, viewClass and fonts are sums of the
    constants of constants.tsv (vVisible + vClickable; written from
    src/views/ViewFlags.h and the packed font specs), where the number is
    exactly such a sum;
  - the files go by what they hold: objects/apps/, objects/protos/, ... -
    one file to a root object (a magic pointer's, an R-constant's) with
    the objects only it reaches, and the objects several reach together in
    objects/shared/.

Nothing about the objects changes: the tree builds to the same object file
byte for byte (romsrc.py build --relayout, before and after, with the same
builder; it was so for romsrc/ on 2026-10-31).  A package's part trees
under rex/ are rewritten the same way, each with names of its own, its
root object `part` in objects/part.ns.  The tree must be in the
extractor's form: the git tag romsrc-rom has one
(`git archive romsrc-rom romsrc | tar -x -C <dir>`).
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romsrc							# noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
TOKEN = romsrc.Reader.TOKEN


# ---- the old notation, read keeping its text

class Node:
	"""A value as written: kind frame (items: [(tag, Node)]), array (cls:
	the class's text, items: [Node]), call (name, items: [Node]: bitmap(...),
	same(...) and the rest), name (text: a definition's), leaf (text: a
	number, symbol, string, character, magic pointer, nil, true), code
	(a function: options, constants [(name, text)], main text)."""
	__slots__ = ("kind", "text", "items", "cls", "options", "constants")

	def __init__(self, kind, text=None, items=None, cls=None):
		self.kind, self.text, self.items, self.cls = kind, text, items if items is not None else [], cls
		self.options, self.constants = "", []


class Parser:
	def __init__(self, text, where):
		self.toks = []
		pos = 0
		while pos < len(text):
			m = TOKEN.match(text, pos)
			if not m:
				raise SyntaxError("%s: cannot read at %r" % (where, text[pos:pos + 30]))
			pos = m.end()
			if m.lastgroup != "space":
				self.toks.append((m.lastgroup, m.group()))
		self.i = 0
		self.where = where

	def peek(self, k=0):
		return self.toks[self.i + k] if self.i + k < len(self.toks) else (None, None)

	def take(self, text=None):
		t = self.peek()
		if text is not None and t[1] != text:
			raise SyntaxError("%s: expected %s, found %s" % (self.where, text, t[1]))
		self.i += 1
		return t

	def definitions(self):
		while self.peek()[0] is not None:
			name = self.take()[1]
			self.take(":=")
			value = self.value()
			self.take(";")
			yield name, value

	def value(self):
		kind, text = self.peek()
		if text == "'":
			self.take()
			return Node("leaf", "'" + self.take()[1])
		if text == "{":
			self.take()
			items = []
			while self.peek()[1] != "}":
				tag = self.take()[1]
				self.take(":")
				items.append((tag, self.value()))
				if self.peek()[1] == ",":
					self.take()
			self.take("}")
			return Node("frame", items=items)
		if text == "[":
			self.take()
			cls = self.take()[1]
			self.take(":")
			items = []
			while self.peek()[1] != "]":
				items.append(self.value())
				if self.peek()[1] == ",":
					self.take()
			self.take("]")
			return Node("array", items=items, cls=cls)
		if kind == "name" and self.peek(1)[1] == "(" and text not in ("nil", "true"):
			self.take()
			self.take("(")
			items = []
			while self.peek()[1] != ")":
				items.append(self.value())
				if self.peek()[1] == ",":
					self.take()
			self.take(")")
			return Node("call", text, items)
		if kind == "name" and text not in ("nil", "true") or kind == "quoted":
			self.take()
			return Node("name", text)
		self.take()
		return Node("leaf", text)


def unquote(text):
	return re.sub(r"\\(.)", r"\1", text[1:-1]) if text.startswith("|") else text


def read_function(path):
	"""A function file (romsrc.py's record: `@@ <addr> [options]`, then
	`@@const name` sections and `@@main`, or just the function, then
	`@@end`) as a code Node."""
	with open(path, encoding="utf-8") as f:
		text = f.read()
	head, body = text.split("\n", 1)
	node = Node("code")
	node.options = romsrc.record_flags(head).strip()
	if not body.rstrip().endswith("@@end"):
		raise SyntaxError("%s: no @@end" % path)
	body = body.rstrip()[:-len("@@end")].rstrip("\n")
	parts = re.split(r"^(@@const [^\n]*|@@main)\n", body, flags=re.M)
	if parts[0].strip():
		node.text = parts[0]
	for k in range(1, len(parts), 2):
		if parts[k] == "@@main":
			node.text = parts[k + 1]
		else:
			node.constants.append((parts[k][len("@@const "):].strip(), parts[k + 1].rstrip("\n")))
	if node.text is None:
		raise SyntaxError("%s: no main function" % path)
	node.text = node.text.rstrip("\n")
	return node


class Tree:
	"""One tree (romsrc/ or a package's part tree): its definitions in the
	order of their files, each one's file, the layout's lines."""

	def __init__(self, src):
		self.src = src
		self.defs = collections.OrderedDict()
		self.file_of = {}
		self.maps_text = None
		for path in romsrc.tree_files(src):
			with open(path, encoding="utf-8") as f:
				text = f.read()
			if os.path.basename(path) == "maps.ns" and os.path.dirname(path) == src:
				self.maps_text = text
				continue
			for name, value in Parser(text, path).definitions():
				self.defs[name] = value
				self.file_of[name] = os.path.relpath(path, src).replace(os.sep, "/")
		self.inline_functions()
		with open(os.path.join(src, "layout.tsv"), encoding="utf-8") as f:
			self.layout = f.read().split("\n")
		self.address = {}
		for line in self.layout:
			fields = line.split("\t")
			if len(fields) > 2 and re.match(r"[0-9a-f]+\Z", fields[0]):
				self.address[fields[1]] = int(fields[0], 16)

	def inline_functions(self):
		"""Every function("...") replaced by its source."""
		self.function_files = set()

		def walk(node):
			if node.kind == "call" and node.text == "function":
				rel = unquote_string(node.items[0].text)
				self.function_files.add(rel)
				return read_function(os.path.join(self.src, rel))
			if node.kind == "frame":
				node.items = [(t, walk(v)) for t, v in node.items]
			elif node.kind in ("array", "call"):
				node.items = [walk(v) for v in node.items]
			return node
		for name in self.defs:
			self.defs[name] = walk(self.defs[name])


def unquote_string(text):
	return text[1:-1]


# ---- what refers to what

def references(node, out):
	"""The definitions a value names (directly, or as a same(path)'s root)."""
	if node.kind == "name":
		out.append(node.text)
	elif node.kind == "call" and node.text == "same":
		out.append(path_root(unquote_string(node.items[0].text))[0])
	elif node.kind == "frame":
		for _, v in node.items:
			references(v, out)
	elif node.kind in ("array", "call"):
		for v in node.items:
			references(v, out)
	return out


PATH_ROOT = re.compile(r"(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)")


def path_root(path):
	"""A layout path's definition name and the rest (.tag, [i], ^)."""
	m = PATH_ROOT.match(path)
	return unquote(m.group(1)), path[m.end():]


# ---- names

def rom_constants():
	"""The ROM's R-constants (src/frames/ROMConstants.cpp, from the ROM's
	symbols): {magic pointer index: [names]}, {object area ref: [names]} -
	the names lower case, as the ROM's symbols have them."""
	magic, objects = collections.defaultdict(list), collections.defaultdict(list)
	with open(os.path.join(ROOT, "src", "frames", "ROMConstants.cpp"), encoding="utf-8") as f:
		for name, ref in re.findall(r'\{ "(\w+)",\s*0x([0-9a-fA-F]+)', f.read()):
			ref = int(ref, 16)
			(magic[ref >> 2] if ref & 3 == 3 else objects[ref]).append(name)
	return magic, objects


def spellings(trees):
	"""How the ROM spells each name (its symbols, its frames' tags): lower
	case -> the spelling seen most."""
	seen = collections.defaultdict(collections.Counter)

	def add(text):
		text = unquote(text)
		if romsrc.IDENT.match(text):
			seen[text.lower()][text] += 1

	def walk(node):
		if node.kind == "leaf" and node.text.startswith("'"):
			add(node.text[1:])
		elif node.kind == "frame":
			for t, v in node.items:
				add(t)
				walk(v)
		elif node.kind in ("array", "call"):
			for v in node.items:
				walk(v)
		elif node.kind == "code":
			for text in [node.text] + [c for _, c in node.constants]:
				for word in re.findall(r"'([A-Za-z_][A-Za-z0-9_]*)|\.([A-Za-z_][A-Za-z0-9_]*)|:([A-Za-z_][A-Za-z0-9_]*)\(", text):
					for w in word:
						if w:
							add(w)
	for tree in trees:
		for line in tree.layout:
			fields = line.split("\t")
			if len(fields) > 1 and fields[1].startswith("'"):
				add(fields[1][1:])
		for v in tree.defs.values():
			walk(v)
	return {k: c.most_common(1)[0][0] for k, c in seen.items()}


class Speller:
	"""A lower-case R-constant's name in camel case: as the ROM spells it
	where one of its symbols or tags is the name, else cut into the words
	the ROM's camel-case names are made of (the cut that uses the commonest
	words), each after the first capitalised: protoperiodicalarmeditor ->
	protoPeriodicAlarmEditor."""

	SUFFIXES = {"s", "es", "er", "ers", "ee", "ees", "ed", "ing", "ings", "ion", "ions", "or", "ors", "able", "y", "ly"}
	# (the only words of one or two letters: the headers' abbreviations
	# would cut names anywhere)
	SHORT = {"a", "n", "r", "x", "y", "to", "in", "on", "of", "at", "by", "no", "up", "id", "ok", "pm", "am", "io",
			 "tx", "rc", "ui", "is", "do", "go", "me", "my", "dc", "3d", "2d"}
	WORD = re.compile(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+")

	def __init__(self, spelling):
		self.spelling = spelling
		words = collections.Counter()
		for name in spelling.values():
			for w in self.WORD.findall(name):
				words[w.lower()] += 5
		# (and the words of the DDK headers' names, Apple's too, counting less)
		for top, _, files in sorted(os.walk(os.path.join(ROOT, "headers"))):
			for f in sorted(files):
				if f.endswith(".h"):
					with open(os.path.join(top, f), encoding="latin-1") as h:
						for ident in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", h.read()):
							for w in self.WORD.findall(ident):
								words[w.lower()] += 1
		total = sum(words.values())
		import math
		self.cost = {w: 2.0 + math.log(total / c) for w, c in words.items() if len(w) > 2 or w in self.SHORT}
		for w in self.SHORT:
			self.cost.setdefault(w, 2.0 + math.log(total))

	def __call__(self, name):
		spelt = self.spelling.get(name.lower())
		# (a spelling of the ROM's own, unless it starts with a capital where
		# a global's name does not: the ROM has 'ProToadDressPicker)
		if spelt and (spelt[:1] == name[:1] or not spelt[:1].isupper()):
			return spelt
		if name != name.lower() or "_" in name:
			return name
		text = name
		best = [(0.0, [])] + [None] * len(text)
		for end in range(1, len(text) + 1):
			for start in range(max(0, end - 24), end):
				if best[start] is None:
					continue
				w = text[start:end]
				c = self.cost.get(w)
				if c is None:
					# (an ending kept on the word before it, cheaply; any other
					# run the words do not make dear, so that a known word is
					# always preferred)
					c = 1.0 if (w in self.SUFFIXES and start > 0) or w.isdigit() else 8.0 + 3 * len(w)
				total = best[start][0] + c
				if best[end] is None or total < best[end][0]:
					best[end] = (total, best[start][1] + [w])
		words = []
		for w in best[len(text)][1]:
			if words and w not in self.cost and not w.isdigit():
				words[-1] += w
			else:
				words.append(w)
		return words[0] + "".join(w[:1].upper() + w[1:] for w in words[1:])


NEWTONSCRIPT_WORDS = {"and", "begin", "break", "by", "call", "constant", "div", "do", "else", "end", "exists",
					  "for", "foreach", "func", "global", "if", "in", "inherited", "local", "loop", "mod",
					  "native", "not", "onexception", "or", "repeat", "return", "self", "then", "to", "try",
					  "until", "while", "with", "nil", "true", "compile", "function", "same", "map"}


class Namer:
	"""New names for a tree's definitions, unique whatever their case."""

	def __init__(self, tree, taken=()):
		self.tree = tree
		self.new = {}
		self.used = {n.lower() for n in taken} | NEWTONSCRIPT_WORDS
		self.used |= {k.lower() for k in romsrc.Reader.__dict__ if False}

	def give(self, old, wanted):
		if old in self.new:
			return self.new[old]
		wanted = re.sub(r"[^A-Za-z0-9_]", "_", wanted) or "object"
		if not re.match(r"[A-Za-z_]", wanted):
			wanted = "_" + wanted
		name, n = wanted, 1
		while name.lower() in self.used:
			n += 1
			name = "%s_%d" % (wanted, n)
		self.used.add(name.lower())
		self.new[old] = name
		return name


def holders(tree):
	"""Each definition's holders: [(holder's definition, path to it, the
	slot's tag or None for an array's element)]."""
	held = collections.defaultdict(list)

	def walk(node, owner, path, tag):
		if node.kind == "name":
			held[node.text].append((owner, path, tag))
		elif node.kind == "frame":
			for t, v in node.items:
				walk(v, owner, path + "." + t, unquote(t))
		elif node.kind == "array":
			for k, v in enumerate(node.items):
				walk(v, owner, path + "[%d]" % k, None)
		elif node.kind == "call" and node.text != "same":
			for k, v in enumerate(node.items):
				walk(v, owner, path, None)
	for name, v in tree.defs.items():
		walk(v, name, name, None)
	return held


def described(node, max_words=4):
	"""A name made of a value's own words: a string's, a frame's
	preallocatedContext, debug or title."""
	text = None
	if node.kind == "frame":
		slots = {unquote(t): v for t, v in node.items}
		for key in ("preallocatedContext", "appSymbol", "symbol", "debug", "name", "title", "label", "text"):
			v = slots.get(key)
			if v is not None and v.kind == "leaf" and v.text[:1] in ("'", '"'):
				text = v.text
				break
	elif node.kind == "leaf" and node.text.startswith('"'):
		text = node.text
	if text is None:
		return None
	if text.startswith("'"):
		return unquote(text[1:])
	words = re.findall(r"[A-Za-z][A-Za-z0-9]*", re.sub(r"\\u[0-9a-fA-F]{4}|\\.", " ", text[1:-1]))[:max_words]
	if not words:
		return None
	return words[0][:1].lower() + words[0][1:] + "".join(w[:1].upper() + w[1:] for w in words[1:])


def name_definitions(tree, magic_names=None, r_names=None, speller=None, taken=(), exports=None):
	"""{old name: new name} for every definition of a tree: magic_names
	{definition: name} and r_names {definition: name} first, then the rest
	in address order, each after what it is or what holds it - an object a
	package exports that says nothing of itself after its index in the
	export table (exports: {definition: index}), export7."""
	namer = Namer(tree, taken)
	namer.exports = exports or {}
	if not magic_names and tree.address:
		# (a package's part: its first object is the part's root)
		first = min(tree.address, key=tree.address.get)
		if first in tree.defs:
			namer.give(first, "part")
	for source in (magic_names or {}, r_names or {}):
		for old, name in source.items():
			if old in tree.defs:
				namer.give(old, speller(name) if speller else name)
	held = holders(tree)
	order = sorted(tree.defs, key=lambda d: tree.address.get(d, 1 << 40))
	# (a definition's name may come from its holder's: those first)
	pending = [d for d in order if d not in namer.new]
	for _ in range(4):
		later = []
		for old in pending:
			wanted = wanted_name(tree, old, held, namer)
			if wanted is None:
				later.append(old)
			else:
				namer.give(old, wanted)
		pending = later
	for old in pending:
		namer.give(old, wanted_name(tree, old, held, namer, last=True))
	return namer.new


def wanted_name(tree, old, held, namer, last=False):
	node = tree.defs[old]
	if not old.startswith(("obj_", "map_")) and not re.match(r"R[a-z]", old):
		return old
	if old in namer.exports and not described(node):
		return "export%d" % namer.exports[old]
	if node.kind == "call" and node.text == "same":
		# (an object compiled functions share: named after the function that
		# holds it, `Foo_literal3`, or `Foo_code` for its instructions)
		root, rest = path_root(unquote_string(node.items[0].text))
		if root not in namer.new and not last:
			return None
		steps = re.findall(r"\.(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)|\[(\d+)\]", rest)
		owner, what = namer.new.get(root, root), None
		for k, (tag, index) in enumerate(steps):
			tag = unquote(tag)
			if tag == "instructions":
				what = "code"
			elif tag == "literals" and k + 1 < len(steps) and steps[k + 1][1]:
				what = "literal" + steps[k + 1][1]
			elif tag and tag not in ("argFrame", "literals"):
				owner, what = tag, None
		return "%s_%s" % (owner, what or "value")
	own = described(node)
	if own:
		return own
	holder = held.get(old)
	if not holder:
		kind = node.kind if node.kind != "call" else node.text
		return {"frame": "frame", "array": "array", "code": "function", "leaf": "value"}.get(kind, kind)
	tags = collections.Counter(tag for _, _, tag in holder if tag)
	if tags:
		tag = tags.most_common(1)[0][0]
		if tag not in GENERIC_TAGS:
			return tag
		# (a slot every proto has, _proto, bounds, icon: with what holds it)
		owner, path, _ = next(h for h in holder if h[2] == tag)
		parent = [unquote(t) for t in re.findall(r"\.(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)", path)[:-1]
				  if unquote(t) not in GENERIC_TAGS]
		if not parent and owner not in namer.new and not last:
			return None
		base = parent[-1] if parent else namer.new.get(owner, owner)
		return base + (tag.lstrip("_")[:1].upper() + tag.lstrip("_")[1:])
	owner = holder[0][0]
	if owner not in namer.new and not last:
		return None
	return "%sItem" % namer.new.get(owner, owner)


# slots too common to name what they hold by themselves
GENERIC_TAGS = {"_proto", "_parent", "bounds", "viewBounds", "bits", "mask", "cbits", "icon", "isa", "item", "items",
				"literals", "instructions", "argFrame", "data", "value", "viewChildren", "stepChildren", "signature",
				"preconditions", "primary_act", "_defaultButton", "entryLine", "picture", "class", "text", "frame"}


# ---- which file a definition goes in

def owners(tree, roots):
	"""{definition: the root that alone reaches it, or None when several
	do} - a definition reached from no root is a root of its own.  The
	edges: a definition names another, and a function holds what a same()
	definition is."""
	edges = collections.defaultdict(list)
	for name, v in tree.defs.items():
		if v.kind == "call" and v.text == "same":
			edges[path_root(unquote_string(v.items[0].text))[0]].append(name)
		else:
			for r in references(v, []):
				if r in tree.defs:
					edges[name].append(r)
	referred = {m for targets in edges.values() for m in targets}
	roots = list(roots) + [d for d in tree.defs if d not in referred and d not in roots]
	reached = set()
	stack = list(roots)
	while stack:
		n = stack.pop()
		if n not in reached:
			reached.add(n)
			stack += edges[n]
	roots = list(roots) + [d for d in tree.defs if d not in reached]
	# the dominator tree (Cooper, Harvey and Kennedy's), from a start that
	# reaches every root
	START = " start"				# (no definition's name)
	order, seen = [], set()

	def dfs(start):
		stack = [(start, iter(roots))]
		seen.add(start)
		while stack:
			node, it = stack[-1]
			nxt = next((x for x in it if x not in seen and x in tree.defs), None)
			if nxt is None:
				order.append(node)
				stack.pop()
			else:
				seen.add(nxt)
				stack.append((nxt, iter(edges[nxt])))
	dfs(START)
	post = {n: k for k, n in enumerate(order)}
	preds = collections.defaultdict(list)
	for r in roots:
		preds[r].append(START)
	for n in order:
		if n != START:
			for m in edges[n]:
				if m in post:
					preds[m].append(n)
	rpo = list(reversed(order))
	idom = {START: START}

	def intersect(a, b):
		while a != b:
			while post[a] < post[b]:
				a = idom[a]
			while post[b] < post[a]:
				b = idom[b]
		return a
	changed = True
	while changed:
		changed = False
		for n in rpo[1:]:
			ps = [p for p in preds[n] if p in idom]
			new = ps[0]
			for p in ps[1:]:
				new = intersect(p, new)
			if idom.get(n) != new:
				idom[n] = new
				changed = True
	root_set = set(roots)
	owner = {}
	for n in rpo[1:]:
		if n in root_set:
			owner[n] = n
		elif idom[n] == START:
			owner[n] = None
		else:
			owner[n] = owner[idom[n]] if idom[n] in owner else None
	return owner


# ---- the constants

FONT_FACES = [("tsBold", 1), ("tsItalic", 2), ("tsUnderline", 4), ("tsOutline", 8), ("tsShadow", 0x10),
			  ("tsCondensed", 0x20), ("tsExtended", 0x40), ("tsSuperscript", 0x80), ("tsSubscript", 0x100)]
FONT_FAMILIES = ["tsSystem", "tsFancy", "tsSimple", "tsHWFont"]		# espy, New York, Geneva, Handwriting (FamilyNumToSym)
SHIFTED = {"vfPen": 8, "vfInset": 16, "vfShadow": 18, "vfRound": 24, "tsSize": 10}


class Constants:
	"""The views' constants (src/views/ViewFlags.h's enums) and the packed
	font specs' (qd/Fonts.h: the family in bits 0-9 - FamilyNumToSym's
	espy, New York, Geneva, Handwriting - the size in 10-19, the face in
	20-29), and the integers of the slots that hold them written with
	them."""

	def __init__(self):
		with open(os.path.join(ROOT, "src", "views", "ViewFlags.h"), encoding="utf-8") as f:
			text = f.read().replace("\r", "\n")
		self.values = collections.OrderedDict()
		for name, value in re.findall(r"^\s*((?:v|vj|vf|cl)[A-Z][A-Za-z0-9]*)\s*=\s*(0x[0-9a-fA-F]+|\d+)", text, re.M):
			if not name.endswith(("Mask", "Shift")) and name != "vIsBeingDeleted":
				self.values.setdefault(name, int(value, 0))
		for k, family in enumerate(FONT_FAMILIES):
			self.values[family] = k
		for name, bit in FONT_FACES:
			self.values[name] = bit << 20

	def write(self, path):
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write("# the constants the tree's notation may name (romsrc.py's TreeNames): a name and\n"
					"# its value, or `<<n` for one used as name(x), x shifted left n.  Written by\n"
					"# tools/newton-rom/analysis/naturalize.py: the views' constants from\n"
					"# src/views/ViewFlags.h, and the packed font specs' (qd/Fonts.h: tsSystem, tsFancy,\n"
					"# tsSimple, tsHWFont the families espy, New York, Geneva, Handwriting; tsSize(n);\n"
					"# the faces)\n")
			for name, value in self.values.items():
				f.write("%s\t%#x\n" % (name, value))
			for name, shift in SHIFTED.items():
				f.write("%s\t<<%d\n" % (name, shift))

	def named(self, tag, number):
		"""The integer `number` of slot `tag` as a sum of constants, or None."""
		tag = tag.lower()
		if tag == "viewflags":
			return self.view_flags(number)
		if tag == "viewjustify":
			return self.view_justify(number)
		if tag == "viewformat":
			return self.view_format(number)
		if tag == "viewclass":
			return next((k for k, v in self.values.items() if v == number and k.startswith("cl")), None)
		if tag.endswith("font"):
			return self.font(number)
		return None

	def view_flags(self, n):
		if n <= 0 or n >= 1 << 28:
			return None
		parts = []
		if n & 0x01fffe00 == 0x01fffe00:
			parts.append("vAnythingAllowed")
			n &= ~0x01fffe00
		for bit in range(28):
			if n & (1 << bit):
				name = next((k for k, v in self.values.items() if v == 1 << bit and re.match(r"v[A-Z]", k)), None)
				if name is None:
					return None
				parts.append(name)
		return " + ".join(parts)

	@staticmethod
	def fields(n, fields):
		"""n made of fields [(mask, {value: name})]: the names, or None."""
		parts = []
		for mask, names in fields:
			v = n & mask
			n &= ~mask
			if v == 0:
				continue
			if v not in names:
				return None
			parts.append(names[v])
		return parts if n == 0 else None

	def names_of(self, prefix, mask):
		return {v: k for k, v in reversed(list(self.values.items())) if k.startswith(prefix) and v and v & mask == v}

	def view_justify(self, n):
		if n <= 0:
			return None
		fields = [(0x3, self.names_of("vj", 0x3)), (0xc, self.names_of("vj", 0xc)),
				  (0x30, self.names_of("vjParent", 0x30)), (0xc0, self.names_of("vjParent", 0xc0)),
				  (0x100, {0x100: "vjParentClip"}),
				  (0xe00, self.names_of("vjSibling", 0xe00)), (0x7000, self.names_of("vjSibling", 0x7000)),
				  (0x8000, {0x8000: "vjChildrenLasso"}), (0x10000, {0x10000: "vjReflow"}),
				  (0x1000000, {0x1000000: "vjOneWordOnly"})] + \
			[(bit, {bit: name}) for name, bit in (("vjLeftRatio", 0x4000000), ("vjRightRatio", 0x8000000),
												  ("vjTopRatio", 0x10000000), ("vjBottomRatio", 0x20000000))]
		parts = self.fields(n, fields)
		return " + ".join(parts) if parts else None

	def view_format(self, n):
		if n <= 0:
			return None
		named = lambda prefix: {v: k for k, v in self.values.items() if k.startswith(prefix) and v}
		fields = [(0xf, named("vfFill")), (0xf0, named("vfFrame")),
				  (0xf00, {k << 8: "vfPen(%d)" % k for k in range(1, 16)}),
				  (0xf000, named("vfLines")),
				  (0x3 << 16, {k << 16: "vfInset(%d)" % k for k in range(1, 4)}),
				  (0x3 << 18, {k << 18: "vfShadow(%d)" % k for k in range(1, 4)}),
				  (0xf << 24, {k << 24: "vfRound(%d)" % k for k in range(1, 16)})]
		parts = self.fields(n, fields)
		return " + ".join(parts) if parts else None

	def font(self, n):
		if n <= 0 or n >= 1 << 30:
			return None
		family, size, face = n & 0x3ff, (n >> 10) & 0x3ff, n >> 20
		if family >= len(FONT_FAMILIES) or size == 0:
			return None
		parts = [FONT_FAMILIES[family], "tsSize(%d)" % size]
		for name, bit in FONT_FACES:
			if face & bit:
				parts.append(name)
				face &= ~bit
		return " + ".join(parts) if face == 0 else None

	def check(self, expression, number):
		"""Whether the expression adds up to the number, as romsrc.py reads it."""
		total = 0
		for term in expression.split(" + "):
			m = re.match(r"(\w+)\((\d+)\)\Z", term)
			total += int(m.group(2)) << SHIFTED[m.group(1)] if m else self.values[term]
		return total == number


# ---- writing the natural form

INTEGER = re.compile(r"-?\d+\Z")
WIDTH = 100						# a frame or array longer than this is written a slot to a line


class Writer:
	"""A tree's definitions written out: names changed, magic pointers by
	name, constants by name, functions in place."""

	def __init__(self, tree, renames, magic_name, constants):
		self.tree, self.renames, self.magic_name, self.constants = tree, renames, magic_name, constants
		self.symbolised = 0

	def magic(self, text):
		if text[:1] == "@" and text[1:].isdigit():
			name = self.magic_name.get(int(text[1:]))
			return "@" + name if name else text
		return text

	def code(self, text):
		out, last = [], 0
		for kind, start, end in romsrc.code_tokens(text):
			if kind == "magic" and text[start + 1].isdigit():
				out.append(text[last:start] + self.magic(text[start:end]))
				last = end
		return "".join(out) + text[last:]

	def path(self, path):
		if not PATH_ROOT.match(path):
			return path			# (a symbol's line: 'name)
		root, rest = path_root(path)
		new = self.renames.get(root)
		return new + rest if new else path

	def value(self, node, indent, tag=None):
		"""A value's text at an indent (tabs): its first line goes where it
		is put, its others at the indent."""
		pad = "\t" * indent
		if node.kind == "leaf":
			text = self.magic(node.text)
			if tag and INTEGER.match(text):
				named = self.constants.named(unquote(tag), int(text))
				if named:
					if not self.constants.check(named, int(text)):
						raise ValueError("%s: %s is not %s" % (tag, named, text))
					self.symbolised += 1
					return named
			return text
		if node.kind == "name":
			new = self.renames.get(unquote(node.text))
			return new if new else node.text
		if node.kind == "code":
			return self.function(node, indent)
		if node.kind == "call":
			if node.text == "same":
				return 'same("%s")' % self.path(unquote_string(node.items[0].text))
			return "%s(%s)" % (node.text, ", ".join(self.value(v, indent) for v in node.items))
		if node.kind == "frame":
			items = [(t, self.value(v, indent + 1, t)) for t, v in node.items]
			flat = "{" + ", ".join("%s: %s" % (t, v) for t, v in items) + "}"
			if "\n" not in flat and len(flat) + len(pad) <= WIDTH:
				return flat
			return "{\n" + ",\n".join("%s\t%s: %s" % (pad, t, v) for t, v in items) + "\n" + pad + "}"
		if node.kind == "array":
			items = [self.value(v, indent + 1) for v in node.items]
			flat = "[%s: %s]" % (node.cls, ", ".join(items))
			if "\n" not in flat and len(flat) + len(pad) <= WIDTH:
				return flat
			return "[%s:\n" % node.cls + ",\n".join(pad + "\t" + v for v in items) + "\n" + pad + "]"
		raise ValueError(node.kind)

	@staticmethod
	def reindent(text, indent):
		pad = "\t" * indent
		return "\n".join(line if k == 0 or not line else pad + line for k, line in enumerate(text.split("\n")))

	def function(self, node, indent):
		main = self.code(node.text).strip()
		if not node.constants and not node.options and main.startswith("func"):
			return self.reindent(main, indent)
		pad = "\t" * indent
		lines = ["compile("]
		if node.constants:
			lines.append(pad + "\tconstants: {")
			lines.append(",\n".join("%s\t\t%s: %s" % (pad, name, self.reindent(self.code(text).strip(), indent + 2))
									for name, text in node.constants))
			lines.append(pad + "\t},")
		if node.options:
			lines.append('%s\toptions: "%s",' % (pad, node.options))
		lines.append(pad + "\t" + self.reindent(main, indent + 1) + ")")
		return "\n".join(lines)

	def definition(self, name):
		return "%s := %s;\n" % (self.renames.get(name, name), self.value(self.tree.defs[name], 0))


def category(tree, name, new_name):
	"""The directory of objects/ a root's file goes in (None: values.ns)."""
	node = tree.defs[name]
	low = new_name.lower()
	tags = {unquote(t) for t, _ in node.items} if node.kind == "frame" else set()
	if node.kind == "leaf":
		return None
	if low.startswith("proto") and not low.startswith("protocol"):
		return "protos"
	if low.startswith("newt") and not low.startswith("newton"):
		return "newtapp"
	if low.startswith("canonical"):
		return "canonical"
	if low.endswith("slip"):
		return "slips"
	if "appSymbol" in tags:
		return "apps"
	if node.kind == "call" and node.text in ("bitmap", "picture", "pict") or tags & {"bits", "cbits", "picture"} \
			or low.endswith(("bitmap", "bitmaps", "bits", "icon", "mask", "picture", "picts")):
		return "pictures"
	if tags & {"samples", "sndFrameType"} or low.endswith(("sound", "beep")) or low.startswith("dtmf"):
		return "sounds"
	if low.endswith(("soupname", "soupdef", "queryspec", "indices", "soupindexes")):
		return "soups"
	if low.startswith(("rc", "corrector", "recog", "rec")) or "lexicon" in low or "dict" in low:
		return "recognition"
	if low.endswith(("filter", "popup", "picker")):
		return "views"
	return "system"


def kind_file(node):
	kind = node.kind if node.kind != "call" else node.text
	return {"frame": "frames", "array": "arrays", "code": "functions", "leaf": "values", "same": "compiled"}.get(kind, "data")


def file_for(tree, renames, owner, package=False):
	"""{definition: its file under objects/}: a root's own, with what only
	it reaches; the plain values together in values.ns; what several roots
	reach in shared/<kind>.ns."""
	where = {}
	for name in tree.defs:
		root = owner.get(name)
		if root is None:
			where[name] = "shared/%s.ns" % kind_file(tree.defs[name])
			continue
		new = renames.get(root, root)
		if new == "part" and package:
			where[name] = "part.ns"
			continue
		cat = category(tree, root, new)
		where[name] = "values.ns" if cat is None else "%s/%s.ns" % (cat, new)
	return where


def header_for(renames):
	def header(rel, names):
		if rel.startswith("shared/"):
			return "objects several roots reach, none alone (naturalize.py)"
		if rel == "values.ns":
			return "the plain values the magic pointers and the C code name"
		return "%s, and what only it reaches" % renames.get(names[0], names[0])
	return header


def write_tree(tree, out, renames, owner, magic_name, constants, package=False):
	"""The tree's objects/ written afresh into out, its layout's names
	changed, its function files gone, renames.tsv."""
	writer = Writer(tree, renames, magic_name, constants)
	where = file_for(tree, renames, owner, package)
	header = header_for(renames)
	files = collections.defaultdict(list)
	# (a root first in its file, the rest in address order, what compiled
	# functions share last)
	order = sorted(tree.defs, key=lambda d: (owner.get(d) != d, tree.defs[d].kind == "call" and tree.defs[d].text == "same",
											 tree.address.get(d, 1 << 40)))
	for name in order:
		files[where[name]].append(name)
	objdir = os.path.join(out, "objects")
	if os.path.isdir(objdir):
		shutil.rmtree(objdir)
	for rel, names in sorted(files.items()):
		path = os.path.join(objdir, rel)
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write("// %s\n" % header(rel, names))
			shared = False
			for name in names:
				node = tree.defs[name]
				if node.kind == "call" and node.text == "same" and not shared and len(names) > 1:
					f.write("\n// what the functions above hold and share with other objects (same(): romsrc/README.md)\n")
					shared = True
				f.write("\n" + writer.definition(name))
	lines = []
	for line in tree.layout:
		fields = line.split("\t")
		if len(fields) > 2 and fields[0] == "alias":
			fields[1] = writer.path(fields[1])
			fields[2] = renames.get(fields[2], fields[2])
		elif len(fields) > 2 and re.match(r"[0-9a-f]+\Z", fields[0]):
			fields[1] = writer.path(fields[1])
		lines.append("\t".join(fields))
	with open(os.path.join(out, "layout.tsv"), "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines))
	# (functions/ goes: every file a function("...") named is in its frame
	# now, and the extractor wrote a few no definition names - functions
	# compiled functions share, which same() definitions take from them)
	functions = os.path.join(out, "functions")
	if os.path.isdir(functions):
		shutil.rmtree(functions)
	with open(os.path.join(out, "renames.tsv"), "w", encoding="utf-8", newline="\n") as f:
		f.write("# each definition's name before the tree was put in its natural form "
				"(tools/newton-rom/analysis/naturalize.py), and after\n")
		for old in sorted(renames, key=lambda d: tree.address.get(d, 1 << 40)):
			if renames[old] != old:
				f.write("%s\t%s\n" % (old, renames[old]))
	return writer, len(files)


def naturalize(src, out):
	"""src written in the natural form into out (which may be src). ==> lines
	saying what was done."""
	if os.path.abspath(out) != os.path.abspath(src):
		if os.path.exists(out):
			shutil.rmtree(out)
		shutil.copytree(src, out)
	constants = Constants()
	constants.write(os.path.join(out, "constants.tsv"))
	part_trees = sorted(top for top, dirs, files in os.walk(os.path.join(src, "rex")) if "layout.tsv" in files and "objects" in dirs)
	main = Tree(src)
	trees = [main] + [Tree(p) for p in part_trees]
	speller = Speller(spellings(trees))
	# the magic pointers' names: the R-constants that are magic pointers
	magic_r, object_r = rom_constants()
	with open(os.path.join(src, "magic.tsv"), encoding="utf-8") as f:
		magic_lines = f.read().split("\n")
	by_index = {int(l.split("\t")[0]): l.split("\t")[1] for l in magic_lines if l.split("\t")[0].isdigit()}
	magic_names = {}				# definition -> its magic pointer's name
	for i, names in sorted(magic_r.items()):
		if by_index.get(i) in main.defs:
			magic_names.setdefault(by_index[i], names[0])
	at = {a: d for d, a in main.address.items()}
	r_names = {at[ref - 1]: names[0] for ref, names in sorted(object_r.items()) if at.get(ref - 1) in main.defs}
	taken = set(constants.values) | set(SHIFTED)
	renames = name_definitions(main, magic_names, r_names, speller, taken)
	# magic.tsv: each entry's object by its new name, then its other names
	magic_name = {}
	named_entries = set()
	used = {n.lower() for n in renames.values()} | {t.lower() for t in taken}
	lines = []
	for line in magic_lines:
		fields = line.split("\t")
		if not fields[0].isdigit():
			lines.append(line)
			continue
		i, what = int(fields[0]), renames.get(fields[1], fields[1])
		others = []
		for name in magic_r.get(i, []):
			spelt = speller(name)
			if spelt.lower() != what.lower() and spelt.lower() not in used:
				others.append(spelt)
				used.add(spelt.lower())
		# (@name is the first entry that is the object: another entry that is
		# the same object is written by a name of its own, or its number)
		own = [what] if romsrc.IDENT.match(what) and what not in ("nil", "true") and what.lower() not in named_entries else []
		own += [o for o in others if o.lower() not in named_entries]
		magic_name[i] = own[0] if own else None
		named_entries.update(n.lower() for n in [what] + others)
		lines.append("\t".join([fields[0], what] + ([",".join(others)] if others else [])))
	if lines and lines[0].startswith("#"):
		lines[0] = ("# the magic-pointer table: @index, the object (its name or path) or value, and "
					"any other names it goes by (@name in the notation)")
	with open(os.path.join(out, "magic.tsv"), "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines))
	roots = list(magic_names) + [d for d in r_names if d not in magic_names]
	writer, nfiles = write_tree(main, out, renames, owners(main, roots), magic_name, constants)
	report = ["romsrc: %d definitions renamed, %d integers by constants, %d files"
			  % (sum(1 for o, n in renames.items() if o != n), writer.symbolised, nfiles)]
	export_names = {}
	exports = export_indexes(src)
	for tree in trees[1:]:
		rel = os.path.relpath(tree.src, src).replace(os.sep, "/")
		renames = name_definitions(tree, speller=speller, taken=taken, exports=exports.get(rel.split("/")[1], {}))
		writer, nfiles = write_tree(tree, os.path.join(out, rel), renames, owners(tree, []), magic_name, constants,
									package=True)
		export_names[rel] = renames
		report.append("%s: %d definitions renamed, %d integers by constants, %d files"
					  % (rel, sum(1 for o, n in renames.items() if o != n), writer.symbolised, nfiles))
	rename_exports(out, export_names)
	return report


def export_indexes(src):
	"""{package: {definition: its index in the export table}} (rex/fexp_*.txt's comments)."""
	found = collections.defaultdict(dict)
	rex = os.path.join(src, "rex")
	for name in sorted(os.listdir(rex)):
		if name.startswith("fexp_"):
			with open(os.path.join(rex, name), encoding="utf-8") as f:
				for k, package, what in re.findall(r"# (\d+): (\S+) (\S+)$", f.read(), re.M):
					found[package].setdefault(what, int(k))
	return found


def rename_exports(out, export_names):
	"""The export table's comments (rex/fexp_*.txt: `# k: Package obj_...`)
	with the objects' new names."""
	rex = os.path.join(out, "rex")
	for name in sorted(os.listdir(rex)):
		if not name.startswith("fexp_"):
			continue
		path = os.path.join(rex, name)
		with open(path, encoding="utf-8") as f:
			text = f.read()

		def renamed(m):
			renames = export_names.get("rex/" + m.group(1), {})
			root, rest = path_root(m.group(2))
			return "%s %s%s" % (m.group(1), renames.get(root, root), rest)
		text = re.sub(r"(?<=: )(\S+) (\S+)$", renamed, text, flags=re.M)
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(text)


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("tree")
	parser.add_argument("-o", "--output", help="where to write the natural tree (default: the tree itself)")
	a = parser.parse_args(argv)
	for line in naturalize(a.tree, a.output or a.tree):
		print(line)
	return 0


if __name__ == "__main__":
	sys.exit(main())
