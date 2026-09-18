#!/usr/bin/env python3
"""Emit the NewtonScript compiler's parser tables and grammar from the ROM.

Usage:
    python nsgrammar.py <build_dir> -o <src/frames dir> [--doc docs/frames/grammar.md]
    python nsgrammar.py build/MP2100D -o src/frames --doc docs/frames/grammar.md

The ROM's NewtonScript parser (TCompiler::Parser, 0x002fd9b0) is a
Berkeley yacc parser: its tables (yylhs, yylen, yydefred, yydgoto,
yysindex, yyrindex, yygindex, yytable, yycheck - arrays of 16-bit words
in the ROM's read-only data), the token names (yyname, an array of C
string pointers) and the grammar's rules as text (yyrule).  This reads
them all through the debug symbols and writes:

  ParserTables.h    the token numbers as an enum (from yyname: tokenSYMBOL
                    259, ...), the parser's constants (YYTABLESIZE, YYFINAL,
                    YYMAXTOKEN, YYERRCODE, as the ROM's parser has them) and
                    the tables' declarations
  ParserTables.cpp  the tables, yyname, yyrule, and the reserved words
                    (gReservedWords, the lexer's table in the initialised
                    RAM area: name, token)
  grammar.md        the grammar, rule by rule, from yyrule (--doc)

The constants: YYTABLESIZE is one less than the yytable count (the parser
checks index <= YYTABLESIZE), YYFINAL is the state the parser accepts in
(31, read from the parser's code), YYMAXTOKEN the last token (yyname's
count - 1), YYERRCODE 256.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import romid			# the ROM names itself in what these write

YYFINAL = 31            # TCompiler::Parser 0x002fd9b0: "shifting from state 0 to state 0x1f"
YYERRCODE = 256

TABLES = ["yylhs", "yylen", "yydefred", "yydgoto", "yysindex", "yyrindex", "yygindex", "yytable", "yycheck"]


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("-o", "--output", required=True, help="directory for ParserTables.h/.cpp")
    ap.add_argument("--doc", help="write the grammar as markdown here")
    args = ap.parse_args(argv)

    with open(os.path.join(args.build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    with open(os.path.join(args.build_dir, "symbols.json"), encoding="utf-8") as f:
        data = json.load(f)
    with open(os.path.join(args.build_dir, "layout.json"), encoding="utf-8") as f:
        layout = json.load(f)
    by_name = {}
    addresses = []
    for s in data["symbols"]:
        if "jt_index" in s:
            continue
        by_name.setdefault(s["name"], s["address"])
        addresses.append(s["address"])
    addresses = sorted(set(addresses))
    rwinit = next(r for r in layout["regions"] if r["name"] == "ROM_RWINIT")
    data_base = layout["aif_header"]["data_base"]
    rw_size = layout["aif_header"]["rw_area_size"]

    def next_symbol_after(addr: int) -> int:
        import bisect
        i = bisect.bisect_right(addresses, addr)
        return addresses[i] if i < len(addresses) else len(rom)

    def read(addr: int, size: int) -> bytes:
        if data_base <= addr < data_base + rw_size:
            off = rwinit["rom_offset"] + (addr - data_base)
            return rom[off:off + size]
        return rom[addr:addr + size]

    def cstr(addr: int) -> str:
        end = rom.index(b"\0", addr)
        return rom[addr:end].decode("latin-1")

    # the 16-bit tables
    tables = {}
    for name in TABLES:
        addr = by_name[name]
        count = (next_symbol_after(addr) - addr) // 2
        tables[name] = (addr, list(struct.unpack(">%dh" % count, rom[addr:addr + 2 * count])))

    # yyname: pointers until yyrule
    names_addr = by_name["yyname"]
    names = []
    a = names_addr
    while a < by_name["yyrule"]:
        p = struct.unpack(">I", rom[a:a + 4])[0]
        names.append(cstr(p) if p != 0 else None)
        a += 4
    # yyrule: pointers until a 0 or something not a string
    rules_addr = by_name["yyrule"]
    rules = []
    a = rules_addr
    while True:
        p = struct.unpack(">I", rom[a:a + 4])[0]
        if p == 0 or p >= len(rom):
            break
        rules.append(cstr(p))
        a += 4

    # The reserved words TCompiler::ReservedWordToken searches: (name
    # pointer, token) pairs in the initialised RAM area, 0-terminated and
    # in no particular order (`do` comes before `div`).  The table has no
    # symbol and its RAM address differs between ROMs, so it is found by
    # its shape - a run of pairs, the first naming `and`, each naming a
    # distinct word, ending in a zero pointer.
    def reserved_words_at(addr: int):
        words = []
        seen = set()
        a = addr
        while True:
            pair = read(a, 8)
            if len(pair) < 8:
                return None
            ptr, token = struct.unpack(">II", pair)
            if ptr == 0:
                break
            if ptr >= len(rom) or token == 0 or token > 0x400:
                return None
            try:
                name = cstr(ptr)
            except ValueError:
                return None
            if not name or not name.isascii() or not name.replace("_", "").isalnum():
                return None
            if name in seen or len(words) > 256:
                return None
            seen.add(name)
            words.append((name, token))
            a += 8
        return words if len(words) > 8 else None

    first = rom.find(b"and\0")
    found = []
    while first >= 0:
        pointer = struct.pack(">I", first)
        at = rwinit["rom_offset"]
        end = at + rw_size
        while True:
            at = rom.find(pointer, at, end)
            if at < 0 or at % 4:
                if at < 0:
                    break
                at += 1
                continue
            addr = data_base + (at - rwinit["rom_offset"])
            words = reserved_words_at(addr)
            if words is not None:
                found.append((addr, words))
            at += 4
        first = rom.find(b"and\0", first + 1)
    if len(found) != 1:
        print(f"error: {len(found)} reserved word tables, expected one", file=sys.stderr)
        return 1
    reserved_addr, reserved = found[0]

    tokens = [(i, n) for i, n in enumerate(names) if n is not None and n.startswith("token")]
    tablesize = len(tables["yytable"][1]) - 1
    maxtoken = len(names) - 1

    header = [
        "// Generated by tools/newton-rom/analysis/nsgrammar.py; do not edit.",
        f"//   python tools/newton-rom/analysis/nsgrammar.py <build_dir> -o {args.output.replace(os.sep, '/')}",
        "// The NewtonScript parser's tokens, constants and tables, read from the",
        f"// {romid.rom_version(args.build_dir)} ROM (TCompiler::Parser is a Berkeley yacc",
        "// parser; the tables are its own, at the addresses cited in ParserTables.cpp).",
        "",
        "#ifndef __PARSERTABLES_H",
        "#define __PARSERTABLES_H",
        "",
        "// the tokens (yyname); characters are their own tokens",
        "enum {",
    ]
    for i, n in tokens:
        header.append(f"\t{n} = {i},")
    header += [
        "};",
        "",
        f"const int YYTABLESIZE = {tablesize};",
        f"const int YYFINAL = {YYFINAL};",
        f"const int YYMAXTOKEN = {maxtoken};",
        f"const int YYERRCODE = {YYERRCODE};",
        f"const int kNumParserRules = {len(rules)};",
        f"const int kNumReservedWords = {len(reserved)};",
        "",
    ]
    for name in TABLES:
        header.append(f"extern const short\t{name}[{len(tables[name][1])}];")
    header += [
        f"extern const char* const\tyyname[{len(names)}];",
        f"extern const char* const\tyyrule[{len(rules)}];",
        "",
        "// the lexer's reserved words and their tokens (sorted by name)",
        "struct ReservedWord { const char* fName; int fToken; };",
        f"extern const ReservedWord\tgReservedWords[{len(reserved)}];",
        "",
        "#endif\t/* __PARSERTABLES_H */",
        "",
    ]
    with open(os.path.join(args.output, "ParserTables.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header))

    out = [
        "// Generated by tools/newton-rom/analysis/nsgrammar.py; do not edit.",
        f"//   python tools/newton-rom/analysis/nsgrammar.py <build_dir> -o {args.output.replace(os.sep, '/')}",
        "// The NewtonScript parser's tables (Berkeley yacc's, from the ROM's",
        "// read-only data), its token names and grammar rules, and the lexer's",
        "// reserved words (from the initialised RAM area).",
        "",
        '#include "ParserTables.h"',
        "",
    ]
    for name in TABLES:
        addr, values = tables[name]
        out.append(f"// ROM 0x{addr:08x} {name}")
        out.append(f"const short\t{name}[{len(values)}] = {{")
        for i in range(0, len(values), 12):
            out.append("\t" + " ".join(f"{v}," for v in values[i:i + 12]))
        out.append("};")
        out.append("")
    out.append(f"// ROM 0x{names_addr:08x} yyname")
    out.append(f"const char* const\tyyname[{len(names)}] = {{")
    for n in names:
        out.append("\t" + (c_string(n) if n is not None else "0") + ",")
    out.append("};")
    out.append("")
    out.append(f"// ROM 0x{rules_addr:08x} yyrule")
    out.append(f"const char* const\tyyrule[{len(rules)}] = {{")
    for r in rules:
        out.append("\t" + c_string(r) + ",")
    out.append("};")
    out.append("")
    lexer = by_name["ReservedWordToken__9TCompilerFPc"]
    out.append(f"// the reserved words TCompiler::ReservedWordToken (0x{lexer:08x}) searches: an unnamed table at 0x{reserved_addr:08x} in the initialised RAM area")
    out.append(f"const ReservedWord\tgReservedWords[{len(reserved)}] = {{")
    for n, t in reserved:
        tname = names[t] if t < len(names) and names[t] else str(t)
        out.append(f"\t{{ {c_string(n)}, {tname} }},")
    out.append("};")
    out.append("")
    with open(os.path.join(args.output, "ParserTables.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))

    if args.doc:
        doc = [
            "# The NewtonScript grammar",
            "",
            "Generated by `tools/newton-rom/analysis/nsgrammar.py <build_dir> --doc docs/frames/grammar.md`",
            "from the %s ROM's `yyrule` table (0x%08x): the rules of the Berkeley yacc grammar"
            % (romid.rom_version(args.build_dir), rules_addr),
            "`TCompiler::Parser` (0x002fd9b0) parses, in yacc's own notation (`$$n` is a",
            "mid-rule action).  `tokenX` are the lexer's tokens (`yyname`, 0x%08x); the" % names_addr,
            "reserved words, in the lexer's table, are:",
            "",
            "    " + " ".join(n for n, t in reserved),
            "",
            "Precedence and associativity are in the tables, not the rules: `:=` is",
            "right-associative and lowest, then `and`/`or`, `not`, the comparisons,",
            "`&`/`&&`, `+`/`-`, `*`/`/`/`div`/`mod`, `<<`/`>>`, unary `-`, `exists`,",
            "then `.`/`[]`/`:` (the operator table of the NewtonScript Reference).",
            "",
            "```",
        ]
        for i, r in enumerate(rules):
            doc.append(f"{i:3d}  {r}")
        doc += ["```", ""]
        with open(args.doc, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(doc))

    print(f"{len(rules)} rules, {len(tokens)} tokens, {len(reserved)} reserved words -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
