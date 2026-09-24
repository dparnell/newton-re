/*
	File:		toolbox/FixedMathExtra.h

	Contains:	The ROM's fixed-point routines that the DDK's FixedMath.h
				does not declare (they were internal to Apple), reconstructed
				alongside the declared ones in FixedMath.cpp.  Kept here, out
				of the generated src/ddk/FixedMath.h, so callers have a home
				for them.

				FixedASin/FixedACos take a Fract (2.30, -1..1) and answer the
				angle in 16.16 radians, over the same arctangent polynomial
				as FixedAtan2 (FixedMath.h).  FractSin/FractCos go the
				other way: 16.16 radians in, a 2.30 sine or cosine out,
				over FractSineCosine, which works in degrees.
*/

#ifndef __FIXEDMATHEXTRA_H
#define __FIXEDMATHEXTRA_H

#ifndef __FIXEDMATH_H
#include "FixedMath.h"
#endif

extern "C" {
Fixed	FixedLength(Fixed dx, Fixed dy);	// ROM 0x000bd87c FixedLength - no square root: a shift-and-add approximation
Fixed	FixedRoundBy(Fixed value, Fixed unit);	// ROM 0x000bd904 FixedRoundBy - rounded to a multiple of unit
}

Fixed	FixedASin(Fract x);		// ROM 0x00255158 FixedASin__Fl
Fixed	FixedACos(Fract x);		// ROM 0x002551a4 FixedACos__Fl

Fract	FractSin(Fixed radians);	// ROM 0x00038060 xFracSin__Fl (FractSin 0x0011b850 branches to it)
Fract	FractCos(Fixed radians);	// ROM 0x00038088 xFracCos__Fl (FractCos 0x0011b854 branches to it)

extern const Fixed	kFixedRadiansToDegrees;		// 57.29578 in 16.16

// The ARM's own `add` and `sub`: they wrap, where a signed 32-bit add or
// subtract written out in C++ is undefined and traps under the host's
// sanitiser.  Reconstructed arithmetic that the ROM lets run past the
// end of a Fixed goes through these, so the host computes the same
// (wrong) number the machine did.
inline Fixed	WrapAdd(Fixed a, Fixed b)	{ return (Fixed) ((ULong) a + (ULong) b); }
inline Fixed	WrapSub(Fixed a, Fixed b)	{ return (Fixed) ((ULong) a - (ULong) b); }

#endif	/* __FIXEDMATHEXTRA_H */
