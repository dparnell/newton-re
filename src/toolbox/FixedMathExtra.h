/*
	File:		toolbox/FixedMathExtra.h

	Contains:	The ROM's fixed-point routines that the DDK's FixedMath.h
				does not declare (they were internal to Apple), reconstructed
				alongside the declared ones in FixedMath.cpp.  Kept here, out
				of the generated src/ddk/FixedMath.h, so callers have a home
				for them.

				FixedASin/FixedACos take a Fract (2.30, -1..1) and answer the
				angle in 16.16 radians, over the same arctangent polynomial
				as FixedAtan2 (FixedMath.h).
*/

#ifndef __FIXEDMATHEXTRA_H
#define __FIXEDMATHEXTRA_H

#ifndef __FIXEDMATH_H
#include "FixedMath.h"
#endif

Fixed	FixedASin(Fract x);		// ROM 0x00253210 FixedASin__Fl
Fixed	FixedACos(Fract x);		// ROM 0x0025325c FixedACos__Fl

#endif	/* __FIXEDMATHEXTRA_H */
