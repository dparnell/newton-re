/*
	File:		frames/NarrowRef.h

	Contains:	The 32-bit boundary of the 64-bit NewtonScript flavour
				(NEWTON_NS64, docs/frames/64bit.md): what a value wider than
				the device's becomes when it crosses into a format that
				holds the ARM's word - a store, NSOF (beaming, docking,
				endpoints, frame parts), a soup index key, package native
				code on src/armcpu, a device-side C field.

	Every such format stays 32-bit, so a store, a package or an NSOF stream
	is the same bytes whichever flavour wrote it.  An integer that fits in
	the device's 30 bits crosses unchanged; one that does not is narrowed
	by one policy, applied the same way at every outbound site:

	  wrap (the default)  the integer is cut to 30 bits, sign-extended - the
	                      value the MessagePad itself would have computed,
	                      so a store holds what the device would have held,
	                      and an index key and the entry it indexes agree;
	  strict              NEWTON_NS64_STRICT set in the environment: the
	                      crossing throws kNSErrLongOutOfRange with the
	                      value, so a script or a test finds where a wide
	                      value meets the boundary.

	NEWTON_TRACE_NARROW prints each value narrowed and where.

	In the faithful flavour (the default build) every integer already fits
	and these are the identity, inline: nothing changes.
*/

#ifndef __NARROWREF_H
#define __NARROWREF_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

#if NEWTON_NS64

// a Ref as the ARM's word: an integer cut to 30 bits, anything else unchanged
// (the other immediates fit; a pointer Ref is the format's business)
Ref		NarrowRef(Ref ref, const char* where);

// an integer's value as the device's 30-bit integer
Long	NarrowInteger(Long value, const char* where);

// a value for a 32-bit device field (a C long or ULong in a template, a
// word on the wire): kept if it is a signed or an unsigned 32-bit value,
// cut to 32 bits otherwise
Long	NarrowToWord(Long value, const char* where);

// (tests: strict or not, whatever NEWTON_NS64_STRICT says)
void	SetNarrowStrict(bool strict);

// does an integer fit in the device's 30 bits?
inline bool	FitsDeviceInteger(Long value)	{ return value >= -(1L << 29) && value < (1L << 29); }

#else

inline Ref	NarrowRef(Ref ref, const char*)				{ return ref; }
inline Long	NarrowInteger(Long value, const char*)		{ return value; }
inline Long	NarrowToWord(Long value, const char*)		{ return value; }
inline bool	FitsDeviceInteger(Long)						{ return true; }

#endif

#endif	/* __NARROWREF_H */
