#!/usr/bin/env python3
"""The host's internal flash images: describe one, and convert between the
flat and the sparse format.

The reconstruction's `newton --store FILE` keeps the MessagePad's internal
flash in FILE (src/hal/host/HostFlash.h, docs/stores/README.md "Bigger
flash").  A file is one of:

  * flat - the flash's bytes and nothing else, bank 1 then bank 2, as big
    as the flash (4 MB times a power of two up to 128 MB; at 4 or 8 MB this
    is Einstein's own flash file);
  * sparse - 'NewtFlsh': a 0x40-byte header, a map of one big-endian word
    per 1 KB chunk of the flash (0: erased, n: kept in slot n), and the
    chunks that are not all 0xFF, slot n at the chunks' offset +
    (n - 1) * 1 KB.  The header's word at 0x28 is flags: bit 0 set when the
    store keeps its binaries in a MessagePad's byte order (every sparse file
    made since 2026-10-01; an older one's reals, shapes and string large
    binaries are in the host's order, and newton repairs it when it mounts
    it).

Usage:
    python tools/stores/flashimage.py info FILE [--expect-stored MIN:MAX]
    python tools/stores/flashimage.py to-sparse FLAT OUT
    python tools/stores/flashimage.py to-flat SPARSE OUT
    python tools/stores/flashimage.py make-sparse OUT --size MB

`info` says which format FILE is, the flash's size and banks, how many
chunks are not erased and how many bytes the file takes; with
`--expect-stored MIN:MAX` (megabytes, either side may be empty) it exits 1
unless the file's own size lies between them - how the host's test checks
that a sparse image grew with what was written and not with the flash.
`info` also says whether a sparse image is in a MessagePad's byte order
or awaits newton's repair.  `to-sparse` and `to-flat` write a copy of an image in the other format
(the flash's bytes are the same: `to-flat` of `to-sparse` of a flat file
is that file).  A flat file is taken to be in a MessagePad's byte order
(Einstein's are), so `to-sparse` marks its copy so, and `to-flat` of a
sparse image that awaits the repair refuses (mount it with newton first).
`make-sparse` writes a new, erased sparse image.

Pure Python 3, no dependencies.
"""

import argparse
import struct
import sys

MAGIC = b"NewtFlsh"
VERSION = 1
HEADER_SIZE = 0x40
FLAGS_OFFSET = 0x28
BIG_ENDIAN_BINARIES = 0x01
CHUNK = 0x400
MIN_SIZE = 4 << 20
MAX_SIZE = 128 << 20


def valid_size(size):
    s = MIN_SIZE
    while s <= MAX_SIZE:
        if s == size:
            return True
        s <<= 1
    return False


def bank_size(size):
    return size if size <= MIN_SIZE else size // 2


def data_offset(size):
    chunks = size // CHUNK
    return (HEADER_SIZE + chunks * 4 + CHUNK - 1) & ~(CHUNK - 1)


class Image:
    """A flash image read whole: its format, and its bytes as a bytearray."""

    def __init__(self, fmt, flash, file_size, stored_chunks):
        self.format = fmt
        self.flash = flash
        self.file_size = file_size
        self.stored_chunks = stored_chunks
        self.flags = BIG_ENDIAN_BINARIES


def read_image(path):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:8] == MAGIC:
        if len(raw) < HEADER_SIZE:
            raise ValueError("%s: a sparse image's header is cut short" % path)
        (version, header_size, size, bank, chunk, count, map_offset,
         data_off) = struct.unpack(">8I", raw[8:40])
        if (version != VERSION or header_size != HEADER_SIZE or not valid_size(size)
                or bank != bank_size(size) or chunk != CHUNK or count != size // CHUNK
                or map_offset != HEADER_SIZE):
            raise ValueError("%s: not a sparse image this tool knows" % path)
        flash = bytearray(b"\xff" * size)
        slots_in_file = max(0, (len(raw) - data_off) // CHUNK)
        used = set()
        stored = 0
        for n in range(count):
            at = HEADER_SIZE + 4 * n
            slot = struct.unpack(">I", raw[at:at + 4])[0] if at + 4 <= len(raw) else 0
            # as the host reads it: a slot past the end, or one already
            # named, is an erased chunk (src/hal/host/HostFlash.h)
            if slot == 0 or slot > slots_in_file or slot in used:
                continue
            used.add(slot)
            start = data_off + (slot - 1) * CHUNK
            flash[n * CHUNK:(n + 1) * CHUNK] = raw[start:start + CHUNK]
            stored += 1
        image = Image("sparse", flash, len(raw), stored)
        image.flags = struct.unpack(">I", raw[FLAGS_OFFSET:FLAGS_OFFSET + 4])[0]
        return image
    if not valid_size(len(raw)):
        raise ValueError("%s: neither a sparse image nor a flat flash (%d bytes)" % (path, len(raw)))
    flash = bytearray(raw)
    stored = sum(1 for n in range(len(raw) // CHUNK)
                 if flash[n * CHUNK:(n + 1) * CHUNK].count(0xFF) != CHUNK)
    return Image("flat", flash, len(raw), stored)


def write_sparse(path, flash):
    size = len(flash)
    count = size // CHUNK
    off = data_offset(size)
    header = MAGIC + struct.pack(">8I", VERSION, HEADER_SIZE, size, bank_size(size), CHUNK,
                                 count, HEADER_SIZE, off) + struct.pack(">I", BIG_ENDIAN_BINARIES)
    header += b"\0" * (HEADER_SIZE - len(header))
    words = bytearray(4 * count)
    chunks = []
    erased = b"\xff" * CHUNK
    for n in range(count):
        piece = bytes(flash[n * CHUNK:(n + 1) * CHUNK])
        if piece != erased:
            chunks.append(piece)
            struct.pack_into(">I", words, 4 * n, len(chunks))
    with open(path, "wb") as f:
        f.write(header)
        f.write(words)
        f.write(b"\0" * (off - HEADER_SIZE - len(words)))
        for piece in chunks:
            f.write(piece)


def write_flat(path, flash):
    with open(path, "wb") as f:
        f.write(flash)


def parse_range(text):
    low, _, high = text.partition(":")
    return (float(low) if low else None, float(high) if high else None)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("info", help="describe an image")
    p.add_argument("file")
    p.add_argument("--expect-stored", metavar="MIN:MAX",
                   help="exit 1 unless the file's size is within MIN..MAX megabytes")
    p = sub.add_parser("to-sparse", help="a flat image as a sparse one")
    p.add_argument("file")
    p.add_argument("out")
    p = sub.add_parser("to-flat", help="a sparse image as a flat one")
    p.add_argument("file")
    p.add_argument("out")
    p = sub.add_parser("make-sparse", help="a new, erased sparse image")
    p.add_argument("out")
    p.add_argument("--size", type=int, required=True, help="megabytes: 4, 8, 16, 32, 64 or 128")
    args = parser.parse_args(argv)

    if args.command == "make-sparse":
        if not valid_size(args.size << 20):
            parser.error("--size is 4, 8, 16, 32, 64 or 128")
        write_sparse(args.out, bytearray(b"\xff" * (args.size << 20)))
        return 0

    try:
        image = read_image(args.file)
    except (OSError, ValueError) as e:
        print("flashimage: %s" % e, file=sys.stderr)
        return 1

    if args.command == "info":
        size = len(image.flash)
        banks = 1 if size <= MIN_SIZE else 2
        print("%s: %s image of a %d MB flash (%d bank%s of %d MB, chips of %d MB)"
              % (args.file, image.format, size >> 20, banks, "s" if banks > 1 else "",
                 bank_size(size) >> 20, bank_size(size) >> 21))
        print("chunks not erased: %d of %d (%.2f MB)"
              % (image.stored_chunks, size // CHUNK, image.stored_chunks * CHUNK / 1048576.0))
        print("file: %d bytes (%.2f MB)" % (image.file_size, image.file_size / 1048576.0))
        if image.format == "sparse":
            print("binaries: %s" % ("a MessagePad's byte order" if image.flags & BIG_ENDIAN_BINARIES
                                    else "an older host's byte order (newton repairs it when it mounts it)"))
        if args.expect_stored:
            low, high = parse_range(args.expect_stored)
            mb = image.file_size / 1048576.0
            if (low is not None and mb < low) or (high is not None and mb > high):
                print("flashimage: the file is %.2f MB, not within %s" % (mb, args.expect_stored))
                return 1
            print("file size as expected")
        return 0
    if args.command == "to-sparse":
        write_sparse(args.out, image.flash)
    else:
        if not image.flags & BIG_ENDIAN_BINARIES:
            print("flashimage: %s was written by an older host (its reals and text in the host's byte order);"
                  " mount it with newton first, which repairs it" % args.file, file=sys.stderr)
            return 1
        write_flat(args.out, image.flash)
    return 0


if __name__ == "__main__":
    sys.exit(main())
