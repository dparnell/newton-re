"""Tests for the AIF / symbol / REx / jump-table readers against the real ROMs.

These need the images in DebugRom/ and are skipped if they are absent.

Run with:  python -m pytest tools/newton-rom/tests
       or: python tools/newton-rom/tests/test_rom.py
"""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))

from newtonrom import jumptable  # noqa: E402
from newtonrom.aif import LANG_NONE, AIFImage  # noqa: E402
from newtonrom.rex import RExBlock  # noqa: E402
from newtonrom.symbols import by_name, read_symbols  # noqa: E402

ROMS = {
    "MP2100 D": ("Senior DCirrusNoDebug image", "Senior DCirrusNoDebug high", 52150, 16723),
    "MP2x00 US": ("Senior CirrusNoDebug image", "Senior CirrusNoDebug high", 52751, 16919),
}


def rom_path(folder, name):
    return os.path.join(ROOT, "DebugRom", folder, name)


class JumpTableMath(unittest.TestCase):
    def test_round_trip(self):
        for i in (0, 1, 31, 32, 33, 1023, 1024, 16722):
            self.assertEqual(jumptable.virtual_to_index(jumptable.index_to_virtual(i)), i)

    def test_known_addresses(self):
        # first slot, first slot of the second page, first slot after the
        # 32-page diagonal wraps (all observed in the symbol tables)
        self.assertEqual(jumptable.index_to_virtual(0), 0x01A00000)
        self.assertEqual(jumptable.index_to_virtual(32), 0x01A01080)
        self.assertEqual(jumptable.index_to_virtual(1024), 0x01A20000)
        self.assertFalse(jumptable.is_virtual_slot(0x01A00080, 16723))  # gap byte
        self.assertFalse(jumptable.is_virtual_slot(0x01A01000, 16723))  # slice of page 0

    def test_page_source(self):
        self.assertEqual(jumptable.virtual_page_source(0), 0x2000)
        self.assertEqual(jumptable.virtual_page_source(31), 0x2000)
        self.assertEqual(jumptable.virtual_page_source(32), 0x3000)


class RealImages(unittest.TestCase):
    def _each(self):
        for folder, (image, high, nsyms, njt) in ROMS.items():
            if not os.path.exists(rom_path(folder, image)):
                continue
            yield folder, rom_path(folder, image), rom_path(folder, high), nsyms, njt

    def test_header_and_sections(self):
        for folder, image, _, nsyms, _ in self._each():
            with self.subTest(rom=folder):
                img = AIFImage.from_file(image)
                self.assertTrue(img.big_endian)
                h = img.header
                self.assertEqual(h.image_base, 0)
                self.assertEqual(h.rw_base, 0x0C100800)
                secs = list(img.sections())
                self.assertEqual(len(secs), 1)
                self.assertEqual(secs[0].language, LANG_NONE)
                self.assertEqual(secs[0].name_nsyms, nsyms)
                self.assertEqual(secs[0].codesize, h.ro_area_size)

    def test_symbols(self):
        for folder, image, _, nsyms, _ in self._each():
            with self.subTest(rom=folder):
                syms = read_symbols(AIFImage.from_file(image))
                self.assertEqual(len(syms), nsyms)
                names = by_name(syms)
                self.assertEqual(names["Reset"][0].value, 0)
                self.assertTrue(names["Reset"][0].is_code)
                self.assertIn("ROM$$Size", names)

    def test_jump_table_matches_symbols(self):
        for folder, image, _, _, njt in self._each():
            with self.subTest(rom=folder):
                img = AIFImage.from_file(image)
                self.assertEqual(jumptable.JumpTable.locate(img), 0x2000)
                jt = jumptable.JumpTable(img)
                self.assertEqual(jt.count, njt)
                matched, mismatches = jt.verify(read_symbols(img))
                self.assertEqual(matched, njt)
                self.assertEqual([n for _, n in mismatches], ["_DebugStr"])

    def test_rex_follows_rom(self):
        for folder, image, high, _, _ in self._each():
            with self.subTest(rom=folder):
                img = AIFImage.from_file(image)
                rex = RExBlock.from_file(high)
                h = img.header
                self.assertEqual(rex.start, h.image_base + h.ro_area_size + h.rw_area_size)
                self.assertEqual(rex.length, len(rex.data))
                self.assertEqual(by_name(read_symbols(img))["ROM$$Size"][0].value, rex.start)
                self.assertIsNotNone(rex.entry("jump"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
