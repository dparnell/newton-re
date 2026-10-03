#!/usr/bin/env python3
"""A sampling profiler for a running host build: where its time goes.

Usage:
    python tools/host/profile.py <pid> [--seconds N] [--interval MS] [--top N] [--exe PATH]
    python tools/host/profile.py <pid> --walk [--seconds N] [--interval MS] [--top N]
                                 [--save FILE] [--callees NAME]... [--callers NAME]...
    python tools/host/profile.py --load FILE [--top N] [--callees NAME]... [--callers NAME]...

Every interval it suspends each thread of the process in turn, and a
thread whose instruction pointer is in the executable (not waiting in
the system) is a sample: its function counts once as *self*, and every
function whose return address is on its stack (the same scan as
stacksample.py: words just after a call instruction) once as
*inclusive*.  After the seconds it prints the functions by self time and
by inclusive time, as percentages of the samples.  The host runs one
Newton task at a time, so the samples are nearly all the running task's.

--walk takes each sample's stack exactly instead: the sampled thread,
held while it is read, is unwound by dbghelp's StackWalk64 from the
image's unwind tables (.pdata/.xdata; no frame pointers needed), and every
frame named through dbghelp from the executable's PDB.  A sample is any
thread running in the process - in the C library as well, named by its
module when it has no symbol (a memcpy is "ucrtbase.dll") - whose stack
reaches the executable; one waiting in the system is not.  Inclusive
figures are then exact (each function counted once a sample however deep
it recurs), and two more views are printed for each name given (a
substring of the function's name; the busiest function that matches):
--callees NAME, what NAME's time went to (the frame just inside it, its
own time as "(self)"), and --callers NAME, where it was called from - a
function that recurs counted at every appearance, each callee or caller
once a sample (so they can add up to more than the function's share).  --save FILE keeps the samples' stacks (names,
innermost first) as JSON, and --load FILE prints the views again from
them without a process.

It is how the drawing work was measured (docs/qd/README.md's "Drawing
speed"): run the benchmark (src/host/demo/drawbench.ns) with more rounds
and profile the process meanwhile.  Windows, and Linux through tools/host/linuxsample.py; standard library only.
"""

import argparse
import bisect
import collections
import ctypes
import ctypes.wintypes as wt
import os
import struct
import sys
import time

# (on Linux the process samples itself: tools/host/linuxsample.py)
if __name__ == "__main__" and sys.platform.startswith("linux"):
	sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
	import linuxsample
	sys.exit(linuxsample.profile_main(sys.argv))


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
	ap.add_argument("pid", type=int, nargs="?")
	ap.add_argument("--seconds", type=float, default=10)
	ap.add_argument("--interval", type=float, default=5, help="milliseconds between samples")
	ap.add_argument("--top", type=int, default=30)
	ap.add_argument("--depth", type=lambda x: int(x, 0), default=0x4000)
	ap.add_argument("--exe", default=None)
	ap.add_argument("--walk", action="store_true", help="exact stacks through dbghelp")
	ap.add_argument("--save", default=None, help="keep the walked stacks as JSON")
	ap.add_argument("--load", default=None, help="print the views from saved stacks")
	ap.add_argument("--callees", action="append", default=[], help="what a function's time went to")
	ap.add_argument("--callers", action="append", default=[], help="where a function was called from")
	a = ap.parse_args(argv[1:])
	if a.load:
		import json
		with open(a.load) as f:
			report_stacks(json.load(f), a)
		return 0
	if a.pid is None:
		ap.error("a pid (or --load FILE)")
	if a.walk:
		return walk_main(a)

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


# --walk: exact stacks through dbghelp

class ADDRESS64(ctypes.Structure):
	_fields_ = [("Offset", ctypes.c_uint64), ("Segment", ctypes.c_uint16), ("Mode", ctypes.c_uint32)]


class STACKFRAME64(ctypes.Structure):
	# (KDHELP64 at the end only needs room: StackWalk64 fills it)
	_fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64),
				("AddrStack", ADDRESS64), ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p),
				("Params", ctypes.c_uint64 * 4), ("Far", ctypes.c_int32), ("Virtual", ctypes.c_int32),
				("Reserved", ctypes.c_uint64 * 3), ("KdHelp", ctypes.c_byte * 512)]


class SYMBOL_INFO(ctypes.Structure):
	_fields_ = [("SizeOfStruct", ctypes.c_uint32), ("TypeIndex", ctypes.c_uint32), ("Reserved", ctypes.c_uint64 * 2),
				("Index", ctypes.c_uint32), ("Size", ctypes.c_uint32), ("ModBase", ctypes.c_uint64),
				("Flags", ctypes.c_uint32), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64),
				("Register", ctypes.c_uint32), ("Scope", ctypes.c_uint32), ("Tag", ctypes.c_uint32),
				("NameLen", ctypes.c_uint32), ("MaxNameLen", ctypes.c_uint32), ("Name", ctypes.c_char * 1024)]


# a thread whose innermost frame is in one of these is waiting, not running
WAITING_MODULES = ("ntdll.dll", "kernelbase.dll", "win32u.dll", "kernel32.dll")


def walk_main(a):
	import json
	dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True)
	dbghelp.SymSetOptions.argtypes = [ctypes.c_uint32]
	dbghelp.SymInitialize.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
	dbghelp.SymFromAddr.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
	dbghelp.SymGetModuleBase64.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
	dbghelp.SymGetModuleBase64.restype = ctypes.c_uint64
	dbghelp.StackWalk64.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
									ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
	process = ss.k32.OpenProcess(ss.PROCESS_ALL, False, a.pid)
	if not process:
		raise SystemExit(f"cannot open process {a.pid}")
	exe_base, exe_size, exe_path = ss.module_of(process, os.path.basename(a.exe or "newton.exe"))
	dbghelp.SymSetOptions(0x2 | 0x4)					# SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
	if not dbghelp.SymInitialize(process, os.path.dirname(exe_path).encode(), True):
		raise SystemExit(f"SymInitialize failed ({ctypes.get_last_error()})")
	dbghelp.SymLoadModuleEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
										ctypes.c_uint64, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
	dbghelp.SymLoadModuleEx(process, None, exe_path.encode(), None, exe_base, exe_size, None, 0)
	table_access = ctypes.cast(dbghelp.SymFunctionTableAccess64, ctypes.c_void_p)
	module_base = ctypes.cast(dbghelp.SymGetModuleBase64, ctypes.c_void_p)
	# (a CONTEXT must be 16-aligned: one made in a larger buffer)
	raw = ctypes.create_string_buffer(ctypes.sizeof(ss.CONTEXT) + 16)
	ctx = ss.CONTEXT.from_address((ctypes.addressof(raw) + 15) & ~15)

	names = {}
	modnames = {}
	modules = {}
	symbol = SYMBOL_INFO()

	def name(pc):
		symbol.SizeOfStruct = 88			# (the C struct with its one-character name, padded)
		symbol.MaxNameLen = 1000
		disp = ctypes.c_uint64()
		if dbghelp.SymFromAddr(process, pc, ctypes.byref(disp), ctypes.byref(symbol)):
			return symbol.Name.decode("latin-1")
		base = dbghelp.SymGetModuleBase64(process, pc)
		if base not in modnames:
			buf = ctypes.create_unicode_buffer(512)
			if base:
				ss.psapi.GetModuleFileNameExW(process, ctypes.c_void_p(base), buf, 512)
			modnames[base] = os.path.basename(buf.value) if base and buf.value else f"?{pc:#x}"
		return modnames[base]

	def cached(pc):
		if pc not in names:
			names[pc] = name(pc)
		return names[pc]

	def module(pc):
		base = dbghelp.SymGetModuleBase64(process, pc)
		if base not in modules:
			buf = ctypes.create_unicode_buffer(512)
			if base:
				ss.psapi.GetModuleFileNameExW(process, ctypes.c_void_p(base), buf, 512)
			modules[base] = os.path.basename(buf.value).lower() if base and buf.value else ""
		return modules[base]

	handles = {}
	stacks = []
	cpu = {}							# each thread's processor time when last looked at
	tids, listed = [], 0.0
	times = [wt.FILETIME() for _ in range(4)]
	end = time.time() + a.seconds
	while time.time() < end:
		if time.time() - listed > 1.0:
			tids, listed = threads_of(a.pid), time.time()
		for tid in tids:
			h = handles.get(tid)
			if h is None:
				h = handles[tid] = ss.k32.OpenThread(ss.THREAD_ALL, False, tid)
			if not h:
				continue
			# (only a thread that has run since it was last looked at is held
			# and walked: the rest are waiting, and holding each is slow)
			if not ss.k32.GetThreadTimes(h, *[ctypes.byref(t) for t in times]):
				continue
			used = sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:])
			if cpu.get(tid) == used:
				continue
			cpu[tid] = used
			if ss.k32.SuspendThread(h) == 0xFFFFFFFF:
				continue
			pcs = []
			try:
				ctx.ContextFlags = ss.CONTEXT_FULL
				if not ss.k32.GetThreadContext(h, ctypes.byref(ctx)):
					continue
				# (a thread waiting in the system is not walked - most are)
				if module(ctx.Rip) in WAITING_MODULES:
					continue
				frame = STACKFRAME64()
				frame.AddrPC.Offset, frame.AddrPC.Mode = ctx.Rip, 3
				frame.AddrFrame.Offset, frame.AddrFrame.Mode = ctx.Rbp, 3
				frame.AddrStack.Offset, frame.AddrStack.Mode = ctx.Rsp, 3
				for depth in range(256):
					if not dbghelp.StackWalk64(0x8664, process, h, ctypes.byref(frame), ctypes.addressof(ctx),
											   None, table_access, module_base, None):
						break
					pc = frame.AddrPC.Offset
					if pc == 0:
						break
					pcs.append(pc if depth == 0 else pc - 1)		# (a return address: the call before it)
			finally:
				ss.k32.ResumeThread(h)
			if not pcs or not any(exe_base <= pc < exe_base + exe_size for pc in pcs):
				continue
			stacks.append([cached(pc) for pc in pcs])
		ss.k32.Sleep(int(a.interval))
	if a.save:
		with open(a.save, "w") as f:
			json.dump(stacks, f)
	print(f"{len(stacks)} samples of process {a.pid} over {a.seconds:g} s (exact stacks)")
	report_stacks(stacks, a)
	return 0


def report_stacks(stacks, a):
	"""The self and inclusive tables, and --callees/--callers, from stacks
	(each a list of names, innermost first)."""
	n = len(stacks)
	if n == 0:
		print("no samples")
		return
	selfs = collections.Counter(s[0] for s in stacks)
	incl = collections.Counter()
	for s in stacks:
		for f in set(s):
			incl[f] += 1
	print("\nself:")
	for f, k in selfs.most_common(a.top):
		print(f"  {100.0 * k / n:5.1f}%  {f}")
	print("\ninclusive:")
	for f, k in incl.most_common(a.top):
		print(f"  {100.0 * k / n:5.1f}%  {f}")

	def resolve(part):
		matches = [f for f, _ in incl.most_common() if part in f]
		return matches[0] if matches else None

	for part in a.callees:
		f = resolve(part)
		if f is None:
			print(f"\nno function matches {part!r}")
			continue
		inside = collections.Counter()
		total = 0
		for s in stacks:
			if f not in s:
				continue
			total += 1
			# (every appearance - a function may recur - each callee once a sample)
			inside.update({"(self)" if i == 0 else s[i - 1] for i, g in enumerate(s) if g == f})
		print(f"\ncallees of {f} ({100.0 * total / n:.1f}% of the samples):")
		for g, k in inside.most_common(a.top):
			print(f"  {100.0 * k / n:5.1f}%  {g}")
	for part in a.callers:
		f = resolve(part)
		if f is None:
			print(f"\nno function matches {part!r}")
			continue
		outside = collections.Counter()
		total = 0
		for s in stacks:
			if f not in s:
				continue
			total += 1
			outside.update({"(the thread's start)" if i == len(s) - 1 else s[i + 1] for i, g in enumerate(s) if g == f})
		print(f"\ncallers of {f} ({100.0 * total / n:.1f}% of the samples):")
		for g, k in outside.most_common(a.top):
			print(f"  {100.0 * k / n:5.1f}%  {g}")


if __name__ == "__main__":
	sys.exit(main(sys.argv))
