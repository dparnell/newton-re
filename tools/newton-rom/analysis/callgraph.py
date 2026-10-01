#!/usr/bin/env python3
"""What a ROM function calls, all the way down, and how much of it is still to do.

Usage:
    python callgraph.py <build_dir> ROOT... [--src src] [--depth N] [--all]
                        [--stop NAME...] [--tree]
    python callgraph.py <build_dir> NAME... --callers [--src src]

ROOT is a function name (as in symbols.txt) or a 0x address.  The call
graph is read straight out of rom.bin: every BL instruction inside a
function's extent (from its symbol to the next code symbol) is an edge,
and a BL into the patchable jump table is resolved through
symbols.json's jump table to the body it branches to.  Indirect calls
(`mov lr,pc; mov pc,rN`, virtual calls, calls through a table) are not
seen, so what is reported is a lower bound on what a root reaches.

Every function reached is marked done when some file under --src cites
its address (`// ROM 0x... name`, the convention coverage.py checks).
By default only the functions that are *not* done are listed - which is
the work left before the roots can run - with their size, how deep they
are and the first function found calling them; --all lists everything.
Traversal does not continue below a function that is done (its callees
were dealt with when it was written) unless --through-done is given,
and never below a name given with --stop.

--callers turns it round: for each NAME, the functions that call it
directly (a BL to it or to its jump-table slot), each marked done or
TODO - a function no done caller reaches is either reached some other
way (a vtable, a protocol, a table of procedures) or not from what the
reconstruction runs yet.

Inputs:  <build_dir>/rom.bin, symbols.json, layout.json; the source tree.
Output:  a table on stdout; with --tree, the reachable graph indented.
"""

from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import re
import struct
import sys

CITE = re.compile(r"//\s*ROM\s+(0x[0-9A-Fa-f]+)\s+([^\s+]\S*)")


def load(build_dir):
    with open(os.path.join(build_dir, "symbols.json")) as f:
        data = json.load(f)
    with open(os.path.join(build_dir, "layout.json")) as f:
        layout = json.load(f)
    with open(os.path.join(build_dir, "rom.bin"), "rb") as f:
        rom = f.read()
    return data, layout, rom


def cited_addresses(src):
    done = set()
    for root, _, files in os.walk(src):
        if os.path.basename(root) == "ddk":
            continue
        for fn in files:
            if not fn.endswith((".cpp", ".h")):
                continue
            with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = CITE.search(line)
                    if m:
                        done.add(int(m.group(1), 16))
    return done


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--src", default="src")
    ap.add_argument("--depth", type=int, default=99)
    ap.add_argument("--all", action="store_true", help="list done functions as well")
    ap.add_argument("--through-done", action="store_true",
                    help="keep walking below functions that are already reconstructed")
    ap.add_argument("--stop", nargs="*", default=[], help="names not to walk below")
    ap.add_argument("--tree", action="store_true", help="print the graph as an indented tree")
    ap.add_argument("--callers", action="store_true", help="list each NAME's direct callers instead")
    args = ap.parse_args(argv)

    data, layout, rom = load(args.build_dir)
    rom_size = layout["rom_size"]
    jt = {v: b for v, b in data["jumptable"]["entries"]}

    names = collections.defaultdict(list)
    by_name = {}
    code = set()
    for s in data["symbols"]:
        if "jt_index" in s:
            continue
        names[s["address"]].append(s["name"])
        by_name.setdefault(s["name"], s["address"])
        if s["class"] == "code" and s["address"] < rom_size:
            code.add(s["address"])
    starts = sorted(code)

    def extent(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i < 0 or starts[i] != addr:
            return addr, addr
        end = starts[i + 1] if i + 1 < len(starts) else rom_size
        return addr, end

    def name(addr):
        n = names.get(addr)
        return n[0] if n else f"sub_{addr:08x}"

    callees_cache = {}

    def callees(addr):
        if addr in callees_cache:
            return callees_cache[addr]
        lo, hi = extent(addr)
        out = []
        for pc in range(lo, hi, 4):
            w = struct.unpack(">I", rom[pc:pc + 4])[0]
            if (w >> 28) == 0xF or (w & 0x0F000000) != 0x0B000000:
                continue
            off = w & 0x00FFFFFF
            if off & 0x800000:
                off -= 0x1000000
            t = (pc + 8 + (off << 2)) & 0xFFFFFFFF
            t = jt.get(t, t)
            if t in code and t not in out:
                out.append(t)
        callees_cache[addr] = out
        return out

    roots = []
    for r in args.roots:
        a = int(r, 0) if r.lower().startswith("0x") else by_name.get(r)
        if a is None:
            print(f"{r}: no such symbol", file=sys.stderr)
            return 1
        roots.append(a)

    done = cited_addresses(args.src)
    stops = {by_name[n] for n in args.stop if n in by_name}

    if args.callers:
        callers = collections.defaultdict(list)
        for a in starts:
            for c in callees(a):
                callers[c].append(a)
        # the other ways a function is reached: a B to it (a vtable entry,
        # a tail call, a glue stub) and its address as a word of data (a
        # table of procedures, a class info, a function pointer stored)
        slot_of = collections.defaultdict(list)
        for v, body in jt.items():
            slot_of[body].append(v)
        wanted = set(roots) | {v for r in roots for v in slot_of.get(r, [])}
        branched = collections.defaultdict(list)
        as_data = collections.defaultdict(list)
        for pc in range(0, rom_size, 4):
            w = struct.unpack(">I", rom[pc:pc + 4])[0]
            if w in wanted:
                as_data[w].append(pc)
            if (w >> 28) != 0xF and (w & 0x0F000000) == 0x0A000000:
                off = w & 0x00FFFFFF
                if off & 0x800000:
                    off -= 0x1000000
                t = (pc + 8 + (off << 2)) & 0xFFFFFFFF
                if t in wanted:
                    branched[t].append(pc)

        def owner(pc):
            i = bisect.bisect_right(starts, pc) - 1
            return name(starts[i]) if i >= 0 else f"0x{pc:08x}"

        for r in roots:
            who = callers.get(r, [])
            marks = ", ".join(f"{name(c)} {'done' if c in done else 'TODO'}" for c in who) or "(no direct caller)"
            targets = [r] + slot_of.get(r, [])
            bs = sorted({owner(pc) for t in targets for pc in branched.get(t, []) if pc != t})
            ds = sorted({f"0x{pc:08x}" for t in targets for pc in as_data.get(t, [])})
            extra = ""
            if bs:
                extra += "; B from " + ", ".join(bs[:6])
            if ds:
                extra += "; address at " + ", ".join(ds[:6])
            print(f"{name(r)}  0x{r:08x}  {'done' if r in done else 'TODO'}  <- {marks}{extra}")
        return 0

    seen = {}
    order = []
    queue = collections.deque((a, 0, None) for a in roots)
    while queue:
        a, depth, caller = queue.popleft()
        if a in seen:
            continue
        seen[a] = (depth, caller)
        order.append(a)
        if depth >= args.depth or a in stops:
            continue
        if a in done and a not in roots and not args.through_done:
            continue
        for c in callees(a):
            if c not in seen:
                queue.append((c, depth + 1, a))

    if args.tree:
        printed = set()

        def walk(a, indent):
            lo, hi = extent(a)
            mark = "done" if a in done else "TODO"
            print(f"{'  ' * indent}{name(a)}  0x{a:08x}  {hi - lo} B  {mark}")
            if a in printed:
                return
            printed.add(a)
            if (a in done and a not in roots and not args.through_done) or a in stops:
                return
            for c in callees(a):
                walk(c, indent + 1)

        for r in roots:
            walk(r, 0)
        return 0

    todo_bytes = 0
    todo = 0
    print(f"{'function':48} {'address':>10} {'size':>6}  depth  first caller")
    for a in sorted(order, key=lambda x: (seen[x][0], x)):
        if a in done and not args.all:
            continue
        lo, hi = extent(a)
        depth, caller = seen[a]
        mark = "" if a not in done else "  (done)"
        print(f"{name(a)[:48]:48} 0x{a:08x} {hi - lo:6}  {depth:5}  "
              f"{name(caller) if caller is not None else '-'}{mark}")
        if a not in done:
            todo += 1
            todo_bytes += hi - lo
    print(f"\n{len(order)} functions reached; {todo} not done, {todo_bytes} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
