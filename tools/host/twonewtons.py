#!/usr/bin/env python3
"""Two host Newtons facing each other over IR, each running a script.

Purpose
    Beaming needs two machines.  This starts two `newton` processes, the
    first with `--ir-peer listen:0` (it prints "[host] IR port N" once its
    IR port listens) and the second with `--ir-peer 127.0.0.1:N`, each
    running its own NewtonScript file (`--script`), both headless, and
    waits for both.  Each line of output is passed through with the name
    of the newton it came from, so a ctest's regular expression can check
    both ends (ctest host.NewtonBeam: src/host/demo/beam-receive.ns on the
    first, beam-send.ns on the second).

Usage
    python tools/host/twonewtons.py <newton> --rom <image> --first a.ns --second b.ns
        [--seconds N] [--name-first A] [--name-second B] [--newton-arg ARG]...
        [--expect-first REGEX] [--expect-second REGEX]

Inputs / outputs
    The newton program, the ROM image and the two scripts.  Output: both
    programs' output, each line prefixed "[A] " or "[B] ".  Exits 0 when
    both programs did (a script ends its run with HostQuit()) and each
    one's own output (its lines joined, in order) matches its --expect
    regular expression, if given - the two outputs interleave as they
    will, so what each end said is checked on its own; otherwise 1.  The
    last line says which ("both ended as expected").
    The seconds (default 120) limit each run (--headless).
"""

import argparse
import os
import re
import subprocess
import sys
import threading


def pump(proc, name, lines, port_event, port_box):
    for raw in proc.stdout:
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        lines.append(line)
        print("[%s] %s" % (name, line), flush=True)
        if port_event is not None and not port_event.is_set():
            m = re.search(r"\[host\] IR port (\d+)", line)
            if m:
                port_box.append(int(m.group(1)))
                port_event.set()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("newton")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--first", required=True, help="the listening newton's script")
    ap.add_argument("--second", required=True, help="the connecting newton's script")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--name-first", default="A")
    ap.add_argument("--name-second", default="B")
    ap.add_argument("--expect-first", help="what the first newton's output must match")
    ap.add_argument("--expect-second", help="what the second newton's output must match")
    ap.add_argument("--newton-arg", action="append", default=[], help="an extra argument for both newtons")
    args = ap.parse_args()
    extra = args.newton_arg

    common = [os.path.abspath(args.newton), "--rom", args.rom, "--headless", str(args.seconds), "--serial-port", "none"] + extra
    first = subprocess.Popen(common + ["--ir-peer", "listen:0", "--script", args.first],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    port_event = threading.Event()
    port_box = []
    lines_a, lines_b = [], []
    ta = threading.Thread(target=pump, args=(first, args.name_first, lines_a, port_event, port_box))
    ta.start()
    if not port_event.wait(60):
        print("twonewtons.py: the first newton never said where its IR port is", flush=True)
        first.kill()
        ta.join()
        return 1
    second = subprocess.Popen(common + ["--ir-peer", "127.0.0.1:%d" % port_box[0], "--script", args.second],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    tb = threading.Thread(target=pump, args=(second, args.name_second, lines_b, None, None))
    tb.start()
    limit = args.seconds + 30
    status = 0
    for proc in (second, first):
        try:
            if proc.wait(limit) != 0:
                status = 1
        except subprocess.TimeoutExpired:
            print("twonewtons.py: a newton did not end; stopped", flush=True)
            proc.kill()
            status = 1
    ta.join()
    tb.join()
    for name, lines, expect in ((args.name_first, lines_a, args.expect_first),
                                (args.name_second, lines_b, args.expect_second)):
        if expect and not re.search(expect, "\n".join(lines), re.S):
            print("twonewtons.py: %s's output does not match %r" % (name, expect), flush=True)
            status = 1
    print("twonewtons.py: %s" % ("both ended as expected" if status == 0 else "failed"), flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
