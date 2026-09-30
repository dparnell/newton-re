#!/usr/bin/env python3
"""The Newton's fonts - sfnt containers of Apple bitmap and metric tables -
as editable text and back, byte for byte.  Pure Python (stdlib only).

The fonts the MessagePad ROM carries ('sfnt binaries in the font family
frames) are not TrueType fonts: they hold no outlines.  Five are bitmap
fonts - Apple's 'bloc'/'bdat' strikes, the forerunners of OpenType's
EBLC/EBDT - and eight are metric-only fonts for the printer faces.  What
each table holds and how the ROM reads it is docs/qd/fonts-sfnt.md.

    python tools/fonts/newtonsfnt.py unpack FONT.sfnt DIR
        writes DIR/: sfnt.txt (the table directory), one text file per
        table, and one BDF file per bitmap strike; then packs DIR again and
        refuses (exit 1) unless that gives back FONT.sfnt's bytes exactly.
    python tools/fonts/newtonsfnt.py pack DIR FONT.sfnt
        builds the font from DIR.
    python tools/fonts/newtonsfnt.py check FONT.sfnt...
        whether each font round-trips through the text form (and which
        tables had to fall back to hex).

The text form (tools/fonts/README.md has the details):

    sfnt.txt    the sfnt version and the tables in directory order, each
                with its file and its checksum ("auto": the one computed)
    head.txt, hhea.txt, maxp.txt, post.txt, hsty.txt
                `field value` lines, with comments saying what each is
    hmtx.tsv    glyph, advance width, left side bearing
    cmap.tsv    each subtable's platform/encoding/format/language, then
                one `code glyph` line per character mapped
    name.tsv    platform, encoding, language, name id, the string
    bloc.txt    the 'bloc' and 'bdat' versions and the strikes' BDF files,
                in order; strikeN-PPEM.bdf is one strike: the per-strike
                metrics as BLOC_* properties, each glyph's small metrics as
                DWIDTH (the advance) and BBX, its rows as BITMAP
    TAG.hex     any table the decoder does not reproduce exactly: its bytes
                in hex, 16 to a line

As a module: unpack(data, dir) -> list of tables left as hex, pack(dir) ->
bytes, roundtrips(data) -> (bool, hex tables).
"""

from __future__ import annotations

import os
import re
import shutil
import struct
import sys
import tempfile
import unicodedata

HEAD_ADJUST_MAGIC = 0xB1B0AFBA


# ---------------------------------------------------------------------------
# small helpers

def checksum(data: bytes) -> int:
	data = data + b"\0" * (-len(data) % 4)
	return sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xffffffff


def safe_name(tag: str) -> str:
	return re.sub(r"[^A-Za-z0-9]", "_", tag)


def strip_comment(line: str) -> str:
	i = line.find("#")
	return (line if i < 0 else line[:i]).strip()


def number(text: str) -> int:
	return int(text, 0)


def hex_text(data: bytes, what: str) -> str:
	lines = ["# %s: %d bytes, in hex (16 to a line)" % (what, len(data))]
	for i in range(0, len(data), 16):
		lines.append(" ".join("%02x" % b for b in data[i:i + 16]))
	return "\n".join(lines) + "\n"


def hex_bytes(text: str) -> bytes:
	return bytes.fromhex("".join(strip_comment(l) for l in text.splitlines()))


def read_text(path: str) -> str:
	with open(path, encoding="utf-8") as f:
		return f.read()


def write_text(path: str, text: str):
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)


# ---------------------------------------------------------------------------
# tables of fixed fields: `name value` lines

# (name, struct code, shown in hex, comment)
HEAD_FIELDS = [
	("version", "I", True, "16.16: 1.0"),
	("fontRevision", "I", True, "16.16"),
	("checkSumAdjustment", "I", True, "auto: 0xB1B0AFBA less the whole font's checksum (with this field nought)"),
	("magicNumber", "I", True, "0x5F0F3CF5"),
	("flags", "H", True, ""),
	("unitsPerEm", "H", False, "the design units the metric-only fonts' 'hmtx'/'hhea' are in (SetupWidthsFont scales them to the size)"),
	("created", "Q", True, "seconds since 1904 (the ROM's fonts have one 32-bit date in both halves)"),
	("modified", "Q", True, ""),
	("xMin", "h", False, ""),
	("yMin", "h", False, ""),
	("xMax", "h", False, ""),
	("yMax", "h", False, ""),
	("macStyle", "H", True, "the faces the data already has (bold 1, italic 2); SetupWidthsFont takes them off the ones to synthesise"),
	("lowestRecPPEM", "H", False, ""),
	("fontDirectionHint", "h", False, ""),
	("indexToLocFormat", "h", False, ""),
	("glyphDataFormat", "h", False, ""),
]

HHEA_FIELDS = [
	("version", "I", True, "16.16: 1.0"),
	("ascender", "h", False, "design units (SetupWidthsFont: the ascent)"),
	("descender", "h", False, "design units (SetupWidthsFont: minus the descent)"),
	("lineGap", "h", False, "design units (SetupWidthsFont: the leading)"),
	("advanceWidthMax", "H", False, "design units (SetupWidthsFont: widMax)"),
	("minLeftSideBearing", "h", False, ""),
	("minRightSideBearing", "h", False, ""),
	("xMaxExtent", "h", False, ""),
	("caretSlopeRise", "h", False, ""),
	("caretSlopeRun", "h", False, ""),
	("caretOffset", "h", False, ""),
	("reserved1", "h", False, ""),
	("reserved2", "h", False, ""),
	("reserved3", "h", False, ""),
	("reserved4", "h", False, ""),
	("metricDataFormat", "h", False, ""),
	("numberOfHMetrics", "H", False, "the 'hmtx' entries with an advance (SetupWidthsFont: a glyph past them takes the last)"),
]

MAXP_FIELDS = [
	("version", "I", True, "16.16: 1.0"),
	("numGlyphs", "H", False, ""),
	("maxPoints", "H", False, ""),
	("maxContours", "H", False, ""),
	("maxCompositePoints", "H", False, ""),
	("maxCompositeContours", "H", False, ""),
	("maxZones", "H", False, ""),
	("maxTwilightPoints", "H", False, ""),
	("maxStorage", "H", False, ""),
	("maxFunctionDefs", "H", False, ""),
	("maxInstructionDefs", "H", False, ""),
	("maxStackElements", "H", False, ""),
	("maxSizeOfInstructions", "H", False, ""),
	("maxComponentElements", "H", False, ""),
	("maxComponentDepth", "H", False, ""),
]

POST_FIELDS = [
	("version", "I", True, "16.16: 3.0, no glyph names"),
	("italicAngle", "I", True, "16.16 degrees"),
	("underlinePosition", "h", False, "design units"),
	("underlineThickness", "h", False, "design units"),
	("isFixedPitch", "I", False, ""),
	("minMemType42", "I", False, ""),
	("maxMemType42", "I", False, ""),
	("minMemType1", "I", False, ""),
	("maxMemType1", "I", False, ""),
]

HSTY_FIELDS = [
	("version", "I", True, "16.16: 1.0"),
	("plain", "h", False, "design units added to every advance when no face is left to synthesise"),
	("bold", "h", False, "face bit 0: added when bold is synthesised"),
	("italic", "h", False, "face bit 1"),
	("underline", "h", False, "face bit 2"),
	("outline", "h", False, "face bit 3"),
	("shadow", "h", False, "face bit 4 (never read: FindSFNT keeps only face bits 0-3, 7 and 8)"),
	("condense", "h", False, "face bit 5 (never read)"),
	("extend", "h", False, "face bit 6 (never read)"),
	("superscript", "h", False, "face bit 7 (the Newton's superscript; subscript, bit 8, reads the two bytes after the table)"),
]

FIELD_TABLES = {
	"head": (HEAD_FIELDS, "the font header"),
	"hhea": (HHEA_FIELDS, "the horizontal header"),
	"maxp": (MAXP_FIELDS, "the maximum profile"),
	"post": (POST_FIELDS, "the PostScript table"),
	"hsty": (HSTY_FIELDS, "Apple's horizontal style table: how much wider each synthesised face makes a glyph (SetupWidthsFont, ROM 0x000ae388)"),
}


def fields_size(fields):
	return struct.calcsize(">" + "".join(f[1] for f in fields))


def fields_text(tag, data, auto=None):
	fields, what = FIELD_TABLES[tag]
	values = struct.unpack(">" + "".join(f[1] for f in fields), data)
	lines = ["# '%s': %s" % (tag, what)]
	for (name, code, hexed, comment), v in zip(fields, values):
		if auto and name in auto and auto[name] == v:
			text = "auto"
		elif hexed:
			text = "0x%0*x" % (struct.calcsize(code) * 2, v)
		else:
			text = str(v)
		lines.append("%-22s %-20s%s" % (name, text, ("# " + comment) if comment else ""))
	return "\n".join(lines) + "\n"


def fields_values(tag, text):
	fields, _ = FIELD_TABLES[tag]
	got = {}
	for line in text.splitlines():
		line = strip_comment(line)
		if not line:
			continue
		name, value = line.split(None, 1)
		got[name] = value.strip()
	values = []
	for name, code, _, _ in fields:
		if name not in got:
			raise ValueError("'%s': no %s" % (tag, name))
		values.append(got[name])
	return values


def fields_bytes(tag, values):
	fields, _ = FIELD_TABLES[tag]
	return struct.pack(">" + "".join(f[1] for f in fields), *[number(v) for v in values])


# ---------------------------------------------------------------------------
# hmtx

def hmtx_text(data, num_hmetrics):
	if num_hmetrics is None or num_hmetrics * 4 > len(data) or (len(data) - num_hmetrics * 4) % 2:
		return None
	lines = ["# 'hmtx': glyph, advance width, left side bearing (design units);",
			 "# a '-' advance: a glyph past 'hhea' numberOfHMetrics, which has only a bearing",
			 "# glyph\tadvance\tlsb"]
	for g in range(num_hmetrics):
		adv, lsb = struct.unpack_from(">Hh", data, g * 4)
		lines.append("%d\t%d\t%d" % (g, adv, lsb))
	for i, off in enumerate(range(num_hmetrics * 4, len(data), 2)):
		lines.append("%d\t-\t%d" % (num_hmetrics + i, struct.unpack_from(">h", data, off)[0]))
	return "\n".join(lines) + "\n"


def hmtx_bytes(text):
	out = bytearray()
	for line in text.splitlines():
		line = strip_comment(line)
		if not line:
			continue
		_, adv, lsb = line.split()
		out += struct.pack(">h", int(lsb)) if adv == "-" else struct.pack(">Hh", int(adv), int(lsb))
	return bytes(out)


# ---------------------------------------------------------------------------
# cmap: format 4 subtables as `code glyph` lines

def cmap4_decode(sub):
	fmt, length, language, segx2 = struct.unpack_from(">HHHH", sub)
	n = segx2 // 2
	ends = struct.unpack_from(">%dH" % n, sub, 14)
	starts = struct.unpack_from(">%dH" % n, sub, 16 + 2 * n)
	deltas = struct.unpack_from(">%dh" % n, sub, 16 + 4 * n)
	ranges = struct.unpack_from(">%dH" % n, sub, 16 + 6 * n)
	if any(ranges):
		return None
	mapping = []
	for s, e, d in zip(starts, ends, deltas):
		if s == e == 0xffff:
			continue
		for c in range(s, e + 1):
			mapping.append((c, (c + d) & 0xffff))
	return language, mapping


def cmap4_encode(language, mapping):
	"""The subtable as the ROM's fonts have it: one segment per maximal run
	of consecutive codes with the same delta (idRangeOffset always
	nought), then the 0xFFFF segment."""
	segs = []
	for c, g in sorted(mapping):
		d = (g - c) & 0xffff
		if segs and segs[-1][1] == c - 1 and segs[-1][2] == d:
			segs[-1][1] = c
		else:
			segs.append([c, c, d])
	segs.append([0xffff, 0xffff, 1])
	n = len(segs)
	p = 1
	while p * 2 <= n:
		p *= 2
	search = 2 * p
	selector = p.bit_length() - 1
	body = struct.pack(">HHHH", n * 2, search, selector, n * 2 - search)
	body += struct.pack(">%dH" % n, *[s[1] for s in segs]) + b"\0\0"
	body += struct.pack(">%dH" % n, *[s[0] for s in segs])
	body += struct.pack(">%dH" % n, *[s[2] for s in segs])
	body += struct.pack(">%dH" % n, *([0] * n))
	return struct.pack(">HHH", 4, 6 + len(body), language) + body


def char_comment(c):
	try:
		ch = chr(c)
		name = unicodedata.name(ch, "")
	except ValueError:
		return ""
	shown = ch if ch.isprintable() and not ch.isspace() else ""
	return ("# %s %s" % (shown, name)).rstrip() if (shown or name) else ""


def cmap_text(data):
	version, count = struct.unpack_from(">HH", data)
	records = [struct.unpack_from(">HHI", data, 4 + 8 * i) for i in range(count)]
	lines = ["# 'cmap': the character-to-glyph maps.  FindSFNT (ROM 0x000aec68) takes the",
			 "# subtable whose platform id is the font family's `encoding` slot (0 for all",
			 "# the ROM's families).  Each subtable is a `subtable platform encoding format",
			 "# language` line, then one `code glyph` line per character (the code in hex);",
			 "# the builder writes format 4 as the ROM's fonts have it (a segment per run",
			 "# of codes with the same glyph delta, no glyph id array, then 0xFFFF).",
			 "version %d" % version]
	offset = 4 + 8 * count
	for platform, encoding, sub_off in records:
		if sub_off != offset or struct.unpack_from(">H", data, sub_off)[0] != 4:
			return None
		length = struct.unpack_from(">H", data, sub_off + 2)[0]
		decoded = cmap4_decode(data[sub_off:sub_off + length])
		if decoded is None:
			return None
		language, mapping = decoded
		lines.append("subtable %d %d 4 %d" % (platform, encoding, language))
		for c, g in mapping:
			lines.append(("0x%04X\t%d\t%s" % (c, g, char_comment(c))).rstrip())
		offset += length
	if offset != len(data):
		return None
	return "\n".join(lines) + "\n"


def cmap_bytes(text):
	version = 0
	subs = []
	for line in text.splitlines():
		line = strip_comment(line)
		if not line:
			continue
		words = line.split()
		if words[0] == "version":
			version = number(words[1])
		elif words[0] == "subtable":
			platform, encoding, fmt, language = (number(w) for w in words[1:5])
			if fmt != 4:
				raise ValueError("'cmap': only format 4 subtables are written, not %d" % fmt)
			subs.append((platform, encoding, language, []))
		else:
			subs[-1][3].append((number(words[0]), number(words[1])))
	head = struct.pack(">HH", version, len(subs))
	offset = 4 + 8 * len(subs)
	bodies = b""
	for platform, encoding, language, mapping in subs:
		body = cmap4_encode(language, mapping)
		head += struct.pack(">HHI", platform, encoding, offset)
		offset += len(body)
		bodies += body
	return head + bodies


# ---------------------------------------------------------------------------
# name

def name_codec(platform, encoding):
	if platform == 1 and encoding == 0:
		return "mac_roman"
	if platform in (0, 3):
		return "utf-16-be"
	return None


def escape(s):
	out = []
	for ch in s:
		if ch == "\\":
			out.append("\\\\")
		elif ch == "\t":
			out.append("\\t")
		elif ch == "\n":
			out.append("\\n")
		elif ch == "\r":
			out.append("\\r")
		elif not ch.isprintable():
			out.append("\\u%04x" % ord(ch))
		else:
			out.append(ch)
	return "".join(out)


def unescape(s):
	return re.sub(r"\\(u[0-9a-fA-F]{4}|.)",
				  lambda m: {"\\": "\\", "t": "\t", "n": "\n", "r": "\r"}.get(m.group(1)) or chr(int(m.group(1)[1:], 16)), s)


def name_text(data):
	fmt, count, storage = struct.unpack_from(">HHH", data)
	if fmt != 0 or storage != 6 + 12 * count:
		return None
	lines = ["# 'name': platform, encoding, language, name id, then the string (tab-separated;",
			 "# Mac Roman for platform 1 encoding 0, UTF-16 for platforms 0 and 3; \\t \\n \\\\",
			 "# and \\uXXXX escapes).  Name ids: 0 copyright, 1 family, 2 subfamily, 3 unique",
			 "# id, 4 full name, 5 version, 6 PostScript name.  The strings are stored in",
			 "# this order, one after the other."]
	at = 0
	for i in range(count):
		platform, encoding, language, name_id, length, offset = struct.unpack_from(">6H", data, 6 + 12 * i)
		codec = name_codec(platform, encoding)
		if offset != at or codec is None:
			return None
		raw = data[storage + offset:storage + offset + length]
		s = raw.decode(codec, "surrogatepass")
		if s.encode(codec, "surrogatepass") != raw:
			return None
		lines.append("%d\t%d\t%d\t%d\t%s" % (platform, encoding, language, name_id, escape(s)))
		at += length
	if storage + at != len(data):
		return None
	return "\n".join(lines) + "\n"


def name_bytes(text):
	records = []
	for line in text.splitlines():
		if not line.strip() or line.lstrip().startswith("#"):
			continue
		platform, encoding, language, name_id, s = line.split("\t", 4)
		records.append((int(platform), int(encoding), int(language), int(name_id),
						unescape(s).encode(name_codec(int(platform), int(encoding)), "surrogatepass")))
	head = struct.pack(">HHH", 0, len(records), 6 + 12 * len(records))
	storage = b""
	for platform, encoding, language, name_id, raw in records:
		head += struct.pack(">6H", platform, encoding, language, name_id, len(raw), len(storage))
		storage += raw
	return head + storage


# ---------------------------------------------------------------------------
# bloc and bdat: the strikes, as BDF files

# a bitmapSizeTable (0x30 bytes); the first four words are the index
# subtables' whereabouts, which the builder works out
LINE_METRICS = ["ASCENDER", "DESCENDER", "WIDTH_MAX", "CARET_SLOPE_NUMERATOR", "CARET_SLOPE_DENOMINATOR",
				"CARET_OFFSET", "MIN_ORIGIN_SB", "MIN_ADVANCE_SB", "MAX_BEFORE_BL", "MIN_AFTER_BL", "PAD1", "PAD2"]
STRIKE_PROPS = (["BLOC_COLOR_REF"] + ["BLOC_HORI_" + m for m in LINE_METRICS] + ["BLOC_VERT_" + m for m in LINE_METRICS]
				+ ["BLOC_START_GLYPH", "BLOC_END_GLYPH", "BLOC_PPEM_X", "BLOC_PPEM_Y", "BLOC_BIT_DEPTH", "BLOC_FLAGS"])
STRIKE_FORMAT = ">I12b12bHHBBBb"
STRIKE_SIZE = 0x30


class Glyph:
	def __init__(self, gid, height, width, bearing_x, bearing_y, advance, bits):
		self.gid, self.height, self.width = gid, height, width
		self.bearing_x, self.bearing_y, self.advance, self.bits = bearing_x, bearing_y, advance, bits


class Strike:
	def __init__(self):
		self.props = {}			# BLOC_* -> int
		self.subtables = []		# (first, last, index format, image format)
		self.glyphs = {}		# gid -> Glyph (only glyphs with data)
		self.info = {}			# informative properties (read, not used)


def small_glyph(gid, bdat, at):
	"""The format 1 image at bdat[at]: small metrics, then byte-aligned rows
	(its length is what its metrics say, as the ROM reads it)."""
	if at + 5 > len(bdat):
		return None
	height, width, bx, by, adv = struct.unpack_from(">BBbbB", bdat, at)
	size = height * ((width + 7) >> 3)
	if at + 5 + size > len(bdat):
		return None
	return Glyph(gid, height, width, bx, by, adv, bdat[at + 5:at + 5 + size])


def decode_strikes(bloc, bdat):
	"""The strikes, or None when the tables are laid out in a way the
	builder would not reproduce (checked afterwards by the round trip).

	Each subtable's images are expected one after the other in glyph id
	order, their lengths what their metrics say.  A glyph id whose offset
	is nought (other than the subtable's first) has no image of its own: it
	points at the first glyph's, as the ids past 'maxp' numGlyphs do in the
	ROM's fonts.  The end offset (after the last glyph) is either the end
	of the images or, in the ROM's fonts, nought too."""
	version, count = struct.unpack_from(">II", bloc)
	strikes = []
	for i in range(count):
		fields = struct.unpack_from(">III" + STRIKE_FORMAT[1:], bloc, 8 + STRIKE_SIZE * i)
		array_off, tables_size, num_subtables = fields[:3]
		s = Strike()
		s.props = dict(zip(STRIKE_PROPS, fields[3:]))
		for j in range(num_subtables):
			first, last, add = struct.unpack_from(">HHI", bloc, array_off + 8 * j)
			sub = array_off + add
			index_format, image_format, image_off = struct.unpack_from(">HHI", bloc, sub)
			if image_format != 1 or index_format not in (1, 3):
				return None
			n = last - first + 2
			if index_format == 1:
				offsets = struct.unpack_from(">%dI" % n, bloc, sub + 8)
			else:
				offsets = struct.unpack_from(">%dH" % n, bloc, sub + 8)
			at = 0
			for k in range(n - 1):
				if k > 0 and offsets[k] == 0:
					continue
				if offsets[k] != at:
					return None
				g = small_glyph(first + k, bdat, image_off + at)
				if g is None:
					return None
				s.glyphs[first + k] = g
				at += 5 + len(g.bits)
			if offsets[-1] == at:
				end_written = 1
			elif offsets[-1] == 0:
				end_written = 0
			else:
				return None
			s.subtables.append((first, last, index_format, image_format, end_written))
		strikes.append(s)
	return version, strikes


def encode_strikes(bloc_version, bdat_version, strikes):
	bdat = bytearray(struct.pack(">I", bdat_version))
	arrays = []
	for s in strikes:
		array = bytearray()
		subs = bytearray()
		n_subs = len(s.subtables)
		for first, last, index_format, image_format, end_written in s.subtables:
			if image_format != 1 or index_format not in (1, 3):
				raise ValueError("strike %d: only index formats 1 and 3 with image format 1 are written" % s.props["BLOC_PPEM_Y"])
			image_off = len(bdat)
			offsets = []
			for gid in range(first, last + 1):
				g = s.glyphs.get(gid)
				if g is None:
					offsets.append(0)			# the first glyph's image
					continue
				offsets.append(len(bdat) - image_off)
				bdat += struct.pack(">BBbbB", g.height, g.width, g.bearing_x, g.bearing_y, g.advance) + g.bits
			offsets.append(len(bdat) - image_off if end_written else 0)
			array += struct.pack(">HHI", first, last, 8 * n_subs + len(subs))
			sub = struct.pack(">HHI", index_format, image_format, image_off)
			if index_format == 1:
				sub += struct.pack(">%dI" % len(offsets), *offsets)
			else:
				if max(offsets) > 0xffff:
					raise ValueError("an index format 3 subtable cannot reach %d bytes of images" % max(offsets))
				sub += struct.pack(">%dH" % len(offsets), *offsets)
				sub += b"\0" * (-len(sub) % 4)
			subs += sub
		arrays.append((n_subs, bytes(array + subs)))
	bloc = bytearray(struct.pack(">II", bloc_version, len(strikes)))
	at = 8 + STRIKE_SIZE * len(strikes)
	for s, (n_subs, table) in zip(strikes, arrays):
		bloc += struct.pack(">III", at, len(table), n_subs)
		bloc += struct.pack(STRIKE_FORMAT, *[s.props[p] for p in STRIKE_PROPS])
		at += len(table)
	for _, table in arrays:
		bloc += table
	return bytes(bloc), bytes(bdat)


def strike_bdf(s, index, family, cmap_codes):
	ppem = s.props["BLOC_PPEM_Y"]
	glyphs = [s.glyphs[g] for g in sorted(s.glyphs)]
	# the font's bounding box (informative)
	if glyphs:
		x0 = min(g.bearing_x for g in glyphs)
		y0 = min(g.bearing_y - g.height for g in glyphs)
		x1 = max(g.bearing_x + g.width for g in glyphs)
		y1 = max(g.bearing_y for g in glyphs)
	else:
		x0 = y0 = x1 = y1 = 0
	out = ["STARTFONT 2.1",
		   "COMMENT Strike %d of a Newton 'sfnt' font (bloc/bdat): tools/fonts/newtonsfnt.py," % index,
		   "COMMENT tools/fonts/README.md.  The builder reads the BLOC_* properties and the",
		   "COMMENT glyphs - STARTCHAR gN (N the glyph id; otherwise ENCODING through 'cmap'),",
		   "COMMENT DWIDTH (the advance), BBX (width, height, the bearing x, the bottom row's",
		   "COMMENT y: bearing y less the height) and the BITMAP rows.  A glyph id in the",
		   "COMMENT subtable ranges with no glyph here has no image (the missing glyph is 0).",
		   "COMMENT The other properties, SIZE, FONTBOUNDINGBOX and SWIDTH are worked out and",
		   "COMMENT not read back: change BLOC_HORI_* (the line metrics the ROM draws text by)",
		   "COMMENT to match an edit that changes the strike's extent.",
		   "FONT -Apple-%s-Medium-R-Normal--%d-%d-72-72-P-0-Apple-Roman" % (family.replace("-", " "), ppem, ppem * 10),
		   "SIZE %d 72 72" % ppem,
		   "FONTBOUNDINGBOX %d %d %d %d" % (x1 - x0, y1 - y0, x0, y0)]
	props = [("FONT_ASCENT", str(s.props["BLOC_HORI_ASCENDER"])),
			 ("FONT_DESCENT", str(-s.props["BLOC_HORI_DESCENDER"])),
			 ("PIXEL_SIZE", str(ppem)),
			 ("FAMILY_NAME", '"%s"' % family.replace('"', '""'))]
	props += [(p, str(s.props[p])) for p in STRIKE_PROPS]
	props += [("BLOC_INDEX_SUBTABLES", str(len(s.subtables)))]
	for j, sub in enumerate(s.subtables):
		props.append(("BLOC_INDEX_SUBTABLE_%d" % j, '"%d %d %d %d %d"' % sub))
	out.append("STARTPROPERTIES %d" % len(props))
	out += ["%s %s" % p for p in props]
	out.append("ENDPROPERTIES")
	out.append("CHARS %d" % len(glyphs))
	for g in glyphs:
		codes = cmap_codes.get(g.gid)
		out.append("STARTCHAR g%d" % g.gid)
		out.append("ENCODING %d" % (codes[0] if codes else -1))
		out.append("SWIDTH %d 0" % (round(g.advance * 1000 / ppem) if ppem else 0))
		out.append("DWIDTH %d 0" % g.advance)
		out.append("BBX %d %d %d %d" % (g.width, g.height, g.bearing_x, g.bearing_y - g.height))
		out.append("BITMAP")
		rb = (g.width + 7) >> 3
		for r in range(g.height):
			out.append(g.bits[r * rb:(r + 1) * rb].hex().upper())
		out.append("ENDCHAR")
	out.append("ENDFONT")
	return "\n".join(out) + "\n"


def bdf_strike(text, code_to_gid):
	s = Strike()
	lines = text.splitlines()
	i = 0
	props = {}
	while i < len(lines):
		words = lines[i].split(None, 1)
		i += 1
		if not words:
			continue
		key = words[0]
		if key == "STARTPROPERTIES":
			while not lines[i].startswith("ENDPROPERTIES"):
				pw = lines[i].split(None, 1)
				if pw:
					props[pw[0]] = pw[1].strip() if len(pw) > 1 else ""
				i += 1
			i += 1
		elif key == "STARTCHAR":
			name = words[1].strip() if len(words) > 1 else ""
			enc = None
			adv = 0
			bbx = (0, 0, 0, 0)
			bits = b""
			while True:
				w = lines[i].split()
				i += 1
				if not w:
					continue
				if w[0] == "ENCODING":
					enc = int(w[1])
					if enc == -1 and len(w) > 2:
						enc = None
				elif w[0] == "DWIDTH":
					adv = int(w[1])
				elif w[0] == "BBX":
					bbx = tuple(int(v) for v in w[1:5])
				elif w[0] == "BITMAP":
					rows = []
					while not lines[i].strip().startswith("ENDCHAR"):
						if lines[i].strip():
							rows.append(lines[i].strip())
						i += 1
					width, height = bbx[0], bbx[1]
					rb = (width + 7) >> 3
					if len(rows) != height:
						raise ValueError("glyph %s: %d rows, BBX says %d" % (name, len(rows), height))
					data = bytearray()
					for r in rows:
						row = bytes.fromhex(r)
						if len(row) < rb:
							row += b"\0" * (rb - len(row))
						data += row[:rb]
					bits = bytes(data)
				elif w[0] == "ENDCHAR":
					break
			m = re.fullmatch(r"g(\d+)", name)
			if m:
				gid = int(m.group(1))
			elif enc is not None and enc in code_to_gid:
				gid = code_to_gid[enc]
			else:
				raise ValueError("glyph %s: no glyph id (name it gN, or give an ENCODING 'cmap' maps)" % name)
			width, height, bx, by0 = bbx
			s.glyphs[gid] = Glyph(gid, height, width, bx, by0 + height, adv, bits)
	for p in STRIKE_PROPS:
		if p not in props:
			raise ValueError("no %s property" % p)
		s.props[p] = int(props[p])
	for j in range(int(props.get("BLOC_INDEX_SUBTABLES", "0"))):
		s.subtables.append(tuple(int(v) for v in props["BLOC_INDEX_SUBTABLE_%d" % j].strip('"').split()))
	return s


# ---------------------------------------------------------------------------
# the font

def read_directory(data):
	version, count = struct.unpack_from(">IH", data)
	tables = []
	for i in range(count):
		tag, ck, off, length = struct.unpack_from(">4sIII", data, 12 + 16 * i)
		tables.append((tag.decode("latin-1"), ck, off, length))
	return version, tables


def table_checksum(tag, body):
	if tag == "head" and len(body) >= 12:
		body = body[:8] + b"\0\0\0\0" + body[12:]
	return checksum(body)


def assemble(version, tables):
	"""tables: [(tag, body, checksum or None for auto)] in directory order;
	laid out in that order, each padded to four bytes with nought."""
	n = len(tables)
	p = 1
	while p * 2 <= n:
		p *= 2
	out = bytearray(struct.pack(">IHHHH", version, n, p * 16, p.bit_length() - 1, n * 16 - p * 16))
	at = 12 + 16 * n
	bodies = bytearray()
	head_at = None
	for tag, body, ck in tables:
		if ck is None:
			ck = table_checksum(tag, body)
		out += struct.pack(">4sIII", tag.encode("latin-1"), ck, at, len(body))
		if tag == "head":
			head_at = at
		padded = body + b"\0" * (-len(body) % 4)
		bodies += padded
		at += len(padded)
	return out + bodies, head_at


def unpack(data: bytes, out_dir: str):
	"""Writes the font's text form into out_dir (made afresh).  ==> the
	tables left as hex."""
	version, directory = read_directory(data)
	if os.path.isdir(out_dir):
		shutil.rmtree(out_dir)
	os.makedirs(out_dir)
	bodies = {tag: data[off:off + length] for tag, _, off, length in directory}
	hexed = []
	files = {}
	family = "Newton"
	cmap_codes = {}				# gid -> [codes]
	if "cmap" in bodies:
		t = cmap_text(bodies["cmap"])
		if t is not None:
			for line in t.splitlines():
				w = strip_comment(line).split()
				if w and w[0].startswith("0x"):
					cmap_codes.setdefault(int(w[1]), []).append(int(w[0], 16))
	if "name" in bodies:
		t = name_text(bodies["name"])
		if t is not None:
			for line in t.splitlines():
				w = line.split("\t")
				if len(w) == 5 and w[3] == "4":
					family = unescape(w[4])
	num_hmetrics = struct.unpack_from(">H", bodies["hhea"], 34)[0] if len(bodies.get("hhea", b"")) == 36 else None

	def put(name, text):
		write_text(os.path.join(out_dir, name), text)
		return name

	strike_files = []
	if "bloc" in bodies and "bdat" in bodies:
		decoded = decode_strikes(bodies["bloc"], bodies["bdat"])
		if decoded is not None:
			bloc_version, strikes = decoded
			lines = ["# 'bloc' and 'bdat': the bitmap strikes, one BDF file each, in the order",
					 "# 'bloc' lists them (LocateEntry, ROM 0x000aebec, takes them to be in",
					 "# order of size).  'bdat' holds the glyphs' images strike after strike,",
					 "# each strike's glyphs in glyph id order.",
					 "bloc-version 0x%08x" % bloc_version,
					 "bdat-version 0x%08x" % struct.unpack_from(">I", bodies["bdat"])[0]]
			for k, s in enumerate(strikes):
				name = "strike%d-%d.bdf" % (k, s.props["BLOC_PPEM_Y"])
				put(name, strike_bdf(s, k, family, cmap_codes))
				lines.append("strike %s" % name)
				strike_files.append(name)
			files["bloc"] = files["bdat"] = put("bloc.txt", "\n".join(lines) + "\n")
	for tag, _, _, _ in directory:
		body = bodies[tag]
		made = None
		if tag in FIELD_TABLES and len(body) == fields_size(FIELD_TABLES[tag][0]):
			auto = None
			if tag == "head":
				zeroed = bytearray(data)
				hoff = [off for t, _, off, _ in directory if t == "head"][0]
				zeroed[hoff + 8:hoff + 12] = b"\0\0\0\0"
				auto = {"checkSumAdjustment": (HEAD_ADJUST_MAGIC - checksum(bytes(zeroed))) & 0xffffffff}
			made = put(tag + ".txt", fields_text(tag, body, auto))
		elif tag == "hmtx":
			t = hmtx_text(body, num_hmetrics)
			made = t and put("hmtx.tsv", t)
		elif tag == "cmap":
			t = cmap_text(body)
			made = t and put("cmap.tsv", t)
		elif tag == "name":
			t = name_text(body)
			made = t and put("name.tsv", t)
		if made:
			files[tag] = made

	# what the text form gives back, table by table: a table it does not
	# reproduce is kept as hex ('bloc' and 'bdat' go together)
	def check(tag):
		want = bodies[tag]
		if tag == "head" and len(want) >= 12:
			want = want[:8] + bytes(4) + want[12:]		# checkSumAdjustment: auto, or checked with the font
		try:
			return table_from_dir(out_dir, tag, files[tag]) == want
		except Exception:
			return False
	for tag, _, _, _ in directory:
		if files.get(tag) and not check(tag):
			for t in (("bloc", "bdat") if tag in ("bloc", "bdat") else (tag,)):
				files[t] = None
	if files.get("bloc"):
		keep = set(files.values()) | set(strike_files)
	else:
		keep = set(files.values())
	for f in os.listdir(out_dir):
		if f not in keep:
			os.remove(os.path.join(out_dir, f))
	for tag, _, _, _ in directory:
		if not files.get(tag):
			files[tag] = put(safe_name(tag) + ".hex", hex_text(bodies[tag], "'%s'" % tag))
			hexed.append(tag)
	lines = ["# A Newton font: an sfnt container (docs/qd/fonts-sfnt.md), unpacked by",
			 "# tools/fonts/newtonsfnt.py.  The tables in directory order: tag, the file",
			 "# holding it, its checksum (auto: the one worked out).  The builder lays them",
			 "# out in this order, each padded to four bytes with nought.",
			 "version 0x%08x" % version]
	for tag, ck, _, _ in directory:
		auto = ck == table_checksum(tag, bodies[tag])
		lines.append("table %s %s %s" % (tag.replace(" ", "\\s"), files[tag], "auto" if auto else "0x%08x" % ck))
	put("sfnt.txt", "\n".join(lines) + "\n")
	return hexed


def table_from_dir(d, tag, name):
	path = os.path.join(d, name)
	text = read_text(path)
	if name.endswith(".hex"):
		return hex_bytes(text)
	if tag in FIELD_TABLES:
		values = fields_values(tag, text)
		if tag == "head":
			values = ["0" if v == "auto" else v for v in values]
		return fields_bytes(tag, values)
	if tag == "hmtx":
		return hmtx_bytes(text)
	if tag == "cmap":
		return cmap_bytes(text)
	if tag == "name":
		return name_bytes(text)
	if tag in ("bloc", "bdat"):
		bloc, bdat = strikes_from_dir(d, text)
		return bloc if tag == "bloc" else bdat
	raise ValueError("no way to read %s from %s" % (tag, name))


def strikes_from_dir(d, text):
	code_to_gid = {}
	cmap_path = os.path.join(d, "cmap.tsv")
	if os.path.exists(cmap_path):
		for line in read_text(cmap_path).splitlines():
			w = strip_comment(line).split()
			if w and w[0].startswith("0x"):
				code_to_gid.setdefault(int(w[0], 16), int(w[1]))
	bloc_version = bdat_version = 0x00020000
	strikes = []
	for line in text.splitlines():
		w = strip_comment(line).split()
		if not w:
			continue
		if w[0] == "bloc-version":
			bloc_version = number(w[1])
		elif w[0] == "bdat-version":
			bdat_version = number(w[1])
		elif w[0] == "strike":
			strikes.append(bdf_strike(read_text(os.path.join(d, w[1])), code_to_gid))
	return encode_strikes(bloc_version, bdat_version, strikes)


def pack(d: str) -> bytes:
	version = 0x00010000
	entries = []
	for line in read_text(os.path.join(d, "sfnt.txt")).splitlines():
		w = strip_comment(line).split()
		if not w:
			continue
		if w[0] == "version":
			version = number(w[1])
		elif w[0] == "table":
			entries.append((w[1].replace("\\s", " "), w[2], None if w[3] == "auto" else number(w[3])))
	tables = [(tag, table_from_dir(d, tag, name), ck) for tag, name, ck in entries]
	font, head_at = assemble(version, tables)
	if head_at is not None:
		# the head's checkSumAdjustment: auto means worked out over the font
		head_name = [name for tag, name, _ in entries if tag == "head"][0]
		auto = head_name.endswith(".txt") and \
			dict(zip([f[0] for f in HEAD_FIELDS], fields_values("head", read_text(os.path.join(d, head_name)))))["checkSumAdjustment"] == "auto"
		if auto:
			font = bytearray(font)
			font[head_at + 8:head_at + 12] = b"\0\0\0\0"
			font[head_at + 8:head_at + 12] = struct.pack(">I", (HEAD_ADJUST_MAGIC - checksum(bytes(font))) & 0xffffffff)
			font = bytes(font)
	return font


def roundtrips(data: bytes, work: str = None):
	"""Whether the font comes back byte for byte through the text form, and
	the tables that fell back to hex."""
	tmp = tempfile.mkdtemp(dir=work)
	try:
		d = os.path.join(tmp, "font")
		hexed = unpack(data, d)
		return pack(d) == data, hexed
	finally:
		shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
	argv = sys.argv[1:] if argv is None else argv
	if len(argv) >= 1 and argv[0] == "unpack" and len(argv) == 3:
		with open(argv[1], "rb") as f:
			data = f.read()
		hexed = unpack(data, argv[2])
		if pack(argv[2]) != data:
			print("%s: the text form does not give the font back byte for byte" % argv[1], file=sys.stderr)
			return 1
		print("%s -> %s%s" % (argv[1], argv[2], (" (as hex: %s)" % ", ".join(hexed)) if hexed else ""))
		return 0
	if len(argv) >= 1 and argv[0] == "pack" and len(argv) == 3:
		data = pack(argv[1])
		with open(argv[2], "wb") as f:
			f.write(data)
		print("%s -> %s, %d bytes" % (argv[1], argv[2], len(data)))
		return 0
	if len(argv) >= 2 and argv[0] == "check":
		bad = 0
		for path in argv[1:]:
			with open(path, "rb") as f:
				data = f.read()
			ok, hexed = roundtrips(data)
			bad += not ok
			print("%s: %s%s" % (path, "identical" if ok else "DIFFERS",
								(" (as hex: %s)" % ", ".join(hexed)) if hexed else ""))
		return 1 if bad else 0
	print(__doc__, file=sys.stderr)
	return 2


if __name__ == "__main__":
	sys.exit(main())
