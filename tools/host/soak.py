#!/usr/bin/env python3
"""A stability soak: newton used for an hour or more while its process is watched.

Purpose
    Real use is long use.  This runs `src/host/demo/soak.ns` - round after
    round of an application swept at random, a word written and read, a
    memory card put in and taken out, the note printed to an IPP printer
    and beamed to a second newton - and watches what grows: the Newton's
    own heaps (the script's GetHeapStats line each round) and the host
    process's handles, threads and memory (sampled every --interval
    seconds).  A crash (the process ends early), a hang (no round for
    --hang seconds; tools/host/stacksample.py is run on it first, on
    Windows) and every "waited in vain" are reported, and at the end each
    measure's growth per hour (a least-squares slope over the second half
    of the run, the first half being the machine settling).

Usage
    python tools/host/soak.py <newton> [--minutes 60] [--out DIR]
        [--heapcheck N] [--no-beam] [--no-print] [--window | --window-pen] [--interval 30]
        [--hang 600] [--taps 20] [--newton-arg ARG]...

    --out       where the logs, stores, card, print jobs and CSVs go
                (default tmp/soak; it is emptied first)
    --heapcheck NEWTON_HEAPCHECK for both newtons: the newt heap walked
                every N allocations (default 50000, sparse; 0 for none)
    --no-beam   one newton only (else a second one, B, receives the beams:
                --ir-peer listen/connect, as tools/host/twonewtons.py)
    --no-print  no IPP printer (else tools/print/ippprinter.py's printer
                is served in this process and newton is given its URI)
    --window    the windowed newton (--window-pen --limit) instead of
                --headless: the window's own pen and paint paths
    --window-pen headless, but the taps through the window's pen path
    --hang      seconds with no new round before a newton counts as hung

Inputs / outputs
    The newton program and the soak script beside the demos.  In --out:
    A.log/B.log (each newton's output), ipp.log (the printer's), A.csv/B.csv (one row per sample:
    seconds, round, the four heap figures, handles, threads, private
    bytes, working set), jobs/ (what was printed), and summary.txt, which
    is also printed.  Exits 1 if a newton crashed or hung, else 0.
    Standard library only; the process figures come from the Windows API
    through ctypes, or from /proc on Linux.
"""

import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "print"))

ROUND = re.compile(r"soak: round (\d+) ticks (\d+) ptrFree (-?\d+) handleFree (-?\d+) framesFree (-?\d+) systemFree (-?\d+)")


# --- the host process's figures --------------------------------------------

if sys.platform == "win32":
    import ctypes.wintypes as wt

    class PMC(ctypes.Structure):
        _fields_ = [("cb", wt.DWORD), ("PageFaultCount", wt.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
                    ("PrivateUsage", ctypes.c_size_t)]

    class THREADENTRY32(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                    ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG),
                    ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi")
    k32.OpenProcess.restype = wt.HANDLE
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE

    def process_figures(pid):
        h = k32.OpenProcess(0x0400 | 0x0010, False, pid)	# QUERY_INFORMATION | VM_READ
        if not h:
            return None
        try:
            count = wt.DWORD()
            k32.GetProcessHandleCount(h, ctypes.byref(count))
            pmc = PMC()
            pmc.cb = ctypes.sizeof(PMC)
            psapi.GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb)
        finally:
            k32.CloseHandle(h)
        threads = 0
        snap = k32.CreateToolhelp32Snapshot(0x4, 0)			# TH32CS_SNAPTHREAD
        te = THREADENTRY32()
        te.dwSize = ctypes.sizeof(te)
        ok = k32.Thread32First(snap, ctypes.byref(te))
        while ok:
            if te.th32OwnerProcessID == pid:
                threads += 1
            ok = k32.Thread32Next(snap, ctypes.byref(te))
        k32.CloseHandle(snap)
        return count.value, threads, pmc.PrivateUsage, pmc.WorkingSetSize
else:
    def process_figures(pid):
        try:
            status = open("/proc/%d/status" % pid).read()
            fds = len(os.listdir("/proc/%d/fd" % pid))
        except OSError:
            return None
        field = lambda name: int(re.search(name + r":\s+(\d+)", status).group(1))
        return fds, field("Threads"), field("VmData") * 1024, field("VmRSS") * 1024


# --- one newton -------------------------------------------------------------

class Newton:
    def __init__(self, name, out):
        self.name = name
        self.log = open(os.path.join(out, name + ".log"), "w", encoding="utf-8", errors="replace")
        self.csv = open(os.path.join(out, name + ".csv"), "w")
        self.csv.write("seconds,round,ptrFree,handleFree,framesFree,systemFree,handles,threads,private,workingSet\n")
        self.round = None			# the last round line's figures
        self.last_round_time = time.time()
        self.vain = []
        self.port = None
        self.port_event = threading.Event()
        self.proc = None
        self.samples = []			# (seconds, round figures, process figures)

    def start(self, command, env):
        self.proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        self.thread = threading.Thread(target=self.pump, daemon=True)
        self.thread.start()

    def pump(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            self.log.write(line + "\n")
            self.log.flush()
            m = ROUND.search(line)
            if m:
                self.round = [int(x) for x in m.groups()]
                self.last_round_time = time.time()
                print("[%s] %s" % (self.name, line.strip('"')), flush=True)
            elif "waited in vain" in line:
                self.vain.append(line.strip('"'))
                print("[%s] %s" % (self.name, line.strip('"')), flush=True)
            if not self.port_event.is_set():
                m = re.search(r"\[host\] IR port (\d+)", line)
                if m:
                    self.port = int(m.group(1))
                    self.port_event.set()

    def sample(self, seconds):
        figures = process_figures(self.proc.pid) if self.proc.poll() is None else None
        if figures is None:
            return
        r = self.round or [None] * 6
        self.samples.append((seconds, r, figures))
        self.csv.write(",".join(str(x) for x in [round(seconds)] + [r[0]] + r[2:6] + list(figures)) + "\n")
        self.csv.flush()


def slope_per_hour(points):
    """Least-squares slope of (seconds, value) points over the second half, per hour."""
    points = [(t, v) for t, v in points if v is not None]
    points = points[len(points) // 2:]
    if len(points) < 3:
        return None
    n = len(points)
    mt = sum(t for t, _ in points) / n
    mv = sum(v for _, v in points) / n
    d = sum((t - mt) ** 2 for t, _ in points)
    if d == 0:
        return None
    return sum((t - mt) * (v - mv) for t, v in points) / d * 3600


def summarise(newton, problems):
    lines = ["%s: %d samples, last round %s" % (newton.name, len(newton.samples),
                                                newton.round[0] if newton.round else "none")]
    names = ["ptrFree", "handleFree", "framesFree", "systemFree", "handles", "threads", "private", "workingSet"]
    for i, name in enumerate(names):
        pts = [(t, (r[2 + i] if i < 4 else f[i - 4])) for t, r, f in newton.samples]
        vals = [v for _, v in pts if v is not None]
        if not vals:
            continue
        s = slope_per_hour(pts)
        lines.append("  %-10s first %12d  last %12d  min %12d  max %12d  per hour (2nd half) %s"
                     % (name, vals[0], vals[-1], min(vals), max(vals), "-" if s is None else "%+.0f" % s))
    if newton.vain:
        lines.append("  %d steps waited in vain:" % len(newton.vain))
        counts = {}
        for v in newton.vain:
            counts[v] = counts.get(v, 0) + 1
        for v, c in sorted(counts.items(), key=lambda x: -x[1]):
            lines.append("    %4d  %s" % (c, v))
    lines += ["  " + p for p in problems]
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("newton")
    ap.add_argument("--minutes", type=float, default=60)
    ap.add_argument("--out", default=os.path.join(ROOT, "tmp", "soak"))
    ap.add_argument("--heapcheck", type=int, default=50000)
    ap.add_argument("--no-beam", action="store_true")
    ap.add_argument("--no-print", action="store_true")
    ap.add_argument("--window", action="store_true")
    ap.add_argument("--window-pen", action="store_true")
    ap.add_argument("--interval", type=float, default=30)
    ap.add_argument("--hang", type=float, default=600)
    ap.add_argument("--taps", type=int, default=20)
    ap.add_argument("--newton-arg", action="append", default=[])
    args = ap.parse_args()

    shutil.rmtree(args.out, ignore_errors=True)
    os.makedirs(args.out)
    seconds = int(args.minutes * 60)
    env = dict(os.environ, SOAK_TAPS=str(args.taps), NEWTON_UNBUFFERED="1")
    if args.heapcheck:
        env["NEWTON_HEAPCHECK"] = str(args.heapcheck)

    listener = None
    if not args.no_print:
        import socket
        import ippprinter
        ipplog = open(os.path.join(args.out, "ipp.log"), "w")
        ippprinter.log = lambda text: (ipplog.write("[ipp] " + text + "\n"), ipplog.flush())
        printer = ippprinter.Printer(os.path.join(args.out, "jobs"), "/ipp/print", 0, None, 0)
        os.makedirs(os.path.join(args.out, "jobs"), exist_ok=True)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        printer.port = listener.getsockname()[1]
        printer.tls = None
        threading.Thread(target=ippprinter.serve, args=(printer, listener), daemon=True).start()
        env["NEWTON_IPP_PRINTER"] = "ipp://127.0.0.1:%d/ipp/print" % printer.port

    script = os.path.join(ROOT, "src", "host", "demo", "soak.ns")
    def command(name, extra):
        limit = ["--window-pen", "--limit", str(seconds)] if args.window else ["--headless", str(seconds)]
        if args.window_pen and not args.window:
            limit.append("--window-pen")
        return ([os.path.abspath(args.newton), "--display", "320x480", "--serial-port", "none",
                 "--store", os.path.join(args.out, name + ".store"), "--erase"]
                + limit + extra + args.newton_arg + ["--script", script])

    newtons = []
    a = Newton("A", args.out)
    newtons.append(a)
    if args.no_beam:
        a.start(command("A", []), env)
    else:
        b = Newton("B", args.out)
        newtons.append(b)
        b.start(command("B", ["--ir-peer", "listen:0"]), dict(env, SOAK_BEAM="receive"))
        if not b.port_event.wait(60):
            print("soak.py: B never said where its IR port is", flush=True)
            b.proc.kill()
            return 1
        a.start(command("A", ["--ir-peer", "127.0.0.1:%d" % b.port]), dict(env, SOAK_BEAM="send"))

    problems = {n.name: [] for n in newtons}
    start = time.time()
    end = start + seconds + 60
    next_sample = start
    while time.time() < end and any(n.proc.poll() is None for n in newtons):
        now = time.time()
        if now >= next_sample:
            for n in newtons:
                n.sample(now - start)
            next_sample += args.interval
        for n in newtons:
            if n.proc.poll() is None and now - n.last_round_time > args.hang:
                problems[n.name].append("HUNG: no round for %d s at %d s" % (args.hang, now - start))
                if sys.platform == "win32":
                    stack = subprocess.run([sys.executable, os.path.join(HERE, "stacksample.py"),
                                            str(n.proc.pid), "--exe", os.path.abspath(args.newton)],
                                           capture_output=True, text=True)
                    with open(os.path.join(args.out, n.name + ".hang.txt"), "w") as f:
                        f.write(stack.stdout + stack.stderr)
                n.proc.kill()
        time.sleep(1)
    for n in newtons:
        try:
            status = n.proc.wait(30)
        except subprocess.TimeoutExpired:
            n.proc.kill()
            status = n.proc.wait()
            problems[n.name].append("did not end; killed")
        n.thread.join(10)
        ran = time.time() - start
        if status != 0 and not any(p.startswith("HUNG") for p in problems[n.name]):
            problems[n.name].append("CRASHED: exit status %d (0x%x) after %d s" % (status, status & 0xffffffff, ran))
        if n.round is None:
            problems[n.name].append("never finished a round (see %s.log)" % n.name)
        elif status == 0 and ran < seconds - 30:
            problems[n.name].append("ended early, after %d s" % ran)
    if listener:
        listener.close()

    lines = []
    for n in newtons:
        lines += summarise(n, problems[n.name])
    if not args.no_print:
        lines.append("print jobs: %d" % len(os.listdir(os.path.join(args.out, "jobs"))))
    text = "\n".join(lines)
    print(text, flush=True)
    with open(os.path.join(args.out, "summary.txt"), "w") as f:
        f.write(text + "\n")
    return 1 if any(p for ps in problems.values() for p in ps if not p.startswith("ended early")) else 0


if __name__ == "__main__":
    sys.exit(main())
