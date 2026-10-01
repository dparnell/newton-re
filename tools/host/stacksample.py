#!/usr/bin/env python3
"""Look inside a host build that has locked up: what its busy thread is doing.

Usage:
    python tools/host/stacksample.py <pid> [--thread TID] [--samples N] [--exe PATH]
    python tools/host/stacksample.py 73092 --samples 5

A host program (`newton.exe`, a test) that stops answering while one thread
eats a whole CPU is looping somewhere.  With no debugger to hand, this is
the next best thing: it suspends the thread, reads its registers
(GetThreadContext) and the top of its stack, lets it go again, and names
what it found.

  * the instruction pointer, sampled a few times, says where the loop is;
  * the words on the stack that point into the executable's code just after
    a call instruction are return addresses - the functions the loop was
    called from, outermost last.  It is a scan, not an unwind, so a stale
    return address left on the stack by an earlier call can turn up too;
    the functions that recur from sample to sample are the real frames.

Each address is turned into a function name the way `whichfunction.py`
does it (the function's bounds from `.pdata`, the name from the object file
whose bytes match), so the executable must be the build the process is
running - do not rebuild before looking.

**Inputs:** the process id (Task Manager, or `Get-Process newton`); the
thread with the most CPU time is taken unless `--thread` names one.
**Output:** per sample, the instruction pointer and the stack's return
addresses as image offsets with function names.  Nothing is written, and
the process carries on afterwards.  Windows, and Linux through tools/host/linuxsample.py; standard library only.
"""

import argparse
import os
import struct
import sys

# (on Linux the process samples itself: tools/host/linuxsample.py)
if __name__ == "__main__" and sys.platform.startswith("linux"):
	sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
	import linuxsample
	sys.exit(linuxsample.stacksample_main(sys.argv))

import ctypes
import ctypes.wintypes as wt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import whichfunction as wf			# noqa: E402

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll")

# handles and module addresses are 64 bits: say so, or ctypes truncates them
k32.OpenProcess.restype = wt.HANDLE
k32.OpenThread.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.SuspendThread.argtypes = [wt.HANDLE]
k32.ResumeThread.argtypes = [wt.HANDLE]
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.c_void_p] * 4
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.Thread32First.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.Thread32Next.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
								  ctypes.POINTER(ctypes.c_size_t)]
psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.DWORD]
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.LPWSTR, wt.DWORD]
psapi.GetModuleInformation.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wt.DWORD]

PROCESS_ALL = 0x0010 | 0x0400 | 0x0008		# VM_READ | QUERY_INFORMATION | VM_OPERATION
THREAD_ALL = 0x0002 | 0x0008 | 0x0010 | 0x0040	# SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION | QUERY_LIMITED
CONTEXT_FULL = 0x10000B					# CONTEXT_AMD64 | CONTROL | INTEGER | FLOATING_POINT


class CONTEXT(ctypes.Structure):
	# the x64 CONTEXT up to the integer registers; it must be 16-aligned and
	# 0x4d0 bytes, so the rest is padding
	_fields_ = [("P1Home", ctypes.c_uint64 * 6), ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
				("SegCs", wt.WORD), ("SegDs", wt.WORD), ("SegEs", wt.WORD), ("SegFs", wt.WORD),
				("SegGs", wt.WORD), ("SegSs", wt.WORD), ("EFlags", wt.DWORD),
				("Dr", ctypes.c_uint64 * 6),
				("Rax", ctypes.c_uint64), ("Rcx", ctypes.c_uint64), ("Rdx", ctypes.c_uint64),
				("Rbx", ctypes.c_uint64), ("Rsp", ctypes.c_uint64), ("Rbp", ctypes.c_uint64),
				("Rsi", ctypes.c_uint64), ("Rdi", ctypes.c_uint64),
				("R8_15", ctypes.c_uint64 * 8), ("Rip", ctypes.c_uint64),
				("Rest", ctypes.c_byte * (0x4d0 - 0xf8 - 8))]


def module_of(process, exe_name):
	"""(base, size) of the process's module with that file name."""
	mods = (wt.HMODULE * 1024)()
	needed = wt.DWORD()
	psapi.EnumProcessModulesEx(process, mods, ctypes.sizeof(mods), ctypes.byref(needed), 3)
	class MODINFO(ctypes.Structure):
		_fields_ = [("base", ctypes.c_void_p), ("size", wt.DWORD), ("entry", ctypes.c_void_p)]
	name = ctypes.create_unicode_buffer(512)
	for i in range(needed.value // ctypes.sizeof(wt.HMODULE)):
		psapi.GetModuleFileNameExW(process, mods[i], name, 512)
		if os.path.basename(name.value).lower() == exe_name.lower():
			info = MODINFO()
			psapi.GetModuleInformation(process, mods[i], ctypes.byref(info), ctypes.sizeof(info))
			return info.base, info.size, name.value
	raise SystemExit(f"no module {exe_name} in the process")


def busiest_thread(pid):
	"""The id of the thread of the process with the most CPU time."""
	TH32CS_SNAPTHREAD = 4
	class THREADENTRY32(ctypes.Structure):
		_fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
					("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG),
					("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]
	snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
	entry = THREADENTRY32()
	entry.dwSize = ctypes.sizeof(entry)
	best, best_time = None, -1
	ok = k32.Thread32First(snap, ctypes.byref(entry))
	while ok:
		if entry.th32OwnerProcessID == pid:
			h = k32.OpenThread(0x0800, False, entry.th32ThreadID)	# QUERY_LIMITED_INFORMATION
			times = [wt.FILETIME() for _ in range(4)]
			if h and k32.GetThreadTimes(h, *[ctypes.byref(t) for t in times]):
				t = (times[2].dwHighDateTime << 32 | times[2].dwLowDateTime) + \
					(times[3].dwHighDateTime << 32 | times[3].dwLowDateTime)
				if t > best_time:
					best, best_time = entry.th32ThreadID, t
			if h:
				k32.CloseHandle(h)
		ok = k32.Thread32Next(snap, ctypes.byref(entry))
	k32.CloseHandle(snap)
	return best


def read(process, address, size):
	buf = ctypes.create_string_buffer(size)
	got = ctypes.c_size_t()
	k32.ReadProcessMemory(process, ctypes.c_void_p(address), buf, size, ctypes.byref(got))
	return buf.raw[:got.value]


def after_call(image, sections, rva):
	"""Whether the code just before rva ends in a call instruction."""
	off = wf.rva_to_offset(sections, rva)
	if off is None or off < 7:
		return False
	b = image[off - 7:off]
	if len(b) < 7:							# (an address past the section's file data)
		return False
	if b[2] == 0xE8:						# call rel32
		return True
	if b[5] == 0xFF and (b[6] >> 3) & 7 == 2:		# call reg
		return True
	if b[4] == 0xFF and (b[5] >> 3) & 7 == 2:		# call [reg+disp8]
		return True
	if b[1] == 0xFF and (b[2] >> 3) & 7 == 2:		# call [reg+disp32]
		return True
	if b[3] == 0xFF and (b[4] >> 3) & 7 == 2:		# call [rip/reg+...] with a prefix
		return True
	return False


def main(argv):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("pid", type=int)
	ap.add_argument("--thread", type=int, default=None, help="the thread id (default: the busiest)")
	ap.add_argument("--samples", type=int, default=3)
	ap.add_argument("--depth", type=lambda x: int(x, 0), default=0x8000, help="bytes of stack to scan (default 0x8000)")
	ap.add_argument("--exe", default=None, help="the executable (default: the process's own)")
	args = ap.parse_args(argv[1:])

	process = k32.OpenProcess(PROCESS_ALL, False, args.pid)
	if not process:
		raise SystemExit(f"cannot open process {args.pid}")
	tid = args.thread or busiest_thread(args.pid)
	base, size, path = module_of(process, os.path.basename(args.exe or "newton.exe"))
	exe = args.exe or path
	with open(exe, "rb") as f:
		image = f.read()
	sections = wf.pe_sections(image)
	root = os.path.dirname(os.path.dirname(os.path.abspath(exe)))
	objs = [p for p in __import__("glob").glob(os.path.join(root, "**", "*.obj"), recursive=True)]
	names = {}

	def name(rva):
		bounds = wf.function_bounds(image, sections, rva)
		if bounds is None:
			return f"{rva:#x} (no function)"
		if bounds not in names:
			begin, end = bounds
			off = wf.rva_to_offset(sections, begin)
			found = wf.find(image[off:off + (end - begin)], objs, 8)
			names[bounds] = found[0][2] if found else f"?{begin:#x}"
		return f"{rva:#x} {names[bounds]} +{rva - bounds[0]:#x}"

	thread = k32.OpenThread(THREAD_ALL, False, tid)
	if not thread:
		raise SystemExit(f"cannot open thread {tid}")
	print(f"process {args.pid}, thread {tid}, image {base:#x} ({exe})")
	for s in range(args.samples):
		ctx = CONTEXT()
		ctx.ContextFlags = CONTEXT_FULL
		k32.SuspendThread(thread)
		try:
			if not k32.GetThreadContext(thread, ctypes.byref(ctx)):
				raise SystemExit(f"GetThreadContext failed ({ctypes.get_last_error()})")
			stack = read(process, ctx.Rsp, args.depth)
		finally:
			k32.ResumeThread(thread)
		print(f"\nsample {s + 1}: rip {name(ctx.Rip - base) if base <= ctx.Rip < base + size else hex(ctx.Rip)}")
		seen = 0
		for i in range(0, len(stack) - 7, 8):
			word, = struct.unpack_from("<Q", stack, i)
			if base <= word < base + size and after_call(image, sections, word - base):
				print(f"  [rsp+{i:#06x}] {name(word - base)}")
				seen += 1
				if seen >= 60:
					break
		k32.Sleep(200)
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
