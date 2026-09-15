/*
	host_compat.h - pre-included (-include) when the DDK headers are compiled
	on a host toolchain rather than the original ARM compiler.

	NewtonTypes.h guards both `typedef unsigned char Boolean` and an
	`enum { false, true }` behind __boolean_defined__; the enum is illegal in
	C++, so we define Boolean ourselves and set the guard.  stdlib.h would
	typedef the wchar_t keyword unless __wchar_t is set.  NewtonExceptions.h
	redefines the try/catch/throw keywords to its setjmp-based handlers unless
	told the compiler has real exceptions (the flag it sets for MSVC itself).
	VAddr, a 32-bit virtual address on the MessagePad, holds a host pointer
	here (hostVAddrIsPointerSized, see sync_ddk_headers.py) because task stacks
	and shared-memory buffers are host memory.
*/
#ifndef __HOST_COMPAT_H
#define __HOST_COMPAT_H

typedef unsigned char Boolean;
#define __boolean_defined__ 1
#define __wchar_t 1
#define hasCppExceptions 1
#define hostVAddrIsPointerSized 1
#include <stdint.h>

#endif /* __HOST_COMPAT_H */
