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
	and shared-memory buffers are host memory; so do ULong and Long
	(hostLongIsPointerSized), the ARM's word, which the OS uses for refcons and
	task arguments that carry pointers - on an LP64 Linux they are already,
	this makes Windows (LLP64) agree.
*/
#ifndef __HOST_COMPAT_H
#define __HOST_COMPAT_H

typedef unsigned char Boolean;
#define __boolean_defined__ 1
#define __wchar_t 1
#define hasCppExceptions 1
#define hostVAddrIsPointerSized 1
#define hostLongIsPointerSized 1
#include <stdint.h>

// the ARM's 32-bit word where its width is what matters: data laid out in
// the ROM image, in packages or on a store (ULong and Long being
// pointer-sized here)
typedef uint32_t	ULong32;
typedef int32_t		Long32;

#endif /* __HOST_COMPAT_H */
