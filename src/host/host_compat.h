/*
	host_compat.h - pre-included (-include) when the DDK headers are compiled
	on a host toolchain rather than the original ARM compiler.

	NewtonTypes.h guards both `typedef unsigned char Boolean` and an
	`enum { false, true }` behind __boolean_defined__; the enum is illegal in
	C++, so we define Boolean ourselves and set the guard.  stdlib.h would
	typedef the wchar_t keyword unless __wchar_t is set.
*/
#ifndef __HOST_COMPAT_H
#define __HOST_COMPAT_H

typedef unsigned char Boolean;
#define __boolean_defined__ 1
#define __wchar_t 1

#endif /* __HOST_COMPAT_H */
