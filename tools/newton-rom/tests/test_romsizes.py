"""romsizes.py finds a ROM byte count sizing a struct that is wider on the
host (the sound server's replies, before 2026-09-30), and leaves alone a
size that is right.

    python tools/newton-rom/tests/test_romsizes.py
"""

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'analysis'))
import romsizes

HEADER = '''
typedef uintptr_t ULong;
struct TAEvent { unsigned long fAEventClass; unsigned long fAEventID; };
struct Reply { TAEvent fEvent; ULong fChannel; long fError; ULong fNodeId; };
struct Bytes { unsigned char fData[0x20]; };
'''

SOURCE = '''
#include "t.h"
void Answer(TUMsgToken* token)
{
	Reply reply;
	ULong size = 0x20;
	token->ReplyRPC(&reply, 0x14, 0);
	SetReply(size, &reply.fEvent);
	Bytes b, c;
	memcpy(&b, &c, 0x20);
	Reply* r = (Reply*) NewPtr(sizeof(Reply));
}
'''


def run(tree, **options):
    out = io.StringIO()
    romsizes.LP64 = options.get('lp64', False)
    with redirect_stdout(out):
        romsizes.scan(tree, [], options.get('all', False), False)
    return out.getvalue()


class TestRomSizes(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        src = os.path.join(self.dir.name, 'src')
        os.makedirs(src)
        open(os.path.join(src, 't.h'), 'w').write(HEADER)
        open(os.path.join(src, 't.cpp'), 'w').write(SOURCE)
        self.src = src

    def tearDown(self):
        self.dir.cleanup()

    def test_wide_reply_sizes(self):
        out = run(self.src)
        self.assertIn('t.cpp:7: WIDE token->ReplyRPC(&reply, 0x14, 0)', out)
        self.assertIn('t.cpp:8: WIDE SetReply(size, &reply.fEvent)  {size = 0x20}', out)
        self.assertIn('2 wide', out)

    def test_narrow_copy(self):
        out = run(self.src, all=True)
        self.assertIn('t.cpp:10: NARROW memcpy(&b, &c, 0x20)', out)
        self.assertNotIn('NewPtr', out)			# (sizeof: nothing to see)

    def test_lp64_long(self):
        # Bytes holds nothing wide even on LP64; TAEvent does there
        romsizes.LP64 = True
        structs = romsizes.collect_structs(self.src)
        self.assertTrue(romsizes.is_wide('TAEvent', structs))
        self.assertFalse(romsizes.is_wide('Bytes', structs))
        romsizes.LP64 = False
        self.assertFalse(romsizes.is_wide('TAEvent', structs))


if __name__ == '__main__':
    unittest.main()
