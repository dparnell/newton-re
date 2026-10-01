#!/usr/bin/env python3
"""includecase.py - quoted #includes spelt with the case of the file they name.

Windows (and WSL's view of a Windows drive) finds `#include "Objects.h"`
when the file is `objects.h`; Linux does not, and the build stops there.
This checks every quoted include in a source tree against the files that
are actually in it, so the mistake shows up on the host it is made on.

An include passes when a file whose path ends with the included path,
spelt exactly so, exists anywhere in the tree (the build's include
directories are all inside it).  It is reported when the only files that
end with it differ in case.  Includes that name no file in the tree at all
(system headers, generated files) are not this tool's business.

Usage:
    python tools/host/includecase.py [SRC]       (default: src)

Output: one line per mis-spelt include, `file:line: "Name.h" is name.h`;
exits 1 if there are any, 0 otherwise.  ctest tools.IncludeCase.
"""

import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "src"
    exact = set()           # every file's path suffixes, as spelt
    folded = {}             # lower-cased suffix -> the spellings there are
    sources = []
    for directory, _, names in os.walk(root):
        for name in names:
            path = os.path.relpath(os.path.join(directory, name), root).replace(os.sep, "/")
            parts = path.split("/")
            for i in range(len(parts)):
                suffix = "/".join(parts[i:])
                exact.add(suffix)
                folded.setdefault(suffix.lower(), set()).add(suffix)
            if name.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                sources.append(os.path.join(directory, name))
    bad = 0
    for source in sorted(sources):
        with open(source, "rb") as f:
            text = f.read().decode("latin-1")
        # (the DDK's headers end their lines with a bare CR)
        for number, line in enumerate(re.split(r"\r\n|\r|\n", text), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            included = m.group(1).replace("\\", "/")
            while included.startswith("./"):
                included = included[2:]
            if included in exact or included.lower() not in folded:
                continue
            spellings = ", ".join(sorted(folded[included.lower()]))
            print("%s:%d: \"%s\" is %s" % (os.path.relpath(source), number, m.group(1), spellings))
            bad += 1
    if bad:
        print("%d include%s spelt with the wrong case" % (bad, "" if bad == 1 else "s"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
