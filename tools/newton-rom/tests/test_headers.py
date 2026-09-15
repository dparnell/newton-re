"""Tests for newtonrom.headers (DDK header extraction and prototype matching).

Needs the `libclang` package; skipped otherwise.  The layout expectations were
verified against the ROM: TAEventHandler's constructor allocates 0x14 bytes,
stores the vtable at offset 0 and its fields at 4..0x10, and TUObject's fId is
at offset 0 (its SingleObject base takes no space).

Run with:  python tools/newton-rom/tests/test_headers.py
"""

import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))

from newtonrom import headers  # noqa: E402
from newtonrom.demangle import demangle  # noqa: E402

try:
    import clang.cindex  # noqa: F401
    HAVE_CLANG = True
except ImportError:
    HAVE_CLANG = False

HEADERS = os.path.join(ROOT, "headers")

_cache = {}


def parsed():
    if "data" not in _cache:
        tmp = tempfile.mkdtemp(prefix="newton-hdr-")
        headers.prepare(HEADERS, tmp)
        _cache["data"] = headers.extract(headers.parse(tmp), tmp)
    return _cache["data"]


@unittest.skipUnless(HAVE_CLANG and os.path.isdir(HEADERS), "libclang or headers/ not available")
class Extraction(unittest.TestCase):
    def test_counts(self):
        d = parsed()
        self.assertGreater(len(d["records"]), 240)
        self.assertGreater(len(d["enums"]), 50)
        self.assertGreater(len(d["typedefs"]), 150)
        self.assertGreater(len(d["functions"]), 500)
        # only the harmless "$\\u0000" macro complaints and one MPW-ism remain
        self.assertLess(len([x for x in d["diagnostics"] if "universal character" not in x["message"]]), 5)

    def test_layout_matches_rom(self):
        r = parsed()["records"]
        h = r["TAEventHandler"]
        self.assertEqual(h["size"], 0x14)
        self.assertTrue(h["introduces_vptr"])
        self.assertEqual([(f["name"], f["offset"]) for f in h["fields"]],
                         [("fNext", 4), ("fEventClass", 8), ("fEventID", 12), ("fIdler", 16)])
        u = r["TUObject"]
        self.assertFalse(u["polymorphic"])
        self.assertEqual(u["fields"][0], {"name": "fId", "type": {"k": "named", "name": "TObjectId"}, "offset": 0})
        self.assertEqual(u["size"], 8)
        s = r["TSystemEventHandler"]
        self.assertEqual(s["bases"][0], {"name": "TAEventHandler", "virtual": False, "offset": 0})
        self.assertFalse(s["introduces_vptr"])
        self.assertEqual(s["fields"][0]["offset"], 0x14)
        self.assertEqual(r["TTime"]["size"], 8)
        self.assertEqual(r["Point"]["fields"][1], {"name": "h", "type": {"k": "named", "name": "short"}, "offset": 2})

    def test_bitfields(self):
        f = parsed()["records"]["TCardPCMCIA"]["fields"]
        self.assertEqual((f[0]["name"], f[0]["bit_offset"], f[0]["bit_width"]), ("fNoAttrMem", 0, 1))
        self.assertEqual((f[1]["name"], f[1]["bit_offset"]), ("fBadCIS", 1))

    def test_typedefs_and_enums(self):
        d = parsed()
        self.assertEqual(d["typedefs"]["ULong"], {"k": "named", "name": "unsigned long"})
        self.assertEqual(d["typedefs"]["Boolean"], {"k": "named", "name": "unsigned char"})
        self.assertEqual(d["typedefs"]["RefArg"], {"k": "ref", "t": {"k": "qual", "const": True, "volatile": False,
                                                                   "t": {"k": "named", "name": "RefVar"}}})
        self.assertIn(["kSeconds", 3686400], d["enums"]["TimeUnits"]["values"])

    def test_no_unresolved_types(self):
        import json
        text = json.dumps(parsed()["records"])
        self.assertNotIn('"unresolved"', text)


@unittest.skipUnless(HAVE_CLANG and os.path.isdir(HEADERS), "libclang or headers/ not available")
class PrototypeMatching(unittest.TestCase):
    def test_method_match(self):
        h = headers.HeaderTypes(parsed())
        d = demangle("InitIdler__14TAEventHandlerFUl9TimeUnitsT1Uc").to_json()
        m = h.find_method("TAEventHandler", "InitIdler", d["params"], d["static"])
        self.assertIsNotNone(m)
        self.assertEqual([p["name"] for p in m["params"]], ["idleAmount", "idleUnits", "refCon", "start"])
        self.assertEqual(m["ret"], {"k": "named", "name": "NewtonErr"})
        # the other overload must not be confused with it
        d2 = demangle("InitIdler__14TAEventHandlerFUlT1Uc").to_json()
        m2 = h.find_method("TAEventHandler", "InitIdler", d2["params"], d2["static"])
        self.assertEqual([p["name"] for p in m2["params"]], ["idle", "refCon", "start"])

    def test_reference_and_typedef_params(self):
        h = headers.HeaderTypes(parsed())
        d = demangle("Init__8TMonitorFPFPvUlT1_vUlPvP12TEnvironmentUcT2T5").to_json()
        # TMonitor::Init in UserMonitor.h takes TObjectId, not TEnvironment*, so no match
        self.assertIsNone(h.find_method("TMonitor", "Init", d["params"], d["static"]))
        d = demangle("SetTimerParms__14TUSharedMemMsgFUlP5TTime").to_json()
        m = h.find_method("TUSharedMemMsg", "SetTimerParms", d["params"], d["static"])
        self.assertEqual([p["name"] for p in m["params"]], ["timeout", "delay"])

    def test_c_function_by_name(self):
        h = headers.HeaderTypes(parsed())
        self.assertEqual(h.find_function("NewPtr")["ret"], {"k": "named", "name": "Ptr"})
        self.assertIsNone(h.find_function("NoSuchFunction"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
