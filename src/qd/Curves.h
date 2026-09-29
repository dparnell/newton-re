/*
	File:		qd/Curves.h

	Contains:	The Newton's curves: a quadratic from a first point through
				a control point to a last one, in 16.16 (NewtQD.h's curve).

	Reconstructed from the MP2x00 US ROM (0x002d1c94-0x002d2298); each
	function cites its origin.
*/

#ifndef __CURVES_H
#define __CURVES_H

#include "Ports.h"
#include "FixedGeometry.h"

void		SetCurve(curve* c, FPoint first, FPoint control, FPoint last);	// ROM 0x002d221c SetCurve__FP5curve6FPointN22
Boolean		EqualCurve(const curve* a, const curve* b);						// ROM 0x002d1d58 EqualCurve__FP5curveT1 - the same handle or the same six values

#endif	/* __CURVES_H */
