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
    python romsrc.py build romsrc --git-ref romsrc-rom --original --check build/MP2x00US --newtonscript <exe>
                     # the tree as the tag romsrc-rom has it (host.ROMSourceCommitted)

The output (-o) is what the host loads in place of a ROM image
(`newton --objects`, frames/ROMImport.h's ImportBuiltObjects): the header
- the 8 bytes "NewtObjs", then as big-endian words the version (1), the
area's base address and size, the magic-pointer table's address and its
count - then the area, then the magic pointers as big-endian words, then
(version 2) the count of other blocks of ROM data and each one's address,
length and bytes (rounded to a word): the lexicons, the ROM's other data
(romdata.tsv) and the ROM extension;
then (version 3) the count of objects that are not where the ROM has them
and, for each, the ROM's ref and the ref now (build --relayout);
then (version 4) the builder's stamp: its length and its characters,
rounded to a word.  The stamp is a hash of this script and the modules it
builds with (builder_stamp); the build compiles the same into newton and
newtonscript (`romsrc.py stamp --header <file>`), which refuse an object
file with another - one written by newer tools than the program, as when
a build replaced the object file but could not replace a newton that was
running.
    python romsrc.py stamp [--header ObjectsStamp.h]   # the stamp, or a header holding it
    python romsrc.py roundtrip build/MP2x00US -o <dir> --newtonscript <exe>   # both, as the ctest runs them
    python romsrc.py edit-test <tree> -o <dir> --objects <file> --newtonscript <exe>
    python romsrc.py romdata build/MP2x00US <tree>   # only the ROM's other data (romdata/), as extract writes it

edit-test is the test of editability: it copies a tree, swaps a frame's
first two slots (@271) and puts an element at the front of an array (@113,
so every later element's path moves along), adds a slot
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

    objects/NNN.ns   the objects, as definitions `name := value;` (every .ns
                     under objects/, its subdirectories too: romsrc/ in its
                     natural form, naturalize.py, has a directory per kind of
                     root object - apps/, protos/, ... - and a file per root)
    functions/<addr>.ns   each top-level function as the decompiler writes it
                     (tools/newton-rom/analysis/nsdecompile.py: its constants,
                     then the function), compiled by the builder with the
                     host's compiler (newtonscript --compile-records) - as the
                     extractor writes the tree; in the natural form each
                     function is written in its frame instead (func, compile)
    maps.ns          the frame maps, `map_<addr> := map(class, supermap, 'tag, ...);`
    constants.tsv    the constants the notation may name (TreeNames; a package's
                     part tree uses the ROM tree's, and its magic.tsv's names)
    renames.tsv      (the natural form) each definition's name as the extractor
                     wrote it, and its name now; romsrc.py rename changes one
    resources/<class>/<addr>.bin   the binaries' bytes
    lexicons/<name>.words|.lex, lexicons.tsv   the recognisers' lexicons
                     (the Airus dictionaries InitROMDictionaryData points
                     gROMDictionaryData at: C data outside the object area) as
                     text - a word list, or a lexical graph of character sets -
                     which tools/lexicons/newtonlex.py packs into the ROM's
                     bytes (.bin, the size word then the dictionary, for one
                     that would not come back the same); the .tsv their ROM
                     addresses (tools/newton-rom/analysis/romdicts.py).  One an
                     edit has made bigger than its room is moved, with
                     --relayout (MOVED_DATA_BASE, the object file's moved table)
    romdata/<name>.txt, romdata.tsv   the ROM's other data that code outside
                     the object area reads at its ROM address: the parameter
                     block gParamBlock (0x1000, a page: gROMVersion, gROMStage,
                     gHardwareType, ...), whose version words a package's
                     native code reads (armcpu/PackageNativeCPU.cpp), as its
                     words named by the ROM's symbols (tools/rex/rextext.py's
                     `words`; a .bin, its bytes, is read too); the .tsv their
                     ROM addresses (ROMDATA below)
    rex/, rex.tsv    the ROM extension (the "high" file: its header, config
                     entries and the ten built-in packages), cut into pieces
                     in address order - a package with a frames part its
                     directory (<Package>.head.txt) and the part as a tree of
                     its own (<Package>/); a soup part (WorldData) a store as
                     text (`store`: tools/stores/newtonstore.py); any other
                     package whole as text (`package`: package.txt and its
                     protocol parts as ARM source, tools/armpart/newtonpkg.py,
                     built at the address it falls at) or a .pkg file as the
                     ROM holds it (its frames parts' refs ROM addresses: see
                     tools/newton-rom/analysis/packages.py), the header,
                     config entries, page, export and patch tables and the
                     padding text (tools/rex/rextext.py: what records where
                     the pieces lie - the header's entries and length, a
                     directory's sizes, a padding's length - worked out as
                     they are put together; a .bin piece, its bytes, is read
                     too, as the tree tagged romsrc-rom has them) - which the
                     builder puts back together;
                     a piece at address `-` was added (a package, `rom-form`,
                     put into the ROM's form where it falls - the Newton
                     Internet Enabler, romsrc/README.md: a directory, its
                     frames parts trees built as if the package lay at
                     PACKAGE_BASE (`romsrc.py package unpack|pack`), or a
                     package file, with <name>.patches.tsv applied first
                     when there is one), and build --original leaves such
                     pieces out
    magic.tsv        the magic-pointer table (gROMMagicPointerTable): each
                     entry's index and what it is - an object's path in
                     the layout, or a value
    layout.tsv       the manifest: every object's address, path and header flags,
                     and each frame's map; `alias` lines say which named
                     object a path inside a compiled function is (an object
                     the ROM shares, which the compiler makes afresh) - while
                     what the compiler makes is still the same as it

The notation of a value (a subset of NewtonScript's literals, plus a few
constructors of its own):

    12  -3          an integer
    vVisible + vClickable   vfPen(1)   a sum of integers and constants
                    (constants.tsv: a name, or a name(n) that shifts n)
    nil  true       themselves
    $a  $\\u00e9     a character
    @123            a magic pointer (ROM table 0, entry 123)
    @protoFloatNGo  the same by name (magic.tsv: the object an entry is, or
                    one of the other names after it)
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
    imasound('samples, "resources/samples/addr.wav")   the samples of an
                    IMA/DVI ADPCM sound (codecName "TIMACodec"), as 16-bit
                    PCM; compressed on build by the ROM's own codec
                    (newtonscript --ima-compress), which gives back the ROM's
                    bytes for every one the ROM has
    tonescore('samples, {version: 1, algorithm: 0, reserved: 0, repeats: 0},
              [tones: {frequency: 750, fraction: 0, peak: 20000, delay: 0,
                       attack: 10, decay: 10, hold: 100, release: 500,
                       sustain: 32768, tail: 0}, ...])
                    a score of the touch-tone synthesiser (TDTMFCodec,
                    sound/DTMFCodec.cpp: the ring tones and the dialler's
                    tones): its header's halfwords and each tone's - the
                    frequency in Hz and its 16-bit fraction, the peak level,
                    the envelope's times in milliseconds, the sustain level
    shorts('boundsRect, 0, 0, 31, 218)   a binary of 16-bit signed values (a
                    rectangle - top, left, bottom, right - a round
                    rectangle, a polygon's points)
    fixed('fixed, 22050.5)   a 16.16 fixed-point number
    hexfile('kchr, "resources/kchr/addr.txt")   a table as text: its bytes in
                    hex, 16 to a line, under # comments saying what each
                    part is (a 'kchr keyboard layout's modifier table and
                    key tables; a 'table of 256 entries)
    texttable('UniC, "resources/UniC/addr.txt")   a table of the Unicode
                    frame (an encoding's maps, the character classes and
                    their case tables), a sorting table ('Sort), a
                    locale's break table ('Intl), an ink code book
                    ('Comp), the letter trigrams ('Trigrams), the Letter
                    Shapes pictures ('letterimages) or the cursive
                    recogniser's tables ('DTEH, 'DTEM, 'PPDB, 'Trigram) as text whose `format`
                    line says which: tools/tables/newtontables.py packs it
                    (tools/tables/README.md)
    lexicon('AirusA, "resources/AirusA/addr.lex")   an Airus dictionary
                    (a locale's date, time, number or phone grammar) as the
                    text tools/lexicons/newtonlex.py writes for the ROM's
                    lexicons: a word list (.words) or a lexical graph (.lex);
                    packed on build (tools/lexicons/README.md)
    sound('samples, "resources/samples/addr.wav")   the samples of a
                    simple sound (8-bit, uncompressed: offset binary, as a
                    WAV file's 8-bit samples are); the sampling rate stays
                    in the sound frame
    pict('picture, "resources/picture/addr.pict")   a QuickDraw picture: a
                    PICT file (512 bytes of nought, then the picture), as a
                    Macintosh drawing program reads and writes it
    picture('picture, "resources/picture/addr")   a QuickDraw picture as
                    the directory tools/pict/newtonpict.py unpacks it into:
                    picture.txt, its opcodes as text (rects top left bottom
                    right; sizes, rowBytes and PackBits counts worked out),
                    and a PNG per bitmap (black the set pixels); packed on
                    build (tools/pict/README.md).  A picture the text form
                    would not give back byte for byte stays a pict(...)
    native('nativeModule, "resources/nativeModule/addr.s")   native ARM code
                    (an NTK native module's, a 'code binary) as the assembly
                    source tools/armpart/newtonarm.py writes: assembled on
                    build at the address its bytes fall at (.origin), its
                    .rword words - pointers into itself, which a package's
                    relocation chunk names - made from it
    sfnt('sfnt, "resources/sfnt/addr")   a font - an sfnt container of
                    Apple's bitmap and metric tables, with no outlines, so
                    not a TrueType font - as the directory
                    tools/fonts/newtonsfnt.py unpacks it into: a BDF file
                    per bitmap strike, a text file per other table
                    (tools/fonts/README.md, docs/qd/fonts-sfnt.md); packed
                    on build.  A font the text form would not give back
                    byte for byte stays binary('sfnt,
                    "resources/sfnt/addr.sfnt"), the font file itself
    function("functions/addr.ns")   a function, compiled from that source
    func(a1) begin ... end   a function written where it is: NewtonScript
                    source to the end of the expression (code_extent), its
                    @names made numbers, compiled with the host's compiler
    compile([constants: {name: source, ...},] [options: "names argframe=0,0",] source)
                    a function whose record has constants of its own (bound
                    while the source is compiled: a function it closes over,
                    a value made when the ROM was built), the compiler's
                    options (record_flags) or a source that is not a func
                    (evaluated: a closure the NTK made at build time)
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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "fonts"))
import newtonsfnt					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pict"))
import newtonpict					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lexicons"))
import newtonlex					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tables"))
import newtontables					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "rex"))
import rextext						# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "stores"))
import newtonstore					# noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "armpart"))
import newtonpkg					# noqa: E402
import newtonarm					# noqa: E402

# where a lexicon an edit has grown past its room goes (romsrc.py build
# --relayout): an address no ROM data uses, the object file's moved table
# taking the host from the ROM's address to it (ROMMovedAddress)
MOVED_DATA_BASE = 0x10000000

import wave							# noqa: E402

BITMAP_CLASSES = ("bits", "mask", "cbits")
# the binaries of native ARM code (an NTK native module's, NIE's): written as
# assembly source (tools/armpart/newtonarm.py), native(...)
NATIVE_CLASSES = ("nativeModule", "code")
# the binaries of 16-bit values (IsHalfwordShapeClass in frames/ObjectAreaImport.cpp)
HALFWORD_CLASSES = {"boundsrect", "rectangle", "oval", "roundrectangle", "line", "polygonshape", "polygondata", "regiondata"}
PICT_HEADER = 512					# a PICT file's header: nought, before the picture

PAD = 0xba						# the bytes between objects
PER_FILE = 400					# definitions in one file of objects/
GROUP_MIN = 8					# fewer definitions than this belonging to one root go into a misc file
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
RESERVED = {"nil", "true", "real", "string", "binary", "array", "map"}

# NewtonScript source inside the notation (a func written in its frame,
# compile(...)): its tokens, as far as finding where an expression ends and
# its magic pointers' names need them
CODE_TOKEN = re.compile(r"""
	(?P<space>\s+|//[^\n]*|/\*.*?\*/)
	|(?P<string>"(?:[^"\\]|\\.)*")
	|(?P<quoted>\|(?:[^|\\]|\\.)*\|)
	|(?P<char>\$\\u[0-9a-fA-F]{4}|\$\\.|\$.)
	|(?P<magic>@(?:\d+|[A-Za-z_][A-Za-z0-9_]*))
	|(?P<word>[A-Za-z_][A-Za-z0-9_]*)
	|(?P<number>\d+)
	|(?P<other>.)
	""", re.X | re.S)


def code_tokens(text, pos=0):
	"""NewtonScript source's tokens from pos: (kind, start, end)."""
	while pos < len(text):
		m = CODE_TOKEN.match(text, pos)
		yield m.lastgroup, m.start(), m.end()
		pos = m.end()


def code_extent(text, pos, where="code"):
	"""Where the NewtonScript expression starting at pos ends: before the
	first `,` `;` `)` `]` or `}` outside any bracket and begin...end."""
	depth = 0
	for kind, start, end in code_tokens(text, pos):
		if kind == "word":
			word = text[start:end]
			if word == "begin":
				depth += 1
			elif word == "end":
				depth -= 1
		elif kind == "other":
			ch = text[start]
			if ch in "([{":
				depth += 1
			elif ch in ")]}":
				if depth == 0:
					return start
				depth -= 1
			elif ch in ",;" and depth == 0:
				return start
	raise SyntaxError("%s: NewtonScript source with no end" % where)


def code_with_magic_numbers(text, magic_names, where="code"):
	"""NewtonScript source with each @name made @n, as the compiler reads it."""
	out = []
	last = 0
	for kind, start, end in code_tokens(text):
		if kind == "magic" and not text[start + 1].isdigit():
			index = magic_names.get(text[start + 1:end].lower())
			if index is None:
				raise SyntaxError("%s: %s is no magic pointer's name (magic.tsv)" % (where, text[start:end]))
			out.append(text[last:start] + "@%d" % index)
			last = end
	return "".join(out) + text[last:]


ARITHMETIC = {"+", "-", "*", "/", "div", "mod"}


def code_with_constants(text, constants, where="code"):
	"""NewtonScript source with the tree's constants (constants.tsv) made
	numbers, as the compiler reads it: a name, or name(n) for one that
	shifts.  A sum of constants that is a whole operand - nothing that binds
	as tightly before it, nothing that binds more tightly after it - is one
	number, so that `viewFlags: vVisible + vClickable` compiles to what
	`viewFlags: 513` does.  A name after `.`, `'` or a send's `:`, or before
	a tag's `:`, is not a constant."""
	toks = [(kind, start, end) for kind, start, end in code_tokens(text) if kind != "space"]
	words = [text[s:e] for _, s, e in toks]

	def term(i):
		"""The constant term at token i: (its value, the token after it), or None."""
		if toks[i][0] != "word" or words[i] not in constants:
			return None
		if i > 0 and (words[i - 1] in (".", "'") or (words[i - 1] == ":" and toks[i - 1][2] == toks[i][1])):
			return None
		if i + 1 < len(toks) and words[i + 1] == ":" and (i + 2 >= len(toks) or words[i + 2] != "="):
			return None
		value = constants[words[i]]
		if not callable(value):
			return value, i + 1
		# name(n): a run of digits between the brackets
		k = i + 2
		digits = ""
		while k < len(toks) and words[k].isdigit():
			digits += words[k]
			k += 1
		if i + 1 >= len(toks) or words[i + 1] != "(" or not digits or k >= len(toks) or words[k] != ")":
			raise SyntaxError("%s: %s takes a number, %s(n)" % (where, words[i], words[i]))
		return value(int(digits)), k + 1
	out, last, i = [], 0, 0
	while i < len(toks):
		found = term(i)
		if found is None:
			i += 1
			continue
		total, after = found
		run_end = after
		terms = 1
		while run_end + 1 < len(toks) and words[run_end] == "+":
			nxt = term(run_end + 1)
			if nxt is None:
				break
			total += nxt[0]
			run_end = nxt[1]
			terms += 1
		before = words[i - 1] if i > 0 else None
		following = words[run_end] if run_end < len(toks) else None
		if terms > 1 and (before in ARITHMETIC or following in ("*", "/", "div", "mod")):
			# (not a whole operand: each name its number)
			total, run_end = term(i)
		number = "%d" % total
		if total < 0 and text[toks[i][1] - 1:toks[i][1]] in ("-", "+"):
			number = " " + number
		out.append(text[last:toks[i][1]] + number)
		last = toks[run_end - 1][2]
		i = run_end
	return "".join(out) + text[last:]


def same_object(rom, v, ref, where):
	"""Where a compiled value (the notation read back) differs from the ROM's
	object at ref; None when it is the same."""
	if isinstance(v, Imm):
		return None if v.ref == ref else "%s: %#x, the ROM's %#x" % (where, v.ref, ref)
	if isinstance(v, Sym) and OBJECT_REF.match(v.name):
		target = int(OBJECT_REF.match(v.name).group(1), 16)
		return None if target == ref else "%s: names %#x, the ROM's %#x" % (where, target, ref)
	if isinstance(v, Sym):
		name = rom.symname(ref) if rom.is_ptr(ref) else None
		return None if name is not None and name.lower() == v.name.lower() else "%s: '%s" % (where, v.name)
	if not isinstance(v, Obj) or not rom.is_ptr(ref):
		return "%s: a different kind of value" % where
	if closure_target(v) is not None:
		return None if closure_target(v) == ref else "%s: a closure of %#x, the ROM's %#x" % (where, closure_target(v), ref)
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
			if not sorted_by_compiler(v.tags, [t for t, _ in slots]):
				return "%s: the tags differ" % where
			# (the same slots in the order the compiler sorted them in: the
			# builder writes them in the layout map's)
			order = {t.lower(): i for i, t in enumerate(v.tags)}
			items = [v.items[order[(t or "").lower()]] for t, _ in slots]
			return next((why for why in (same_object(rom, item, value, where + "." + tag)
										 for item, (tag, value) in zip(items, slots)) if why), None)
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


SORTED_MAP_TAGS = 0x14			# a map with more tags is sorted as it grows (Objects.cpp's AddSlot, ROM 0x0031a3b4)


def sorted_by_compiler(compiled, original):
	"""Whether a compiled frame's tags are a frame's of another compiler in
	the order the host's sorted them in: a frame literal of more than
	SORTED_MAP_TAGS slots is built slot by slot, and its map sorted once it
	passes that many, as the ROM's is; the NTK built its frames with the
	slots where the source had them (a map of flags 0)."""
	return len(compiled) > SORTED_MAP_TAGS and len(compiled) == len(original) \
		and sorted(t.lower() for t in compiled) == sorted((t or "").lower() for t in original)


# a path to a compiled function's literal: the literals array's path, the index
LITERAL_PATH = re.compile(r"(.*\.literals)\[(\d+)\]\Z")
CLOSURE_STUB = re.compile(r"romsrc:(?:closure|native):([0-9a-f]+)\Z")
# a function's source naming an object it cannot hold the bytes of (native
# code, nsdecompile.py's NATIVE_CODE_CLASSES): the symbol '|romsrc:object:<ref>|
OBJECT_REF = re.compile(r"romsrc:object:([0-9a-f]+)\Z")


def closure_target(v):
	"""The ref a compiled closure stub stands for (nsdecompile.py's
	NativeClosure: `func() :|romsrc:closure:<ref>|()`, an NTK native function
	the function closes over), or None when v is no such stub."""
	if not (isinstance(v, Obj) and v.kind == "frame" and "literals" in v.tags):
		return None
	lits = v.items[v.tags.index("literals")]
	if isinstance(lits, Obj):
		for x in lits.items:
			m = CLOSURE_STUB.match(x.name) if isinstance(x, Sym) else None
			if m:
				return int(m.group(1), 16)
	return None


def mark_compiled(v, seen=None):
	"""Every frame a compiled function holds, marked as the compiler's (Obj.compiled)."""
	seen = set() if seen is None else seen
	if isinstance(v, Obj) and id(v) not in seen:
		seen.add(id(v))
		v.compiled = True
		for x in v.items:
			mark_compiled(x, seen)
		mark_compiled(v.cls, seen)


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
	for name in [os.path.relpath(p, folder) for p in tree_files(tree)[1:]]:
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
		self.functions |= self.shared_nested_functions()
		self.simple_sounds = self.find_simple_sounds()
		self.ima_sounds = self.find_ima_sounds()
		self.undecompiled = []			# (function, why): the ones kept as bytecode
		self.odd_gaps = {}				# object -> the bytes after it, when they are not the pad byte
		self.relocated = set()			# the addresses of words a package's relocation chunk names
		self.link_base = 0				# what a native binary's .origin counts from (a package file's part: PACKAGE_BASE)
		self.newtonscript = newtonscript
		self.sources = self.function_sources()
		self.in_function = 0			# inside a function written as source
		self.aliases = []				# (path, name): a named object inside a compiled function
		self.named_refs = []			# (path, name, the definition being written): every named reference
		self.current_def = None

	def shared_nested_functions(self):
		"""The functions inside others that are written as definitions of their
		own: those held by a literals array that more than one object holds
		(the NTK shared one between functions), and those more than one
		object holds - each written as its own source, as every other function
		is, rather than as its bytecode."""
		rom = self.rom
		found = set()
		for ref in self.objs:
			if not (rom.flags(ref) & 1 and rom.size(ref) >= 32):
				continue
			s = rom.slots(ref)
			if s[0] != 0x32 or not rom.is_ptr(s[1]) or not rom.is_ptr(s[2]) or s[2] not in self.inside:
				continue
			shared = self.refcount[s[2]] != 1
			for lit in rom.slots(s[2]):
				if (shared or self.refcount[lit] != 1) and rom.is_ptr(lit) and lit in self.inside 						and rom.flags(lit) & 1 and rom.size(lit) >= 32:
					ls = rom.slots(lit)
					if ls[0] == 0x32 and rom.is_ptr(ls[1]):
						found.add(lit)
		return found

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
		# (the native functions a function closes over, which its source has
		# as stubs: each a definition of its own, which the builder puts in
		# the stub's place - counted once more, so never taken from the
		# compiled function, which has only the stub)
		for o, text in sources.items():
			for ref in re.findall(r"romsrc:(?:closure|native|object):([0-9a-f]+)\|", text):
				self.refcount[int(ref, 16)] += 1
		if self.newtonscript is None or not sources:
			return sources
		failed = self.verify_sources(sources)
		# a function that differs only in the argFrame the NTK's compiler gave
		# it (where the host's compiler makes none, or with 0 where it leaves
		# nil), or in symbols at the end of its literals that nothing uses:
		# its record says so (ntk_arg_frame_flag, ntk_extra_literals_flag)
		# and it is tried again - and again, while each try finds the next
		# such difference
		for _ in range(3):
			again = {}
			for o, why in failed.items():
				head, body = sources[o].split("\n", 1)
				flag = ntk_arg_frame_flag(self.rom, o) if why.startswith(".argFrame") and " argframe=" not in head else ""
				flag = flag or (ntk_extra_literals_flag(self.rom, o, why) if " literals+=" not in head else "")
				if flag:
					again[o] = head + flag + "\n" + body
			if not again:
				break
			still = self.verify_sources(again)
			for o, text in again.items():
				sources[o] = text
				if o in still:
					failed[o] = still[o]
				else:
					del failed[o]
		for o, why in failed.items():
			self.undecompiled.append((o, why))
			del sources[o]
		return sources

	def verify_sources(self, sources):
		"""The functions whose source does not compile back the same, with why
		(one run of the host's compiler)."""
		failed = {}
		os.makedirs(self.out, exist_ok=True)
		records = os.path.join(self.out, ".verify-records.txt")
		compiled = os.path.join(self.out, ".verify-compiled.txt")
		order = sorted(sources)
		with open(records, "w", encoding="utf-8", newline="\n") as f:
			for n, o in enumerate(order):
				head, body = sources[o].split("\n", 1)
				f.write("@@ %x%s\n%s" % (n, record_flags(head), body))
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
				value = Reader(lines[i + 1], "compiled", self.out).value()
				with_record_flags(value, sources[o].split("\n", 1)[0])
				why = same_object(self.rom, value, o, "")
			if why:
				failed[o] = why
		for o in order:
			if o not in done:
				failed[o] = "not compiled"
		os.remove(records)
		os.remove(compiled)
		return failed

	def find_ima_sounds(self):
		"""The samples of the IMA/DVI ADPCM sounds (codecName "TIMACodec"),
		with their frame's sampling rate."""
		rom = self.rom
		found = {}
		for o in self.objs:
			if rom.flags(o) & 3 != 3:
				continue
			slots = dict(rom.frame_slots(o))
			codec = slots.get("codecName")
			if codec is None or not rom.is_ptr(codec) or rom.flags(codec) & 1 or "samples" not in slots:
				continue
			if rom.data(codec)[:-2].decode("utf-16-be", "replace") != "TIMACodec":
				continue
			rate = slots.get("samplingRate")
			hz = 22026
			if rom.is_ptr(rate) and rom.symname(rom.cls(rate)) == "fixed":
				hz = struct.unpack(">i", rom.data(rate))[0] / 65536
			elif rate is not None and rate & 3 == 0:
				hz = rate >> 2
			found[slots["samples"]] = max(1, int(round(hz)))
		return found

	def ima_pcm(self, data):
		"""A compressed sound's samples as 16-bit PCM (big-endian) by the ROM's
		codec, when compressing them again gives back these very bytes."""
		if self.newtonscript is None:
			return None
		import tempfile
		work = tempfile.mkdtemp(prefix="romsrc-ima")
		try:
			paths = [os.path.join(work, n) for n in ("in.ima", "out.pcm", "again.ima")]
			with open(paths[0], "wb") as f:
				f.write(data)
			exe = nd.newtonscript_path(self.newtonscript)
			if subprocess.run([exe, "--ima-expand", paths[0], paths[1]]).returncode != 0:
				return None
			if subprocess.run([exe, "--ima-compress", paths[1], paths[2]]).returncode != 0:
				return None
			with open(paths[2], "rb") as f:
				if f.read() != data:
					return None
			with open(paths[1], "rb") as f:
				return f.read()
		finally:
			import shutil
			shutil.rmtree(work, ignore_errors=True)

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

	def write_romdata(self):
		"""The ROM's other data that code outside the object area reads at its
		ROM address (ROMDATA), each as a text file of its words."""
		write_romdata(self.rom, self.out, self.build_dir)

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
		stores = set()			# a soup part's start (a store: tools/stores/newtonstore.py)
		whole = set()			# a package written whole as text (tools/armpart/newtonpkg.py)
		self.whole_packages = set()
		aligns = {}
		for p in pkgs:
			cuts |= {p["base"], p["base"] + p["size"]}
			name = re.sub(r"[^A-Za-z0-9_]", "_", p["name"])
			names[p["base"]] = name + ".pkg"
			for part in p["parts"]:
				if part["flags"] & 3 == 2 and part["type"] == 0x736f7570 \
						and part["offset"] + part["size"] == p["size"] - p["directory_size"]:
					# a soup part, the package's last: a store as text
					# (tools/stores/newtonstore.py) when it comes back the same
					first = p["base"] + p["directory_size"] + part["offset"]
					data = self.rom.rom[first:first + part["size"]]
					where = os.path.join(self.out, "rex", name)
					try:
						newtonstore.unpack(data, where)
						same = newtonstore.pack(where, warn=None) == data
					except (newtonstore.Unsupported, RuntimeError, ValueError):
						same = False
					if same:
						cuts.add(first)
						names[p["base"]] = name + ".head"		# (.txt, or .bin: rex_texts)
						names[first] = name + "/"
						stores.add(first)
					else:
						import shutil
						shutil.rmtree(where, ignore_errors=True)
					continue
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
				names[p["base"]] = name + ".head"		# (.txt, or .bin: rex_texts)
				names[last] = name + ".tail.bin"
				names[first] = name + "/"
				parts[first] = (last, objs, pad, odd)
		cuts = sorted(c for c in cuts if start <= c <= end)
		os.makedirs(os.path.join(self.out, "rex"), exist_ok=True)
		rom = self.rom.rom
		# a package neither split into a frames tree nor a store (ScreenBuffer,
		# ScreenDrivers: protocol parts) as text, at its address, when it comes back the same
		rom_names = None
		for p in pkgs:
			if not names.get(p["base"], "").endswith(".pkg"):
				continue
			name = names[p["base"]][:-len(".pkg")]
			where = os.path.join(self.out, "rex", name)
			try:
				if rom_names is None:
					rom_names = newtonarm.RomNames(self.build_dir)
				newtonpkg.unpack(rom[p["base"]:p["base"] + p["size"]], where, rom_names, p["base"])
				whole.add(p["base"])
				self.whole_packages.add("rex/" + name)
				names[p["base"]] = name + "/"
			except (newtonpkg.PackageError, newtonarm.AsmError, ValueError):
				import shutil
				shutil.rmtree(where, ignore_errors=True)
		tags = {e["address"]: e["tag"].encode("latin-1") for e in rex["entries"]}
		# each piece's file: a frames part a tree of its own, a piece that has
		# a text form (tools/rex/rextext.py) text, any other its bytes
		pieces = []					# (address, end, rel, its text or None)
		for a, b in zip(cuts, cuts[1:]):
			name = names.get(a, "bytes_%x" % a)
			if a in stores or a in whole:
				rel = "rex/" + name		# (a store or a package, written above)
			elif name.endswith("/"):
				# a package's frames part: its objects as a tree of their own
				last, objs, pad, odd = parts[a]
				rel = "rex/" + name
				sub = Extractor(self.rom, os.path.join(self.out, rel), self.build_dir, objs, (a, last - a), pad,
								self.newtonscript, aligns[a])
				sub.odd_gaps = odd
				sub.run()
				print("  %s: %d objects, %d functions as source, %d as bytecode"
					  % (rel, len(objs), len(sub.sources), len(sub.undecompiled)))
			else:
				rel = "rex/" + (name if "." in name else name + ".bin")
			pieces.append((a, b, rel, None))
		pieces = self.rex_texts(pieces, start, end, tags)
		with open(os.path.join(self.out, "rex.tsv"), "w", encoding="utf-8", newline="\n") as f:
			f.write("# the ROM extension: its pieces in address order (address, file, how): a\n"
					"# directory a package's frames part as a tree, or with `store` its soup part\n"
					"# (tools/stores/newtonstore.py), or with `package` the whole package as text\n"
					"# (tools/armpart/newtonpkg.py)\n")
			f.write("rex\t%x\t%x\n" % (start, end - start))
			for a, b, rel, text in pieces:
				if text is not None:
					with open(os.path.join(self.out, rel), "w", encoding="utf-8", newline="\n") as out:
						out.write(text)
				elif not rel.endswith("/"):
					with open(os.path.join(self.out, rel), "wb") as out:
						out.write(rom[a:b])
				f.write("%x\t%s%s\n" % (a, rel, "\tstore" if a in stores else "\tpackage" if a in whole else ""))

	def rex_texts(self, pieces, start, end, tags):
		"""The pieces of the extension that have a text form
		(tools/rex/rextext.py) given it: the header, the config entries
		that are tables of words, the page tables, the export and patch
		tables, the padding and the package directories.  A piece whose
		text would not give its bytes back keeps them (a .bin).
		==> the pieces as (address, end, rel, text or None)."""
		rom = self.rom.rom
		out = list(pieces)

		def as_text(i, text):
			a, b, rel, _ = out[i]
			stem = rel[:-len(".bin")] if rel.endswith(".bin") else rel
			out[i] = (a, b, stem + ".txt", text) if text is not None else (a, b, stem + ".bin", None)

		def layout():
			return rextext.Layout(start, end - start, [(rel, a - start, b - a) for a, b, rel, _ in out])

		def where(address):
			"""What lies at an address of the extension: its piece and the offset."""
			for a, b, rel, _ in out:
				if a <= address < b:
					return "%s +0x%x" % (rel[len("rex/"):], address - a)
			return None

		def part_object(ref):
			"""The object an exported ref names: its package and its path."""
			for a, b, rel, _ in out:
				if rel.endswith("/") and a <= ref < b:
					found = self.rex_layout_paths(rel).get(ref - 1)
					return "%s %s" % (rel[len("rex/"):-1], found) if found else None
			return None
		rex_id = struct.unpack_from(">I", rom, start + 0x1c)[0]
		for i, (a, b, rel, _) in enumerate(out):
			t = tags.get(a)
			if rel.endswith("/") or a == start or rel.endswith(".head"):
				continue
			data = rom[a:b]
			if t in rextext.CONFIG_COLUMNS:
				as_text(i, rextext.config_table_text(t, data))
			elif t in (b"ptpt", b"glpt"):
				as_text(i, rextext.page_table_text(t, data))
			elif t == b"fexp":
				as_text(i, rextext.refs_text(data, part_object, [
					"# the ROM extension's frame export table 'fexp (written by romsrc.py extract, built",
					"# by tools/rex/rextext.py): the refs of the objects its packages export, which",
					"# magic-pointer table 2 indexes (packages/FramePartHandler.cpp) - each with its index",
					"# and the package and path of its object; build --relayout moves each with its",
					"# object and appends the entries of the packages it adds"]))
			elif t == b"jump":
				# (an entry reaching into a package kept whole as source - ScreenBuffer,
				# ScreenDrivers - names it, so that it follows the package when it moves)
				def piece_of(address):
					for pa, pb, prel, _ in out:
						if prel.endswith("/") and pa <= address < pb and prel[:-1] in self.whole_packages:
							return prel, address - pa
					return None
				as_text(i, rextext.patch_table_text(data, 0x01a00000 + 0x400000 + (rex_id << 20), where,
													piece_of, layout()))
			elif t == b"pad ":
				# (the alignment the piece after it starts on, to a page at most -
				# what rex_relaid_out keeps)
				as_text(i, rextext.pad_text(data, a, min(b & -b, 0x1000)))
		# the package directories (their parts are the pieces after them), then
		# the header, which names the pieces
		for i, (a, b, rel, _) in enumerate(out):
			if rel.endswith(".head"):
				as_text(i, rextext.package_directory_text(rom[a:b], layout(), rel, os.path.basename(rel)[:-len(".head")]))
		if out and out[0][0] == start:
			out[0] = out[0][:2] + ("rex/header", None)
			as_text(0, rextext.header_text(rom[start:out[0][1]], layout(), "rex/header"))
		return out

	def rex_layout_paths(self, rel):
		"""A package part's tree's layout.tsv: each object's address -> its path."""
		cache = self.__dict__.setdefault("_rex_paths", {})
		if rel not in cache:
			paths = {}
			with open(os.path.join(self.out, rel, "layout.tsv"), encoding="utf-8") as f:
				for line in f:
					fields = line.rstrip("\n").split("\t")
					if len(fields) > 1 and re.match(r"[0-9a-f]+\Z", fields[0]):
						paths.setdefault(int(fields[0], 16), fields[1])
			cache[rel] = paths
		return cache[rel]

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
		if cname is not None and cname.lower() in HALFWORD_CLASSES and len(data) % 2 == 0 and data and not self.in_function:
			values = struct.unpack(">%dh" % (len(data) // 2), data)
			return "shorts(%s, %s)" % (self.value(cls, path + "^"), ", ".join(str(v) for v in values))
		if cname == "fixed" and len(data) == 4 and not self.in_function:
			v = struct.unpack(">i", data)[0] / 65536
			if struct.pack(">i", int(round(float(repr(v)) * 65536))) == data:
				return "fixed(%s, %r)" % (self.value(cls, path + "^"), v)
		if cname in HEX_TABLES and not self.in_function:
			text = HEX_TABLES[cname](data)
			if text is not None and hex_file_bytes(text) == data:
				rel = "resources/%s/%x.txt" % (folder, o)
				os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
				with open(os.path.join(self.out, rel), "w", encoding="utf-8", newline="\n") as f:
					f.write(text)
				return "hexfile(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
		if cname in ("samples", "TDTMFCodec") and not self.in_function:
			text = tone_score_text(data)
			if text is not None:
				return "tonescore(%s, %s)" % (self.value(cls, path + "^"), text)
		if o in self.ima_sounds and not self.in_function:
			pcm = self.ima_pcm(data)
			if pcm is not None:
				rel = "resources/%s/%x.wav" % (folder, o)
				os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
				with wave.open(os.path.join(self.out, rel), "wb") as w:
					w.setnchannels(1)
					w.setsampwidth(2)
					w.setframerate(self.ima_sounds[o])
					w.writeframes(struct.pack("<%dh" % (len(pcm) // 2), *struct.unpack(">%dh" % (len(pcm) // 2), pcm)))
				return "imasound(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
		if cname == "picture" and not self.in_function:
			rel = "resources/%s/%x" % (folder, o)
			where = os.path.join(self.out, rel)
			try:
				newtonpict.unpack(data, where)
				if newtonpict.pack(where) == data:
					return "picture(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
			except newtonpict.PictError:
				pass
			import shutil
			shutil.rmtree(where, ignore_errors=True)
			rel = "resources/%s/%x.pict" % (folder, o)
			os.makedirs(os.path.join(self.out, os.path.dirname(rel)), exist_ok=True)
			with open(os.path.join(self.out, rel), "wb") as f:
				f.write(bytes(PICT_HEADER) + data)
			return "pict(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
		table_format = text_table_format(cname, path, data) if not self.in_function else None
		if table_format is not None:
			rel = "resources/%s/%x.txt" % (folder, o)
			where = os.path.join(self.out, rel)
			os.makedirs(os.path.dirname(where), exist_ok=True)
			try:
				newtontables.unpack(data, table_format, where)
				if newtontables.pack(where) == data:
					return "texttable(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
			except ValueError:
				pass
			os.remove(where)
		if cname in NATIVE_CLASSES and not self.in_function:
			rel = "resources/%s/%x.s" % (folder, o)
			where = os.path.join(self.out, rel)
			os.makedirs(os.path.dirname(where), exist_ok=True)
			at = o - 1 + 12
			try:
				newtonarm.unpack(data, where, sorted(r - at for r in self.relocated if at <= r < at + len(data)),
								 at - self.link_base, None, "a %s binary (%s)" % (cname, path), origin_line=False)
				return "native(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
			except (ValueError, newtonarm.AsmError):
				if os.path.exists(where):
					os.remove(where)
		if cname == "AirusA" and not self.in_function and len(data) > 1 and (data[1] & 7) in (1, 2, 3, 5, 7):
			sized = struct.pack(">I", len(data)) + data
			rel = "resources/%s/%x%s" % (folder, o, newtonlex.text_suffix(sized))
			where = os.path.join(self.out, rel)
			os.makedirs(os.path.dirname(where), exist_ok=True)
			try:
				newtonlex.unpack(sized, where)
				if newtonlex.pack(where) == sized:
					return "lexicon(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
			except (ValueError, IndexError, KeyError):
				pass
			os.remove(where)
		if cname == "instructions" and not self.in_function and len(data) <= 64:
			return "bytes(%s, \"%s\")" % (self.value(cls, path + "^"), data.hex())
		if cname == "sfnt" and not self.in_function:
			rel = "resources/%s/%x" % (folder, o)
			where = os.path.join(self.out, rel)
			newtonsfnt.unpack(data, where)
			if newtonsfnt.pack(where) == data:
				return "sfnt(%s, \"%s\")" % (self.value(cls, path + "^"), rel)
			import shutil
			shutil.rmtree(where)
		rel = "resources/%s/%x.%s" % (folder, o, "sfnt" if cname == "sfnt" else "bin")
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
				# (a font is a directory of files: used when its directory is)
				if rel not in used and os.path.dirname(rel) not in used:
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
			f.write("# the recognisers' lexicons: ROM address, name, file - a word list (.words) or\n")
			f.write("# a lexical graph (.lex), tools/lexicons/newtonlex.py; .bin is the bytes (its\n")
			f.write("# size word, then the Airus dictionary), for one that does not come back the same\n")
			for address in sorted({a for a in writes.values() if a}):
				name = re.sub(r"[^A-Za-z0-9_]", "_", rom.symbols.get(address, "lexicon_%x" % address))
				size = rom.word(address)
				data = rom.rom[address:address + 4 + size]
				rel = "lexicons/%s%s" % (name, newtonlex.text_suffix(data))
				where = os.path.join(self.out, rel)
				try:
					newtonlex.unpack(data, where)
					same = newtonlex.pack(where) == data
				except ValueError:
					same = False
				if not same:
					if os.path.exists(where):
						os.remove(where)
					rel = "lexicons/%s.bin" % name
					with open(os.path.join(self.out, rel), "wb") as out:
						out.write(data)
				f.write("%x\t%s\t%s\n" % (address, name, rel))
		self.write_romdata()
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


# ---- the ROM's other data

# What code outside the object area reads of the ROM at a ROM address, by
# symbol, with its length: the parameter block (a page), whose gROMVersion
# (0x13dc) and gROMStage NTK's runtime stubs in a package's native code
# read to choose their entry points (docs/armcpu/README.md).
ROMDATA = [("gParamBlock", 0x1000)]


def romdata_text(rom, build_dir, name, address, length):
	"""A ROMDATA block as text (tools/rex/rextext.py's `words`): each word
	named by the ROM's symbol at its address (not an `abs` one, nor the
	block's own), the stretches that the MMU map the machine starts from
	uses as coarse page tables said so (tools/newton-rom/analysis/mmumap.py).
	None when the text would not give the bytes back."""
	import mmumap
	with open(os.path.join(build_dir, "symbols.json"), encoding="utf-8") as f:
		symbols = json.load(f)["symbols"]
	names = {}
	for s in symbols:
		if address <= s["address"] < address + length and s["class"] != "abs" and s["name"] != name \
				and "jt_index" not in s:
			names.setdefault(s["address"], s["name"])
	notes = {}
	for table in mmumap.TABLES:
		if table not in rom.by_name:
			continue
		for virtual, physical, size, flags in mmumap.read_table(rom.rom, rom.by_name[table]):
			if flags & 3 == 1 and address <= physical < address + length:
				notes.setdefault(physical, []).append(
					"the coarse page table %s (ROM %#x) maps 0x%08x with: 256 small-page descriptors"
					% (table, rom.by_name[table], virtual))
	for lines in notes.values():
		lines.append("(docs/memory/mmu-map.md); the words not named are its entries")
	title = ["# %s, the ROM's data at 0x%x that code reads at its ROM address (written by" % (name, address),
			 "# romsrc.py extract, built by tools/rex/rextext.py): each word at its address,",
			 "# named by the ROM's symbol for it (`word` when it has none); `zeros N` is N bytes of",
			 "# nought.  A line's address must be where the line before it ends"]
	return rextext.words_text(rom.rom[address:address + length], address, names, notes, title)


def write_romdata(rom, out, build_dir):
	"""romdata/<name>.txt (or .bin, when the text would not give the bytes
	back) and romdata.tsv: each ROMDATA block's words and its ROM address.
	Run by extract; the tree's copy was written by it once."""
	os.makedirs(os.path.join(out, "romdata"), exist_ok=True)
	with open(os.path.join(out, "romdata.tsv"), "w", encoding="utf-8", newline="\n") as f:
		f.write("# the ROM's other data read at its ROM address: ROM address, name, file\n")
		for name, length in ROMDATA:
			address = rom.by_name[name]
			text = romdata_text(rom, build_dir, name, address, length)
			if text is not None:
				rel = "romdata/%s.txt" % name
				with open(os.path.join(out, rel), "w", encoding="utf-8", newline="\n") as blob:
					blob.write(text)
			else:
				rel = "romdata/%s.bin" % name
				with open(os.path.join(out, rel), "wb") as blob:
					blob.write(rom.rom[address:address + length])
			f.write("%x\t%s\t%s\n" % (address, name, rel))


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


NEWTONSCRIPT = None					# (set by the builder: the host tool the IMA sounds are compressed with)

def hex_rows(data, width=16):
	return "".join(" ".join("%02x" % b for b in data[i:i + width]) + "\n" for i in range(0, len(data), width))


def hex_file_bytes(text):
	"""A hexfile's bytes: every line not a # comment, as hex byte pairs."""
	out = bytearray()
	for line in text.split("\n"):
		line = line.split("#", 1)[0].strip()
		if line:
			out += bytes.fromhex(line)
	return bytes(out)


def kchr_text(data):
	"""A Macintosh 'KCHR keyboard layout (Inside Macintosh: Text) as text:
	its version, the modifier table (the key table each combination of the
	modifier bits uses), the key tables (the Mac OS Roman character each of
	128 key codes gives), then the dead keys."""
	if len(data) < 262:
		return None
	count = struct.unpack(">H", data[258:260])[0]
	end = 260 + 128 * count
	if end + 2 > len(data):
		return None
	text = "# a 'kchr keyboard layout (Macintosh KCHR): views/Keyboard.h's TranslateKey reads it\n"
	text += "# version\n" + hex_rows(data[0:2])
	text += "# the modifier table: for each of the 256 combinations of the modifier bits\n"
	text += "# (views/Keyboard.h's kCommandModifier..), the key table it uses\n" + hex_rows(data[2:258])
	text += "# the number of key tables\n" + hex_rows(data[258:260])
	for i in range(count):
		table = data[260 + 128 * i:260 + 128 * (i + 1)]
		text += "# key table %d: the character (Mac OS Roman) of each key code 00-7f; here as text: %s\n" % (
			i, "".join(chr(b) if 0x20 <= b < 0x7f else "." for b in table))
		text += hex_rows(table)
	text += "# the dead keys: their count, then each one's record\n" + hex_rows(data[end:])
	return text


def table_text(data):
	"""A 256-entry 'table as text: 16 entries to a line, row by row."""
	if len(data) != 256:
		return None
	return "# a 256-entry table: entry 0x00 first, 16 to a line\n" + hex_rows(data)


HEX_TABLES = {"kchr": kchr_text, "table": table_text}


def text_table_format(cname, path, data):
	"""The text form (tools/tables/newtontables.py) a binary of the Unicode
	frame, a sorting table, a locale's break table, an ink code book, the
	trigrams or the letter pictures takes, by its class and the slot it is
	in; None for any other binary."""
	slot = path.rsplit(".", 1)[-1].lower() if path else ""
	if cname == "UniC":
		if slot == "maptounicode":
			return "to-unicode"
		if slot == "mapfromunicode":
			return "from-unicode"
		if slot == "charclass":
			return "char-classes"
		if slot == "typelist":
			return "class-types"
		if slot in ("upperlist", "lowerlist", "uppernomarklist", "nomarklist"):
			return "class-deltas"
		return None
	if cname == "Sort":
		return "sort-table"
	if cname == "Intl" and slot in ("wordbreaktable", "linebreaktable"):
		return "break-table"
	if cname == "Comp":
		return "code-book"				# (the ink codec's code books: tools/tables/inktables.py)
	if cname == "Trigrams":
		return "trigrams"
	if cname == "letterimages":
		return "letter-images"
	if cname == "DTEH":
		return "dte-header"				# (the cursive recogniser's tables: tools/tables/hwrtables.py)
	if cname == "DTEM":
		return "dte-main"
	if cname == "PPDB":
		return "ppd-main"
	if cname == "Trigram":
		return "dte-trigrams"
	return None

SCORE_HEADER = ("version", "algorithm", "reserved", "repeats")
SCORE_TONE = ("frequency", "fraction", "peak", "delay", "attack", "decay", "hold", "release", "sustain", "tail")


def tone_score_text(data):
	"""A touch-tone score (TDTMFCodec::Produce 0x00088388: the version 1,
	the algorithm, a reserved halfword, the repeat count and the number of
	tones, then 0x14 bytes a tone: its frequency as 16.16 fixed, peak level,
	delay, attack, decay, hold and release in milliseconds, sustain level,
	and the tail after the release) as the notation's tonescore, or None
	when the bytes are not one (or would not come back the same)."""
	if len(data) < 10 or len(data) % 2:
		return None
	halves = struct.unpack(">%dH" % (len(data) // 2), data)
	if halves[0] != 1 or len(data) != 10 + 0x14 * halves[4]:
		return None
	header = "{" + ", ".join("%s: %d" % (n, v) for n, v in zip(SCORE_HEADER, halves[:4])) + "}"
	tones = []
	for k in range(halves[4]):
		t = halves[5 + 10 * k:15 + 10 * k]
		tones.append("{" + ", ".join("%s: %d" % (n, v) for n, v in zip(SCORE_TONE, t)) + "}")
	text = "%s, [tones: %s]" % (header, ", ".join(tones))
	return text if tone_score_bytes_of(halves) == data else None


def tone_score_bytes_of(halves):
	return struct.pack(">%dH" % len(halves), *halves)


def tone_score_bytes(header, tones):
	"""A tonescore's bytes: its header (a frame of SCORE_HEADER's slots) and
	tones (frames of SCORE_TONE's), the tone count worked out."""
	def value(frame, name):
		v = frame.items[[t.lower() for t in frame.tags].index(name.lower())]
		return v.ref >> 2
	halves = [value(header, n) for n in SCORE_HEADER] + [len(tones.items)]
	for tone in tones.items:
		halves += [value(tone, n) for n in SCORE_TONE]
	return tone_score_bytes_of(halves)


def ima_compress(path):
	"""A WAV file as an IMA/DVI ADPCM sound's samples: its samples as 16-bit
	mono (an 8-bit or stereo edit brought to that), padded with silence to
	whole blocks of 0x40, compressed by the ROM's own codec (newtonscript
	--ima-compress, sound/IMACodec.h's CompressIMA)."""
	with wave.open(path, "rb") as w:
		channels, width, count = w.getnchannels(), w.getsampwidth(), w.getnframes()
		frames = w.readframes(count)
	samples = []
	step = channels * width
	for i in range(0, len(frames) - step + 1, step):
		total = 0
		for c in range(channels):
			sample = frames[i + c * width:i + (c + 1) * width]
			total += (sample[0] - 128) << 8 if width == 1 else int.from_bytes(sample[-2:], "little", signed=True)
		samples.append(total // channels)
	samples += [0] * (-len(samples) % 0x40)
	import tempfile
	import shutil
	work = tempfile.mkdtemp(prefix="romsrc-ima")
	try:
		pcm, ima = os.path.join(work, "in.pcm"), os.path.join(work, "out.ima")
		with open(pcm, "wb") as f:
			f.write(struct.pack(">%dh" % len(samples), *samples))
		if NEWTONSCRIPT is None or subprocess.run([nd.newtonscript_path(NEWTONSCRIPT), "--ima-compress", pcm, ima]).returncode != 0:
			raise ValueError("%s: an IMA sound needs the builder's --newtonscript to compress it" % path)
		with open(ima, "rb") as f:
			return f.read()
	finally:
		shutil.rmtree(work, ignore_errors=True)


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
		self.native = None			# a binary of ARM code: its source (assembled where it falls)
		self.compiled = False		# made by the host's compiler (a function's, its literals)
		self.source = None			# a function written in the tree: (its options, its record)


class Reader:
	TOKEN = re.compile(r"""
		(?P<space>\s+|//[^\n]*)
		|(?P<string>"(?:[^"\\]|\\.)*")
		|(?P<quoted>\|(?:[^|\\]|\\.)*\|)
		|(?P<char>\$\\u[0-9a-fA-F]{4}|\$.)
		|(?P<hex>\#[0-9a-fA-F]+)
		|(?P<magic>@(?:\d+|[A-Za-z_][A-Za-z0-9_]*))
		|(?P<number>-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)
		|(?P<name>[A-Za-z_][A-Za-z0-9_]*)
		|(?P<punct>:=|[{}\[\](),:;'+])
		""", re.X | re.S)
	SPACE = re.compile(r"(?:\s+|//[^\n]*)*")
	CODE_START = re.compile(r"func\s*\(")
	COMPILE_START = re.compile(r"compile\s*\(")
	COMPILE_KEY = re.compile(r"(constants|options)\s*:")

	def __init__(self, text, where, root, names=None):
		"""names: the tree's names (TreeNames) - the magic pointers' and the
		constants' - when it has them."""
		self.toks = []
		self.root = root
		self.names = names
		self.where = where
		pos = 0
		while pos < len(text):
			if self.CODE_START.match(text, pos):
				pos = self.code(text, pos)
				continue
			if self.COMPILE_START.match(text, pos):
				pos = self.compile_args(text, pos)
				continue
			m = self.TOKEN.match(text, pos)
			if not m:
				raise SyntaxError("%s: cannot read at %r" % (where, text[pos:pos + 30]))
			pos = m.end()
			if m.lastgroup != "space":
				self.toks.append((m.lastgroup, m.group()))
		self.i = 0

	def code(self, text, pos):
		"""A NewtonScript expression (a func, or compile(...)'s) as one token."""
		end = code_extent(text, pos, self.where)
		self.toks.append(("code", text[pos:end].rstrip()))
		return end

	def compile_args(self, text, pos):
		"""compile([constants: {name: code, ...},] [options: "...",] code) as
		tokens, its code each one token."""
		m = self.COMPILE_START.match(text, pos)
		self.toks += [("name", "compile"), ("punct", "(")]
		pos = self.SPACE.match(text, m.end()).end()
		while True:
			key = self.COMPILE_KEY.match(text, pos)
			if not key:
				break
			self.toks += [("name", key.group(1)), ("punct", ":")]
			pos = self.SPACE.match(text, key.end()).end()
			if key.group(1) == "options":
				s = self.TOKEN.match(text, pos)
				if s.lastgroup != "string":
					raise SyntaxError("%s: compile's options are a string" % self.where)
				self.toks.append(("string", s.group()))
				pos = s.end()
			else:
				if text[pos] != "{":
					raise SyntaxError("%s: compile's constants are {name: value, ...}" % self.where)
				self.toks.append(("punct", "{"))
				pos = self.SPACE.match(text, pos + 1).end()
				while text[pos] != "}":
					name = self.TOKEN.match(text, pos)
					if name.lastgroup not in ("name", "quoted"):
						raise SyntaxError("%s: a constant's name expected at %r" % (self.where, text[pos:pos + 30]))
					self.toks.append((name.lastgroup, name.group()))
					pos = self.SPACE.match(text, name.end()).end()
					if text[pos] != ":":
						raise SyntaxError("%s: : expected after constant %s" % (self.where, name.group()))
					self.toks.append(("punct", ":"))
					pos = self.SPACE.match(text, self.code(text, self.SPACE.match(text, pos + 1).end())).end()
					if text[pos] == ",":
						self.toks.append(("punct", ","))
						pos = self.SPACE.match(text, pos + 1).end()
				self.toks.append(("punct", "}"))
				pos += 1
			pos = self.SPACE.match(text, pos).end()
			if text[pos] != ",":
				raise SyntaxError("%s: , expected after compile's %s" % (self.where, key.group(1)))
			self.toks.append(("punct", ","))
			pos = self.SPACE.match(text, pos + 1).end()
		pos = self.SPACE.match(text, self.code(text, pos)).end()
		if text[pos] != ")":
			raise SyntaxError("%s: ) expected to end compile(...) at %r" % (self.where, text[pos:pos + 30]))
		self.toks.append(("punct", ")"))
		return pos + 1

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
		v = self.primary()
		if self.peek()[1] != "+":
			return v
		# a sum of integers and the tree's constants (vVisible + vClickable)
		total = self.integer_of(v)
		while self.peek()[1] == "+":
			self.take()
			total += self.integer_of(self.primary())
		return self.integer(total)

	def integer(self, number, text=None):
		# the object file holds the ARM's word: a 30-bit integer (a wider
		# one would wrap silently; docs/frames/64bit.md)
		if not -(1 << 29) <= number < (1 << 29):
			raise SyntaxError("%s: an integer the ROM's 30 bits cannot hold" % (text or number))
		return Imm((number << 2) & 0xffffffff)

	def integer_of(self, v):
		if not isinstance(v, Imm) or v.ref & 3:
			raise SyntaxError("%s: only integers are added up" % self.where)
		return (v.ref >> 2) - ((v.ref >> 2) & 0x20000000) * 2

	def code_function(self, main, constants=(), options=""):
		"""A function written in the tree: the record the builder compiles."""
		record = "".join("@@const %s\n%s\n" % (name, self.magic_numbers(code)) for name, code in constants)
		if constants:
			record += "@@main\n"
		o = Obj("function")
		o.source = (options, record + self.magic_numbers(main) + "\n@@end\n")
		return o

	def magic_numbers(self, code):
		"""A function's source as the compiler reads it: its @names and
		constants made numbers."""
		if self.names and self.names.constants:
			code = code_with_constants(code, self.names.constants, self.where)
		if "@" not in code:
			return code
		return code_with_magic_numbers(code, self.names.magic if self.names else {}, self.where)

	def primary(self):
		kind, text = self.peek()
		if kind == "code":
			self.take()
			return self.code_function(text)
		if kind == "name" and text == "compile" and self.toks[self.i + 1][1] == "(":
			self.take()
			self.take("(")
			constants, options = [], ""
			while self.peek()[0] == "name" and self.peek()[1] in ("constants", "options") and self.toks[self.i + 1][1] == ":":
				key = self.take()[1]
				self.take(":")
				if key == "options":
					options = " " + self.take()[1][1:-1].strip()
				else:
					self.take("{")
					while self.peek()[1] != "}":
						name = self.name_text()
						self.take(":")
						constants.append((name, self.take()[1]))
						if self.peek()[1] == ",":
							self.take()
					self.take("}")
				self.take(",")
			main = self.take()[1]
			self.take(")")
			return self.code_function(main, constants, options)
		if kind == "number":
			self.take()
			return self.integer(int(text), text)
		if kind == "name" and self.names and text in self.names.constants:
			self.take()
			value = self.names.constants[text]
			if callable(value):
				self.take("(")
				arg = self.integer_of(self.value())
				self.take(")")
				value = value(arg)
			return self.integer(value, text)
		if kind == "char":
			self.take()
			c = int(text[3:], 16) if text.startswith("$\\u") else ord(text[1])
			return Imm((c << 4) | 6)
		if kind == "hex":
			self.take()
			return Imm(int(text[1:], 16))
		if kind == "magic":
			self.take()
			if not text[1].isdigit():
				index = self.names.magic.get(text[1:].lower()) if self.names else None
				if index is None:
					raise SyntaxError("%s: %s is no magic pointer's name (magic.tsv)" % (self.where, text))
				return Imm((index << 2) | 3)
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
		if kind == "name" and text in ("real", "string", "binary", "array", "map", "bytes", "function", "bitmap", "sound", "pict", "same", "imasound", "tonescore", "shorts", "fixed", "hexfile", "sfnt", "texttable", "lexicon", "picture", "native") \
				and self.toks[self.i + 1][1] == "(":
			self.take()
			self.take("(")
			args = []
			while self.peek()[1] != ")":
				if text in ("real", "fixed") and self.peek()[0] == "number":
					args.append(float(self.take()[1]))
				elif text in ("string", "binary", "bytes", "function", "bitmap", "sound", "pict", "same", "imasound", "hexfile", "sfnt", "texttable", "lexicon", "picture", "native") and self.peek()[0] == "string":
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
			if text == "sfnt":
				return Obj("binary", args[0], data=newtonsfnt.pack(os.path.join(self.root, args[1][1:-1])))
			if text == "texttable":
				return Obj("binary", args[0], data=newtontables.pack(os.path.join(self.root, args[1][1:-1])))
			if text == "picture":
				return Obj("binary", args[0], data=newtonpict.pack(os.path.join(self.root, args[1][1:-1])))
			if text == "native":
				# (its size now - assembled at 0, its source having no .origin;
				# its bytes when it is placed, Builder.build)
				source = os.path.join(self.root, args[1][1:-1])
				o = Obj("binary", args[0], data=newtonarm.pack(source, 0))
				o.native = source
				return o
			if text == "lexicon":
				return Obj("binary", args[0], data=newtonlex.pack(os.path.join(self.root, args[1][1:-1]))[4:])
			if text == "hexfile":
				with open(os.path.join(self.root, args[1][1:-1]), encoding="utf-8") as f:
					return Obj("binary", args[0], data=hex_file_bytes(f.read()))
			if text == "shorts":
				return Obj("binary", args[0], data=struct.pack(">%dh" % (len(args) - 1), *[(v.ref >> 2) - ((v.ref >> 2) & 0x20000000) * 2 for v in args[1:]]))
			if text == "fixed":
				return Obj("binary", args[0], data=struct.pack(">i", int(round(args[1] * 65536))))
			if text == "tonescore":
				return Obj("binary", args[0], data=tone_score_bytes(args[1], args[2]))
			if text == "imasound":
				return Obj("binary", args[0], data=ima_compress(os.path.join(self.root, args[1][1:-1])))
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


class TreeNames:
	"""The names a tree's notation may use beyond its definitions: the
	magic pointers' (@protoFloatNGo: magic.tsv, the object each entry is and
	the names after it) and the constants' (vVisible: constants.tsv, `name
	value`, or `name <<n` for one used as name(x), x shifted left n).  A
	package's part tree uses the ROM's: the nearest directory above it that
	has the files, else the repository's romsrc/."""
	REPO_TREE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "romsrc")

	def __init__(self, src):
		self.magic = {}
		self.constants = {}
		self.magic_path = self.find(src, "magic.tsv")
		if self.magic_path:
			with open(self.magic_path, encoding="utf-8") as f:
				for line in f:
					fields = line.rstrip("\n").split("\t")
					if not fields[0].isdigit():
						continue
					names = ([fields[1]] if IDENT.match(fields[1]) and fields[1] not in ("nil", "true") else []) + (fields[2].split(",") if len(fields) > 2 and fields[2] else [])
					for name in names:
						self.magic.setdefault(name.lower(), int(fields[0]))
		path = self.find(src, "constants.tsv")
		if path:
			with open(path, encoding="utf-8") as f:
				for line in f:
					fields = line.rstrip("\n").split("\t")
					if line.startswith("#") or len(fields) < 2:
						continue
					if fields[1].startswith("<<"):
						self.constants[fields[0]] = (lambda shift: lambda x: x << shift)(int(fields[1][2:]))
					else:
						self.constants[fields[0]] = int(fields[1], 0)

	@classmethod
	def find(cls, src, name):
		here = os.path.abspath(src)
		while True:
			if os.path.exists(os.path.join(here, name)):
				return os.path.join(here, name)
			up = os.path.dirname(here)
			if up == here:
				break
			here = up
		path = os.path.join(cls.REPO_TREE, name)
		return path if os.path.exists(path) else None


def tree_files(src):
	"""A tree's definition files: maps.ns, then every .ns under objects/ (its
	subdirectories too), in order of their paths."""
	files = [os.path.join(src, "maps.ns")]
	objdir = os.path.join(src, "objects")
	found = []
	for top, dirs, names in os.walk(objdir):
		found += [os.path.relpath(os.path.join(top, f), objdir).replace(os.sep, "/") for f in names if f.endswith(".ns")]
	return files + [os.path.join(objdir, f) for f in sorted(found)]


def record_flags(head):
	"""What a function record's @@ line says beyond the function it was: " names"
	(compiled with the variables' names kept, the host's compiler is told),
	" argframe=P,I" (with_arg_frame)."""
	flags = ""
	if re.search(r" names\b", head):
		flags += " names"
	m = re.search(r" argframe=(nil|0),(nil|0)\b", head)
	if m:
		flags += m.group(0)
	m = re.search(r" literals\+=[A-Za-z_][A-Za-z0-9_]*(?:,[A-Za-z_][A-Za-z0-9_]*)*", head)
	if m:
		flags += m.group(0)
	return flags


def ntk_extra_literals_flag(rom, ref, why):
	"""" literals+=a,b" when a function's literals are the compiler's and then
	symbols nothing uses - the NTK left one where it compiled a call it then
	made a freq-func (Stringer) - else ""."""
	m = re.match(r"\.literals: (\d+) slots, the ROM's (\d+)$", why)
	if not m or int(m.group(1)) >= int(m.group(2)):
		return ""
	extra = rom.slots(rom.slots(ref)[2])[int(m.group(1)):]
	names = [rom.symname(r) for r in extra]
	if any(n is None or not IDENT.match(n) for n in names):
		return ""
	return " literals+=" + ",".join(names)


def ntk_arg_frame_flag(rom, ref):
	"""" argframe=P,I" for a function whose argFrame's _nextArgFrame is nil
	and _parent and _implementor nil or 0 (P and I), else "".  The NTK's
	native compiler's bytecode fallbacks (bcFunc) have an argFrame where the
	host's compiler, as the ROM's, makes none - one that keeps nothing, its
	three header slots - and 0 in _parent and _implementor where the host's
	leaves a top-level function's nil."""
	af = rom.slots(ref)[3] if len(rom.slots(ref)) > 3 else 2
	if not rom.is_ptr(af) or rom.flags(af) & 3 != 3:
		return ""
	slots = rom.frame_slots(af)
	if [t for t, _ in slots[:3]] != ["_nextArgFrame", "_parent", "_implementor"] or slots[0][1] != 2 \
			or any(v not in (0, 2) for _, v in slots[1:3]):
		return ""
	return " argframe=%s,%s" % tuple("nil" if v == 2 else "0" for _, v in slots[1:3])


def with_arg_frame(value, head):
	"""A compiled function given the argFrame its record's " argframe=P,I"
	asks for: {_nextArgFrame: nil, _parent: P, _implementor: I} when the
	compiler made none, else P and I in the one it made."""
	m = re.search(r" argframe=(nil|0),(nil|0)\b", head)
	if not m or not isinstance(value, Obj) or value.kind != "frame" or "argFrame" not in value.tags:
		return value
	k = value.tags.index("argFrame")
	ref = {"nil": 2, "0": 0}
	af = value.items[k]
	if isinstance(af, Imm) and af.ref == 2:
		value.items[k] = Obj("frame", tags=["_nextArgFrame", "_parent", "_implementor"],
							 items=[Imm(2), Imm(ref[m.group(1)]), Imm(ref[m.group(2)])])
	elif isinstance(af, Obj) and af.kind == "frame" and af.tags[:3] == ["_nextArgFrame", "_parent", "_implementor"]:
		af.items[1], af.items[2] = Imm(ref[m.group(1)]), Imm(ref[m.group(2)])
	return value


def with_record_flags(value, head):
	"""A compiled function given what its record's @@ line asks for beyond
	what the compiler made: the argFrame (with_arg_frame), and symbols
	appended to its literals (" literals+=a,b")."""
	with_arg_frame(value, head)
	m = re.search(r" literals\+=([A-Za-z_][A-Za-z0-9_]*(?:,[A-Za-z_][A-Za-z0-9_]*)*)", head)
	if m and isinstance(value, Obj) and value.kind == "frame" and "literals" in value.tags:
		k = value.tags.index("literals")
		lits = value.items[k]
		if isinstance(lits, Imm) and lits.ref == 2:
			lits = value.items[k] = Obj("array", Sym("literals"), items=[])
		lits.items += [Sym(n) for n in m.group(1).split(",")]
	return value


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
	def __init__(self, src, newtonscript=None, relayout=False, base=None, original=False):
		"""relayout: the objects laid out afresh, one after another in the
		layout's order at the sizes they now have (an edit that grows or
		shrinks one moves every one after it; an object the layout does not
		know goes at the end), rather than at the layout's addresses.
		original: the pieces of the extension that were not in the ROM
		(rex.tsv's `-` lines: the Newton Internet Enabler) left out, so that
		the rest can still be compared with the ROM."""
		self.src = src
		self.original = original
		self.newtonscript = newtonscript
		global NEWTONSCRIPT
		if newtonscript is not None:
			NEWTONSCRIPT = newtonscript
		self.relayout = relayout
		self.new_base = base				# (relayout: where the area now starts - a package part that has moved)
		self.relocations = []			# (the ROM's ref, the ref now) for every object that moved
		self.link_base = 0				# what a native binary's .origin counts from (a package file's part: PACKAGE_BASE)
		self.native_relocations = []	# the addresses of the words native binaries' .rword made
		self.defs = {}
		self.names = TreeNames(src)
		for path in tree_files(src):
			with open(path, encoding="utf-8") as f:
				for name, value in Reader(f.read(), path, src, self.names).definitions():
					if name in self.defs:
						raise SyntaxError("%s defined twice" % name)
					if name in self.names.constants:
						raise SyntaxError("%s: %s is a constant's name (constants.tsv)" % (path, name))
					self.defs[name] = value
		self.read_layout()
		self.compile_functions()
		self.dropped = set()			# same() definitions no function holds any more
		for name in list(self.defs):
			if name in self.defs:
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
		self.alias_paths = {}			# a name -> the paths inside compiled functions the layout says hold it
		self.literal_aliases = {}		# a function's literals array's path -> the names its aliases give
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
					self.alias_paths.setdefault(fields[2], []).append(fields[1])
					m = LITERAL_PATH.match(fields[1].lower())
					if m:
						self.literal_aliases.setdefault(m.group(1), []).append(fields[2])
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
				if getattr(v, "source", None):
					# (a function written where it is used: its options, its record)
					head, body = v.source
				else:
					with open(os.path.join(self.src, v.data), encoding="utf-8") as f:
						text = f.read()
					# (each record numbered afresh: the file's own @@ line names
					# the ROM's function, which an edited tree need not have)
					head, body = text.split("\n", 1)
				out.write("@@ %x%s\n%s" % (n, record_flags(head), body))
				by_id[n] = (v, head)
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
				target, head = by_id.pop(n)
				with_record_flags(value, head)
				value = self.replace_closure_stubs(value)
				mark_compiled(value)
				target.kind, target.cls, target.items, target.tags, target.data = \
					value.kind, value.cls, value.items, value.tags, value.data
				i += 3
			else:
				i += 1
		import shutil
		shutil.rmtree(work, ignore_errors=True)
		if by_id:
			raise ValueError("%d functions came back uncompiled" % len(by_id))

	def replace_closure_stubs(self, v, seen=None):
		"""A compiled function's closure stubs (closure_target: the NTK native
		functions its source cannot write) and object names ('|romsrc:object:
		<ref>|: native code it does not hold the bytes of) replaced by the
		definitions the layout places at the addresses they name."""
		if seen is None:
			seen = set()
			self.__dict__.setdefault("defined_at", {a + 1: path for a, path, _, _ in self.entries if IDENT.match(path)})
		target = closure_target(v)
		if target is None and isinstance(v, Sym) and OBJECT_REF.match(v.name):
			target = int(OBJECT_REF.match(v.name).group(1), 16)
		if target is not None:
			name = self.defined_at.get(target)
			if name is None or name not in self.defs:
				raise ValueError("a function's source names %#x (romsrc:closure, native or object), which no "
								 "definition of the layout is at" % target)
			return Name(name)
		if isinstance(v, Obj) and id(v) not in seen:
			seen.add(id(v))
			v.items = [self.replace_closure_stubs(x, seen) for x in v.items]
		return v

	def resolve_same(self, name, seen):
		"""A same(path) definition replaced by the object at that path."""
		v = self.defs[name]
		if not (isinstance(v, Obj) and v.kind == "same"):
			return v
		if name in seen:
			raise ValueError("same() goes round in a circle at %s" % name)
		seen.add(name)
		# (the object is wherever a compiled function holds it: the path
		# same() gives, or any the layout's alias lines give for it.  An edit
		# to a function can move its literals, so a path may now lead
		# nowhere, or to another object: the reading most of the paths agree
		# on is taken, the same() path's when they are as many)
		paths = [v.data] + [p for p in self.alias_paths.get(name, []) if p.lower() != v.data.lower()]
		found = []
		for path in paths:
			try:
				found.append(self.at_path(path, set(seen)))
			except (ValueError, IndexError, AttributeError, KeyError):
				pass
		if not found:
			if self.names_anywhere(name):
				raise ValueError("%s := same(\"%s\"): no function holds it any more at that path or at its "
								 "alias lines' (layout.tsv), and it is named elsewhere: point the same() at a "
								 "function that holds the object" % (name, v.data))
			# (no function holds it now: an edit took it out - nothing to lay out)
			del self.defs[name]
			self.dropped.add(name)
			return None
		best = max(range(len(found)), key=lambda i: (sum(1 for f in found if self.same_value(found[i], f, set())), -i))
		self.defs[name] = found[best]
		return self.defs[name]

	def names_anywhere(self, name):
		"""Whether a definition or the magic-pointer table names name (not a
		compiled function's alias)."""
		seen = set()

		def holds(v):
			if isinstance(v, Name):
				return v.name == name
			if isinstance(v, Obj) and id(v) not in seen:
				seen.add(id(v))
				return any(holds(x) for x in v.items) or holds(v.cls)
			return False
		if any(holds(v) for n, v in self.defs.items() if n != name):
			return True
		magic = os.path.join(self.src, "magic.tsv")
		if os.path.exists(magic):
			with open(magic, encoding="utf-8") as f:
				return any(line.rstrip("\n").split("\t")[1:2] == [name] for line in f if not line.startswith("#"))
		return False

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
				# (a symbol is the same whatever its case: the compiler keeps the
				# first spelling it met, so a slot may be spelt otherwise here)
				v = v.items[[t.lower() for t in v.tags].index(tag.lower())]
			elif index:
				v = v.items[int(index)]
			else:
				v = v.cls
			if isinstance(v, Name):
				if v.name in self.dropped:
					raise KeyError(v.name)
				v = self.resolve_same(v.name, seen)
		return v

	def walk(self, v, path):
		if not isinstance(v, Obj):
			return v
		# (paths are matched whatever their case: a compiled frame's tags are
		# spelt as the host first interned the symbols)
		if closure_target(v) is not None:
			# (a native function the function closes over: the definition the
			# layout's alias names, always - the stub is only its place)
			if path.lower() not in self.aliases or self.aliases[path.lower()] not in self.defs:
				raise ValueError("%s: a closure stub (romsrc:closure) with no alias in layout.tsv naming its "
								 "native function's definition" % path)
			return Name(self.aliases[path.lower()])
		name = self.aliases.get(path.lower())
		if name is not None and name not in self.dropped:
			if name not in self.defs:
				raise ValueError("%s: the alias %s is not defined" % (path, name))
			# (only while it is still the same: a function edited so that it
			# compiles to other instructions or literals has its own)
			if self.same_value(v, self.defs[name], set()):
				return Name(name)
		if v.compiled:
			# (a literal of a compiled function whose literals an edit moved:
			# the alias of one of that function's literals with the same
			# contents - the object it was before the edit)
			m = LITERAL_PATH.match(path.lower())
			for candidate in self.literal_aliases.get(m.group(1), []) if m else []:
				if candidate != name and candidate in self.defs and self.same_value(v, self.defs[candidate], set()):
					return Name(candidate)
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

	def same_value(self, a, b, seen):
		"""Whether two values are the same object's contents: what an alias
		in the layout needs of the object a compiled function made."""
		if isinstance(a, Name) and isinstance(b, Name) and a.name == b.name:
			return True
		if isinstance(a, Name):
			a = self.defs.get(a.name, a)
		if isinstance(b, Name):
			b = self.defs.get(b.name, b)
		if isinstance(a, Obj) and isinstance(b, Obj):
			if a is b or (id(a), id(b)) in seen:
				return True
			seen.add((id(a), id(b)))
			# (a compiled frame's map is an array the layout's map stands for)
			kinds = {a.kind, b.kind}
			if a.kind == "frame" == b.kind and [t.lower() for t in a.tags] != [t.lower() for t in b.tags] \
					and (a.compiled or b.compiled) and sorted_by_compiler(a.tags, b.tags):
				# (a compiled frame whose map the compiler sorted: the same slots)
				ia = {t.lower(): x for t, x in zip(a.tags, a.items)}
				return a.data == b.data and self.same_value(a.cls, b.cls, seen) \
					and all(self.same_value(ia[t.lower()], y, seen) for t, y in zip(b.tags, b.items))
			return ((len(kinds) == 1 or kinds == {"array", "map"})
				and [t.lower() for t in a.tags] == [t.lower() for t in b.tags] and a.data == b.data
				and len(a.items) == len(b.items) and self.same_value(a.cls, b.cls, seen)
				and all(self.same_value(x, y, seen) for x, y in zip(a.items, b.items)))
		if type(a) is not type(b):
			return False
		if isinstance(a, (Name, Sym)):
			return a.name.lower() == b.name.lower() if isinstance(a, Sym) else a.name == b.name
		if isinstance(a, Imm):
			return a.ref == b.ref
		return a == b

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

	@staticmethod
	def patched(data, patches):
		"""A package file with the patches of <package>.patches.tsv applied,
		when there is one: each line an offset in the file (hex), the bytes
		there (hex, checked) and the bytes they become, then why - the
		changes a package needs to work from the ROM that its own code did
		not foresee (romsrc/README.md)."""
		if not os.path.exists(patches):
			return data
		data = bytearray(data)
		with open(patches, encoding="utf-8") as f:
			for number, line in enumerate(f, 1):
				if not line.strip() or line.startswith("#"):
					continue
				fields = line.rstrip("\n").split("\t")
				at, old, new = int(fields[0], 16), bytes.fromhex(fields[1]), bytes.fromhex(fields[2])
				if len(old) != len(new) or data[at:at + len(old)] != old:
					raise ValueError("%s:%d: the bytes at %#x are not %s" % (patches, number, at, fields[1]))
				data[at:at + len(new)] = new
		return bytes(data)

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
		docs/rom-free/README.md).  A header or directory that is text
		(tools/rex/rextext.py) is made when every piece is placed, its
		lengths and offsets taken from where the pieces now lie; one kept
		as bytes (.bin, as the tree tagged romsrc-rom has them) is patched
		by how far things moved.

		A piece whose address is `-` was not in the ROM: an ordinary
		package file (`rom-form` in the third column), added to the package
		list where it stands and put into the form the ROM keeps its own
		packages in, at the address it falls at (packages.py's
		rom_form_package: its refs made addresses, its relocation applied,
		the units it exports given 'fexp entries and its imports resolved
		to them), the entries appended to 'fexp.  A padding piece (`pad_*`)
		is as long as it must be for the piece after it to keep the
		alignment it had (to 0x1000 at most: the page tables and the patch
		table start on pages; a text one says its alignment).  The patch table's page tables ('ptpt,
		'glpt: MMU small-page entries naming the physical page the patch
		table 'jump is in, which they map at its virtual address - its
		branches are relative to that, so they stay as they are) are
		carried as they are: nothing on the host reads them, and on a
		MessagePad they would have to name the table's new page."""
		pieces = []
		with open(rex, encoding="utf-8") as f:
			for line in f:
				if line.startswith("#"):
					continue
				fields = line.rstrip("\n").split("\t")
				if fields[0] == "rex":
					base, length = int(fields[1], 16), int(fields[2], 16)
				elif fields[0] != "-" or not self.original:
					old = None if fields[0] == "-" else int(fields[0], 16)
					pieces.append((old, fields[1], fields[2] if len(fields) > 2 else ""))
		# the next free 'fexp entry: after the ROM's own (the fexp piece's words)
		fexp_next = 0
		for old, rel, how in pieces:
			if os.path.basename(rel).startswith("fexp"):
				path = os.path.join(self.src, rel)
				fexp_next = (len(rextext.pack(path)) if rel.endswith(".txt") else os.path.getsize(path)) // 4
		units = {}					# the units the added packages export (rom_form_package)
		new_fexp = []				# their 'fexp entries
		data = bytearray()
		moved = {}					# a piece's old address -> its new one
		relocations = {}			# an exported object's old ref -> its new one
		head = None					# the offset in data of the last package directory
		fexp_at = None
		placed = []					# (rel, offset, size) of every piece
		texts = []					# (rel, offset, its rextext piece): the text pieces worked out last
		for n, (old, rel, how) in enumerate(pieces):
			new = base + len(data)
			if old is not None:
				moved[old] = new
			if placed and placed[-1][2] is None:
				placed[-1] = placed[-1][:2] + (new - base - placed[-1][1],)
			placed.append((rel, new - base, None))
			text = None
			if rel.endswith(".txt"):
				# a piece as text (tools/rex/rextext.py): one that says where
				# other pieces are (the header, a package's directory, padding)
				# made when every piece is placed - a package's directory then
				# takes its size and its parts' from them, so it is not
				# patched as a .bin one is below
				text = rextext.load(os.path.join(self.src, rel))
				if text.needs_layout or isinstance(text, rextext.Pad):
					size = text.size(new)
					texts.append((rel, len(data), text))
					data += bytes(size)
					if isinstance(text, rextext.PackageDirectory):
						head = None
					continue
			if text is None and os.path.basename(rel).startswith("pad_") and n + 1 < len(pieces) and pieces[n + 1][0] is not None:
				after = pieces[n + 1][0]
				align = min(after & -after, 0x1000)
				data += b"\0" * ((after - new) % align)
				continue
			if how == "rom-form":
				if rel.endswith("/"):
					# a package as text (tools/armpart/newtonpkg.py), built as the package
					# file, its frames parts kept as trees built here (PackageTrees)
					blob = newtonpkg.pack(os.path.join(self.src, rel), trees=PackageTrees(self.newtonscript))
				else:
					with open(os.path.join(self.src, rel), "rb") as piece:
						blob = piece.read()
				blob = self.patched(blob, os.path.join(self.src, rel.rstrip("/").rsplit(".", 1)[0] + ".patches.tsv"))
				blob, entries = rexpackages.rom_form_package(blob, new, fexp_next + len(new_fexp), units)
				new_fexp += entries
				head = len(data)
				data += blob
				continue
			if how == "package":
				# a whole package as text (tools/armpart/newtonpkg.py: its directory,
				# its protocol parts as ARM source) built at the address it now
				# falls at, so that its branches out keep reaching the ROM
				head = len(data)
				data += newtonpkg.pack(os.path.join(self.src, rel), base=new)
				continue
			if how == "store":
				# a package's soup part, a store as text (tools/stores/newtonstore.py),
				# rounded up to a word so that what follows stays aligned (the
				# store's directory says where its objects end)
				blob = newtonstore.pack(os.path.join(self.src, rel))
				data += blob + bytes(-len(blob) % 4)
				continue
			if old is None and text is None:
				raise ValueError("rex.tsv: %s is not from the ROM and not a package to put into its form" % rel)
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
			if text is not None:
				blob = text.data()
			else:
				with open(os.path.join(self.src, rel), "rb") as piece:
					blob = piece.read()
			if blob[:7] == b"package":
				head = len(data)
			if os.path.basename(rel).startswith("fexp"):
				blob += b"".join(struct.pack(">I", r) for r in new_fexp)
			data += blob
		moved[base + length] = base + len(data)
		if placed and placed[-1][2] is None:
			placed[-1] = placed[-1][:2] + (len(data) - placed[-1][1],)
		layout = rextext.Layout(base, len(data), placed)
		for rel, offset, text in texts:
			blob = text.render(layout, rel)
			if len(blob) != layout.at(rel, rel)[1]:
				raise ValueError("rex.tsv: %s made %d bytes, not the %d it had room for" % (rel, len(blob), layout.at(rel, rel)[1]))
			data[offset:offset + len(blob)] = blob
		if not pieces[0][1].endswith(".txt"):
			# a header kept as bytes: its length, and each config entry's offset and size
			struct.pack_into(">I", data, 0x18, len(data))
			for i in range(struct.unpack_from(">I", data, 0x24)[0]):
				entry = 0x28 + 12 * i
				tag, offset, size = struct.unpack_from(">4sII", data, entry)
				start, end = base + offset, base + offset + size
				new_start, new_end = moved.get(start, start), moved.get(end, end)
				struct.pack_into(">II", data, entry + 4, new_start - base, new_end - new_start)
		for i in range(struct.unpack_from(">I", data, 0x24)[0]):
			tag, offset, size = struct.unpack_from(">4sII", data, 0x28 + 12 * i)
			if tag == b"fexp":
				fexp_at, fexp_size = offset, size
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
		# (in an order of their places a definition's new name does not change:
		# the new maps are numbered in it)
		path_order = self.path_order(entries)
		for path, v in sorted(self.by_path.items(), key=lambda pv: path_order(pv[0])):
			if v.kind != "frame":
				continue
			tags = [t.lower() for t in v.tags]
			current = frame_map.get(path)
			if current is not None and self.map_tags(current) == tags:
				continue
			if current is not None and v.compiled and self.map_tags(current) is not None \
					and sorted_by_compiler(v.tags, self.map_tags(current)):
				# (a frame literal the compiler sorted the map of: its slots put
				# back in the order of the map the layout gives it)
				order = {t.lower(): i for i, t in enumerate(v.tags)}
				new = [order[t] for t in self.map_tags(current)]
				v.tags = [v.tags[i] for i in new]
				v.items = [v.items[i] for i in new]
				continue
			name = by_tags.get(tuple(tags))
			if name is None:
				made += 1
				name = "romsrc_map_%d" % made
				# (a map's class is its flags: 4, kMapProto, says the frame has
				# a _proto slot, without which nothing is inherited through it)
				flags = (4 << 2) if "_proto" in tags else 0		# (as an integer ref)
				m = Obj("map", Imm(flags), items=[Imm(2)] + [Sym(t) for t in v.tags])
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

	def place_grown_lexicons(self):
		"""A lexicon an edit has made bigger than the room it had - the
		distance to the next block of ROM data - goes to an address of its
		own above everything (MOVED_DATA_BASE), and the object file's moved
		table says where (the host's ROMMovedAddress); only with --relayout,
		since the result is no longer the ROM's layout."""
		starts = sorted(a for a, _ in self.blocks)
		at = MOVED_DATA_BASE
		for i in self.lexicon_blocks:
			address, data = self.blocks[i]
			later = [s for s in starts if s > address]
			room = (later[0] - address) if later else len(data)
			if len(data) <= room:
				continue
			if not self.relayout:
				raise ValueError("lexicon at %#x has grown from %#x bytes to %#x: build with --relayout"
								 % (address, room, len(data)))
			self.blocks[i] = (at, data)
			self.relocations.append((address, at))
			at += (len(data) + 15) & ~15

	@staticmethod
	def path_order(entries):
		"""A key that orders paths by the layout's address of the definition
		each is in (a new one last), then by the rest of the path: an order
		the definitions' names do not change."""
		defined_at = {path.lower(): a for a, path, _, _ in entries if IDENT.match(path)}

		def key(path):
			m = re.match(r"(\|(?:[^|\\]|\\.)*\||[A-Za-z_][A-Za-z0-9_]*)", path)
			if not m:
				return (1 << 40, path)
			return (defined_at.get(m.group(1).lower(), 1 << 40), path[m.end():].lower(), path.lower())
		return key

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
			v = self.by_path.get(path.lower())
			if v is not None:
				# (an edit may have put another kind of object at a path - an
				# element inserted in an array moves every later one along:
				# the header's kind bits follow what is there now)
				kind = {"frame": 3, "binary": 0}.get(v.kind, 1)
				if flags & 3 != kind:
					flags = (flags & ~3) | kind
					extra = {k: x for k, x in extra.items() if k != "map"}
			out.append((at, path, flags, extra))
			if at != a:
				self.relocations.append((a + 1, at + 1))
			at = start + ((at - start + size + self.align - 1) & ~(self.align - 1))
		# (the new ones: the symbols by name, then the objects in the order of
		# the definitions they are in - an order a definition's new name does
		# not change)
		path_order = self.path_order(entries)
		for path in sorted(new_paths, key=lambda p: (not p.startswith("'"), path_order(p))):
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
				# (the object, then any other names it goes by)
				index, what = line.rstrip("\n").split("\t")[:2]
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

		# the other ROM data: the lexicons (a word list or a lexical graph
		# built by newtonlex.py, or kept as bytes)
		self.blocks = []
		self.lexicon_blocks = []
		lexicons = os.path.join(self.src, "lexicons.tsv")
		if os.path.exists(lexicons):
			with open(lexicons, encoding="utf-8") as f:
				for line in f:
					if line.startswith("#"):
						continue
					address, _, rel = line.rstrip("\n").split("\t")
					path = os.path.join(self.src, rel)
					if rel.endswith((".words", ".lex")):
						data = newtonlex.pack(path)
					else:
						with open(path, "rb") as blob:
							data = blob.read()
					self.lexicon_blocks.append(len(self.blocks))
					self.blocks.append((int(address, 16), data))

		# the ROM's other data (romdata.tsv)
		romdata = os.path.join(self.src, "romdata.tsv")
		if os.path.exists(romdata):
			with open(romdata, encoding="utf-8") as f:
				for line in f:
					if line.startswith("#"):
						continue
					address, _, rel = line.rstrip("\n").split("\t")
					if rel.endswith(".txt"):
						# its words as text (tools/rex/rextext.py's `words`)
						self.blocks.append((int(address, 16), rextext.pack(os.path.join(self.src, rel))))
						continue
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
				placed = []				# (rel, offset, size) of every piece
				texts = []				# (rel, offset, its rextext piece) of the text pieces worked out last
				for line in f:
					if line.startswith("#"):
						continue
					fields = line.rstrip("\n").split("\t")
					if fields[0] == "rex":
						base, length = int(fields[1], 16), int(fields[2], 16)
						continue
					if fields[0] == "-" and self.original:
						continue
					if fields[0] == "-":
						raise ValueError("rex.tsv: %s was not in the ROM: the tree builds only with --relayout" % fields[1])
					if int(fields[0], 16) != base + len(data):
						raise ValueError("rex.tsv: %s is not where the piece before it ends" % fields[1])
					if len(fields) > 2 and fields[2] == "package":
						# a whole package as text (tools/armpart/newtonpkg.py)
						blob = newtonpkg.pack(os.path.join(self.src, fields[1]), base=base + len(data))
						placed.append((fields[1], len(data), len(blob)))
						data += blob
						continue
					if len(fields) > 2 and fields[2] == "store":
						# a package's soup part, a store as text (tools/stores/newtonstore.py)
						blob = newtonstore.pack(os.path.join(self.src, fields[1]))
						placed.append((fields[1], len(data), len(blob)))
						data += blob
						continue
					if fields[1].endswith("/"):
						# a package's frames part, built from its own tree
						part_base, part = Builder(os.path.join(self.src, fields[1]), self.newtonscript).build()
						if part_base != base + len(data):
							raise ValueError("rex.tsv: %s's objects are laid out at %#x" % (fields[1], part_base))
						placed.append((fields[1], len(data), len(part)))
						data += part
						continue
					if fields[1].endswith(".txt"):
						# a piece as text (tools/rex/rextext.py): its room now, its
						# bytes when every piece is placed
						piece = rextext.load(os.path.join(self.src, fields[1]))
						size = piece.size(base + len(data))
						texts.append((fields[1], len(data), piece))
						placed.append((fields[1], len(data), size))
						data += bytes(size)
						continue
					with open(os.path.join(self.src, fields[1]), "rb") as piece:
						blob = piece.read()
					placed.append((fields[1], len(data), len(blob)))
					data += blob
				if len(data) != length:
					raise ValueError("rex.tsv: the pieces make %#x bytes, not %#x" % (len(data), length))
				layout = rextext.Layout(base, len(data), placed)
				for rel, offset, piece in texts:
					blob = piece.render(layout, rel)
					if len(blob) != layout.at(rel, rel)[1]:
						raise ValueError("rex.tsv: %s made %d bytes, not the %d it had room for" % (rel, len(blob), layout.at(rel, rel)[1]))
					data[offset:offset + len(blob)] = blob
				self.blocks.append((base, bytes(data)))

		self.place_grown_lexicons()

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
					if v.native is not None:
						# ARM code assembled at the address its bytes fall at (less
						# link_base: a package file's offsets), its relocated words noted
						assembled = newtonarm.assemble_file(v.native, a + 12 - self.link_base)
						body = assembled.data
						self.native_relocations += [a + 12 + r for r in assembled.relocations]
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


# A package file's frames part (romsrc/rex/<package>/partN/: the packages
# added to the extension) is written as a tree as if the package lay at
# PACKAGE_BASE, an address nothing else uses: its pointer refs, offsets in
# the package, are moved there to extract it and back to build it.
PACKAGE_BASE = 0x01000000


class PackageTrees:
	"""What newtonpkg.py hands a package's frames parts to: extract writes
	one as a tree (as write_rex does the ROM's own packages' parts), build
	builds one at an offset in the package."""

	def __init__(self, newtonscript, build_dir=None):
		self.newtonscript = newtonscript
		self.build_dir = build_dir

	def extract(self, part, offset, relocations, path):
		"""The part at offset in its package, with the words at relocations
		(offsets in it) relocated, written as a tree at path when it builds
		back the same (else nothing is written and the answer is False)."""
		import shutil
		blob = bytearray(part)
		try:
			rexpackages.rebase_frames_part(blob, 0, PACKAGE_BASE, PACKAGE_BASE, 4)
		except ValueError:
			return False
		rom = nf.ROM(self.build_dir)
		first = PACKAGE_BASE + offset
		rom.rom = bytes(rom.rom) + bytes(first - len(rom.rom)) + bytes(blob)
		found = part_objects(rom, first, first + len(blob), 4)
		if found is None:
			return False
		objs, pad, odd = found
		shutil.rmtree(path, ignore_errors=True)
		sub = Extractor(rom, path, self.build_dir, objs, (first, len(blob)), pad, self.newtonscript, 4)
		sub.odd_gaps = odd
		sub.relocated = {first + r for r in relocations}
		sub.link_base = PACKAGE_BASE
		sub.run()
		print("  %s: %d objects, %d functions as source, %d as bytecode"
			  % (path, len(objs), len(sub.sources), len(sub.undecompiled)))
		built, relocated = self.build(path, offset)
		if built != bytes(part) or sorted(relocated) != sorted(relocations):
			shutil.rmtree(path, ignore_errors=True)
			return False
		return True

	def build(self, path, offset):
		"""The tree built at offset in its package: (its bytes, the offsets in
		it of the words its native binaries' .rword made)."""
		b = Builder(path, self.newtonscript, relayout=True, base=PACKAGE_BASE + offset)
		b.link_base = PACKAGE_BASE
		start, part = b.build()
		part = bytearray(part)
		rexpackages.rebase_frames_part(part, PACKAGE_BASE, 2 * PACKAGE_BASE, -PACKAGE_BASE, 4)
		return bytes(part), [r - start for r in b.native_relocations]


# the modules the builder builds with, besides this one: what the stamp is a
# hash of (all imported above, so a run of any command has them)
STAMP_MODULES = ("nsfunctions", "nsdecompile", "romdicts", "packages", "romid",
				 "png", "newtonsfnt", "newtonlex", "newtontables", "inktables", "hwrtables", "newtonpict", "rextext", "newtonstore", "newtonpkg", "newtonarm", "armv4")


def builder_stamp():
	"""Which builder this is: sixteen hex digits of a SHA-256 of this script
	and STAMP_MODULES (line ends made LF, so a checkout's own line ends do
	not change it).  An object file carries it and the host compiled with it
	refuses another (HostObjectsFile.h's HostObjectsFileMatches) - an edit
	to romsrc/ leaves it as it is."""
	import hashlib
	h = hashlib.sha256()
	files = [os.path.abspath(__file__)] + [os.path.abspath(sys.modules[m].__file__) for m in STAMP_MODULES]
	for path in files:
		with open(path, "rb") as f:
			data = f.read().replace(b"\r\n", b"\n")
		h.update(os.path.basename(path).encode("utf-8") + b"\0" + data + b"\0")
	return h.hexdigest()[:16]


def container(builder, area_base, area):
	"""The file the host loads: header, area, magic pointers."""
	header = b"NewtObjs" + struct.pack(">5I", 4, area_base, len(area), builder.magic_base, len(builder.magic))
	blocks = struct.pack(">I", len(builder.blocks))
	for address, data in builder.blocks:
		blocks += struct.pack(">II", address, len(data)) + data + bytes(-len(data) % 4)
	# (version 3) where each object that moved now is: the ROM's ref, then the
	# ref now, sorted by the first
	moved = struct.pack(">I", len(builder.relocations))
	moved += b"".join(struct.pack(">II", old, new) for old, new in sorted(builder.relocations))
	# (version 4) the builder's stamp
	stamp = builder_stamp().encode("ascii")
	stamp = struct.pack(">I", len(stamp)) + stamp + bytes(-len(stamp) % 4)
	return header + area + b"".join(struct.pack(">I", r) for r in builder.magic) + blocks + moved + stamp


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
		print("%d blocks of ROM data (lexicons, other data, the extension) differ from the ROM's, e.g. %#x" % (len(bad), bad[0]), file=sys.stderr)
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


def rename_definition(tree, old, new):
	"""A definition of a tree renamed everywhere the tree names it: its
	definition and every reference in objects/ and maps.ns (not a slot's
	tag, a symbol or a function's source, which do not name definitions),
	the same("...") paths that start at it, layout.tsv's paths and alias
	lines, and magic.tsv's entry for it.  ==> what was changed, as lines."""
	if not IDENT.match(new) or new in RESERVED or new in ("true", "func", "compile"):
		raise ValueError("%s is not a name a definition can have" % new)
	names = TreeNames(tree)
	if new in names.constants:
		raise ValueError("%s is a constant's name (constants.tsv)" % new)
	for path in tree_files(tree):
		with open(path, encoding="utf-8") as f:
			if re.search(r"^%s :=" % re.escape(new), f.read(), re.M | re.I):
				raise ValueError("%s: %s is defined already (a name is the same whatever its case)" % (path, new))
	# (the object of a magic pointer of this tree's table: @old, written in
	# this tree and in the packages' trees under it, goes by the new name too)
	magic = names.magic_path == os.path.join(os.path.abspath(tree), "magic.tsv") and old.lower() in names.magic
	files = tree_files(tree)
	if magic and os.path.isdir(os.path.join(tree, "rex")):
		for top, dirs, found_files in sorted(os.walk(os.path.join(tree, "rex"))):
			if "layout.tsv" in found_files and "objects" in dirs:
				files += [(p, False) for p in tree_files(top)]
	found, report = False, []
	for entry in files:
		path, own = entry if isinstance(entry, tuple) else (entry, True)
		if not os.path.exists(path):
			continue
		with open(path, encoding="utf-8") as f:
			text = f.read()
		out, last, count = [], 0, 0

		def put(start, end, what):
			nonlocal last, count
			out.append(text[last:start] + what)
			last = end
			count += 1

		def at_magic(code, offset):
			for kind, start, end in code_tokens(code):
				if kind == "magic" and code[start + 1:end].lower() == old.lower():
					put(offset + start, offset + end, "@" + new)
		# the name's tokens outside functions' source (Reader's tokens, with
		# their places), each a definition or a reference unless a tag or
		# symbol; in a function's source, only @old
		pos, prev = 0, None
		while pos < len(text):
			if Reader.CODE_START.match(text, pos) or Reader.COMPILE_START.match(text, pos):
				r = Reader("", path, tree)
				end = r.compile_args(text, pos) if Reader.COMPILE_START.match(text, pos) else r.code(text, pos)
				if magic:
					at_magic(text[pos:end], pos)
				pos, prev = end, None
				continue
			m = Reader.TOKEN.match(text, pos)
			if not m:
				raise SyntaxError("%s: cannot read at %r" % (path, text[pos:pos + 30]))
			pos = m.end()
			if m.lastgroup == "space":
				continue
			if m.lastgroup == "magic" and magic and m.group()[1:].lower() == old.lower():
				put(m.start(), m.end(), "@" + new)
			elif not own:
				pass
			elif m.lastgroup == "string" and prev == "(":
				# (same("path"): its path's first name)
				before = text[max(0, m.start() - 40):m.start()]
				if re.search(r"same\(\s*$", before) and re.match(r'"%s(?=[."\[^])' % re.escape(old), m.group()):
					put(m.start() + 1, m.start() + 1 + len(old), new)
			elif m.lastgroup in ("name", "quoted") and prev != "'" and unquote_name(m.group()) == old:
				following = Reader.SPACE.match(text, pos).end()
				if text.startswith(":=", following) or not text.startswith(":", following):
					put(m.start(), m.end(), new)
					found = found or text.startswith(":=", following)
			prev = m.group()
		if count:
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write("".join(out) + text[last:])
			report.append("%s: %d" % (os.path.relpath(path, tree), count))
	if not found:
		raise ValueError("%s: no definition of %s" % (tree, old))
	for name in ("layout.tsv", "magic.tsv"):
		path = os.path.join(tree, name)
		if not os.path.exists(path):
			continue
		with open(path, encoding="utf-8") as f:
			lines = f.read().split("\n")
		count = 0
		for k, line in enumerate(lines):
			fields = line.split("\t")
			for i in ((1, 2) if fields[0] == "alias" else (1,)):
				if i < len(fields) and re.match(r"%s(?=$|[.\[^])" % re.escape(old), fields[i]):
					fields[i] = new + fields[i][len(old):]
					count += 1
			lines[k] = "\t".join(fields)
		if count:
			with open(path, "w", encoding="utf-8", newline="\n") as f:
				f.write("\n".join(lines))
			report.append("%s: %d" % (name, count))
	return report


def unquote_name(text):
	return re.sub(r"\\(.)", r"\1", text[1:-1]) if text.startswith("|") else text


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
	b.add_argument("--original", action="store_true",
				   help="leave out what was added to the ROM extension (rex.tsv's `-` pieces), to compare the rest with the ROM")
	b.add_argument("--git-ref", metavar="REF",
				   help="build the tree as it was at that git commit or tag (git archive into a temporary directory), "
						"not as it is in the working tree - the ctest host.ROMSourceCommitted builds the tag romsrc-rom")
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
	d = sub.add_parser("romdata", help="write the ROM's other data (ROMDATA) into an existing tree, as extract does")
	d.add_argument("build_dir")
	d.add_argument("tree")
	k = sub.add_parser("package", help="a package file as text - its frames parts as trees, its protocol "
					   "parts and native code as ARM source - and back (tools/armpart/newtonpkg.py)")
	k.add_argument("action", choices=("unpack", "pack"))
	k.add_argument("input", help="unpack: the package file; pack: the directory")
	k.add_argument("output", help="unpack: the directory (made afresh); pack: the package file")
	k.add_argument("--newtonscript", required=True, help="the host's newtonscript, which compiles the functions")
	k.add_argument("--rom", metavar="BUILD_DIR", help="unpack: the extracted ROM, whose names the decompiler and "
					   "the ARM disassembler use (required to unpack)")
	k.add_argument("--patches", metavar="TSV", help="unpack: apply a <package>.patches.tsv first (its lines then "
					   "are what the source says)")
	n = sub.add_parser("rename", help="rename a definition everywhere the tree names it (its references, "
						"same() paths, layout.tsv, magic.tsv)")
	n.add_argument("tree")
	n.add_argument("old")
	n.add_argument("new")
	s = sub.add_parser("stamp", help="print the builder's stamp, which an object file carries")
	s.add_argument("--header", help="write it as a C header (kObjectsStamp) instead")
	a = ap.parse_args(argv)
	if a.command == "rename":
		for line in rename_definition(a.tree, a.old, a.new):
			print(line)
		return 0
	if a.command == "stamp":
		if a.header is None:
			print(builder_stamp())
			return 0
		text = ("/* generated by tools/newton-rom/analysis/romsrc.py stamp: the builder's stamp,\n"
				"   which an object file must carry for this program to boot it */\n"
				"static const char kObjectsStamp[] = \"%s\";\n" % builder_stamp())
		os.makedirs(os.path.dirname(os.path.abspath(a.header)), exist_ok=True)
		# (left alone when it says the same, so that an edit to another of
		# the tools does not make the programs that include it again)
		if os.path.exists(a.header):
			with open(a.header, encoding="utf-8") as f:
				if f.read() == text:
					return 0
		with open(a.header, "w", encoding="utf-8", newline="\n") as f:
			f.write(text)
		return 0
	if a.command == "package":
		trees = PackageTrees(os.path.abspath(a.newtonscript), a.rom)
		if a.action == "pack":
			data = newtonpkg.pack(a.input, trees=trees)
			with open(a.output, "wb") as f:
				f.write(data)
			print("%s -> %s, %d bytes" % (a.input, a.output, len(data)))
			return 0
		if a.rom is None:
			ap.error("package unpack needs --rom")
		with open(a.input, "rb") as f:
			data = f.read()
		if a.patches:
			data = bytes(Builder.patched(data, a.patches))
		newtonpkg.unpack(data, a.output, newtonarm.RomNames(a.rom), 0, trees)
		print("%s -> %s" % (a.input, a.output))
		return 0
	if a.command == "romdata":
		write_romdata(nf.ROM(a.build_dir), a.tree, a.build_dir)
		print("romdata.tsv and romdata/ written into %s" % a.tree)
		return 0
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
		files = tree_files(a.output)[1:]
		# a frame's slots put in another order, and an element put at the
		# front of an array (every later one's path moves along): the layout
		# is not touched
		magic = {}
		with open(os.path.join(a.output, "magic.tsv"), encoding="utf-8") as f:
			for line in f:
				fields = line.rstrip("\n").split("\t")
				if fields[0].isdigit():
					magic[int(fields[0])] = fields[1]
		for path in files:
			name = os.path.relpath(path, a.output)
			with open(path, encoding="utf-8") as f:
				text = f.read()
			changed = False
			# (a definition's slots on one line or a line each)
			m = re.search(r"\n%s := \{(\s*)([^:\s]+): (\"[^\"]*\"),(\s*)([^:\s]+): (\"[^\"]*\")," % re.escape(magic.get(271, "-")), text)
			if m:
				text = text[:m.start()] + "\n%s := {%s%s: %s,%s%s: %s," % (magic[271], m.group(1), m.group(5), m.group(6),
																		 m.group(4), m.group(2), m.group(3)) + text[m.end():]
				print("edited @271 (%s) in %s: its first two slots swapped" % (magic[271], name))
				changed = True
			m = re.search(r"\n%s := \[\|Array\|:\s*" % re.escape(magic.get(113, "-")), text)
			if m:
				text = text[:m.end()] + '"edit-test", ' + text[m.end():]
				print("edited @113 (%s) in %s: an element put at its front" % (magic[113], name))
				changed = True
			if changed:
				with open(path, "w", encoding="utf-8", newline="\n") as f:
					f.write(text)
		# and a slot added to a frame, holding a new frame: a map made for
		# each, and symbols for the new names (the canonical ink shape: its
		# R-constant's name, or the name the tree's natural form gives it)
		for path in files:
			name = os.path.relpath(path, a.output)
			with open(path, encoding="utf-8") as f:
				text = f.read()
			m = re.search(r"\n(Rcanonicalinkshape|canonicalInkShape) := \{", text)
			if m:
				text = text[:m.end()] + 'romsrcEdited: {romsrcNote: "added by edit-test"}, ' + text[m.end():]
				with open(path, "w", encoding="utf-8", newline="\n") as f:
					f.write(text)
				print("edited %s in %s: a slot romsrcEdited added, holding a new frame" % (m.group(1), name))
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
	if a.git_ref:
		import tempfile
		with tempfile.TemporaryDirectory(prefix="romsrc-") as scratch:
			tree = tree_at_ref(a.source, a.git_ref, scratch)
			print("the tree at %s" % a.git_ref)
			return build_command(a, tree)
	return build_command(a, a.source)


def tree_at_ref(source, ref, scratch):
	"""The tree `source` (a directory in a git working tree) as it was at
	the commit or tag `ref`, written under `scratch` by git archive.
	==> its path there."""
	import tarfile
	source = os.path.abspath(source)
	top = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=source, check=True,
						 capture_output=True, text=True).stdout.strip()
	rel = os.path.relpath(source, top).replace(os.sep, "/")
	archive = os.path.join(scratch, "tree.tar")
	with open(archive, "wb") as f:
		subprocess.run(["git", "archive", "--format=tar", ref, rel], cwd=top, check=True, stdout=f)
	with tarfile.open(archive) as t:
		t.extractall(scratch)
	os.remove(archive)
	return os.path.join(scratch, rel)


def build_command(a, source):
	builder = Builder(source, a.newtonscript, a.relayout, original=a.original)
	base, area = builder.build()
	if a.output:
		with open(a.output, "wb") as f:
			f.write(container(builder, base, area))
	print("built %d objects, %#x bytes at %#x%s" % (len(builder.by_path), len(area), base,
		  ", %d of them moved" % len(builder.relocations) if a.relayout else ""))
	if a.check:
		rom = nf.ROM(a.check)
		result = check(base, area, rom, None) | check_magic(builder, rom) | check_blocks(builder, rom)
		print("the object area, magic pointers and %d blocks of ROM data (the lexicons, other data, the extension) are %s"
			  % (len(builder.blocks), "identical to the ROM's" if result == 0 else "NOT the ROM's"))
		return result
	return 0


if __name__ == "__main__":
	sys.exit(main())
