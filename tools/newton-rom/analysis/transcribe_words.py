#!/usr/bin/env python3
"""Transcribe Ghidra's C for small word-at-a-time blitter routines into
host C++ whose every 32-bit word access goes through big-endian
accessors, so that the routines work on buffers laid out as the ARM's
memory (big-endian words) on any host.

Usage:
    python transcribe_words.py decompiled.c NAME [NAME ...] > out.cpp

The input is `decompile.py` output (one or more functions).  Each named
function is emitted with:
  - its types made fixed-width: uint/ulong/long/int -> ULong32/Long32,
    uchar/byte -> UChar/UByte;
  - every dereference of a word pointer (a variable declared as a pointer
    to a 32-bit type, or a `*(uint *)` cast) read through LW(p) and every
    store `*p = v;` made SW(p, v);
  - `(char *)((int)X + n)` pointer arithmetic made `(X + n)`.
The output still has to be read, cleaned and cited by hand: this only
removes the retyping that a hand transcription would get wrong.  It was
written for the row stretchers and combiners of StretchBits
(src/qd/Stretch.cpp).
"""

import re
import sys

WORD = r"(?:uint|ulong|long|int|undefined4)"
BYTE = r"(?:char|uchar|byte|undefined1)"


def functions(text):
    """Split decompile.py output into {name: body}."""
    out = {}
    for block in re.split(r"^=+\n", text, flags=re.M):
        m = re.search(r"^// (\S+)\s+@", block, re.M)
        if not m:
            continue
        out[m.group(1)] = block[block.index("\n", m.end()) + 1:]
    return out


def join_statements(body):
    """A statement the decompiler broke over several lines put on one."""
    out = []
    pending = None
    for line in body.split("\n"):
        stripped = line.strip()
        if pending is not None:
            pending += " " + stripped
            if stripped.endswith((";", "{", "}")):
                out.append(pending)
                pending = None
            continue
        if (stripped and not stripped.endswith((";", "{", "}", ":", "*/"))
                and not stripped.startswith(("/*", "//", "#"))):
            pending = line.rstrip()
            continue
        out.append(line)
    if pending is not None:
        out.append(pending)
    return "\n".join(out)


def convert(body):
    head, brace, rest = body.partition("{")
    body = head + brace + join_statements(rest)
    word_ptrs = set(re.findall(WORD + r"\s*\*\s*(\w+)", body))
    pp = set(re.findall(WORD + r"\s*\*\*\s*(\w+)", body))
    word_ptrs -= pp

    def store(m):
        return "%sSW(%s, %s);" % (m.group(1), m.group(2), m.group(3))

    # word stores and loads through a cast first, while the cast is there
    body = re.sub(r"^(\s*)\*\(uint \*\)(\w+) = (.*);$", store, body, flags=re.M)
    body = re.sub(r"\*\(uint \*\)(\w+)", r"LW(\1)", body)
    body = re.sub(r"\((?:char|uint|long|ulong|int|byte) \*\)\(\(int\)(\w+) \+ ([^)]+)\)", r"(\1 + \2)", body)
    body = re.sub(r"\((?:long|uint|ulong|int) \*\)", "", body)

    head, brace, rest = body.partition("{")
    for v in pp:
        rest = rest.replace("(*%s)[-1]" % v, "LW(*%s - 1)" % v)
        rest = rest.replace("**%s" % v, "LW(*%s)" % v)
    body = head + brace + rest

    for v in sorted(word_ptrs, key=len, reverse=True):
        body = re.sub(r"^(\s*)\*(%s) = (.*);$" % v, store, body, flags=re.M)

    decl = re.compile(r"^\s*" + r"(?:" + WORD + "|" + BYTE + r")" + r"\s*\*")
    lines = body.split("\n")
    for i, line in enumerate(lines):
        stripped = line.lstrip()
        if decl.match(line) or stripped.startswith("/*") or "__" in line or re.match(r"^\w[\w\s]*\w\s*\(", line):
            continue
        for v in sorted(word_ptrs, key=len, reverse=True):
            # a dereference, not a multiplication (the decompiler writes
            # those with spaces round the star)
            line = re.sub(r"(?<![\w\]\)])\*%s\b" % v, "LW(%s)" % v, line)
            line = re.sub(r"(?<=\))\*%s\b" % v, "LW(%s)" % v, line)
        lines[i] = line
    body = "\n".join(lines)

    body = re.sub(WORD + r"\s*\*\*\s*(\w+)", r"ULong32** \1", body)
    body = re.sub(WORD + r"\s*\*\s*(\w+)", r"ULong32* \1", body)
    body = re.sub(r"\bulong\b|\buint\b", "ULong32", body)
    body = re.sub(r"\blong\b|\bint\b", "Long32", body)
    body = re.sub(r"\buchar\b", "UChar", body)
    body = re.sub(r"\bbyte\b", "UByte", body)
    return body


def main():
    text = open(sys.argv[1], encoding="utf-8").read()
    funcs = functions(text)
    for name in sys.argv[2:]:
        if name not in funcs:
            sys.exit("no function %s" % name)
        print("// " + name)
        print(convert(funcs[name]))


if __name__ == "__main__":
    main()
