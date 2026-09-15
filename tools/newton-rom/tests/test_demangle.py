"""Tests for newtonrom.demangle.

Two layers:

1. Hand-written cases whose expected output was cross-checked against the
   Apple headers in headers/ (these pin down the back-reference semantics that
   libiberty gets wrong).
2. An oracle comparison against tools/mpdumper's pre-generated output for the
   MP2x00 US ROM (Symbols_demangled_by_address.txt).  For every symbol that
   libiberty managed to demangle, our text must match exactly.  Symbols that
   libiberty left mangled are reported (they are the nested back-reference
   cases) but do not fail the test.

Run with:  python -m pytest tools/newton-rom/tests
       or: python tools/newton-rom/tests/test_demangle.py   (no pytest needed)
"""

import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))

from newtonrom.demangle import demangle  # noqa: E402

US_IMAGE = os.path.join(ROOT, "DebugRom", "MP2x00 US", "Senior CirrusNoDebug image")
ORACLE = os.path.join(ROOT, "tools", "mpdumper", "Symbols_demangled_by_address.txt")

CASES = {
    # constructors / destructors / operators
    "__ct__10TString255Fv": "TString255::TString255(void)",
    "__dt__10TString255Fv": "TString255::~TString255(void)",
    "__as__8TAddressFRC8TAddress": "TAddress::operator=(TAddress const &)",
    "__nw__6TArrayFUl": "TArray::operator new(unsigned long)",
    # const member function
    "GetLineRightBlanks__12TXBasicRulerCFv": "TXBasicRuler::GetLineRightBlanks(void) const",
    # free function with repeats
    "AddDomainToEnvironment__FUlN21":
        "AddDomainToEnvironment(unsigned long, unsigned long, unsigned long)",
    # nested scope
    "Foo__Q23Bar3BazFi": "Bar::Baz::Foo(int)",
    # function pointer parameter
    "RegisterGPIOInterrupt__20TVoyagerMiscIntfImplFUcPvPFPv_vUl":
        "TVoyagerMiscIntfImpl::RegisterGPIOInterrupt(unsigned char, void *, void (*)(void *), unsigned long)",
    # T inside a nested list refers to the nested list itself when the outer
    # list is still empty (checked against TMonitor::Init in UserMonitor.h)
    "Init__8TMonitorFPFPvUlT1_vUlPvP12TEnvironmentUcT2T5":
        "TMonitor::Init(void (*)(void *, unsigned long, void *), unsigned long, void *, "
        "TEnvironment *, unsigned char, unsigned long, unsigned char)",
    "Init__9TUMonitorFPFPvUlT1_vUlPvT2UcT2T5":
        "TUMonitor::Init(void (*)(void *, unsigned long, void *), unsigned long, void *, "
        "unsigned long, unsigned char, unsigned long, unsigned char)",
    # ... and to the enclosing list's earlier entries otherwise
    "GetPicBits__FlP7PicPlayPCPFT1T2P8GrafPort_v":
        "GetPicBits(long, PicPlay *, void (*const *)(long, PicPlay *, GrafPort *))",
    # pointer to member function
    "ReadWrite__17TNewInternalFlashFM11TFlashRangeFUlT2Pc_lN22Pc":
        "TNewInternalFlash::ReadWrite(long (TFlashRange::*)(unsigned long, unsigned long, char *), "
        "unsigned long, unsigned long, char *)",
    # static member function marker and unsigned-volatile
    "CleanErrorStatus__19T28F016_SA_SVDriverSFPUVlUlT2":
        "T28F016_SA_SVDriver::CleanErrorStatus(volatile unsigned long *, unsigned long, unsigned long) static",
    # '__' inside a plain identifier
    "Decision_GU_or_O___FP13CrossInfoType": "Decision_GU_or_O_(CrossInfoType *)",
    # static data member
    "fgInstance__4TFoo": "TFoo::fgInstance",
}

NOT_MANGLED = ["Reset", "gInitHardware", "SYM__xxtemplabel", "__fp_decode", "_DebugStr",
               "Image$$RO$$Limit", "kEntriesPerSlot"]


class HandWrittenCases(unittest.TestCase):
    def test_cases(self):
        for mangled, expected in CASES.items():
            with self.subTest(mangled=mangled):
                r = demangle(mangled)
                self.assertIsNotNone(r, mangled)
                self.assertEqual(r.signature, expected)

    def test_not_mangled(self):
        for name in NOT_MANGLED:
            with self.subTest(name=name):
                self.assertIsNone(demangle(name))

    def test_structure(self):
        r = demangle("Init__8TMonitorFPFPvUlT1_vUlPvP12TEnvironmentUcT2T5")
        self.assertEqual(r.scope, ["TMonitor"])
        self.assertEqual(r.name, "Init")
        self.assertEqual(r.kind, "function")
        self.assertEqual(len(r.params), 7)
        self.assertFalse(r.is_const)
        r = demangle("__dt__10TString255Fv")
        self.assertEqual(r.kind, "dtor")
        self.assertEqual(r.params, [])


@unittest.skipUnless(os.path.exists(US_IMAGE) and os.path.exists(ORACLE),
                     "US ROM image or mpdumper oracle output not present")
class OracleComparison(unittest.TestCase):
    """Compare against libiberty's output for every symbol in the US ROM."""

    LINE = re.compile(r"^\s+\S+\s+symbol: (.*) = ([0-9A-F]{8})$")

    def test_against_mpdumper(self):
        from newtonrom.aif import AIFImage
        from newtonrom.symbols import read_symbols

        oracle = {}
        with open(ORACLE, encoding="latin-1") as f:
            for line in f:
                m = self.LINE.match(line.rstrip("\n"))
                if m:
                    oracle.setdefault(int(m.group(2), 16), set()).add(m.group(1))

        syms = read_symbols(AIFImage.from_file(US_IMAGE))
        self.assertEqual(len(syms), sum(len(v) for v in oracle.values()))

        mismatches, libiberty_failed = [], []
        for s in syms:
            # libiberty prints "(char const *,...)"; we print ", ..." like everyone else
            candidates = {c.replace(",...", ", ...") for c in oracle[s.value]}
            r = demangle(s.name)
            text = r.signature if r else s.name
            if text in candidates:
                continue
            if s.name in candidates:
                # libiberty could not demangle it; we did (or also left it)
                libiberty_failed.append((s.name, text))
            else:
                mismatches.append((s.name, text, sorted(candidates)))

        if libiberty_failed:
            print(f"\n{len(libiberty_failed)} symbols demangled that libiberty left mangled, e.g.:")
            for name, text in libiberty_failed[:5]:
                print(f"   {name}\n      -> {text}")
        self.assertEqual(mismatches, [], f"{len(mismatches)} mismatches, first: {mismatches[:5]}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
