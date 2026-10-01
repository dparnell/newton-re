#!/usr/bin/env python3
"""Where a running host's memory is, mapping by mapping, over time (Linux).

Usage:
    python3 tools/host/smapswatch.py (<pid> | --match TEXT) [--interval S] [--seconds S] [--csv FILE] [--detail GROUP]

A process whose resident memory grows (tools/host/soak.py's workingSet)
can be growing in its malloc heap, in an anonymous mapping of its own
(the frames heap, a task stack, the ARM world), or in a file it maps.
This reads /proc/<pid>/smaps every --interval seconds and sums the
resident (Rss) and the private-dirty kilobytes of each mapping, keyed by
what the mapping is:

  [heap]           the brk heap (glibc's main arena)
  anon:<size>      an anonymous mapping, by its size (glibc's other arenas
                   are 64 MB mappings; a task's stack is 8 MB; the Newton's
                   own heaps and the ARM world have sizes of their own)
  <path>           a mapped file
  [stack], [vdso], ...

With --match it finds the process by a fragment of its command line (the
first, oldest, newton whose arguments contain TEXT).

Output: one line per sample - the total RSS and the groups that changed
by more than a megabyte since the first sample - and, at the end, each
group's first and last RSS and its growth per hour (a least-squares slope
over the second half of the run, as soak.py's).  --csv writes every
group's RSS per sample.  Read-only; standard library only.
"""

import argparse
import collections
import os
import re
import sys
import time

HEADER = re.compile(r"^([0-9a-f]+)-([0-9a-f]+) \S+ \S+ \S+ \S+\s*(.*)$")


def find(match):
    best = None
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                args = f.read().split(b"\0")
            start = os.stat(f"/proc/{pid}").st_mtime
        except OSError:
            continue
        if args and args[0].endswith(b"newton") and match.encode() in b" ".join(args):
            if best is None or start < best[1]:
                best = (int(pid), start)
    return best[0] if best else None


def sample(pid, detail=None):
    groups = collections.Counter()
    dirty = collections.Counter()
    key = None
    where = None
    with open(f"/proc/{pid}/smaps") as f:
        for line in f:
            m = HEADER.match(line)
            if m:
                lo, hi, name = int(m.group(1), 16), int(m.group(2), 16), m.group(3).strip()
                if name == "":
                    size = hi - lo
                    key = f"anon:{size // (1024 * 1024)}MB" if size >= 1024 * 1024 else "anon:<1MB"
                else:
                    key = name
                where = (lo, hi)
                continue
            if line.startswith("Rss:"):
                groups[key] += int(line.split()[1])
                if detail is not None and key == detail[0] and int(line.split()[1]) > 0:
                    detail[1].append((where[0], where[1] - where[0], int(line.split()[1])))
            elif line.startswith("Private_Dirty:"):
                dirty[key] += int(line.split()[1])
    return groups, dirty


def slope(points):
    n = len(points)
    if n < 2:
        return 0.0
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    den = sum((p[0] - mx) ** 2 for p in points)
    return sum((p[0] - mx) * (p[1] - my) for p in points) / den if den else 0.0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pid", type=int, nargs="?")
    ap.add_argument("--match", default=None)
    ap.add_argument("--interval", type=float, default=30)
    ap.add_argument("--seconds", type=float, default=600)
    ap.add_argument("--csv", default=None)
    ap.add_argument("--detail", default=None,
                    help="at the end, list the mappings of this group (e.g. anon:<1MB) with their RSS")
    a = ap.parse_args(argv[1:])
    pid = a.pid
    deadline = time.time() + 60
    while pid is None:
        pid = find(a.match) if a.match else None
        if pid is None:
            if a.match is None or time.time() > deadline:
                raise SystemExit("no process to watch")
            time.sleep(1)
    print(f"watching process {pid}", flush=True)
    history = []				# (seconds, Counter)
    start = time.time()
    end = start + a.seconds
    while time.time() < end:
        try:
            groups, dirty = sample(pid)
        except OSError:
            print("the process has ended", flush=True)
            break
        t = time.time() - start
        history.append((t, groups))
        first = history[0][1]
        changed = [(k, groups[k] - first[k]) for k in set(groups) | set(first) if abs(groups[k] - first[k]) >= 1024]
        changed.sort(key=lambda kv: -abs(kv[1]))
        print(f"{t:7.0f}s rss {sum(groups.values()) // 1024} MB  " +
              "  ".join(f"{k} {d / 1024:+.1f}" for k, d in changed[:6]), flush=True)
        time.sleep(a.interval)
    if not history:
        return 1
    if a.detail:
        found = (a.detail, [])
        try:
            sample(pid, found)
            print(f"\nthe mappings of {a.detail} with anything resident:")
            for lo, size, rss in sorted(found[1], key=lambda m: -m[2])[:40]:
                print(f"  {lo:#x} {size // 1024:8} KB  rss {rss} KB")
        except OSError:
            print("(the process has ended: no detail)")
    keys = sorted({k for _, g in history for k in g})
    if a.csv:
        with open(a.csv, "w") as f:
            f.write("seconds," + ",".join(k.replace(",", ";") for k in keys) + "\n")
            for t, g in history:
                f.write(f"{t:.0f}," + ",".join(str(g[k]) for k in keys) + "\n")
    half = [h for h in history if h[0] >= history[-1][0] / 2]
    print("\ngroup                                  first MB   last MB   per hour (2nd half)")
    rows = []
    for k in keys:
        s = slope([(t, g[k]) for t, g in half]) * 3600 / 1024
        rows.append((abs(s), k, history[0][1][k] / 1024, history[-1][1][k] / 1024, s))
    for _, k, f0, f1, s in sorted(rows, reverse=True)[:15]:
        print(f"{k[:38]:38} {f0:9.1f} {f1:9.1f} {s:+12.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
