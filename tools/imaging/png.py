#!/usr/bin/env python3
"""Grayscale PNG files, written and read in pure Python (zlib + struct).

What the ROM-free track's extraction (tools/newton-rom/analysis/romsrc.py)
turns the ROM's bitmaps into, and reads back after a person has edited
them:

    write_gray(path, width, height, rows, depth)
        rows: `height` lists of `width` gray levels, 0 (black) up to
        2**depth - 1 (white); written as a grayscale PNG of that bit depth
        (1, 2, 4 or 8), so that the file holds exactly those levels.
    read_gray(path, depth) -> (width, height, rows)
        any non-interlaced PNG (gray, RGB, palette, with or without alpha,
        1 to 8 bits per sample; 16-bit samples are taken by their high
        byte) as rows of levels 0 .. 2**depth - 1.  A file of that very
        bit depth and gray is read exactly; any other is brought to it by
        luminance, rounded (transparent pixels count as white).

As a program it prints a PNG's size and colour type:

    python tools/imaging/png.py file.png
"""
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind, data):
	return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def write_gray(path, width, height, rows, depth):
	if depth not in (1, 2, 4, 8):
		raise ValueError("a gray PNG is 1, 2, 4 or 8 bits deep, not %d" % depth)
	raw = bytearray()
	per_byte = 8 // depth
	for row in rows:
		raw.append(0)							# filter: none
		byte = 0
		n = 0
		for v in row:
			byte = (byte << depth) | v
			n += 1
			if n == per_byte:
				raw.append(byte)
				byte = n = 0
		if n:
			raw.append(byte << (depth * (per_byte - n)))
	data = SIGNATURE
	data += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, 0, 0, 0, 0))
	data += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
	data += _chunk(b"IEND", b"")
	with open(path, "wb") as f:
		f.write(data)


def _paeth(a, b, c):
	p = a + b - c
	pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
	if pa <= pb and pa <= pc:
		return a
	return b if pb <= pc else c


def read(path):
	"""(width, height, bit depth, colour type, palette, rows of samples):
	each row a list of per-pixel tuples of samples."""
	with open(path, "rb") as f:
		data = f.read()
	if data[:8] != SIGNATURE:
		raise ValueError("%s is not a PNG" % path)
	pos = 8
	idat = bytearray()
	palette = None
	trns = None
	while pos < len(data):
		length, kind = struct.unpack(">I4s", data[pos:pos + 8])
		body = data[pos + 8:pos + 8 + length]
		pos += 12 + length
		if kind == b"IHDR":
			width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
			if interlace:
				raise ValueError("%s is interlaced" % path)
		elif kind == b"PLTE":
			palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
		elif kind == b"tRNS":
			trns = body
		elif kind == b"IDAT":
			idat += body
		elif kind == b"IEND":
			break
	channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
	bits = depth * channels
	stride = (width * bits + 7) // 8
	bpp = max(1, bits // 8)
	raw = zlib.decompress(bytes(idat))
	rows = []
	prev = bytearray(stride)
	p = 0
	for _ in range(height):
		ftype = raw[p]
		line = bytearray(raw[p + 1:p + 1 + stride])
		p += 1 + stride
		for i in range(stride):
			a = line[i - bpp] if i >= bpp else 0
			b = prev[i]
			c = prev[i - bpp] if i >= bpp else 0
			if ftype == 1:
				line[i] = (line[i] + a) & 0xff
			elif ftype == 2:
				line[i] = (line[i] + b) & 0xff
			elif ftype == 3:
				line[i] = (line[i] + (a + b) // 2) & 0xff
			elif ftype == 4:
				line[i] = (line[i] + _paeth(a, b, c)) & 0xff
		prev = line
		samples = []
		if depth < 8:
			for i in range(width * channels):
				byte = line[(i * depth) // 8]
				shift = 8 - depth - (i * depth) % 8
				samples.append((byte >> shift) & ((1 << depth) - 1))
		else:
			step = depth // 8
			samples = [line[i * step] for i in range(width * channels)]
		rows.append([tuple(samples[x * channels:(x + 1) * channels]) for x in range(width)])
	return width, height, min(depth, 8), ctype, palette, rows, trns


def read_gray(path, depth):
	width, height, bitdepth, ctype, palette, rows, trns = read(path)
	top = (1 << depth) - 1
	if ctype == 0 and bitdepth == depth:
		return width, height, [[px[0] for px in row] for row in rows]
	full = (1 << bitdepth) - 1
	out = []
	for row in rows:
		line = []
		for px in row:
			if ctype == 3:
				r, g, b = palette[px[0]]
				alpha = trns[px[0]] if trns is not None and px[0] < len(trns) else 255
				y = 0.299 * r + 0.587 * g + 0.114 * b
			elif ctype in (2, 6):
				y = (0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]) * 255 / full
				alpha = px[3] * 255 / full if ctype == 6 else 255
			else:
				y = px[0] * 255 / full
				alpha = px[1] * 255 / full if ctype == 4 else 255
			y = y * alpha / 255 + 255 * (1 - alpha / 255)		# over white
			line.append(int(round(y * top / 255)))
		out.append(line)
	return width, height, out


if __name__ == "__main__":
	w, h, d, ct, _, _, _ = read(sys.argv[1])
	print("%s: %d x %d, %d bits, colour type %d" % (sys.argv[1], w, h, d, ct))
