#!/usr/bin/env python3
"""Tests for flashimage.py (ctest tools.FlashImage):

    python tools/stores/test_flashimage.py

A flat image with a few written bytes goes to sparse and back unchanged; the
sparse copy holds only the chunks that are not erased; a new 128 MB sparse
image is small; and a map word naming a slot past the end of the file reads
as an erased chunk, as the host reads it (src/hal/host/HostFlash.h).
Temporary files go under the repository's tmp/.
"""

import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import flashimage  # noqa: E402

TMP = os.path.join(HERE, "..", "..", "tmp")


class FlashImageTest(unittest.TestCase):
    def setUp(self):
        os.makedirs(TMP, exist_ok=True)
        self.dir = tempfile.mkdtemp(prefix="flashimage-", dir=TMP)

    def path(self, name):
        return os.path.join(self.dir, name)

    def test_round_trip(self):
        flash = bytearray(b"\xff" * (8 << 20))
        flash[0x20000:0x20004] = b"\x00\x00\x00\xff"
        flash[0x5ffffc:0x600000] = b"newt"
        with open(self.path("flat"), "wb") as f:
            f.write(flash)
        self.assertEqual(flashimage.main(["to-sparse", self.path("flat"), self.path("sparse")]), 0)
        image = flashimage.read_image(self.path("sparse"))
        self.assertEqual(image.format, "sparse")
        self.assertEqual(image.stored_chunks, 2)
        self.assertEqual(os.path.getsize(self.path("sparse")),
                         flashimage.data_offset(8 << 20) + 2 * flashimage.CHUNK)
        self.assertEqual(flashimage.main(["to-flat", self.path("sparse"), self.path("back")]), 0)
        with open(self.path("back"), "rb") as f:
            self.assertEqual(f.read(), bytes(flash))

    def test_new_sparse_is_small(self):
        self.assertEqual(flashimage.main(["make-sparse", self.path("big"), "--size", "128"]), 0)
        size = os.path.getsize(self.path("big"))
        self.assertEqual(size, flashimage.data_offset(128 << 20))
        self.assertLess(size, 600 * 1024)
        self.assertEqual(flashimage.main(["info", self.path("big"), "--expect-stored", ":1"]), 0)
        self.assertEqual(flashimage.main(["info", self.path("big"), "--expect-stored", "2:"]), 1)

    def test_slot_past_end_is_erased(self):
        flashimage.main(["make-sparse", self.path("torn"), "--size", "4"])
        with open(self.path("torn"), "r+b") as f:
            f.seek(flashimage.HEADER_SIZE + 4 * 7)
            f.write(struct.pack(">I", 5))
        image = flashimage.read_image(self.path("torn"))
        self.assertEqual(image.stored_chunks, 0)
        self.assertEqual(image.flash.count(0xFF), 4 << 20)


if __name__ == "__main__":
    unittest.main()
