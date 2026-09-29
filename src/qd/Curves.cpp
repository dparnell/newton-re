/*
	File:		qd/Curves.cpp

	Contains:	The Newton's curves - see Curves.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Curves.h"


// ROM 0x002d221c SetCurve__FP5curve6FPointN22
void
SetCurve(curve* c, FPoint first, FPoint control, FPoint last)
{
	c->first = first;
	c->control = control;
	c->last = last;
}


// ROM 0x002d1d58 EqualCurve__FP5curveT1
Boolean
EqualCurve(const curve* a, const curve* b)
{
	if (a == b)
		return true;
	const Fixed* pa = &a->first.x;
	const Fixed* pb = &b->first.x;
	for (long i = 6; i > 0; i--)
		if (*pa++ != *pb++)
			return false;
	return true;
}
