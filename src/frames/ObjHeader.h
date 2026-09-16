/*
	File:		frames/ObjHeader.h

	Contains:	The header of every object in the NewtonScript object heap.
				objects.h defines ObjHeader (the sync patches make it the
				host's; the DDK left eight opaque bytes); ObjectHeap.h has
				the accessors and the layouts.

	Host re-expression: the ROM's header is two 32-bit words - size << 8 |
	flags, and a GC word holding the lock count in its top byte and, during
	a collection, the marking index or the forwarding address.  With Refs
	pointer-sized the GC word must be too (it holds an address), so both
	words are ULongs; on a 32-bit host that is the ROM's layout.
*/

#ifndef __OBJHEADER_H
#define __OBJHEADER_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

#endif	/* __OBJHEADER_H */
