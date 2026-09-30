#!/usr/bin/env python3
"""A PalmDoc e-text (a Palm database of type TEXt, creator REAd) made from a
text file - a document for a host Newton to download.

Usage:
    python tools/host/palmdoc.py IN.txt OUT.pdb [--name NAME] [--plain]

Writes IN.txt (Latin-1) as a PalmDoc: the 78-byte Palm database header
named NAME (default: IN's name without its extension), a record list, the
PalmDoc header record (compression, text length, record count, 4096-byte
records) and the text in records of up to 4096 bytes, each compressed with
PalmDoc's own LZ77 coding (compression 2: runs of literals, space-plus-
character pairs and back references of 3 to 10 bytes up to 2047 back) -
or stored as it is with --plain (compression 1).  --check reads OUT.pdb
back and says whether it decodes to IN.txt.

src/host/demo/www/story.pdb is `palmdoc.py src/host/demo/www/story.txt
src/host/demo/www/story.pdb --name "Host Story"` - the document Newt's
Cape's PalmDoc helper (pdoc10d2.pkg) opens in
src/host/demo/apps-newtscape-helpers.ns.

Standard library only.
"""

import argparse
import os
import struct


def compress(data: bytes) -> bytes:
    """PalmDoc compression (type 2) of one record."""
    out = bytearray()
    literals = bytearray()

    def flush():
        while literals:
            run = literals[:8]
            del literals[:8]
            if len(run) == 1 and (run[0] == 0 or 0x09 <= run[0] <= 0x7f):
                out.append(run[0])
            else:
                out.append(len(run))
                out += run

    i = 0
    n = len(data)
    while i < n:
        # the longest match of 3..10 bytes within 2047 back
        best_len, best_dist = 0, 0
        start = max(0, i - 2047)
        for j in range(start, i):
            length = 0
            while length < 10 and i + length < n and data[j + length] == data[i + length]:
                length += 1
            if length > best_len:
                best_len, best_dist = length, i - j
        if best_len >= 3:
            flush()
            word = 0x8000 | (best_dist << 3) | (best_len - 3)
            out += struct.pack(">H", word)
            i += best_len
            continue
        c = data[i]
        if c == 0x20 and i + 1 < n and 0x40 <= data[i + 1] <= 0x7f:
            flush()
            out.append(data[i + 1] ^ 0x80)
            i += 2
            continue
        if c == 0 or 0x09 <= c <= 0x7f:
            if literals:
                literals.append(c)
            else:
                out.append(c)
        else:
            literals.append(c)
        i += 1
    flush()
    return bytes(out)


def decompress(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        i += 1
        if 1 <= c <= 8:
            out += data[i:i + c]
            i += c
        elif c <= 0x7f:
            out.append(c)
        elif c <= 0xbf:
            word = (c << 8) | data[i]
            i += 1
            dist = (word >> 3) & 0x7ff
            length = (word & 7) + 3
            for _ in range(length):
                out.append(out[-dist])
        else:
            out.append(0x20)
            out.append(c ^ 0x80)
    return bytes(out)


def make(text: bytes, name: str, plain: bool) -> bytes:
    chunks = [text[i:i + 4096] for i in range(0, len(text), 4096)] or [b""]
    records = [compress(c) if not plain else c for c in chunks]
    header0 = struct.pack(">HHIHHI", 1 if plain else 2, 0, len(text), len(chunks), 4096, 0)
    records.insert(0, header0)
    count = len(records)
    offset = 78 + 8 * count + 2
    listing = bytearray()
    for n, r in enumerate(records):
        listing += struct.pack(">IB3s", offset, 0, (n + 1).to_bytes(3, "big"))
        offset += len(r)
    stamp = 0xb4000000                      # (a 1999-ish Palm date; nothing reads it)
    head = struct.pack(">32sHHIIIIII4s4sIIH", name.encode("latin-1")[:31], 0, 0, stamp, stamp, 0, 0, 0, 0,
                       b"TEXt", b"REAd", 0, 0, count)
    return head + bytes(listing) + b"\0\0" + b"".join(records)


def read(pdb: bytes) -> bytes:
    count = struct.unpack(">H", pdb[76:78])[0]
    offsets = [struct.unpack(">I", pdb[78 + 8 * n:82 + 8 * n])[0] for n in range(count)] + [len(pdb)]
    compression = struct.unpack(">H", pdb[offsets[0]:offsets[0] + 2])[0]
    text = bytearray()
    for n in range(1, count):
        r = pdb[offsets[n]:offsets[n + 1]]
        text += decompress(r) if compression == 2 else r
    return bytes(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--name")
    ap.add_argument("--plain", action="store_true", help="store the text uncompressed (compression 1)")
    ap.add_argument("--check", action="store_true", help="read OUTPUT back and compare it with INPUT")
    args = ap.parse_args(argv)
    with open(args.input, "rb") as f:
        text = f.read().replace(b"\r\n", b"\n")      # (PalmDoc lines end with LF)
    if args.check:
        with open(args.output, "rb") as f:
            back = read(f.read())
        print("%s: %s" % (args.output, "decodes to the text" if back == text else "DIFFERS"))
        return 0 if back == text else 1
    name = args.name or os.path.splitext(os.path.basename(args.input))[0]
    pdb = make(text, name, args.plain)
    if read(pdb) != text:
        raise SystemExit("palmdoc: the coder does not round-trip")
    with open(args.output, "wb") as f:
        f.write(pdb)
    print("%s: \"%s\", %d bytes of text in %d bytes" % (args.output, name, len(text), len(pdb)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
