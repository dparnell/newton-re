#!/usr/bin/env python3
"""Write a GIF (GIF87a, one image, a global colour table) in pure Python,
for test pictures the host's web server gives a Newton browser
(src/host/demo/www/; docs/qd/colour.md).

    write_gif(path, width, height, palette, pixels)
        palette: up to 256 (r, g, b) tuples; pixels: width * height palette
        indices, row by row.  The image data uses GIF's LZW in its plainest
        form - every code a literal, a clear code often enough that the
        code size never grows - which any decoder reads (it is the
        "uncompressed GIF" trick): larger than need be, simple to check.

As a program it writes the colour test picture, a 64 x 32 image of four
bands - red, green, blue, yellow - inside a black frame:

    python tools/imaging/gifwrite.py colour.gif

Standard library only.
"""
import struct
import sys


def write_gif(path, width, height, palette, pixels):
	bits = 1
	while (1 << bits) < max(2, len(palette)):
		bits += 1
	table = list(palette) + [(0, 0, 0)] * ((1 << bits) - len(palette))
	min_code = max(2, bits)						# LZW's minimum code size (2 for a two-colour image)
	clear = 1 << min_code
	end = clear + 1
	code_size = min_code + 1
	# literals only: the table grows by one a code after the first, so a
	# clear before it would need a wider code keeps the size fixed
	run = (1 << code_size) - (clear + 2) - 1
	codes = [clear]
	since = 0
	for p in pixels:
		if since == run:
			codes.append(clear)
			since = 0
		codes.append(p)
		since += 1
	codes.append(end)
	data = bytearray()
	acc = 0
	nbits = 0
	for c in codes:
		acc |= c << nbits
		nbits += code_size
		while nbits >= 8:
			data.append(acc & 0xff)
			acc >>= 8
			nbits -= 8
	if nbits:
		data.append(acc & 0xff)
	out = bytearray(b"GIF87a")
	out += struct.pack("<HHBBB", width, height, 0x80 | (bits - 1), 0, 0)	# global table of 2**bits
	for r, g, b in table:
		out += bytes((r, g, b))
	out += b"," + struct.pack("<HHHHB", 0, 0, width, height, 0)
	out.append(min_code)
	for i in range(0, len(data), 255):
		chunk = data[i:i + 255]
		out.append(len(chunk))
		out += chunk
	out += b"\x00;"
	with open(path, "wb") as f:
		f.write(out)


def colour_test(path):
	"""64 x 32: four bands (red, green, blue, yellow) in a black frame."""
	palette = [(255, 255, 255), (0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
	w, h = 64, 32
	pixels = []
	for y in range(h):
		for x in range(w):
			if x == 0 or y == 0 or x == w - 1 or y == h - 1:
				pixels.append(1)
			else:
				pixels.append(2 + min(3, (x - 1) * 4 // (w - 2)))
	write_gif(path, w, h, palette, pixels)


if __name__ == "__main__":
	if len(sys.argv) < 2:
		print(__doc__)
		sys.exit(2)
	colour_test(sys.argv[1])
	print("wrote %s" % sys.argv[1])
