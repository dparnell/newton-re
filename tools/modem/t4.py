#!/usr/bin/env python3
"""ITU-T T.4 modified Huffman (MH) coding of fax pages, and a test page.

Purpose
    A Group 3 fax page is sent as its scan lines, each coded as alternate
    runs of white and black pixels (starting with white, a zero-length
    white run if the line starts black) in the modified Huffman code of
    ITU-T Recommendation T.4 (07/2003), section 4.1: runs of 0 to 63 by
    their terminating code, longer ones by a make-up code for the
    multiple of 64 followed by a terminating code; each line followed by
    an end of line (EOL, 000000000001); six EOLs end the page (RTC).  Over
    the telephone the bits of each byte go least significant first
    (T.4 / T.30: the first bit of a code is the least significant bit of
    the first byte).

    This is written from the recommendation alone, not from the Newton ROM,
    so that the reconstructed decoder (src/comms/fax/T4FaxLine.h, the
    ROM's TT4FaxLine) can be checked against it: a page encoded here,
    decoded there, compared line by line (ctest comms.T4FaxLine).  The fax
    peer in fakemodem.py sends pages coded here.

Usage
    python tools/modem/t4.py --test-page --pbm page.pbm
        writes the test page (1728 x 200 pixels: bars, a checkerboard,
        diagonals, text-like strokes, lines starting black) as a PBM.
    python tools/modem/t4.py --encode page.pbm -o page.t4 [--fill N]
        codes a PBM (P1 or P4) page as T.4 MH bytes, least significant bit
        first, with an EOL before the first line, N nought fill bytes after
        each line's EOL, and the RTC.
    python tools/modem/t4.py --decode page.t4 --width 1728 --pbm out.pbm
        decodes MH bytes back into a PBM (a round trip check of this file).
    python tools/modem/t4.py --self-test
        encodes and decodes the test page and every run length.

Inputs / outputs
    PBM images (1 is black) and raw T.4 byte streams, as above.
"""

import argparse
import sys

# T.4 Table 2: terminating codes (run 0 to 63), white then black
WHITE_TERMINATING = [
    "00110101", "000111", "0111", "1000", "1011", "1100", "1110", "1111",
    "10011", "10100", "00111", "01000", "001000", "000011", "110100", "110101",
    "101010", "101011", "0100111", "0001100", "0001000", "0010111", "0000011", "0000100",
    "0101000", "0101011", "0010011", "0100100", "0011000", "00000010", "00000011", "00011010",
    "00011011", "00010010", "00010011", "00010100", "00010101", "00010110", "00010111", "00101000",
    "00101001", "00101010", "00101011", "00101100", "00101101", "00000100", "00000101", "00001010",
    "00001011", "01010010", "01010011", "01010100", "01010101", "00100100", "00100101", "01011000",
    "01011001", "01011010", "01011011", "01001010", "01001011", "00110010", "00110011", "00110100",
]
BLACK_TERMINATING = [
    "0000110111", "010", "11", "10", "011", "0011", "0010", "00011",
    "000101", "000100", "0000100", "0000101", "0000111", "00000100", "00000111", "000011000",
    "0000010111", "0000011000", "0000001000", "00001100111", "00001101000", "00001101100", "00000110111", "00000101000",
    "00000010111", "00000011000", "000011001010", "000011001011", "000011001100", "000011001101", "000001101000", "000001101001",
    "000001101010", "000001101011", "000011010010", "000011010011", "000011010100", "000011010101", "000011010110", "000011010111",
    "000001101100", "000001101101", "000011011010", "000011011011", "000001010100", "000001010101", "000001010110", "000001010111",
    "000001100100", "000001100101", "000001010010", "000001010011", "000000100100", "000000110111", "000000111000", "000000100111",
    "000000101000", "000001011000", "000001011001", "000000101011", "000000101100", "000001011010", "000001100110", "000001100111",
]
# T.4 Table 3a: make-up codes (64 to 1728), white then black
WHITE_MAKEUP = [
    "11011", "10010", "010111", "0110111", "00110110", "00110111", "01100100", "01100101",
    "01101000", "01100111", "011001100", "011001101", "011010010", "011010011", "011010100", "011010101",
    "011010110", "011010111", "011011000", "011011001", "011011010", "011011011", "010011000", "010011001",
    "010011010", "011000", "010011011",
]
BLACK_MAKEUP = [
    "0000001111", "000011001000", "000011001001", "000001011011", "000000110011", "000000110100", "000000110101", "0000001101100",
    "0000001101101", "0000001001010", "0000001001011", "0000001001100", "0000001001101", "0000001110010", "0000001110011", "0000001110100",
    "0000001110101", "0000001110110", "0000001110111", "0000001010010", "0000001010011", "0000001010100", "0000001010101", "0000001011010",
    "0000001011011", "0000001100100", "0000001100101",
]
# T.4 Table 3b: extended make-up codes (1792 to 2560), both colours
EXTENDED_MAKEUP = [
    "00000001000", "00000001100", "00000001101", "000000010010", "000000010011", "000000010100", "000000010101",
    "000000010110", "000000010111", "000000011100", "000000011101", "000000011110", "000000011111",
]
EOL = "000000000001"
WIDTH = 1728


def run_codes(run, black):
    """The codes of one run: make-ups (the longest that fit), then a
    terminating code."""
    terminating = BLACK_TERMINATING if black else WHITE_TERMINATING
    makeup = BLACK_MAKEUP if black else WHITE_MAKEUP
    codes = []
    while run >= 64:
        if run >= 1792:
            n = min(run // 64, 40)
            codes.append(EXTENDED_MAKEUP[n - 28])
            run -= n * 64
        else:
            n = run // 64
            codes.append(makeup[n - 1])
            run -= n * 64
    codes.append(terminating[run])
    return codes


def encode_line(pixels):
    """A line of 0/1 pixels (1 black) as MH code bits, without its EOL."""
    bits = []
    black = False
    i = 0
    while i < len(pixels):
        j = i
        while j < len(pixels) and pixels[j] == (1 if black else 0):
            j += 1
        bits.extend(run_codes(j - i, black))
        black = not black
        i = j
    if not black:
        pass            # the line ended on a white run: nothing more
    return "".join(bits)


def pack_lsb_first(bits):
    """Bits (a string of 0/1) as bytes, the first bit least significant."""
    out = bytearray()
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8].ljust(8, "0")
        value = 0
        for k, b in enumerate(chunk):
            if b == "1":
                value |= 1 << k
        out.append(value)
    return bytes(out)


def encode_page(rows, fill=0):
    """A page (a list of lines of 0/1 pixels) as T.4 bytes: EOL, then each
    line and its EOL, fill bytes' worth of noughts ahead of every EOL (the
    first included), then five more EOLs (RTC)."""
    stream = ["0" * (8 * fill), EOL]
    for row in rows:
        stream.append(encode_line(row))
        stream.append("0" * (8 * fill))
        stream.append(EOL)
    stream.extend([EOL] * 5)
    return pack_lsb_first("".join(stream))


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def bit(self):
        byte = self.data[self.pos >> 3]
        b = (byte >> (self.pos & 7)) & 1
        self.pos += 1
        return b

    def more(self):
        return self.pos < len(self.data) * 8


def decode_page(data, width=WIDTH):
    """T.4 bytes back into lines of pixels (for this file's own round trip)."""
    tables = {}
    for black in (False, True):
        table = {}
        for run, code in enumerate(BLACK_TERMINATING if black else WHITE_TERMINATING):
            table[code] = run
        for n, code in enumerate(BLACK_MAKEUP if black else WHITE_MAKEUP):
            table[code] = (n + 1) * 64
        for n, code in enumerate(EXTENDED_MAKEUP):
            table[code] = (n + 28) * 64
        tables[black] = table
    reader = BitReader(data)
    rows = []
    code = ""
    # to the first EOL
    zeros = 0
    while reader.more():
        if reader.bit():
            if zeros >= 11:
                break
            zeros = 0
        else:
            zeros += 1
    while reader.more():
        row = []
        black = False
        code = ""
        while reader.more():
            code += str(reader.bit())
            if code.startswith("00000000") and code.endswith("1") and code.count("1") == 1:
                break           # EOL (with any fill ahead of it)
            if code in tables[black]:
                run = tables[black][code]
                row.extend([1 if black else 0] * run)
                code = ""
                if run < 64:
                    black = not black
        if not row:
            break               # an EOL straight after an EOL: the RTC
        rows.append(row[:width])
    return rows


def test_page(width=WIDTH, height=200):
    rows = []
    for y in range(height):
        row = [0] * width
        band = y // 20
        if band == 0:                           # vertical bars of growing width
            x, w = 8, 1
            while x + w < width:
                for i in range(x, x + w):
                    row[i] = 1
                x += 2 * w + 3
                w += 1
        elif band == 1:                         # a checkerboard
            for x in range(width):
                row[x] = ((x // 8) + (y // 8)) & 1
        elif band == 2:                         # diagonals
            for x in range(width):
                row[x] = 1 if (x + y) % 37 < 3 or (x - y) % 53 < 2 else 0
        elif band == 3:                         # lines that start black
            for x in range(0, (y % 20 + 1) * 70):
                row[x] = 1
        elif band == 4:                         # a solid line and runs past 1728's make-up codes
            row = [1] * width if y % 2 == 0 else [0] * width
        elif band == 5:                         # text-like strokes
            for x in range(40, width - 40, 12):
                if (x * 7 + y * 3) % 11 < 5:
                    for i in range(x, x + ((x + y) % 7) + 1):
                        row[i] = 1
        elif band == 6:                         # single pixels
            for x in range(y % 5, width, 5):
                row[x] = 1
        elif band == 7:                         # a frame
            row[0] = row[width - 1] = 1
            if y % 20 in (0, 19):
                row = [1] * width
        elif band == 8:                         # every run length 0..63 black and white
            x, n = 0, y % 20
            while x < width:
                for i in range(x, min(width, x + n)):
                    row[i] = 1
                x += n + 64 - n
                n = (n + 7) % 64
        rows.append(row)
    return rows


def write_pbm(path, rows):
    width = len(rows[0]) if rows else 0
    with open(path, "wb") as f:
        f.write(b"P4\n%d %d\n" % (width, len(rows)))
        for row in rows:
            for i in range(0, width, 8):
                value = 0
                for k in range(8):
                    if i + k < width and row[i + k]:
                        value |= 0x80 >> k
                f.write(bytes([value]))


def read_pbm(path):
    with open(path, "rb") as f:
        data = f.read()
    tokens = []
    pos = 0
    while len(tokens) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    magic, width, height = tokens[0], int(tokens[1]), int(tokens[2])
    pos += 1
    rows = []
    if magic == b"P4":
        stride = (width + 7) // 8
        for y in range(height):
            line = data[pos + y * stride:pos + (y + 1) * stride]
            rows.append([(line[x >> 3] >> (7 - (x & 7))) & 1 for x in range(width)])
    elif magic == b"P1":
        digits = [c - 48 for c in data[pos:] if c in (48, 49)]
        rows = [digits[y * width:(y + 1) * width] for y in range(height)]
    else:
        raise ValueError("not a PBM: %r" % magic)
    return rows


def self_test():
    for black in (False, True):
        for run in list(range(0, 130)) + [640, 1727, 1728, 1791, 1792, 2560, 2600]:
            row = ([0] if black else []) + [1 if black else 0] * run
            if run == 0:
                continue
            back = decode_page(encode_page([row]), width=len(row))
            assert back == [row], (black, run)
    page = test_page()
    for fill in (0, 3):
        assert decode_page(encode_page(page, fill)) == page, fill
    print("t4.py: self test passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--test-page", action="store_true", help="the test page")
    ap.add_argument("--encode", help="a PBM to code")
    ap.add_argument("--decode", help="T.4 bytes to decode")
    ap.add_argument("--width", type=int, default=WIDTH)
    ap.add_argument("--fill", type=int, default=0, help="nought bytes before each EOL")
    ap.add_argument("--pbm", help="the PBM written")
    ap.add_argument("-o", "--output", help="the T.4 bytes written")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.test_page:
        rows = test_page(args.width)
        if args.pbm:
            write_pbm(args.pbm, rows)
        if args.output:
            with open(args.output, "wb") as f:
                f.write(encode_page(rows, args.fill))
        return 0
    if args.encode:
        with open(args.output, "wb") as f:
            f.write(encode_page(read_pbm(args.encode), args.fill))
        return 0
    if args.decode:
        with open(args.decode, "rb") as f:
            write_pbm(args.pbm, decode_page(f.read(), args.width))
        return 0
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
