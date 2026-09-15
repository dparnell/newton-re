#!/usr/bin/env python3
"""Produce the compilable copy of the DDK headers used by the reconstruction (src/ddk).

Usage:
    python sync_ddk_headers.py headers src/ddk

Copies every header into one flat directory (the DDK's includes are flat),
converting Mac Roman text and CR line endings to UTF-8/LF, and applies the
small list of PATCHES below - things the 1990s ARM compiler accepted but a
modern C++ compiler rejects.  Each patch is exact-match and must apply, so a
change in the originals is noticed.  The output is committed; re-run this
after touching headers/ or PATCHES.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from newtonrom.headers import prepare  # noqa: E402

CLIBRARY = ["limits.h", "New.h", "setjmp.h", "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h"]

PATCHES = {
    # UserSemaphore.h: GetRefCon takes void**, fSem is a ULong* -> needs a cast in C++
    "UserSemaphore.h": [
        ("TULockingSemaphore(TObjectId id = 0) : TUSemaphoreGroup(id) { GetRefCon(&fSem); }",
         "TULockingSemaphore(TObjectId id = 0) : TUSemaphoreGroup(id) { GetRefCon((void**)&fSem); }"),
    ],
    # NewtonTypes.h: a virtual address is a 32-bit word on the MessagePad; a host build
    # (host_compat.h defines hostVAddrIsPointerSized) keeps whole host pointers in it,
    # since task stacks and the buffers behind shared memory are host memory there
    "NewtonTypes.h": [
        ("typedef ULong\tVAddr;",
         "#ifdef hostVAddrIsPointerSized\ntypedef uintptr_t\tVAddr;\n#else\ntypedef ULong\tVAddr;\n#endif"),
        # ULong/Long are the ARM's 32-bit word, which also carries pointers (refcons, task
        # arguments); a host build makes them pointer-sized, as an LP64 Linux does anyway
        ("typedef long\t\t\tLong;\t\t\t/* In ANSI C long is signed long */\ntypedef signed long\t\tSLong;\ntypedef unsigned long\tULong;\n\ntypedef signed long\t\tFastInt;",
         "#ifdef hostLongIsPointerSized\ntypedef intptr_t\t\tLong;\ntypedef intptr_t\t\tSLong;\ntypedef uintptr_t\t\tULong;\ntypedef intptr_t\t\tFastInt;\n#else\ntypedef long\t\t\tLong;\t\t\t/* In ANSI C long is signed long */\ntypedef signed long\t\tSLong;\ntypedef unsigned long\tULong;\n\ntypedef signed long\t\tFastInt;\n#endif"),
    ],
    # UserTasks.h: the include is spelt in the wrong case for a case-sensitive file system;
    # TUTaskWorld's spawned task starts at a member function in the ROM (its address is
    # passed as the TaskProcPtr), which C++ forbids - a static trampoline stands in
    "UserTasks.h": [
        ('#include "sharedTypes.h"', '#include "SharedTypes.h"'),
        ("\t\tvoid\t\t\tTaskEntry(ULong, TObjectId taskId);\t// low level entry for spawned task (only in base class)",
         "\t\tvoid\t\t\tTaskEntry(ULong, TObjectId taskId);\t// low level entry for spawned task (only in base class)\n"
         "\t\tstatic void\t\tTaskEntryProc(void* theObject, ULong size, TObjectId taskId);\t// the TaskProcPtr that calls TaskEntry (reconstruction)"),
    ],
    # UserPorts.h: the ROM's Sleep() and TUTaskWorld::StartTask use TUPort's private
    # Send*Goo like SleepTill does, but only SleepTill is a friend in the DDK's header
    # UserDomain.h: tokens after #endif; the include is spelt in the wrong case
    "UserDomain.h": [
        ("#endif __USERDOMAIN__", "#endif /* __USERDOMAIN__ */"),
        ('#include "sharedTypes.h"', '#include "SharedTypes.h"'),
    ],
    # DynamicArray.h: pointer arithmetic through a long truncates 64-bit host pointers
    "DynamicArray.h": [
        ("{ return (void*)((long)fArrayBlock + (fElementSize * index)); }",
         "{ return (void*)((char*)fArrayBlock + (fElementSize * index)); }"),
    ],
    "UserPorts.h": [
        ("\t\tfriend void SleepTill(TTime* futureTimeToSend);",
         "\t\tfriend void SleepTill(TTime* futureTimeToSend);\n\t\tfriend void Sleep(TTimeout timeout);\n\t\tfriend class TUTaskWorld;"),
    ],
}


def main(argv=None) -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    # The DDK's CLibrary headers describe Newton's own C library (stddef.h,
    # stdlib.h, ...).  They would shadow the host's, so a host build does not
    # get them; the reconstruction uses the host C library instead.
    names = prepare(src, out, excludes={n: "CLibrary header, host C library used instead" for n in CLIBRARY},
                    write_all_cpp=False)
    for n in CLIBRARY:            # prepare() copies everything; drop the excluded ones
        if os.path.exists(os.path.join(out, n)):
            os.remove(os.path.join(out, n))
    patched = 0
    for name, edits in PATCHES.items():
        path = os.path.join(out, name)
        text = open(path, encoding="utf-8").read()
        for old, new in edits:
            if text.count(old) != 1:
                print(f"error: patch for {name} does not apply exactly once", file=sys.stderr)
                return 1
            text = text.replace(old, new)
            patched += 1
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print(f"wrote {len(names)} headers to {out}, {patched} patches applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
