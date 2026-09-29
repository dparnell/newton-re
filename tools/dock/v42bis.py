#!/usr/bin/env python3
"""V.42bis data compression, written from the ITU-T recommendation.

Purpose
    A desktop that docks with a Newton over MNP may negotiate V.42bis
    compression.  This is an implementation of it written from the
    standard, independently of the ROM's coder (src/comms/V42bis.cpp), so
    that the two can be checked against each other: what one compresses the
    other must give back.  It keeps to the procedures both ends must share
    - the dictionary of strings (codewords 3-258 the characters, new
    strings from 259 on, a string only ever the dictionary string it
    extends plus one character, never longer than N7, never the entry just
    made), the recycling of leaf entries once the N2 entries are all used
    (the next leaf after the last one taken, as the entries are made), the
    codewords of C2 bits (9 at first, STEPUP to widen them) packed low bit
    first, the control codewords ETM (0), FLUSH (1) and STEPUP (2), and the
    transparent mode with its escape character (0 at first, moved on by 51
    each time it appears in the data) and the escape sequences ECM (0), EID
    (1) and RESET (2).  When to change mode is the encoder's own choice;
    this one changes as it is told (so a test can send both modes).

Usage
    from v42bis import Encoder, Decoder
    enc = Encoder(n2=1024, n7=32)
    out = enc.encode(b"...") + enc.flush()
    dec = Decoder(n2=1024, n7=32)
    assert dec.decode(out) == b"..."
    python tools/dock/v42bis.py --check <program>
        cross-checks the coder <program> implements (the ROM's, ctest
        comms.V42bis: "<program> encode N2 N7 < in > out", "<program>
        decode N2 N7 < in > out") against this one, both ways, over test
        texts in both modes; exits non-zero on a difference.

Inputs / outputs
    Bytes in, bytes out; the parameters N2 (the dictionary size, 512 and
    up) and N7 (the longest string, 6-250).
"""

import argparse
import random
import subprocess
import sys

ETM, FLUSH, STEPUP = 0, 1, 2
ECM, EID, RESET = 0, 1, 2
FIRST = 3 + 256          # the first codeword for a string of more than one character


class Dictionary:
    """The strings: each entry its parent entry and its last character;
    entries 3-258 are the single characters."""

    def __init__(self, n2, n7):
        self.n2 = n2
        self.n7 = n7
        self.reset()

    def reset(self):
        self.parent = {}
        self.char = {}
        self.children = {}           # (parent, char) -> entry
        self.child_count = {}
        for c in range(256):
            self.char[3 + c] = c
            self.parent[3 + c] = 0
        self.next = FIRST
        self.full = False
        self.last_made = 0

    def find(self, parent, c):
        return self.children.get((parent, c))

    def make(self, parent, c):
        """Parent + c made an entry, and the next entry to be made readied:
        the next unused one, or - all used - the next leaf, taken out."""
        e = self.next
        self.parent[e] = parent
        self.char[e] = c
        self.children[(parent, c)] = e
        self.child_count[parent] = self.child_count.get(parent, 0) + 1
        self.last_made = e
        was_full = self.full
        n = e + 1
        if not was_full and n > self.n2 - 1:
            self.full = True
        if was_full:
            n = e
        if self.full:
            while True:
                n += 1
                if n > self.n2 - 1:
                    n = FIRST
                if self.child_count.get(n, 0) == 0:
                    break
            p, ch = self.parent.pop(n, None), self.char.pop(n, None)
            if p is not None:
                del self.children[(p, ch)]
                self.child_count[p] -= 1
        self.next = n

    def string(self, e):
        out = []
        while e:
            out.append(self.char[e])
            e = self.parent[e]
        return bytes(reversed(out))


def max_bits(n2):
    """N2's codeword size: 9 bits for 512 entries, one more each doubling."""
    return max(9, (n2 - 1).bit_length())


class BitWriter:
    def __init__(self):
        self.acc = 0
        self.bits = 0
        self.out = bytearray()

    def put(self, value, n):
        self.acc |= value << self.bits
        self.bits += n
        while self.bits >= 8:
            self.out.append(self.acc & 0xff)
            self.acc >>= 8
            self.bits -= 8

    def align(self):
        if self.bits:
            self.out.append(self.acc & 0xff)
            self.acc = 0
            self.bits = 0

    def take(self):
        out = bytes(self.out)
        self.out = bytearray()
        return out


class Encoder:
    def __init__(self, n2=2048, n7=32):
        self.d = Dictionary(n2, n7)
        self.bits = BitWriter()
        self.c2 = 9
        self.c3 = 512
        self.transparent = True
        self.escape = 0
        self.string = 0          # the entry of the string so far (0: none)
        self.length = 0
        self.flushed = False

    def _codeword(self, code):
        while self.c3 <= code and self.c2 < max_bits(self.d.n2):
            self.bits.put(STEPUP, self.c2)
            self.c2 += 1
            self.c3 *= 2
        self.bits.put(code, self.c2)

    def _char(self, c):
        if self.string == 0:
            self.string, self.length = 3 + c, 1
        else:
            e = self.d.find(self.string, c)
            if e is not None and not self.flushed and e != self.d.last_made:
                self.string, self.length = e, self.length + 1
            else:
                add = e is None
                if not self.flushed and not self.transparent:
                    self._codeword(self.string)
                if add and self.length + 1 <= self.d.n7:
                    self.d.make(self.string, c)
                else:
                    self.d.last_made = 0
                self.string, self.length = 3 + c, 1
        if self.transparent:
            self.bits.out.append(c)
            if c == self.escape:
                self.bits.out.append(EID)
        if c == self.escape:
            self.escape = (self.escape + 51) & 0xff
        self.flushed = False

    def encode(self, data):
        for c in data:
            self._char(c)
        return self.bits.take()

    def to_compressed(self):
        """ESC ECM: compressed mode from here on (the string so far ends
        here: the next character does not extend it)."""
        if self.transparent:
            self.bits.out += bytes([self.escape, ECM])
            self.transparent = False
            self.flushed = True
        return self.bits.take()

    def to_transparent(self):
        """The string so far sent, then ETM: transparent mode."""
        if not self.transparent:
            if self.string and not self.flushed:
                self._codeword(self.string)
                self.flushed = True
            self.bits.put(ETM, self.c2)
            self.bits.align()
            self.transparent = True
        return self.bits.take()

    def flush(self):
        """The string so far sent and FLUSH, to an octet boundary."""
        if not self.transparent and not self.flushed:
            if self.string:
                self._codeword(self.string)
                self.flushed = True
            self.bits.put(FLUSH, self.c2)
            self.bits.align()
        return self.bits.take()


class Decoder:
    def __init__(self, n2=2048, n7=32):
        self.d = Dictionary(n2, n7)
        self.c2 = 9
        self.transparent = True
        self.escape = 0
        self.pending_escape = False
        self.acc = 0
        self.nbits = 0
        self.string = 0          # transparent: the string so far; compressed: the last one
        self.length = 0
        self.after_etm = False

    def _extend(self, c):
        """A character in transparent mode followed through the dictionary."""
        if self.string == 0:
            self.string, self.length = 3 + c, 1
            return
        e = self.d.find(self.string, c)
        if e is not None and not self.after_etm and e != self.d.last_made:
            self.string, self.length = e, self.length + 1
        else:
            if e is None and self.length + 1 <= self.d.n7:
                self.d.make(self.string, c)
            else:
                self.d.last_made = 0
            self.string, self.length = 3 + c, 1
        self.after_etm = False

    def decode(self, data):
        out = bytearray()
        for b in data:
            if self.transparent:
                if self.pending_escape:
                    self.pending_escape = False
                    if b == ECM:
                        self.transparent = False
                        self.acc = self.nbits = 0
                        continue
                    if b == RESET:
                        raise ValueError("RESET is not handled here")
                    if b != EID:
                        raise ValueError("unknown escape sequence %d" % b)
                    c = self.escape
                elif b == self.escape:
                    self.pending_escape = True
                    continue
                else:
                    c = b
                out.append(c)
                if c == self.escape:
                    self.escape = (self.escape + 51) & 0xff
                self._extend(c)
                continue
            self.acc |= b << self.nbits
            self.nbits += 8
            while self.nbits >= self.c2 and not self.transparent:
                code = self.acc & ((1 << self.c2) - 1)
                self.acc >>= self.c2
                self.nbits -= self.c2
                if code == ETM or code == FLUSH:
                    self.acc >>= self.nbits % 8
                    self.nbits -= self.nbits % 8
                    if code == ETM:
                        self.transparent = True
                        self.after_etm = True
                        self.acc = self.nbits = 0
                    continue
                if code == STEPUP:
                    self.c2 += 1
                    if self.c2 > max_bits(self.d.n2):
                        raise ValueError("STEPUP past N2's codeword size")
                    continue
                if code not in self.d.char:
                    raise ValueError("unknown codeword %d" % code)
                s = self.d.string(code)
                for c in s:
                    if c == self.escape:
                        self.escape = (self.escape + 51) & 0xff
                out += s
                if self.string:
                    e = self.d.find(self.string, s[0])
                    if e is None and self.length + 1 <= self.d.n7:
                        self.d.make(self.string, s[0])
                    else:
                        self.d.last_made = 0
                self.string, self.length = code, len(s)
        return bytes(out)


# --- the cross-check against another coder (the ROM's, ctest comms.V42bis)

TEXTS = [
    b"",
    b"a",
    b"abababababababababab",
    b"The quick brown fox jumps over the lazy dog. " * 40,
    bytes(range(256)) * 4,
    bytes([0, 0, 0, 51, 51, 102, 0, 51]) * 50,
]


def texts():
    rnd = random.Random(1)
    yield from TEXTS
    yield bytes(rnd.randrange(256) for _ in range(3000))
    words = [b"newton", b"dock", b"package", b"soup", b"entry", b"store", b" ", b"\n"]
    yield b"".join(rnd.choice(words) for _ in range(2000))


def run(program, verb, n2, n7, data):
    r = subprocess.run(program + [verb, str(n2), str(n7)], input=data, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError("%s %s failed: %s" % (program, verb, r.stderr.decode(errors="replace")))
    return r.stdout


def ours(data, n2, n7, mode):
    enc = Encoder(n2, n7)
    out = bytearray()
    if mode == "compressed":
        out += enc.to_compressed()
        out += enc.encode(data)
    elif mode == "alternate":
        for i in range(0, len(data), 300):
            out += enc.to_compressed() if (i // 300) % 2 == 0 else enc.to_transparent()
            out += enc.encode(data[i:i + 300])
    else:
        out += enc.encode(data)
    out += enc.flush()
    return bytes(out)


def check(program):
    failures = 0
    for n2, n7 in ((512, 6), (1024, 32), (2048, 250)):
        for i, data in enumerate(texts()):
            # the ROM's coder compressing, ours decompressing
            packed = run(program, "encode", n2, n7, data)
            try:
                back = Decoder(n2, n7).decode(packed)
            except ValueError as e:
                back = ("error: %s" % e).encode()
            ok = back == data
            print("v42bis.py: N2 %d N7 %d text %d: the ROM's %d bytes -> %d, ours gives back %s"
                  % (n2, n7, i, len(data), len(packed), "the same" if ok else "SOMETHING ELSE"))
            failures += not ok
            # ours compressing (in each mode), the ROM's decompressing
            for mode in ("transparent", "compressed", "alternate"):
                packed = ours(data, n2, n7, mode)
                back = run(program, "decode", n2, n7, packed)
                ok = back == data
                if not ok:
                    print("v42bis.py: N2 %d N7 %d text %d %s: ours %d bytes, the ROM's gives back %d bytes, not the same"
                          % (n2, n7, i, mode, len(packed), len(back)))
                failures += not ok
            # and ours against ours
            ok = Decoder(n2, n7).decode(ours(data, n2, n7, "alternate")) == data
            failures += not ok
    print("v42bis.py: %s" % ("all the same" if failures == 0 else "%d differences" % failures))
    return failures == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", nargs=argparse.REMAINDER, required=True, help="the coder to check, and its arguments")
    args = ap.parse_args()
    sys.exit(0 if check(args.check) else 1)


if __name__ == "__main__":
    main()
