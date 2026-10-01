#!/usr/bin/env python3
"""stacksample.py and profile.py on a Linux host.

Linux will not let one process read another's registers without ptrace,
and WSL and most distributions allow ptrace only to a process's own
parent (kernel.yama.ptrace_scope = 1).  So newton samples itself: it
installs a handler for SIGRTMIN+3 (src/host/newton.cpp's
HostInstallSampler), and a thread sent that signal (tgkill) appends one
line to the sample file - its thread id, the interrupted instruction and
the stack's return addresses, unwound by glibc's backtrace() through the
signal frame, each as an offset into the image ("-" for one outside it).
The file is NEWTON_SAMPLE_FILE in newton's environment, else
/tmp/newton-sample-<pid>.txt.  The offsets are named from the executable's
own symbol table (whichfunction.py's ElfSymbols), demangled with c++filt.

Used through stacksample.py and profile.py, which call stacksample_main
and profile_main here when they are run on Linux; the options are theirs.
Standard library only (ctypes for tgkill).
"""

import argparse
import collections
import ctypes
import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import whichfunction as wf			# noqa: E402

SYS_tgkill = {"x86_64": 234, "aarch64": 131}.get(os.uname().machine, 234)
_libc = ctypes.CDLL(None, use_errno=True)


def sample_file(pid):
    """Where newton writes its samples (its environment says, else /tmp)."""
    try:
        with open(f"/proc/{pid}/environ", "rb") as f:
            for item in f.read().split(b"\0"):
                if item.startswith(b"NEWTON_SAMPLE_FILE="):
                    return item.split(b"=", 1)[1].decode()
    except OSError:
        pass
    return f"/tmp/newton-sample-{pid}.txt"


def threads(pid):
    """[(tid, name, cpu ticks, state)] of the process's threads."""
    out = []
    for tid in os.listdir(f"/proc/{pid}/task"):
        try:
            with open(f"/proc/{pid}/task/{tid}/stat") as f:
                stat = f.read()
        except OSError:
            continue
        name = stat[stat.index("(") + 1:stat.rindex(")")]
        rest = stat[stat.rindex(")") + 2:].split()
        state, utime, stime = rest[0], int(rest[11]), int(rest[12])
        out.append((int(tid), name, utime + stime, state))
    return out


def busiest(pid, seconds=0.5):
    """The thread that used the most processor time over a moment."""
    before = {t[0]: t[2] for t in threads(pid)}
    time.sleep(seconds)
    after = threads(pid)
    return max(after, key=lambda t: t[2] - before.get(t[0], 0))[0]


class Sampler:
    def __init__(self, pid):
        self.pid = pid
        self.path = sample_file(pid)
        self.offset = os.path.getsize(self.path) if os.path.exists(self.path) else 0
        self.signal = signal.SIGRTMIN + 3

    def sample(self, tid, timeout=1.0):
        """(pc, [return addresses]) of the thread, as image offsets (None for
        one outside the image); None if the thread did not answer."""
        if _libc.syscall(SYS_tgkill, self.pid, tid, self.signal) != 0:
            return None
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(self.path) and os.path.getsize(self.path) > self.offset:
                with open(self.path) as f:
                    f.seek(self.offset)
                    text = f.read()
                if text.endswith("\n"):
                    self.offset += len(text.encode())
                    for line in text.splitlines():
                        words = line.split()
                        if len(words) >= 2 and int(words[0], 16) == tid:
                            value = [None if w == "-" else int(w, 16) for w in words[1:]]
                            pc, stack = value[0], value[1:]
                            # (the handler's own frames come first, then
                            # libc's signal trampoline - outside the image -
                            # then the interrupted function's callers)
                            if None in stack:
                                stack = stack[stack.index(None) + 1:]
                            return pc, stack
            time.sleep(0.005)
        return None


def _symbols(pid, exe):
    exe = exe or os.readlink(f"/proc/{pid}/exe")
    return exe, wf.ElfSymbols(exe)


def stacksample_main(argv):
    ap = argparse.ArgumentParser(description="stacksample.py on Linux (tools/host/linuxsample.py)")
    ap.add_argument("pid", type=int)
    ap.add_argument("--thread", type=int, default=None, help="the thread id (default: the busiest)")
    ap.add_argument("--samples", type=int, default=3)
    ap.add_argument("--depth", default=None, help="(Windows only: ignored)")
    ap.add_argument("--exe", default=None, help="the executable (default: the process's own)")
    args = ap.parse_args(argv[1:])
    exe, symbols = _symbols(args.pid, args.exe)
    tid = args.thread or busiest(args.pid)
    sampler = Sampler(args.pid)

    def name(offset):
        if offset is None:
            return "(outside the image)"
        hit = symbols.lookup(offset)
        if hit is None:
            return f"{offset:#x} (no function)"
        return f"{offset:#x} {wf.demangle(hit[2])} +{offset - hit[0]:#x}"

    print(f"process {args.pid}, thread {tid} ({exe})")
    for s in range(args.samples):
        got = sampler.sample(tid)
        if got is None:
            raise SystemExit(f"thread {tid} did not answer (is this a newton with the sampler?)")
        pc, stack = got
        print(f"\nsample {s + 1}: pc {name(pc)}")
        for offset in stack:
            if offset is not None:
                print(f"  {name(offset)}")
        time.sleep(0.2)
    return 0


def profile_main(argv):
    ap = argparse.ArgumentParser(description="profile.py on Linux (tools/host/linuxsample.py)")
    ap.add_argument("pid", type=int)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--interval", type=float, default=10, help="milliseconds between rounds")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--depth", default=None, help="(Windows only: ignored)")
    ap.add_argument("--exe", default=None)
    a = ap.parse_args(argv[1:])
    exe, symbols = _symbols(a.pid, a.exe)
    sampler = Sampler(a.pid)

    def function(offset):
        if offset is None:
            return None
        hit = symbols.lookup(offset)
        return hit[0] if hit else None

    selfs = collections.Counter()
    incl = collections.Counter()
    samples = 0
    end = time.time() + a.seconds
    while time.time() < end:
        for tid, tname, ticks, state in threads(a.pid):
            if state != "R":				# (waiting in the system: not a sample)
                continue
            got = sampler.sample(tid, timeout=0.2)
            if got is None:
                continue
            pc, stack = got
            f = function(pc)
            if f is None:
                continue
            samples += 1
            selfs[f] += 1
            seen = {f}
            for offset in stack:
                g = function(offset)
                if g is not None:
                    seen.add(g)
            for g in seen:
                incl[g] += 1
        time.sleep(a.interval / 1000.0)

    names = {}

    def name(start):
        if start not in names:
            names[start] = wf.demangle(symbols.lookup(start)[2])
        return names[start]

    print(f"{samples} samples of process {a.pid} over {a.seconds:g} s")
    if samples == 0:
        return 0
    print("\nself:")
    for f, n in selfs.most_common(a.top):
        print(f"  {100.0 * n / samples:5.1f}%  {name(f)}")
    print("\ninclusive:")
    for f, n in incl.most_common(a.top):
        print(f"  {100.0 * n / samples:5.1f}%  {name(f)}")
    return 0
