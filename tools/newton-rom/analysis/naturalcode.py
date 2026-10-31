#!/usr/bin/env python3
"""romsrc/'s functions made to read more naturally: their numbers by name
and their arguments and locals named - in place, on a tree already in the
natural form (naturalize.py, romsrc/README.md "Reading it").

    python tools/newton-rom/analysis/naturalcode.py romsrc [--stats]

The decompiler writes what the ROM's compiled functions keep: their
numbers, and `a1`, `l1` for their arguments and locals, whose names the
ROM does not keep.  This rewrites each function's source:

  - **Numbers by name** (constants.tsv, written afresh first by
    naturalize.py's Constants: the views' constants, the font specs', and
    now the error codes - the DDK headers' #defines and enums, then the
    reconstruction's, src/frames/NSErrors.h).  A number is written by name
    only where what it is is certain: put in a view's slot (`viewJustify:
    8388610` in a frame of the source, `.viewFont := 10243`, SetValue(v,
    'viewFlags, 513)) as the objects' slots are; and an error code - a
    number in the codes' range that is a known code's - given to a
    function whose name says error or throw (Throw('|evt.ex.comm|,
    kCommErrRequestCanceled), MBuildStatusFrameFromError(...)), put in or
    compared with a slot or variable called error, errorCode, err.  (A
    number in that range is not always one: -21600 and -10800 are time
    zones' offsets, and -10800 is also kError_DMA_Base.)  The builder
    makes a name the number again before the compiler sees it, and a sum
    of names that is a whole operand one number (romsrc.py's
    code_with_constants), so the compiled function is the same.
  - **Arguments and locals named**: an argument of a script whose arguments
    the Newton Programmer's Guide names by its slot (viewClickScript's unit,
    pickActionScript's index - SCRIPT_ARGUMENTS); a local after what it is
    first given (a call's name less its verb - GetRoot() root,
    self:MakeNewMeetingFrame(...) newMeetingFrame -, a slot's name, a
    loop's index or its key and value).  A name is used only if no word of
    the function - its free variables, its slots, the functions it calls,
    any nested function's - is spelt so, whatever the case, nor a
    constant's: so nothing the function names changes meaning.  The
    compiler keeps no names (romsrc.py builds with dbgNoVarNames), so the
    compiled function is the same; what has no good name keeps its own.

The tree builds to the same object file after it as before
(romsrc.py build --relayout, both).  --stats says what would change and
writes nothing.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romsrc							# noqa: E402
import naturalize						# noqa: E402

# the slots of a view whose numbers are the views' constants (naturalize.py's
# Constants.named: these, and a font's)
VIEW_SLOTS = {"viewflags", "viewjustify", "viewformat", "viewclass"}
# the functions whose number arguments are error codes
ERROR_FUNCTION = re.compile(r"throw|error|err\b|err[A-Z]", re.I)

# the arguments of the scripts the system calls, by the slot that holds the
# script (the Newton Programmer's Guide's names)
SCRIPT_ARGUMENTS = {
	"viewclickscript": ["unit"],
	"viewstrokescript": ["unit"],
	"viewgesturescript": ["unit", "gesture"],
	"viewwordscript": ["unit"],
	"viewchangedscript": ["slot", "view"],
	"viewhilitescript": ["on"],
	"viewcaretchangedscript": ["view", "offset", "length"],
	"viewkeydownscript": ["char", "flags"],
	"viewkeyupscript": ["char", "flags"],
	"viewkeystringscript": ["str"],
	"viewdropscript": ["dragInfo", "dropPt"],
	"pickactionscript": ["index"],
	"buttonclickscript": [],
	"installscript": ["partFrame", "removeFrame"],
	"removescript": ["partFrame"],
	"devinstallscript": ["partFrame", "removeFrame"],
	"devremovescript": ["partFrame"],
	"alarmuser": ["alarm"],
	"setupform": ["entry"],
	"stringextract": ["entry", "nLines"],
	"textscript": ["entry", "target"],
}

VERBS = ("Get", "Make", "Find", "New", "Create", "Build", "Read", "Compute", "Calc", "Determine", "Lookup",
		 "Fetch", "Load", "Copy", "Do")
CALL_NAMES = {"clone": "copy", "deepclone": "copy", "totalclone": "copy", "length": "count", "strlen": "len",
			  "array": "list", "stringer": "text", "sprintf": "text", "paramstr": "text", "numberstr": "text",
			  "smartstrtodate": "date", "time": "now", "ticks": "ticks", "random": "n", "getpoint": "pt",
			  "getroot": "root", "currentexception": "ex", "query": "cursor", "intern": "sym",
			  "getunionsoupalways": "soup", "getunionsoup": "soup", "getstores": "stores", "getdefaultstore": "store",
			  "getappparams": "params", "relbounds": "bounds", "setbounds": "bounds", "globalbox": "box",
			  "localbox": "box", "strequal": "same", "lsearch": "pos", "arraypos": "pos", "bfind": "found",
			  "stringtonumber": "number", "strpos": "pos", "substr": "part", "classof": "cls", "isinstance": "isa",
			  "min": "least", "max": "most", "date": "date", "entrystore": "store", "entrysoup": "soup",
			  "getvariable": "value", "getslot": "value", "getglobalvar": "value", "uppercase": "text",
			  "downcase": "text", "trimstring": "text"}
RESERVED = naturalize.NEWTONSCRIPT_WORDS | {"self", "inherited", "nil", "true", "exists", "instructions", "literals",
											"argframe", "numargs", "class", "array", "string", "frame", "binary",
											"symbol", "integer", "real", "char", "boolean"}
MAP_SORTED = romsrc.SORTED_MAP_TAGS		# (a map with more tags is kept sorted)
ARGUMENT = re.compile(r"a\d+\Z")
LOCAL = re.compile(r"l\d+\Z")


class Code:
	"""A file's tokens (romsrc.code_tokens, spaces out) with their places."""

	def __init__(self, text):
		self.text = text
		self.toks = [(k, s, e) for k, s, e in romsrc.code_tokens(text) if k != "space"]
		self.words = [text[s:e] for _, s, e in self.toks]

	def kind(self, i):
		return self.toks[i][0] if 0 <= i < len(self.toks) else None

	def word(self, i):
		return self.words[i] if 0 <= i < len(self.words) else None

	def tight(self, i, j):
		"""Whether token j follows token i with nothing between."""
		return self.toks[i][2] == self.toks[j][1]

	def match(self, i, open_, close):
		"""The token closing the bracket at i."""
		depth = 0
		for k in range(i, len(self.toks)):
			w = self.words[k]
			if w == open_:
				depth += 1
			elif w == close:
				depth -= 1
				if depth == 0:
					return k
		raise SyntaxError("unbalanced %s" % open_)


def functions(code):
	"""Every func of the file: (its `func` token, the token after its
	parameters' `)`, its last token), outermost first."""
	found = []
	for i, w in enumerate(code.words):
		if w == "func" and code.kind(i) == "word" and code.word(i + 1) == "(" and code.word(i - 1) not in ("'", "."):
			close = code.match(i + 1, "(", ")")
			if code.word(close + 1) == "begin":
				# (to its own end: a nested func is followed by the code around it)
				depth, last = 0, close + 1
				for k in range(close + 1, len(code.toks)):
					if code.kind(k) == "word" and code.word(k) == "begin":
						depth += 1
					elif code.kind(k) == "word" and code.word(k) == "end":
						depth -= 1
						if depth == 0:
							last = k
							break
			else:
				end = romsrc.code_extent(code.text, code.toks[i][1])
				last = max(k for k in range(close, len(code.toks)) if code.toks[k][2] <= end)
			found.append((i, close + 1, last))
	return found


def holder_tag(code, i):
	"""The slot a func at token i is put in: `tag: func`, `tag: compile(... func)`."""
	k = i - 1
	if code.word(k) == ":" and code.kind(k - 1) in ("word", "quoted"):
		return naturalize.unquote(code.word(k - 1))
	return None


def compile_holder(code, i):
	"""For the main func of a compile(...): the slot holding the compile."""
	k = i - 1
	if code.word(k) == ",":
		# (back to the compile's own bracket)
		depth = 0
		while k >= 0:
			w = code.word(k)
			if w in (")", "}", "]"):
				depth += 1
			elif w in ("(", "{", "["):
				if depth == 0:
					break
				depth -= 1
			k -= 1
		if code.word(k - 1) == "compile":
			return holder_tag(code, k - 1)
	return None


def from_call(name):
	"""A name for what a call answers."""
	low = name.lower()
	if low in CALL_NAMES:
		return CALL_NAMES[low]
	for verb in VERBS:
		if name.startswith(verb) and len(name) > len(verb) and name[len(verb)].isupper():
			name = name[len(verb):]
			break
	return name[:1].lower() + name[1:]


def call_name(code, name, open_):
	"""A name for what the call of `name` whose `(` is at open_ answers: the
	symbol it asks for, when it is given just one (GetMeetingSlot(m,
	'mtgDuration): mtgDuration), else the call's name less its verb."""
	close = code.match(open_, "(", ")")
	symbols = [code.word(k + 1) for k in range(open_ + 1, close) if code.word(k) == "'" and code.kind(k + 1) == "word"]
	if len(symbols) == 1 and name.lower() not in CALL_NAMES:
		return symbols[0]
	return from_call(name)


def first_value_name(code, i, scope_end):
	"""A name for what the assignment whose `:=` ends at token i gives: the
	last step of the chain it is - a slot (x.foo.bar: bar), a call or a
	message send (Foo(...), x:Foo(...), self:?Foo(...)), an element (x[i]:
	an item of x) - or what kind of value it is."""
	k = i + 1
	w = code.word(k)
	if w is None or k > scope_end:
		return None
	kind = code.kind(k)
	if kind == "string":
		return "text"
	if w == "{":
		return "frame"
	if w == "[":
		return "list"
	if w == "func":
		return "fn"
	if kind != "word" and w != ":" or w in RESERVED and w != "self":
		return None
	# the chain: a first step, then .slot, :Send(...), :?Send(...), [i]
	name = None
	if w == ":":
		j = k
	elif code.word(k + 1) == "(":
		name = call_name(code, w, k + 1)
		j = code.match(k + 1, "(", ")") + 1
	else:
		name = None if w == "self" else w
		j = k + 1
	while j <= scope_end:
		step = code.word(j)
		if step == "." and code.kind(j + 1) == "word" and code.word(j + 2) != "(":
			name = code.word(j + 1)
			j += 2
		elif step == ":" and code.word(j + 1) == "?" and code.kind(j + 2) == "word" and code.word(j + 3) == "(":
			name = call_name(code, code.word(j + 2), j + 3)
			j = code.match(j + 3, "(", ")") + 1
		elif step == ":" and code.kind(j + 1) == "word" and code.word(j + 2) == "(":
			name = call_name(code, code.word(j + 1), j + 2)
			j = code.match(j + 2, "(", ")") + 1
		elif step == "[" and name:
			name = name[:-1] if name.endswith("s") and len(name) > 3 else name + "Item"
			j = code.match(j, "[", "]") + 1
		else:
			break
	# (the chain must be the whole value; another variable's or a constant's
	# name says nothing new)
	if code.word(j) not in (";", "end", ")", "else", "then", "do", ",") and j <= scope_end:
		return None
	if name and (ARGUMENT.match(name) or LOCAL.match(name) or re.match(r"(a|l)\d+Item\Z|k[A-Z]", name)):
		return None
	return name


class Namer:
	"""Names for one outermost function's arguments and locals."""

	def __init__(self, code, outer, constants):
		_, _, last = outer
		start = outer[0]
		self.code = code
		# (the words a name must not be: what the function - nested ones too -
		# uses as a variable or calls; a slot's name after `.`, a symbol, a
		# message, a frame literal's tag are none of these)
		self.taken = set()
		for k in range(start, last + 1):
			if code.kind(k) not in ("word", "quoted"):
				continue
			before, after = code.word(k - 1), code.word(k + 1)
			if before in (".", "'") or before == ":" and code.tight(k - 1, k) or before == "?" and code.word(k - 2) == ":":
				continue
			if after == ":" and code.word(k + 2) != "=" and before in ("{", ",") and code.tight(k, k + 1) \
					and not code.tight(k + 1, k + 2):
				continue
			self.taken.add(naturalize.unquote(code.words[k]).lower())
		self.taken |= {n.lower() for n in constants.values} | {n.lower() for n in naturalize.SHIFTED} | RESERVED
		self.named = 0
		self.skipped = 0
		self.callers = None

	def scope(self, func, nested, tag, renames):
		"""Names for the arguments and locals of one func (its own tokens:
		not a nested func's)."""
		code = self.code
		i, body, last = func
		params = [code.word(k) for k in range(i + 2, body - 1) if code.kind(k) == "word"]
		own = [k for k in range(body, last + 1) if not any(n[0] <= k <= n[2] for n in nested)]
		# (a function whose argFrame may grow past 20 slots - its header's 3,
		# arguments, locals and the hidden ones of each for and foreach - has
		# its map sorted as it grows, Objects.cpp's AddSlot: there a name
		# decides a variable's place, so they are left as they are)
		declared = set(params)
		for k in own:
			if code.word(k) == "local":
				j = k + 1
				while code.kind(j) == "word":
					declared.add(code.word(j).lower())
					j += 1
					if code.word(j) == ":" and code.word(j + 1) == "=":
						break
					if code.word(j) != ",":
						break
					j += 1
		hidden = sum(4 for k in own if code.word(k) == "foreach") + sum(2 for k in own if code.word(k) == "for")
		if 3 + len(declared) + hidden > MAP_SORTED - 2:
			self.skipped += 1
			return
		used = set()

		def take(old, wanted):
			if not wanted or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", wanted):
				return
			base = wanted[:1].lower() + wanted[1:]
			name, n = base, 1
			while name.lower() in self.taken or name.lower() in used:
				n += 1
				name = "%s%d" % (base, n)
				if n > 9:
					return
			used.add(name.lower())
			renames[old] = name
		# the arguments: by the script's slot, else after what the calls of the
		# function's slot pass there (callers)
		known = SCRIPT_ARGUMENTS.get((tag or "").lower(), [])
		for k, p in enumerate(params):
			if ARGUMENT.match(p) and k < len(known):
				take(p, known[k])
			elif ARGUMENT.match(p) and self.callers and tag:
				take(p, self.callers.get((tag.lower(), k)))
		# the loops' variables
		loops = 0
		for k in own:
			w = code.word(k)
			if w == "for" and LOCAL.match(code.word(k + 1) or "") and code.word(k + 1) not in renames:
				take(code.word(k + 1), "ijkmn"[loops] if loops < 5 else None)
				loops += 1
			elif w == "foreach":
				vars_ = []
				j = k + 1
				while code.kind(j) == "word" and code.word(j) not in ("in", "deeply"):
					vars_.append(code.word(j))
					j += 2 if code.word(j + 1) == "," else 1
				names = ["key", "value"] if len(vars_) == 2 else ["item"]
				for v, wanted in zip(vars_, names):
					if (LOCAL.match(v) or ARGUMENT.match(v)) and v not in renames:
						take(v, wanted)
		# the locals: after what they are first given
		for k in own:
			w = code.word(k)
			if LOCAL.match(w or "") and w not in renames and code.word(k + 1) == ":" and code.word(k + 2) == "=" \
					and code.word(k - 1) != ".":
				take(w, first_value_name(code, k + 2, last))
		self.named += len(renames)


def call_sites(code, found):
	"""What the file's calls pass: found[(the called name, lower case, an
	argument's place)] counts the names the arguments' values give
	(first_value_name: a variable's, a slot's, a call's)."""
	for i, w in enumerate(code.words):
		if code.kind(i) != "word" or code.word(i + 1) != "(" or w.lower() in RESERVED:
			continue
		before = code.word(i - 1)
		if before in (".", "'") or before == "call":
			continue
		close = code.match(i + 1, "(", ")")
		start, k, depth, place = i + 2, i + 2, 0, 0
		while k <= close:
			x = code.word(k)
			if x in ("(", "[", "{") or x == "begin":
				depth += 1
			elif x in (")", "]", "}") or x == "end":
				depth -= 1
			if depth < 0 or (x == "," and depth == 0):
				if k > start:
					name = first_value_name(code, start - 1, k - 1)
					if name:
						# (a caller's meetingSlot2 is a meetingSlot here)
						found[(w.lower(), place)][re.sub(r"(?<=[A-Za-z])\d+\Z", "", name)] += 1
				place += 1
				start = k + 1
				if depth < 0:
					break
			k += 1


def callers_names(found):
	"""{(called name, place): the name most of its calls' arguments give,
	when more than half of those that give one agree}."""
	chosen = {}
	for key, names in found.items():
		name, n = names.most_common(1)[0]
		if n * 2 > sum(names.values()):
			chosen[key] = name
	return chosen


def rewrite_names(code, constants, callers=None):
	"""{token index: new text} for the arguments and locals named."""
	edits = {}
	funcs = functions(code)
	outer = [f for f in funcs if not any(g[0] < f[0] <= g[2] for g in funcs if g is not f)]
	count = 0
	for o in outer:
		namer = Namer(code, o, constants)
		namer.callers = callers
		inside = [f for f in funcs if o[0] <= f[0] <= o[2]]
		for f in inside:
			nested = [g for g in inside if f[0] < g[0] <= f[2]]
			tag = holder_tag(code, f[0]) or compile_holder(code, f[0])
			renames = {}
			namer.scope(f, nested, tag, renames)
			if not renames:
				continue
			for k in range(f[0], f[2] + 1):
				if any(g[0] <= k <= g[2] for g in nested):
					continue
				w = code.word(k)
				if w in renames and code.kind(k) == "word" and code.word(k - 1) not in (".", "'") \
						and not (code.word(k - 1) == ":" and code.tight(k - 1, k)):
					edits[k] = renames[w]
			count += len(renames)
	return edits, count


def rewrite_numbers(code, constants):
	"""{token index: new text, token index: ""} for the numbers named."""
	edits = {}
	count = 0
	for i, w in enumerate(code.words):
		if code.kind(i) != "number":
			continue
		negative = code.word(i - 1) == "-" and code.word(i - 2) in ("(", ",", ":", "=", ">", "<", "[", "{", "then",
																	"else", "return", "do", "and", "or", "not")
		start = i - 1 if negative else i
		value = -int(w) if negative else int(w)
		before, after = code.word(start - 1), code.word(i + 1)
		if after not in (",", ")", "}", "]", ";", "end", "then", "else", "do", None):
			continue
		named = None
		# a slot or variable: `tag: n` (a frame's), `tag := n`, `x.tag := n`,
		# and for an error code `x.tag = n`, `x.tag <> n`
		at, compared = None, False
		if before == ":":
			at = start - 2
		elif before == "=" and code.word(start - 2) == ":":
			at = start - 3
		elif before == "=" and code.word(start - 2) not in ("<", ">"):
			at, compared = start - 2, True
		elif before == ">" and code.word(start - 2) == "<":
			at, compared = start - 3, True
		if at is not None and code.kind(at) in ("word", "quoted"):
			tag = naturalize.unquote(code.word(at))
			low = tag.lower()
			is_error = naturalize.ERROR_TAG.search(low) is not None
			if (low in VIEW_SLOTS or low.endswith("font")) and not compared or is_error:
				named = constants.named(tag, value)
		# SetValue(v, 'viewFlags, n)
		if named is None and before == "," and code.word(start - 3) == "'" and code.kind(start - 2) == "word":
			if (code.word(start - 2) or "").lower() in VIEW_SLOTS:
				named = constants.named(code.word(start - 2), value)
		# an error code given to Throw..., ...Error(...)
		if named is None and value in constants.error_name and before in ("(", ","):
			depth, k = 0, start - 1
			while k >= 0:
				x = code.word(k)
				if x in (")", "]", "}"):
					depth += 1
				elif x in ("(", "[", "{"):
					if depth == 0:
						break
					depth -= 1
				k -= 1
			if code.word(k) == "(" and code.kind(k - 1) == "word" and ERROR_FUNCTION.search(code.word(k - 1)):
				named = constants.error_name[value]
		if named:
			edits[start] = named
			if negative:
				edits[i] = ""
			count += 1
	return edits, count


def apply(code, edits):
	out, last = [], 0
	for k in sorted(edits):
		_, s, e = code.toks[k]
		out.append(code.text[last:s] + edits[k])
		last = e
	text = "".join(out) + code.text[last:]
	return text


def rewrite_file(path, constants, write=True, callers=None):
	with open(path, encoding="utf-8") as f:
		text = f.read()
	code = Code(text)
	numbers, n_numbers = rewrite_numbers(code, constants) if callers is None else ({}, 0)
	names, n_names = rewrite_names(code, constants, callers)
	overlap = set(numbers) & set(names)
	if overlap:
		raise ValueError("%s: tokens both renamed and named: %s" % (path, sorted(overlap)[:5]))
	new = apply(code, {**numbers, **names})
	if write and new != text:
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(new)
	return n_numbers, n_names


def trees(root):
	found = [root]
	for top, dirs, files in sorted(os.walk(os.path.join(root, "rex"))):
		if "layout.tsv" in files and "objects" in dirs:
			found.append(top)
	return found


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("tree")
	ap.add_argument("--stats", action="store_true", help="say what would change, write nothing")
	a = ap.parse_args(argv)
	constants = naturalize.Constants()
	if not a.stats:
		constants.write(os.path.join(a.tree, "constants.tsv"))
	files = [path for tree in trees(a.tree) for path in romsrc.tree_files(tree)[1:]]
	total = collections.Counter()
	for path in files:
		n, m = rewrite_file(path, constants, write=not a.stats)
		total["numbers"] += n
		total["names"] += m
	print("%d numbers by name, %d arguments and locals named" % (total["numbers"], total["names"]))
	if a.stats:
		return 0
	# the arguments after what their calls pass, again while that names more
	# (an argument named passes its name on to the calls it makes)
	for round_ in range(1, 5):
		found = collections.defaultdict(collections.Counter)
		for path in files:
			with open(path, encoding="utf-8") as f:
				call_sites(Code(f.read()), found)
		callers = callers_names(found)
		named = sum(rewrite_file(path, constants, callers=callers)[1] for path in files)
		print("round %d: %d arguments named after their calls" % (round_, named))
		if not named:
			break
	return 0


if __name__ == "__main__":
	sys.exit(main())
