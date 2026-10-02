#!/usr/bin/env python3
"""Two (or three) host Newtons facing each other over IR, each running a script.

Purpose
    Beaming needs two machines.  This starts two `newton` processes, each
    running its own NewtonScript file (`--script`), both headless, and
    waits for both.  By default their IR ports face each other over a TCP
    connection: the first with `--ir-peer listen:0` (it prints
    "[host] IR port N" once its IR port listens) and the second with
    `--ir-peer 127.0.0.1:N`.  With --lan they are on the LAN medium
    instead (`--ir-lan PORT`, a UDP multicast group every newton given the
    same port hears - hal/host/HostIRChip.h), started together, and a
    third newton may stand by on the same medium (--third): that is the
    medium's point, a shared one where whoever is listening answers.
    Each line of output is passed through with the name of the newton it
    came from, so a ctest's regular expression can check each end
    (ctest host.NewtonBeam: src/host/demo/beam-receive.ns on the first,
    beam-send.ns on the second; host.NewtonBeamLAN*: --lan).

Usage
    python tools/host/twonewtons.py <newton> [--rom <image>] --first a.ns --second b.ns
        [--third c.ns [--third-ready REGEX]] [--lan] [--lan-port N] [--lan-interface ADDRESS|all]
        [--seconds N] [--name-first A] [--name-second B] [--name-third C]
        [--newton-arg ARG]... [--env NAME=VALUE]...
        [--expect-first REGEX] [--expect-second REGEX] [--expect-third REGEX]
        [--expect-one REGEX] [--wait-for first|both]

    --lan picks a free UDP port for the group (so that two tests running
    at once never hear each other) unless --lan-port names one, and keeps
    the newtons to the loopback interface (127.0.0.1: this machine only,
    nothing on the network, nothing for a firewall to ask about) unless
    --lan-interface names another or says all (every interface, as
    `newton --ir-lan` does by default).  --third needs --lan.  --env sets
    a variable in every newton's environment (a script reads it with
    HostGetEnv).

    A third newton (a bystander, or a second receiver) is waited for until
    the others have ended and then given a few seconds more before it is
    stopped, since it may never hear what it would end on; --wait-for first
    treats the second the same way; --third-ready REGEX starts the third
    first and the other two once its output matches (a bystander that is
    up before the beam begins) (two receivers, either of which may be
    the one that gets the beam).  --expect-one REGEX: exactly one of the
    second and third newtons' outputs must match - as with IR, only one
    receiver takes a beam.

Inputs / outputs
    The newton program, the ROM image and the scripts.  Output: every
    program's output, each line prefixed "[A] ", "[B] " or "[C] ".  Exits
    0 when every program did (a script ends its run with HostQuit()) and
    each one's own output (its lines joined, in order) matches its
    --expect regular expression, if given - the outputs interleave as they
    will, so what each end said is checked on its own; otherwise 1.  The
    last line says which ("both ended as expected").
    The seconds (default 120) limit each run (--headless).
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import threading


def pump(proc, name, lines, port_event, port_box, ready=None):
    for raw in proc.stdout:
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        lines.append(line)
        print("[%s] %s" % (name, line), flush=True)
        if ready is not None and ready[0].search(line):
            ready[1].set()
        if port_event is not None and not port_event.is_set():
            m = re.search(r"\[host\] IR port (\d+)", line)
            if m:
                port_box.append(int(m.group(1)))
                port_event.set()


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("newton")
    ap.add_argument("--rom", help="a ROM image to boot (default: newton's own default, the object file built from romsrc/)")
    ap.add_argument("--first", required=True, help="the first newton's script (the listening one, over TCP)")
    ap.add_argument("--second", required=True, help="the second newton's script (the connecting one, over TCP)")
    ap.add_argument("--third", help="a third newton's script, on the same LAN medium (--lan)")
    ap.add_argument("--third-ready", help="start the third first, the others once its output matches this")
    ap.add_argument("--lan", action="store_true", help="the LAN medium (--ir-lan) instead of a TCP connection")
    ap.add_argument("--lan-port", type=int, help="the group's port (default: a free one)")
    ap.add_argument("--lan-interface", default="127.0.0.1", help="the interface to keep to, or 'all' (default 127.0.0.1)")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--name-first", default="A")
    ap.add_argument("--name-second", default="B")
    ap.add_argument("--name-third", default="C")
    ap.add_argument("--expect-first", help="what the first newton's output must match")
    ap.add_argument("--expect-second", help="what the second newton's output must match")
    ap.add_argument("--expect-third", help="what the third newton's output must match")
    ap.add_argument("--expect-one", help="what exactly one of the second and third newtons' outputs must match")
    ap.add_argument("--wait-for", choices=["first", "both"], default="both", help="which newtons must end by themselves")
    ap.add_argument("--newton-arg", action="append", default=[], help="an extra argument for every newton")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE in every newton's environment")
    args = ap.parse_args()
    if args.third and not args.lan:
        ap.error("--third needs --lan (a TCP connection has two ends)")
    env = dict(os.environ)
    for item in args.env:
        name, _, value = item.partition("=")
        env[name] = value

    common = [os.path.abspath(args.newton)] + (["--rom", args.rom] if args.rom else []) + ["--headless", str(args.seconds), "--serial-port", "none"] + args.newton_arg
    runs = []      # (name, proc, lines, thread, expect)

    def start(name, medium, script, expect, port_event=None, port_box=None, ready=None):
        own = env
        if ready is not None:
            own = dict(env, NEWTON_UNBUFFERED="1")      # (its readiness read as it says it)
        proc = subprocess.Popen(common + medium + ["--script", script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=own)
        lines = []
        thread = threading.Thread(target=pump, args=(proc, name, lines, port_event, port_box, ready))
        thread.start()
        runs.append((name, proc, lines, thread, expect))
        return proc

    if args.lan:
        port = args.lan_port or free_udp_port()
        medium = ["--ir-lan", str(port)]
        if args.lan_interface != "all":
            medium += ["--ir-lan-interface", args.lan_interface]
        print("twonewtons.py: the LAN medium on port %d (%s)" % (port, args.lan_interface), flush=True)
        third = None
        if args.third and args.third_ready:
            ready = (re.compile(args.third_ready), threading.Event())
            third = start(args.name_third, medium, args.third, args.expect_third, ready=ready)
            if not ready[1].wait(120):
                print("twonewtons.py: the third newton never said it was ready", flush=True)
                third.kill()
                runs[0][3].join()
                return 1
        start(args.name_first, medium, args.first, args.expect_first)
        start(args.name_second, medium, args.second, args.expect_second)
        if args.third and third is None:
            start(args.name_third, medium, args.third, args.expect_third)
        runs.sort(key=lambda run: [args.name_first, args.name_second, args.name_third].index(run[0]))
    else:
        port_event = threading.Event()
        port_box = []
        first = start(args.name_first, ["--ir-peer", "listen:0"], args.first, args.expect_first, port_event, port_box)
        if not port_event.wait(60):
            print("twonewtons.py: the first newton never said where its IR port is", flush=True)
            first.kill()
            runs[0][3].join()
            return 1
        start(args.name_second, ["--ir-peer", "127.0.0.1:%d" % port_box[0]], args.second, args.expect_second)

    limit = args.seconds + 30
    status = 0
    waited = 1 if args.wait_for == "first" else 2
    for name, proc, lines, thread, expect in reversed(runs[:waited]):
        try:
            if proc.wait(limit) != 0:
                status = 1
        except subprocess.TimeoutExpired:
            print("twonewtons.py: %s did not end; stopped" % name, flush=True)
            proc.kill()
            status = 1
    for name, proc, lines, thread, expect in runs[waited:]:
        try:
            proc.wait(15)
        except subprocess.TimeoutExpired:
            print("twonewtons.py: %s stopped, the others having ended" % name, flush=True)
            proc.kill()
    for name, proc, lines, thread, expect in runs:
        thread.join()
    for name, proc, lines, thread, expect in runs:
        if expect and not re.search(expect, "\n".join(lines), re.S):
            print("twonewtons.py: %s's output does not match %r" % (name, expect), flush=True)
            status = 1
    if args.expect_one:
        matched = [name for name, proc, lines, thread, expect in runs[1:] if re.search(args.expect_one, "\n".join(lines), re.S)]
        if len(matched) != 1:
            print("twonewtons.py: %d of the receivers' outputs match %r (%s), not one" % (len(matched), args.expect_one, ", ".join(matched) or "none"), flush=True)
            status = 1
    print("twonewtons.py: %s" % (("both ended as expected" if len(runs) == 2 else "all ended as expected") if status == 0 else "failed"), flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
