#!/usr/bin/env python3
"""Rank what a sweep of the applications ran into.

    src/host/demo/sweep.ns opens every application newton has, taps it at
    random and closes it, printing "sweep: open <app>" as it goes; run with
    NEWTON_TRACE_MISSING and NEWTON_TRACE_EXCEPTIONS set, newton says on the
    same log (stdout is unbuffered while tracing, so the two streams stay in
    order when both go to one file) everything the reconstruction could not
    do.  This reads such a log and lists each distinct problem with the
    applications it happened in, most widespread first:

      native      a native built-in not reconstructed ([frames] native not
                  reconstructed: NAME), or a package's ARM function with no
                  host re-expression and no fallback
      armcpu      a ROM function the ARM interpreter cannot answer
                  ([armcpu] the ROM's NAME ... is not answered)
      not yet     any other line saying NOT YET
      exception   an exception thrown (--- evt.ex...: {errorCode: ...}),
                  keyed by its name, error and the function it came from
                  (caught ones included: many are the applications' own
                  doing, and harmless)
      open        an application that would not open (sweep: X would not open)
      wait        a wait the sweep gave up on (... waited in vain)
      crash       newton crashing ([host] crashed / fell over)

Usage:
    python tools/host/sweeprank.py LOG [--markdown] [--min-apps N]

    --markdown prints a table for docs/next-steps.md; otherwise a plain list.
    LOG is newton's merged stdout and stderr (2>&1).
"""

import argparse
import re
import sys
from collections import defaultdict


def normalise(text):
    """Numbers that change from run to run (addresses, refs) taken out."""
    text = re.sub(r"#[0-9A-F]{6,8}", "#…", text)
    text = re.sub(r"0x[0-9a-fA-F]{6,}", "0x…", text)
    return text.strip()


def parse(lines):
    problems = defaultdict(lambda: {"apps": set(), "count": 0, "kind": ""})
    app = "(boot)"
    i = 0

    def note(kind, key, where):
        p = problems[(kind, key)]
        p["kind"] = kind
        p["apps"].add(where)
        p["count"] += 1

    while i < len(lines):
        line = lines[i].rstrip("\n")
        m = re.search(r'sweep: open (\S+)"?', line)
        if m:
            app = m.group(1)
        elif re.search(r'sweep: closed ', line) or re.search(r'sweep: done', line):
            app = "(between applications)"
        m = re.search(r"\[frames\] native not reconstructed: (\S+)", line)
        if m:
            note("native", m.group(1), app)
        elif "[frames] a package's native (ARM) function" in line:
            note("native", normalise(line.split("] ", 1)[1]), app)
        elif re.search(r"\[armcpu\].*not answered", line):
            m = re.search(r"the ROM's (\S+)", line)
            note("armcpu", m.group(1) if m else normalise(line), app)
        elif "NOT YET" in line and "sweep:" not in line:
            note("not yet", normalise(line), app)
        m = re.match(r"\s*(?:!!! )?--- (evt\.ex[^:]*): \{errorCode: (-?\d+)(.*)", line)
        if m:
            where = ""
            for j in range(i + 1, min(i + 12, len(lines))):
                f = re.match(r"\s+\d+ : (.*)", lines[j])
                if f:
                    where = normalise(f.group(1))
                    where = re.sub(r"\s*:\s*-?\d+$", "", where)
                    break
            extra = ""
            s = re.search(r"symbol: (\S+?)[},]", m.group(3))
            if s:
                extra = " " + s.group(1)
            note("exception", "%s %s%s in %s" % (m.group(1), m.group(2), extra, where or "?"), app)
        m = re.search(r"sweep: (\S+) would not open: (\S+)", line)
        if m:
            note("open", "%s: %s" % (m.group(1), m.group(2).rstrip('"')), m.group(1))
        m = re.search(r'"?(.*): waited in vain', line)
        if m:
            note("wait", normalise(m.group(1)), app)
        if re.search(r"\[host\] (crashed|.*fell over)", line):
            note("crash", normalise(line), app)
        i += 1
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("log")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--min-apps", type=int, default=1)
    args = ap.parse_args()
    with open(args.log, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    problems = parse(lines)
    ranked = sorted(problems.items(), key=lambda kv: (-len(kv[1]["apps"]), -kv[1]["count"], kv[0]))
    if args.markdown:
        print("| # | kind | what | applications | times |")
        print("|---|------|------|--------------|-------|")
    for n, ((kind, key), p) in enumerate(ranked, 1):
        if len(p["apps"]) < args.min_apps:
            continue
        apps = ", ".join(sorted(p["apps"]))
        if args.markdown:
            print("| %d | %s | `%s` | %d: %s | %d |" % (n, kind, key.replace("|", "\\|"), len(p["apps"]), apps, p["count"]))
        else:
            print("%3d. [%s] %s\n      %d app(s), %d time(s): %s" % (n, kind, key, len(p["apps"]), p["count"], apps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
