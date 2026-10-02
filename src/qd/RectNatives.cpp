/*
	File:		qd/RectNatives.cpp

	Contains:	The NewtonScript functions over rectangles (RectNatives.h):
				a script's rectangle is a bounds frame {left, top, right,
				bottom} (frames/Frames.h's ToObject and FromObject), and
				these are the operations of qd/Rects.h over them.  A
				rectangle that is not four integers answers nil rather than
				throwing.

	Reconstructed from the MP2x00 US ROM (0x000e2aa4-0x000e3020); each
	function cites its origin.
*/

#include "RectNatives.h"

#include "Rects.h"
#include "Frames.h"
#include "NativeFunctions.h"


// ROM 0x000e2d40 FOffsetRect
// OffsetRect(r, dx, dy): the rectangle moved.  (The ROM leaves it alone
// when both deltas are 0, which makes no difference to the answer.)
static Ref
FOffsetRect(RefArg /*rcvr*/, RefArg bounds, RefArg dx, RefArg dy)
{
	Rect r;
	if (!FromObject(bounds, r))
		return NILREF;
	Long h = RINT(dx);
	Long v = RINT(dy);
	if (h != 0 || v != 0)
		OffsetRect(&r, h, v);
	return ToObject(r);
}


// ROM 0x000e2cc0 FInsetRect
// InsetRect(r, dx, dy): the rectangle shrunk by the deltas (grown by a
// negative one).
static Ref
FInsetRect(RefArg /*rcvr*/, RefArg bounds, RefArg dx, RefArg dy)
{
	Rect r;
	if (!FromObject(bounds, r))
		return NILREF;
	InsetRect(&r, RINT(dx), RINT(dy));
	return ToObject(r);
}


// ROM 0x000e2aa4 FIsPtInRect
// IsPtInRect(x, y, r): whether the point is inside.
static Ref
FIsPtInRect(RefArg /*rcvr*/, RefArg x, RefArg y, RefArg bounds)
{
	Rect r;
	if (!FromObject(bounds, r))
		return NILREF;
	Point pt;
	pt.h = (short) RINT(x);
	pt.v = (short) RINT(y);
	return PtInRect(pt, &r) ? TRUEREF : NILREF;
}


// ROM 0x000e2dc4 FRectsOverlap
// RectsOverlap(a, b): whether they have any pixel in common.
static Ref
FRectsOverlap(RefArg /*rcvr*/, RefArg first, RefArg second)
{
	Rect a, b, sect;
	if (!FromObject(first, a) || !FromObject(second, b))
		return NILREF;
	return SectRect(&a, &b, &sect) ? TRUEREF : NILREF;
}


// ROM 0x000e2fbc FSectRect
// SectRect(a, b): the rectangle they have in common (empty when none).
static Ref
FSectRect(RefArg /*rcvr*/, RefArg first, RefArg second)
{
	Rect a, b;
	if (!FromObject(first, a) || !FromObject(second, b))
		return NILREF;
	SectRect(&b, &a, &a);
	return ToObject(a);
}


// ROM 0x000e3020 FUnionRect
// UnionRect(a, b): the smallest rectangle holding both; a copy of the
// second when the first is nil, so that it can start an accumulation.
static Ref
FUnionRect(RefArg /*rcvr*/, RefArg first, RefArg second)
{
	if (ISNIL(first))
		return Clone(second);
	Rect a, b;
	if (!FromObject(first, a) || !FromObject(second, b))
		return NILREF;
	UnionRect(&a, &b, &a);
	return ToObject(a);
}


// ROM 0x000e2f40 FMapRect
// MapRect(r, src, dst): the rectangle mapped from one frame of reference
// to another, as QuickDraw's MapRect does (scaled by the two rectangles'
// sizes and moved by their origins).
static Ref
FMapRect(RefArg /*rcvr*/, RefArg bounds, RefArg source, RefArg destination)
{
	Rect r, src, dst;
	if (!FromObject(bounds, r) || !FromObject(source, src) || !FromObject(destination, dst))
		return NILREF;
	MapRect(&r, &src, &dst);
	return ToObject(r);
}


// ROM 0x000e2e24 FMapPtX
// MapPtX(x, src, dst): the x mapped between the two rectangles.
//
// The ROM makes a whole point of it and leaves the vertical half
// uninitialised - whatever was in r4 - before mapping; it does not show,
// because MapPt maps the two halves independently and only the horizontal
// one is answered.  The point is made here with a nought in it instead:
// reading an uninitialised register is not something a host can do.
static Ref
FMapPtX(RefArg /*rcvr*/, RefArg x, RefArg source, RefArg destination)
{
	Rect src, dst;
	Point pt;
	pt.h = (short) RINT(x);
	pt.v = 0;
	if (!FromObject(source, src) || !FromObject(destination, dst))
		return NILREF;
	MapPt(&pt, &src, &dst);
	return MAKEINT(pt.h);
}


// ROM 0x000e2eb4 FMapPtY
// MapPtY(y, src, dst): the y mapped between the two rectangles.
static Ref
FMapPtY(RefArg /*rcvr*/, RefArg y, RefArg source, RefArg destination)
{
	Rect src, dst;
	Point pt;
	pt.h = 0;
	pt.v = (short) RINT(y);
	if (!FromObject(source, src) || !FromObject(destination, dst))
		return NILREF;
	MapPt(&pt, &src, &dst);
	return MAKEINT(pt.v);
}


void
RegisterRectNatives(void)
{
	RegisterNativeFunction("FOffsetRect", (void*) FOffsetRect, 3);
	RegisterNativeFunction("FInsetRect", (void*) FInsetRect, 3);
	RegisterNativeFunction("FIsPtInRect", (void*) FIsPtInRect, 3);
	RegisterNativeFunction("FRectsOverlap", (void*) FRectsOverlap, 2);
	RegisterNativeFunction("FSectRect", (void*) FSectRect, 2);
	RegisterNativeFunction("FUnionRect", (void*) FUnionRect, 2);
	RegisterNativeFunction("FMapRect", (void*) FMapRect, 3);
	RegisterNativeFunction("FMapPtX", (void*) FMapPtX, 3);
	RegisterNativeFunction("FMapPtY", (void*) FMapPtY, 3);
}
