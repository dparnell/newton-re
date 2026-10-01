/*
	File:		host/HostCStack.h

	Contains:	The host's own C stack, as offsets into the program's image
				- what tools/host/whichfunction.py names - for the traces
				that print one (a host that falls over, NEWTON_TRACE_EXCEPTIONS=2,
				NEWTON_HEAPCHECK).  Header only, so that any library can
				print one without linking anything: on Windows
				RtlCaptureStackBackTrace and the module's base, on a glibc
				or macOS host backtrace() and dladdr's base of the object (<windows.h>
				itself is kept out - its names clash with the Newton's).
				macOS has both as well.  Elsewhere (musl) no stack:
				HostCaptureCStack answers 0.
*/

#ifndef __HOSTCSTACK_H
#define __HOSTCSTACK_H

#ifdef _WIN32
extern "C" {
__declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long skip, unsigned long count, void** trace, unsigned long* hash);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
}
#elif defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#include <dlfcn.h>
#endif

// The program image's base: an address minus this is the offset
// whichfunction.py takes.
static inline char*
HostImageBase(void)
{
#ifdef _WIN32
	return (char*) GetModuleHandleA(0);
#elif defined(__GLIBC__) || defined(__APPLE__)
	Dl_info info;
	if (dladdr((void*) &HostImageBase, &info) != 0)
		return (char*) info.dli_fbase;
	return 0;
#else
	return 0;
#endif
}

// Up to max return addresses of the stack, the innermost first (this
// function's own may be among them when it is not inlined),
// skipping this many of the innermost: ==> how many
static inline int
HostCaptureCStack(void** trace, int max, int skip)
{
#ifdef _WIN32
	return (int) RtlCaptureStackBackTrace((unsigned long) skip, (unsigned long) max, trace, 0);
#elif defined(__GLIBC__) || defined(__APPLE__)
	void* all[128];
	int n = backtrace(all, max + skip < 128 ? max + skip : 128);
	int k = 0;
	for (int i = skip; i < n && k < max; i++)
		trace[k++] = all[i];
	return k;
#else
	(void) trace; (void) max; (void) skip;
	return 0;
#endif
}

#endif	/* __HOSTCSTACK_H */
