/*
	File:		qd/LocalToGlobal.cpp

	Contains:	LocalToGlobal (LocalToGlobal.h).

	Reconstructed from the MP2x00 US ROM (0x003352b4); each function cites
	its origin.
*/

#include "LocalToGlobal.h"


// ROM 0x003352b4 LocalToGlobal__FP5Point
void
LocalToGlobal(Point* pt)
{
	short v = pt->v - GetCurrentPort()->portBits.bounds.top;
	short h = pt->h - GetCurrentPort()->portBits.bounds.left;
	pt->v = v;
	pt->h = h;
}
