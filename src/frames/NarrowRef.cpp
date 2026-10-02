/*
	File:		frames/NarrowRef.cpp

	Contains:	The 64-bit flavour's narrowing policy at the 32-bit
				boundary (NarrowRef.h).  Host code: no ROM counterpart.
*/

#include "NarrowRef.h"

#if NEWTON_NS64

#include "ObjectHeap.h"
#include "NSErrors.h"

#include <stdio.h>
#include <stdlib.h>


// read once each (a getenv on a hot path is costly on Windows)
static int sStrict = -1;

static bool
Strict(void)
{
	if (sStrict < 0)
		sStrict = getenv("NEWTON_NS64_STRICT") != nil;
	return sStrict != 0;
}


void
SetNarrowStrict(bool strict)
{
	sStrict = strict ? 1 : 0;
}


static bool
Trace(void)
{
	static int trace = -1;
	if (trace < 0)
		trace = getenv("NEWTON_TRACE_NARROW") != nil;
	return trace != 0;
}


static void
Narrowed(Long value, Long narrowed, const char* where)
{
	if (Trace())
		fprintf(stderr, "[narrow] %s: %lld -> %lld\n", where, (long long) value, (long long) narrowed);
	if (Strict())
		ThrowExFramesWithBadValue(kNSErrLongOutOfRange, RefVar(MAKEINT(value)));
}


Long
NarrowInteger(Long value, const char* where)
{
	if (FitsDeviceInteger(value))
		return value;
	// the low 30 bits, sign-extended: what the ARM's word would have held
	Long narrowed = (Long) ((Long32) ((ULong32) value << 2) >> 2);
	Narrowed(value, narrowed, where);
	return narrowed;
}


Ref
NarrowRef(Ref ref, const char* where)
{
	if (!ISINT(ref))
		return ref;
	Long value = RVALUE(ref);
	if (FitsDeviceInteger(value))
		return ref;
	return MAKEINT(NarrowInteger(value, where));
}


long
LongArg(Long value)
{
	if ((Long) (long) value != value)
		ThrowExFramesWithBadValue(kNSErrOutOfRange, RefVar(MAKEINT(value)));
	return (long) value;
}


Long
NarrowToWord(Long value, const char* where)
{
	if (value >= -((Long) 1 << 31) && value < ((Long) 1 << 32))
		return value;
	Long narrowed = (Long) (Long32) (ULong32) value;
	Narrowed(value, narrowed, where);
	return narrowed;
}

#endif
