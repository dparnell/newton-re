#!/usr/bin/env python3
"""Extract structs, classes, enums, typedefs and prototypes from the DDK headers.

Usage:
    python parse_headers.py headers -o build/MP2100D/types.json [--include-dir build/MP2100D/include]
                            [--define NAME[=VAL] ...] [--exclude GLOB ...]

Normalises the headers (Mac Roman, CR line endings) into a flat include
directory, parses them all in one clang translation unit configured for the
MP2100 D build (see newtonrom/headers.py for the defines and exclusions), and
writes types.json:

    records    {name: {kind, size, align, polymorphic, introduces_vptr,
                       bases: [{name, offset}], fields: [{name, type, offset[, bit_offset, bit_width]}],
                       methods: [{name, kind, params, ret, virtual, static, const}], file, line}}
    enums      {name: {size, values: [[name, value]], anonymous, file, line}}
    typedefs   {name: type}
    functions  [{name, params: [{name, type}], ret, variadic, file, line}]
    diagnostics  clang errors (for information; parsing continues past them)

Types use the same JSON schema as dump_symbols.py.  Requires the `libclang`
package (pip install libclang==18.1.1).
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from newtonrom import headers  # noqa: E402


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("headers_dir", help="directory tree with the DDK .h files")
    ap.add_argument("-o", "--out", required=True, help="output types.json")
    ap.add_argument("--include-dir", help="where to write the normalised headers (default: <out dir>/include)")
    ap.add_argument("--define", action="append", default=None, metavar="NAME[=VAL]",
                    help=f"preprocessor define (default: {' '.join(headers.DEFAULT_DEFINES)})")
    ap.add_argument("--exclude", action="append", default=None, metavar="GLOB",
                    help=f"header to leave out (default: {' '.join(headers.DEFAULT_EXCLUDES)})")
    args = ap.parse_args(argv)

    try:
        import clang.cindex  # noqa: F401
    except ImportError:
        print("error: the libclang package is required: pip install libclang==18.1.1", file=sys.stderr)
        return 1

    include_dir = args.include_dir or os.path.join(os.path.dirname(os.path.abspath(args.out)), "include")
    excludes = dict.fromkeys(args.exclude, "command line") if args.exclude else None
    names = headers.prepare(args.headers_dir, include_dir, excludes)
    print(f"normalised {len(names)} headers into {include_dir}")

    tu = headers.parse(include_dir, args.define)
    data = headers.extract(tu, include_dir)
    data["config"] = {"defines": args.define or headers.DEFAULT_DEFINES,
                      "excludes": sorted(excludes or headers.DEFAULT_EXCLUDES),
                      "clang_args": headers.CLANG_ARGS}
    with open(args.out, "w") as f:
        json.dump(data, f, indent=1)

    recs = data["records"]
    kinds = collections.Counter(r["kind"] for r in recs.values())
    warned = [r for r in recs.values() if r.get("warnings")]
    print(f"wrote {args.out}: {len(recs)} records ({dict(kinds)}), {len(data['enums'])} enums, "
          f"{len(data['typedefs'])} typedefs, {len(data['functions'])} functions, "
          f"{sum(len(r['methods']) for r in recs.values())} methods")
    errs = collections.Counter(d["message"].split("'")[0] for d in data["diagnostics"])
    if errs:
        print(f"{len(data['diagnostics'])} clang errors (parsing continued):")
        for msg, n in errs.most_common():
            print(f"  {n:3d} {msg}")
    for r in warned:
        print(f"  layout warning {r['name']}: {'; '.join(r['warnings'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
