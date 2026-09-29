/*
	File:		qd/Paths.cpp

	Contains:	The Newton's paths - see Paths.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Paths.h"
#include "Curves.h"
#include "PicRecord.h"
#include "Polygons.h"
#include "Draw.h"
#include "Rects.h"
#include "Regions.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "OSErrors.h"


// (host) The halfword of a rectangle's corner in the top of a word, as the
// ROM takes it.
static inline Fixed
CornerFixed(short value)
{
	return (Fixed) ((ULong32) (unsigned short) value << 16);
}


// (host) The ARM's sum of two 16.16 values halved (towards nought).
static inline Fixed
Midway(Fixed a, Fixed b)
{
	return (Long32) ((ULong32) a + (ULong32) b) / 2;
}


// (host) A contour's points: past its count and its bit words.
static inline FPoint*
ContourPoints(path* contour)
{
	return (FPoint*) (contour->controlBits + ((contour->vectors + 0x1f) >> 5));
}


// ROM 0x00327950 CallPaths__FUcPP5paths
// The port's pathsProc, or StdPaths.  (Host: a nil proc is the standard one.)
void
CallPaths(GrafVerb verb, pathsHandle p)
{
	GrafPort* port = GetCurrentPort();
	PathsProcPtr proc = (port->grafProcs != nil && port->grafProcs->pathsProc != nil) ? port->grafProcs->pathsProc : StdPaths;
	proc(verb, p);
}


// ROM 0x00327800 FramePaths__FPP5paths
void
FramePaths(pathsHandle p)
{
	CallPaths(frame, p);
}


// ROM 0x0032780c PaintPaths__FPP5paths
void
PaintPaths(pathsHandle p)
{
	CallPaths(paint, p);
}


// ROM 0x00327c74 ErasePaths__FPP5paths
void
ErasePaths(pathsHandle p)
{
	CallPaths(erase, p);
}


// ROM 0x00327dc4 InvertPaths__FPP5paths
void
InvertPaths(pathsHandle p)
{
	CallPaths(invert, p);
}


// ROM 0x00327dd0 FillPaths__FPP5pathsPP8PixelMap
// Filled with a pattern, installed as the port's for the call.
void
FillPaths(pathsHandle p, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallPaths(fill, p);
	port->fgPat = saved;
}


// ROM 0x00327fbc OffsetPaths__FPP5pathslT2
void
OffsetPaths(pathsHandle p, Fixed dh, Fixed dv)
{
	path* contour = (*p)->contour;
	for (long n = (*p)->contours; n != 0; n--)
	{
		long count = contour->vectors;
		FPoint* pt = ContourPoints(contour);
		for (; count != 0; count--, pt++)
		{
			pt->x = AddFixed(pt->x, dh);
			pt->y = AddFixed(pt->y, dv);
		}
		contour = (path*) pt;
	}
}


// ROM 0x00328028 ScalePaths__FPP5pathslT2
void
ScalePaths(pathsHandle p, Fixed hScale, Fixed vScale)
{
	path* contour = (*p)->contour;
	for (long n = (*p)->contours; n != 0; n--)
	{
		long count = contour->vectors;
		FPoint* pt = ContourPoints(contour);
		for (; count != 0; count--, pt++)
		{
			pt->x = FixedMultiply(pt->x, hScale);
			pt->y = FixedMultiply(pt->y, vScale);
		}
		contour = (path*) pt;
	}
}


// ROM 0x00327818 MapPaths__FPP5pathsP4RectT2
// The paths mapped from one rectangle onto another, as MapCurve maps a
// curve (the last move made in line).
void
MapPaths(pathsHandle p, const Rect* src, const Rect* dst)
{
	Fixed hScale = FixedDivide(dst->right - dst->left, src->right - src->left);
	Fixed vScale = FixedDivide(dst->bottom - dst->top, src->bottom - src->top);
	OffsetPaths(p, -CornerFixed(src->left), -CornerFixed(src->top));
	ScalePaths(p, hScale, vScale);
	OffsetPaths(p, CornerFixed(dst->left), CornerFixed(dst->top));
}


// ROM 0x00327ed0 GetPathsBounds__FPP5pathsP4Rect
// The box of every point, rounded to pixels.  ROM QUIRK, kept: the maximum
// starts at -0x7fa6 (as GetCurveBounds' does).
void
GetPathsBounds(pathsHandle p, Rect* bounds)
{
	long left = 0x7fff, top = 0x7fff;
	long right = 0x5a - 0x8000, bottom = 0x5a - 0x8000;
	path* contour = (*p)->contour;
	for (long n = (*p)->contours; n != 0; n--)
	{
		long count = contour->vectors;
		FPoint* pt = ContourPoints(contour);
		for (long i = 0; i < count; i++, pt++)
		{
			long h = (short) RoundFixed(pt->x);
			long v = (short) RoundFixed(pt->y);
			if (h < left)
				left = h;
			if (h > right)
				right = h;
			if (v < top)
				top = v;
			if (v > bottom)
				bottom = v;
		}
		contour = (path*) pt;
	}
	bounds->top = (short) top;
	bounds->left = (short) left;
	bounds->bottom = (short) bottom;
	bounds->right = (short) right;
}


// ROM 0x003278dc SizeOfPaths__FPP5paths
long
SizeOfPaths(pathsHandle p)
{
	return GetHandleSize((Handle) p);
}


// ROM 0x003278e0 CopyPaths__FPP5pathsT1
// One handle's paths into another, resized to hold them.  ROM QUIRK, kept:
// when the resizing fails the destination is emptied (no contours) - unless
// the paths are four bytes or fewer, which are copied into it regardless.
void
CopyPaths(pathsHandle src, pathsHandle dst)
{
	if (src == dst)
		return;
	long size = SizeOfPaths(src);
	if (SizeOfPaths(dst) != size)
	{
		if (SetHandleSize((Handle) dst, size) != noErr && size > 4)
		{
			(*dst)->contours = 0;
			return;
		}
	}
	BlockMove(*src, *dst, size);
}


// ROM 0x00327ecc DisposePaths__FPP5paths
void
DisposePaths(pathsHandle p)
{
	DisposHandle((Handle) p);
}


/*------------------------------------------------------------------------------
	T h e   p a t h   w a l k e r
------------------------------------------------------------------------------*/

// ROM 0x00327990 NextPath__FP4path
path*
NextPath(path* contour)
{
	return (path*) (ContourPoints(contour) + contour->vectors);
}


// ROM 0x00327c50 OnCurve__FPll
// A point is on the curve when its bit is clear.
Boolean
OnCurve(const Long32* bits, long index)
{
	return (bits[index >> 5] & (0x80000000u >> (index & 0x1f))) == 0;
}


// ROM 0x003279ac InitPathWalker__FP10pathWalkerP4path
// The walk starts at the first point on the curve: the first point, or the
// last (the walk then starts a step before the first), or - when both are
// off it - the middle of the two, where the curve passes between them.
void
InitPathWalker(pathWalker* walker, path* contour)
{
	long last = contour->vectors - 1;
	walker->index = 0;
	walker->ep = last;
	walker->bits = contour->controlBits;
	walker->p = ContourPoints(contour);
	const FPoint* start;
	if (OnCurve(walker->bits, 0))
		start = walker->p;
	else
	{
		walker->index = -1;
		if (!OnCurve(walker->bits, last))
		{
			walker->c.last.x = Midway(walker->p[last].x, walker->p[0].x);
			walker->c.last.y = Midway(walker->p[last].y, walker->p[0].y);
			return;
		}
		start = walker->p + last;
	}
	walker->c.last = *start;
}


// ROM 0x00327a74 NextPathSegment__FP10pathWalker
// The segment from where the last one ended: to the next point if it is on
// the curve (a line), else a curve through it to the point after (wrapping
// to the first) or, when that is off the curve too, to the middle of the
// two.  ==> false when the walk has reached the end.
Boolean
NextPathSegment(pathWalker* walker)
{
	if (walker->index >= walker->ep)
		return false;
	walker->c.first = walker->c.last;
	long next = walker->index + 1;
	if (!OnCurve(walker->bits, next))
	{
		walker->isLine = 0;
		walker->c.control = walker->p[next];
		long after = walker->index + 2;
		if (after > walker->ep)
			after = 0;
		if (!OnCurve(walker->bits, after))
		{
			walker->c.last.x = Midway(walker->p[next].x, walker->p[after].x);
			walker->c.last.y = Midway(walker->p[next].y, walker->p[after].y);
			walker->index = walker->index + 1;
		}
		else
		{
			walker->c.last = walker->p[after];
			walker->index = walker->index + 2;
		}
		return true;
	}
	walker->c.last = walker->p[next];
	walker->isLine = 1;
	walker->index = walker->index + 1;
	return true;
}


// (host) One segment drawn from the pen: a line to its end, or the curve.
static void
DrawSegment(pathWalker* walker)
{
	if (walker->isLine == 0)
		FrCurve(&walker->c, -1);
	else
	{
		Point to;
		to.h = (short) RoundFixed(walker->c.last.x);
		to.v = (short) RoundFixed(walker->c.last.y);
		DoLine(to);
	}
}


// ROM 0x00327c80 FramePath__FP4path
// A contour drawn as lines, the pen moved to its start first.
//
// ROM QUIRK, kept: the walk ends at the last point without going back to
// the start, so a contour is closed only when its last point is its first
// or the walk started a step early (the first point off the curve).
path*
FramePath(path* contour)
{
	pathWalker walker;
	InitPathWalker(&walker, contour);
	if (NextPathSegment(&walker))
	{
		MoveTo((short) RoundFixed(walker.c.first.x), (short) RoundFixed(walker.c.first.y));
		DrawSegment(&walker);
	}
	while (NextPathSegment(&walker))
		DrawSegment(&walker);
	return NextPath(contour);
}


// ROM 0x00327bfc FrPaths__FPP5paths
// Every contour framed, the handle locked meanwhile.
void
FrPaths(pathsHandle p)
{
	long n = (*p)->contours;
	path* contour = (*p)->contour;
	HLock((Handle) p);
	for (; n != 0; n--)
		contour = FramePath(contour);
	HUnlock((Handle) p);
}


// ROM 0x00327b9c DrawPaths__FPP5pathslPP8PixelMap
// The outlines made into a region and drawn in the mode and pattern
// (nothing while the pen is hidden).
void
DrawPaths(pathsHandle p, long mode, PatternHandle pattern)
{
	if (GetCurrentPort()->pnVis < 0)
		return;
	RgnHandle rgn = NewRgn();
	OpenRgn();
	FrPaths(p);
	CloseRgn(rgn);
	DrawRgn(rgn, mode, pattern);
	DisposeRgn(rgn);
}


// ROM 0x00327e08 StdPaths
// Recorded into an open picture (0x8190 + the verb, then the handle);
// framed with the pen, anything else as the region of the outlines if
// their bounds meet the clip and visible regions.
extern "C" void
StdPaths(GrafVerb verb, pathsHandle p)
{
	GrafPort* port = GetCurrentPort();
	if (CheckPic())
	{
		PutPicVerb(verb);
		PutPicOpcode(0x8190 + verb);
		PutPicPaths(p);
	}
	if (verb != frame)
	{
		Rect bounds, r;
		GetPathsBounds(p, &bounds);
		if (RSect(&r, 3, &(*port->clipRgn)->rgnBBox, &(*port->visRgn)->rgnBBox, &bounds))
		{
			long mode;
			PatternHandle pattern;
			PushVerb(verb, &mode, &pattern);
			DrawPaths(p, mode, pattern);
		}
		return;
	}
	long n = (*p)->contours;
	path* contour = (*p)->contour;
	HLock((Handle) p);
	for (; n != 0; n--)
		contour = FramePath(contour);
	HUnlock((Handle) p);
}
