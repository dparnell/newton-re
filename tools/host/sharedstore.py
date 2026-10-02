#!/usr/bin/env python3
"""
sharedstore.py - one store file used by both flavours of newton in turn
(docs/frames/64bit.md, "One store, both flavours").

newton (faithful) and newton64 (NEWTON_NS64) keep every store format 32-bit,
so a store either writes the other reads.  This runs src/host/demo/
sharedstore.ns five times on one store file - newton, newton64, newton,
newton64 with NEWTON_NS64_STRICT, newton - SHARED_STEP telling the script
which step it is, and checks:

  * each step's own checks passed ("shared: step N done, 0 failed");
  * the note's word hints are the same bytes after every step;
  * the store file's byte-order flag (tools/stores/flashimage.py info) is
    the same after every step.

Only one newton may have a store file open at a time: the steps run one
after another, never together.

Inputs: the two programs, a store path (made afresh), the script.
Output: each step's lines and a verdict; exit status 0 when all agree.

Usage:

    python tools/host/sharedstore.py --newton build/host/host/newton \
        --newton64 build/host/host/newton64 --store tmp/shared.store \
        --script src/host/demo/sharedstore.ns

Prints "sharedstore: done" when everything agreed (ctest
host.NewtonSharedStore).
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FLASHIMAGE = os.path.join(HERE, "..", "stores", "flashimage.py")


def run_step(program, step, store, script, strict, timeout):
    env = dict(os.environ)
    env["SHARED_STEP"] = str(step)
    env.pop("NEWTON_NS64_STRICT", None)
    if strict:
        env["NEWTON_NS64_STRICT"] = "1"
    args = [program, "--display", "320x480", "--store", store, "--headless", "120", "--script", script]
    if step == 1:
        args.insert(1, "--erase")
    out = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=timeout, cwd=os.path.dirname(os.path.abspath(script)))
    return out.stdout.decode("utf-8", "replace")


def byte_order(store):
    out = subprocess.run([sys.executable, FLASHIMAGE, "info", store], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT).stdout.decode("utf-8", "replace")
    for line in out.splitlines():
        if line.startswith("binaries:"):
            return line
    return "(no byte-order line: %s)" % out.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("--newton", required=True)
    ap.add_argument("--newton64", required=True)
    ap.add_argument("--store", required=True)
    ap.add_argument("--script", required=True)
    ap.add_argument("--timeout", type=int, default=200)
    args = ap.parse_args()

    if os.path.exists(args.store):
        os.remove(args.store)
    steps = [(1, args.newton, False), (2, args.newton64, False), (3, args.newton, False),
             (4, args.newton64, True), (5, args.newton, False)]
    failures = 0
    hints = []
    orders = []
    for step, program, strict in steps:
        text = run_step(program, step, args.store, os.path.abspath(args.script), strict, args.timeout)
        lines = [l.strip().strip('"') for l in text.splitlines() if "shared:" in l]
        for l in lines:
            print("[%d %s] %s" % (step, os.path.basename(program), l))
        done = [l for l in lines if l.startswith("shared: step %d done" % step)]
        if not done or not done[0].endswith(", 0 failed"):
            failures += 1
            print("sharedstore: step %d FAILED" % step)
            if not done:
                print(text[-2000:])
        hints += [l for l in lines if l.startswith("shared: hints")]
        orders.append(byte_order(args.store))
        print("[%d] %s" % (step, orders[-1]))
    if len(set(hints)) != 1 or len(hints) != len(steps):
        failures += 1
        print("sharedstore: the note's word hints differ between steps: FAILED")
    else:
        print("sharedstore: the note's word hints the same after every step")
    if len(set(orders)) != 1:
        failures += 1
        print("sharedstore: the byte-order flag differs between steps: FAILED")
    else:
        print("sharedstore: the byte-order flag the same after every step")
    if failures == 0:
        print("sharedstore: done")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
