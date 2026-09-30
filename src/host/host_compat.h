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

	Fixed and Fract, the DDK's 16.16 fixed-point numbers, are the ARM's
	32-bit word and nothing more.  Windows' `long` is that width already, so
	nothing is said there; where it is wider - an LP64 Linux or macOS - they
	are spelt `int` instead (hostLongIsWiderThanARMWord, see
	sync_ddk_headers.py), so that the fixed-point arithmetic wraps as the
	ARM's does and an FPoint and an FRect are laid out as the ROM's are.
*/
#ifndef __HOST_COMPAT_H
#define __HOST_COMPAT_H

typedef unsigned char Boolean;
#define __boolean_defined__ 1
#define __wchar_t 1
#define hasCppExceptions 1
#define hostVAddrIsPointerSized 1
#define hostLongIsPointerSized 1
#if defined(__LP64__) || defined(_LP64)
#define hostLongIsWiderThanARMWord 1
#endif
#include <stdint.h>

// the ARM's 32-bit word where its width is what matters: data laid out in
// the ROM image, in packages or on a store (ULong and Long being
// pointer-sized here)
typedef uint32_t	ULong32;
typedef int32_t		Long32;

#endif /* __HOST_COMPAT_H */
