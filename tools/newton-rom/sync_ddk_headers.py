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
