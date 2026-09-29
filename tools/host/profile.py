#!/usr/bin/env python3
"""A sampling profiler for a running host build: where its time goes.

Usage:
    python tools/host/profile.py <pid> [--seconds N] [--interval MS] [--top N] [--exe PATH]

Every interval it suspends each thread of the process in turn, and a
thread whose instruction pointer is in the executable (not waiting in
the system) is a sample: its function counts once as *self*, and every
function whose return address is on its stack (the same scan as
stacksample.py: words just after a call instruction) once as
*inclusive*.  After the seconds it prints the functions by self time and
by inclusive time, as percentages of the samples.  The host runs one
Newton task at a time, so the samples are nearly all the running task's.

It is how the drawing work was measured (docs/qd/README.md's "Drawing
speed"): run the benchmark (src/host/demo/drawbench.ns) with more rounds
and profile the process meanwhile.  Windows only; standard library only.
"""

import argparse
import bisect
import collections
import ctypes
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stacksample as ss			# noqa: E402
import whichfunction as wf			# noqa: E402

TH32CS_SNAPTHREAD = 4


def threads_of(pid):
	class THREADENTRY32(ctypes.Structure):
		_fields_ = [("dwSize", ctypes.c_uint32), ("cntUsage", ctypes.c_uint32), ("th32ThreadID", ctypes.c_uint32),
					("th32OwnerProcessID", ctypes.c_uint32), ("tpBasePri", ctypes.c_long),
					("tpDeltaPri", ctypes.c_long), ("dwFlags", ctypes.c_uint32)]
	snap = ss.k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
	entry = THREADENTRY32()
	entry.dwSize = ctypes.sizeof(entry)
	tids = []
	ok = ss.k32.Thread32First(snap, ctypes.byref(entry))
	while ok:
		if entry.th32OwnerProcessID == pid:
			tids.append(entry.th32ThreadID)
		ok = ss.k32.Thread32Next(snap, ctypes.byref(entry))
	ss.k32.CloseHandle(snap)
	return tids


def main(argv):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("pid", type=int)
	ap.add_argument("--seconds", type=float, default=10)
	ap.add_argument("--interval", type=float, default=5, help="milliseconds between samples")
	ap.add_argument("--top", type=int, default=30)
	ap.add_argument("--depth", type=lambda x: int(x, 0), default=0x4000)
	ap.add_argument("--exe", default=None)
	a = ap.parse_args(argv[1:])

	process = ss.k32.OpenProcess(ss.PROCESS_ALL, False, a.pid)
	if not process:
		raise SystemExit(f"cannot open process {a.pid}")
	base, size, path = ss.module_of(process, os.path.basename(a.exe or "newton.exe"))
	exe = a.exe or path
	image = open(exe, "rb").read()
	sections = wf.pe_sections(image)
	# the functions' bounds, from the .pdata table once, sorted (a lookup is
	# a bisection - whichfunction's linear scan is too slow to sample with)
	pdata = next(s for s in sections if s[0] == ".pdata")
	starts, ends = [], []
	for i in range(pdata[4] // 12):
		begin, end_, _ = struct.unpack_from("<III", image, pdata[3] + i * 12)
		starts.append(begin)
		ends.append(end_)
	order = sorted(range(len(starts)), key=lambda k: starts[k])
	starts = [starts[k] for k in order]
	ends = [ends[k] for k in order]

	def function(rva):
		k = bisect.bisect_right(starts, rva) - 1
		if k >= 0 and rva < ends[k]:
			return (starts[k], ends[k])
		return None

	handles = {}
	selfs = collections.Counter()
	incl = collections.Counter()
	samples = 0
	end = time.time() + a.seconds
	while time.time() < end:
		for tid in threads_of(a.pid):
			h = handles.get(tid)
			if h is None:
				h = handles[tid] = ss.k32.OpenThread(ss.THREAD_ALL, False, tid)
			if not h:
				continue
			ctx = ss.CONTEXT()
			ctx.ContextFlags = ss.CONTEXT_FULL
			if ss.k32.SuspendThread(h) == 0xFFFFFFFF:
				continue
			try:
				if not ss.k32.GetThreadContext(h, ctypes.byref(ctx)):
					continue
				if not (base <= ctx.Rip < base + size):
					continue
				stack = ss.read(process, ctx.Rsp, a.depth)
			finally:
				ss.k32.ResumeThread(h)
			f = function(ctx.Rip - base)
			if f is None:
				continue
			samples += 1
			selfs[f] += 1
			seen = {f}
			for i in range(0, len(stack) - 7, 8):
				word, = struct.unpack_from("<Q", stack, i)
				if base <= word < base + size and ss.after_call(image, sections, word - base):
					g = function(word - base)
					if g is not None and g not in seen:
						seen.add(g)
			for g in seen:
				incl[g] += 1
		ss.k32.Sleep(int(a.interval))

	objs = [p for p in __import__("glob").glob(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(exe))), "**", "*.obj"), recursive=True)]
	names = {}

	def name(bounds):
		if bounds not in names:
			b, e = bounds
			off = wf.rva_to_offset(sections, b)
			found = wf.find(image[off:off + (e - b)], objs, 8)
			names[bounds] = found[0][2] if found else f"?{b:#x}"
		return names[bounds]

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


if __name__ == "__main__":
	sys.exit(main(sys.argv))
