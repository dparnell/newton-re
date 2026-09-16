/*
	File:		frames/ObjHeader.h

	Contains:	The header of every object in the NewtonScript object heap.
				objects.h declares ObjHeader as eight opaque bytes unless
				DEFINED_OBJHEADER says otherwise; the object system defines it
				here, before objects.h, for itself and for anyone who includes
				Frames.h.  ObjectHeap.h has the accessors and the layouts.

	Host re-expression: the ROM's header is two 32-bit words - size << 8 |
	flags, and a GC word holding the lock count in its top byte and, during
	a collection, the marking index or the forwarding address.  With Refs
	pointer-sized the GC word must be too (it holds an address), so both
	words are ULongs; on a 32-bit host that is the ROM's layout.
*/

#ifndef __OBJHEADER_H
#define __OBJHEADER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

struct ObjHeader
{
	ULong	fSizeAndFlags;		// size << 8 | flags
	ULong	fGCStuff;			// lock count in bits 24-31 (0xff: never unlocked); the collector's
								// slot index while marking, the new address while compacting
};
#define DEFINED_OBJHEADER 1

#endif	/* __OBJHEADER_H */
