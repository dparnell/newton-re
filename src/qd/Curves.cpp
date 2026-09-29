/*
	File:		qd/Curves.cpp

	Contains:	The Newton's curves - see Curves.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Curves.h"
#include "PicRecord.h"
#include "Polygons.h"
#include "Draw.h"
#include "Rects.h"
#include "Regions.h"
#include "FixedMath.h"


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


// (host) The corner of a rectangle as the ROM takes it, the halfword put in
// the top of a word: (unsigned) << 16.
static inline Fixed
CornerFixed(short value)
{
	return (Fixed) ((ULong32) (unsigned short) value << 16);
}


// (host) The ARM's sum of two 16.16 values, halved as its signed division
// does (towards nought).
static inline Fixed
Midway(Fixed a, Fixed b)
{
	return (Long32) ((ULong32) a + (ULong32) b) / 2;
}


// (host) A 16.16 point rounded to a pixel, as the ROM rounds it.
static inline Point
RoundPoint(const FPoint& p)
{
	Point pt;
	pt.h = (short) RoundFixed(p.x);
	pt.v = (short) RoundFixed(p.y);
	return pt;
}


// ROM 0x002d1d98 CallCurve__FUcP5curve
// The port's curveProc, or StdCurve.  (Host: a nil proc is the standard one.)
void
CallCurve(GrafVerb verb, curve* c)
{
	GrafPort* port = GetCurrentPort();
	CurveProcPtr proc = (port->grafProcs != nil && port->grafProcs->curveProc != nil) ? port->grafProcs->curveProc : StdCurve;
	proc(verb, c);
}


// ROM 0x002d1c7c FrameCurve__FP5curve
void
FrameCurve(curve* c)
{
	CallCurve(frame, c);
}


// ROM 0x002d1c88 PaintCurve__FP5curve
void
PaintCurve(curve* c)
{
	CallCurve(paint, c);
}


// ROM 0x002d1fdc EraseCurve__FP5curve
void
EraseCurve(curve* c)
{
	CallCurve(erase, c);
}


// ROM 0x002d1fe8 InvertCurve__FP5curve
void
InvertCurve(curve* c)
{
	CallCurve(invert, c);
}


// ROM 0x002d1ff4 FillCurve__FP5curvePP8PixelMap
// Filled with a pattern, installed as the port's for the call.
void
FillCurve(curve* c, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallCurve(fill, c);
	port->fgPat = saved;
}


// ROM 0x002d21d0 OffsetCurve__FP5curvelT2
void
OffsetCurve(curve* c, Fixed dh, Fixed dv)
{
	c->first.x = AddFixed(c->first.x, dh);
	c->first.y = AddFixed(c->first.y, dv);
	c->control.x = AddFixed(c->control.x, dh);
	c->control.y = AddFixed(c->control.y, dv);
	c->last.x = AddFixed(c->last.x, dh);
	c->last.y = AddFixed(c->last.y, dv);
}


// ROM 0x002d2254 ScaleCurve__FP5curvelT2
void
ScaleCurve(curve* c, Fixed hScale, Fixed vScale)
{
	FPoint* p = &c->first;
	for (long i = 0; i < 3; i++, p++)
	{
		p->x = FixedMultiply(p->x, hScale);
		p->y = FixedMultiply(p->y, vScale);
	}
}


// ROM 0x002d1c94 MapCurve__FP5curveP4RectT2
// The curve mapped from one rectangle onto another: moved to the first's
// corner, scaled by the ratio of their sizes, moved to the second's.
void
MapCurve(curve* c, const Rect* src, const Rect* dst)
{
	Fixed hScale = FixedDivide(dst->right - dst->left, src->right - src->left);
	Fixed vScale = FixedDivide(dst->bottom - dst->top, src->bottom - src->top);
	OffsetCurve(c, -CornerFixed(src->left), -CornerFixed(src->top));
	ScaleCurve(c, hScale, vScale);
	OffsetCurve(c, CornerFixed(dst->left), CornerFixed(dst->top));
}


// ROM 0x002d211c GetCurveBounds__FP5curveP4Rect
// The box of the three points, rounded to pixels.
//
// ROM BUGS, kept: the maximum starts at -0x7fa6 rather than -0x8000, and a
// point that lowers the minimum is not looked at for the maximum - so the
// first point never counts towards the right and bottom edges.
void
GetCurveBounds(const curve* c, Rect* bounds)
{
	long left = 0x7fff, top = 0x7fff;
	long right = 0x5a - 0x8000, bottom = 0x5a - 0x8000;
	const FPoint* p = &c->first;
	for (long i = 3; i > 0; i--, p++)
	{
		long h = (short) RoundFixed(p->x);
		long v = (short) RoundFixed(p->y);
		if (h < left)
			left = h;
		else if (h > right)
			right = h;
		if (v < top)
			top = v;
		else if (v > bottom)
			bottom = v;
	}
	bounds->top = (short) top;
	bounds->left = (short) left;
	bounds->bottom = (short) bottom;
	bounds->right = (short) right;
}


// ROM 0x002d1dd8 FrCurve__FP5curvel
// The curve drawn from the pen as lines: split in two at its middle (de
// Casteljau) depth times - -1 meaning five, so thirty-two lines - each
// half drawn in turn, a curve at the bottom drawn as one line to its end.
void
FrCurve(const curve* c, long depth)
{
	if (depth == -1)
		depth = 5;
	else if (depth < 1)
	{
		DoLine(RoundPoint(c->last));
		return;
	}
	curve first, second;
	first.first = c->first;
	first.control.x = Midway(c->control.x, c->first.x);
	first.control.y = Midway(c->first.y, c->control.y);
	second.control.x = Midway(c->control.x, c->last.x);
	second.control.y = Midway(c->control.y, c->last.y);
	second.first.x = Midway(first.control.x, second.control.x);
	second.first.y = Midway(first.control.y, second.control.y);
	second.last = c->last;
	first.last = second.first;
	FrCurve(&first, depth - 1);
	FrCurve(&second, depth - 1);
}


// ROM 0x002d1f10 DrawCurve__FP5curvelPP8PixelMap
// The inside of the curve closed by a line back to its start, as a region
// drawn in the mode and pattern (nothing while the pen is hidden).
void
DrawCurve(const curve* c, long mode, PatternHandle pattern)
{
	if (GetCurrentPort()->pnVis < 0)
		return;
	RgnHandle rgn = NewRgn();
	OpenRgn();
	Point start = RoundPoint(c->first);
	MoveTo(start.h, start.v);
	FrCurve(c, -1);
	DoLine(RoundPoint(c->first));
	CloseRgn(rgn);
	DrawRgn(rgn, mode, pattern);
	DisposeRgn(rgn);
}


// ROM 0x002d202c StdCurve
// Recorded into an open picture; framed as lines from its first point with
// the pen, anything else as the region DrawCurve makes, if its bounds meet
// the clip and visible regions.
extern "C" void
StdCurve(GrafVerb verb, curve* c)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicCurve(verb + 0xc80, c);
	}
	if (verb == frame)
	{
		Point start = RoundPoint(c->first);
		MoveTo(start.h, start.v);
		FrCurve(c, -1);
		return;
	}
	Rect bounds, r;
	GetCurveBounds(c, &bounds);
	if (RSect(&r, 3, &(*port->clipRgn)->rgnBBox, &(*port->visRgn)->rgnBBox, &bounds))
	{
		long mode;
		PatternHandle pattern;
		PushVerb(verb, &mode, &pattern);
		DrawCurve(c, mode, pattern);
	}
}
