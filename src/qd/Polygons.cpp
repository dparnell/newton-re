/*
	File:		qd/Polygons.cpp

	Contains:	Recording shapes into regions and polygons; the polygon verbs.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Polygons.h"
#include "Rects.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

const long kRecordingGrowth = 0x100;		// the buffers start and grow by this
const long kPolyHeaderSize = 12;			// polySize, filler, polyBBox


/*------------------------------------------------------------------------------
	R e g i o n s
------------------------------------------------------------------------------*/

// ROM 0x0034103c OpenRgn__Fv
// A point buffer opened for the region: the globals' handle, offset and
// size, the port's rgnSave, and the pen hidden - the lines are recorded,
// not drawn.
void
OpenRgn(void)
{
	qdGlobals.fRgnOffset = 0;
	qdGlobals.fRgnSize = kRecordingGrowth;
	Handle points = NewHandle(kRecordingGrowth);
	qdGlobals.fRgnHandle = points;
	if (points == nil)
		return;
	GetCurrentPort()->rgnSave = points;
	GetCurrentPort()->pnVis--;
}


// ROM 0x0034142c CloseRgn__FPP6Region
// The recorded points sorted, culled (pairs at one place cancel) and
// packed into the region; the buffer given back, the pen shown.
void
CloseRgn(RgnHandle rgn)
{
	GrafPort* port = GetCurrentPort();
	if (port->rgnSave == nil)
		return;
	long count = qdGlobals.fRgnOffset / (long) sizeof(Point);
	port->rgnSave = nil;
	ShowPen();
	SortPoints((Point*) *qdGlobals.fRgnHandle, count);
	CullPoints((Point*) *qdGlobals.fRgnHandle, &count);
	PackRgn(qdGlobals.fRgnHandle, count, rgn);
	DisposHandle(qdGlobals.fRgnHandle);
	qdGlobals.fRgnHandle = nil;
}


// a point (v, h) appended to the buffer at the offset
static inline void
PutPoint(char* out, long v, long h)
{
	((short*) out)[0] = (short) v;
	((short*) out)[1] = (short) h;
}


// ROM 0x002f88e4 PutLine__F5PointT1PPcPlT4
// The line's inversion points appended to the buffer (grown when they
// would not fit: the smaller of the line's extents, plus one, pairs).
// A horizontal line: its two ends on its row.  Any other: from the upper
// end down, the edge's x for each row (the line's slope in 16.16 from
// half a pixel in - a shallow slope added once more, a steep negative
// one a pixel over); a pair (the row, the previous x) (the row, the new
// x) wherever the x changes, and one for the lower end when the last x
// is not its own.
void
PutLine(Point from, Point to, Handle points, long* offset, long* limit)
{
	long dh = from.h - to.h;
	if (dh < 0)
		dh = -dh;
	long dv = from.v - to.v;
	if (dv < 0)
		dv = -dv;
	long extent = dv <= dh ? dv : dh;
	long needed = *offset + (extent + 1) * 8;
	if (*limit < needed)
	{
		needed += 0x200;
		if (SetHandleSize(points, needed) != noErr)
			return;
		*limit = needed;
	}
	char* out = *points + *offset;
	if (from.v == to.v)
	{
		PutPoint(out, to.v, from.h);
		PutPoint(out + 4, to.v, to.h);
		out += 8;
	}
	else
	{
		Point upper = from;
		Point lower = to;
		if (to.v < from.v)
		{
			upper = to;
			lower = from;
		}
		Fixed slope = FixedDivide((Fixed) ((lower.h - upper.h) * 0x10000), (Fixed) ((lower.v - upper.v) * 0x10000));
		Fixed x = (Fixed) (upper.h * 0x10000) + 0x8000 + slope / 2;
		if (slope < 0)
		{
			if (slope < -0x10000)
				x += 0x10000;
		}
		else if (slope < 0x10000)
			x += slope;
		long lastX = upper.h;
		long v = upper.v;
		for ( ; v < lower.v; v++)
		{
			if (lastX != x >> 16)
			{
				PutPoint(out, v, lastX);
				PutPoint(out + 4, v, x >> 16);
				out += 8;
				lastX = x >> 16;
			}
			x += slope;
		}
		if (lastX != lower.h)
		{
			PutPoint(out, v, lastX);
			PutPoint(out + 4, v, lower.h);
			out += 8;
		}
	}
	*offset = (long) (out - *points);
}


// the line from the pen recorded into the open region or polygon: a
// polygon's buffer grows by 0x100 when its next point would not fit,
// the pen's location goes first into an empty one
static void
RecordLine(GrafPort* port, Point from, Point to)
{
	if (port->polySave == nil)
	{
		if (port->rgnSave != nil)
			PutLine(from, to, qdGlobals.fRgnHandle, &qdGlobals.fRgnOffset, &qdGlobals.fRgnSize);
		return;
	}
	Handle h = qdGlobals.fPolyHandle;
	long count = PolyPointCount((Polygon*) *h);
	if (qdGlobals.fPolySize <= ((Polygon*) *h)->polySize + 8)
	{
		if (SetHandleSize(h, qdGlobals.fPolySize + kRecordingGrowth) != noErr)
			return;
		qdGlobals.fPolySize += kRecordingGrowth;
	}
	Polygon* poly = (Polygon*) *h;
	if (count == 0)
	{
		poly->polyPoints[0] = from;
		count = 1;
	}
	poly->polyPoints[count] = to;
	poly->polySize = (short) ((count + 1) * 4 + kPolyHeaderSize);
}


// ROM 0x002f77dc DoLine__F5Point
// A line from the pen to the point: recorded into the open polygon or
// region, drawn (when the pen shows), the pen moved to the point.
void
DoLine(Point to)
{
	GrafPort* port = GetCurrentPort();
	Point from = port->pnLoc;
	RecordLine(port, from, to);
	DrawLine(from, to);
	port->pnLoc = to;
}


/*------------------------------------------------------------------------------
	P o l y g o n s
------------------------------------------------------------------------------*/

// ROM 0x003354d0 OpenPoly__Fv
// A polygon opened: its handle in the globals and the port's polySave,
// empty (the header alone, no bounds), the pen hidden.
PolyHandle
OpenPoly(void)
{
	GrafPort* port = GetCurrentPort();
	qdGlobals.fPolySize = kRecordingGrowth;
	Handle h = NewHandle(kRecordingGrowth);
	if (h == nil)
		return nil;
	qdGlobals.fPolyHandle = h;
	port->polySave = h;
	Polygon* poly = (Polygon*) *h;
	poly->polySize = kPolyHeaderSize;
	SetEmptyRect(&poly->polyBBox);
	HidePen();
	return (PolyHandle) h;
}


// ROM 0x0033553c ClosePoly__Fv
// The open polygon finished: its bounds the points' extent, its handle
// cut to its size, the pen shown.
void
ClosePoly(void)
{
	GrafPort* port = GetCurrentPort();
	Handle h = qdGlobals.fPolyHandle;
	Polygon* poly = (Polygon*) *h;
	long size = poly->polySize;
	long count = PolyPointCount(poly);
	port->polySave = nil;
	SetEmptyRect(&poly->polyBBox);
	if (count != 0)
	{
		long top = poly->polyPoints[0].v;
		long bottom = top;
		long left = poly->polyPoints[0].h;
		long right = left;
		for (long i = count - 1; i > 0; i--)
		{
			long v = poly->polyPoints[i].v;
			long h = poly->polyPoints[i].h;
			if (v < top) top = v;
			if (v > bottom) bottom = v;
			if (h < left) left = h;
			if (h > right) right = h;
		}
		SetRect(&poly->polyBBox, left, top, right, bottom);
	}
	SetHandleSize(h, size);
	ShowPen();
}


// ROM 0x00335810 KillPoly__FPP7Polygon
void
KillPoly(PolyHandle poly)
{
	DisposHandle((Handle) poly);
}


// ROM 0x00335814 OffsetPoly__FPP7PolygonlT2
// The points and the bounds moved.
void
OffsetPoly(PolyHandle poly, long dh, long dv)
{
	Polygon* p = *poly;
	long count = PolyPointCount(p);
	for (long i = 0; i < count; i++)
	{
		p->polyPoints[i].v = (short) (p->polyPoints[i].v + dv);
		p->polyPoints[i].h = (short) (p->polyPoints[i].h + dh);
	}
	OffsetRect(&p->polyBBox, dh, dv);
}


// ROM 0x0033561c MapPoly__FPP7PolygonP4RectT2
// The bounds and points mapped from one rectangle to another (nothing
// for the same rectangle).
void
MapPoly(PolyHandle poly, const Rect* src, const Rect* dst)
{
	if (EqualRect(src, dst))
		return;
	MapRect(&(*poly)->polyBBox, src, dst);
	long count = PolyPointCount(*poly);
	for (long i = 0; i < count; i++)
		MapPt(&(*poly)->polyPoints[i], src, dst);
}


// ROM 0x003356e4 FrPoly__FPP7Polygonl
// The polygon's outline as lines from its first point (nothing for no
// points); an xor mode (the mode's low bits 2) has every point's line
// drawn twice, from the first - as the ROM has it.
void
FrPoly(PolyHandle poly, long mode)
{
	long count = PolyPointCount(*poly);
	if (count == 0)
		return;
	MoveTo((*poly)->polyPoints[0].h, (*poly)->polyPoints[0].v);
	if ((mode & 3) == 2)
	{
		for (long i = 0; i < count; i++)
		{
			DoLine((*poly)->polyPoints[i]);
			DoLine((*poly)->polyPoints[i]);
		}
		return;
	}
	for (long i = 1; i < count; i++)
		DoLine((*poly)->polyPoints[i]);
}


// ROM 0x0033579c DrawPoly__FPP7PolygonlPP8PixelMap
// The polygon's inside: its outline, closed back to the first point,
// recorded into a region (nothing while the pen is hidden), drawn in the
// mode and pattern.
void
DrawPoly(PolyHandle poly, long mode, PatternHandle pattern)
{
	if (GetCurrentPort()->pnVis < 0)
		return;
	RgnHandle rgn = NewRgn();
	if (rgn == nil)
		return;
	OpenRgn();
	FrPoly(poly, mode);
	DoLine((*poly)->polyPoints[0]);
	CloseRgn(rgn);
	DrawRgn(rgn, mode, pattern);
	DisposeRgn(rgn);
}


// ROM 0x003358fc StdPoly
// The standard polygon proc: framed as its lines in the pen's mode; the
// other verbs draw the inside (DrawPoly) when its bounds meet the port's
// clip and visible regions.  NOT YET RECONSTRUCTED: recording into an
// open picture.
void
StdPoly(GrafVerb verb, PolyHandle poly)
{
	GrafPort* port = GetCurrentPort();
	if (verb == frame)
	{
		FrPoly(poly, port->pnMode);
		return;
	}
	Rect r;
	if (!RSect(&r, 3, &(*poly)->polyBBox, &(*port->clipRgn)->rgnBBox, &(*port->visRgn)->rgnBBox))
		return;
	long mode;
	PatternHandle pattern;
	PushVerb(verb, &mode, &pattern);
	DrawPoly(poly, mode, pattern);
}


// ROM 0x003356a4 CallPoly__FUcPP7Polygon
void
CallPoly(GrafVerb verb, PolyHandle poly)
{
	GrafPort* port = GetCurrentPort();
	PolyProcPtr proc = (port->grafProcs != nil && port->grafProcs->polyProc != nil) ? port->grafProcs->polyProc : StdPoly;
	proc(verb, poly);
}


// ROM 0x00335894 FramePoly__FPP7Polygon
void	FramePoly(PolyHandle poly)		{ CallPoly(frame, poly); }
// ROM 0x003358a0 PaintPoly__FPP7Polygon
void	PaintPoly(PolyHandle poly)		{ CallPoly(paint, poly); }
// ROM 0x003358ac ErasePoly__FPP7Polygon
void	ErasePoly(PolyHandle poly)		{ CallPoly(erase, poly); }
// ROM 0x003358b8 InvertPoly__FPP7Polygon
void	InvertPoly(PolyHandle poly)		{ CallPoly(invert, poly); }


// ROM 0x003358c4 FillPoly__FPP7PolygonPP8PixelMap
void
FillPoly(PolyHandle poly, PatternHandle pattern)
{
	GrafPort* port = GetCurrentPort();
	PatternHandle saved = port->fgPat;
	port->fgPat = pattern;
	CallPoly(fill, poly);
	port->fgPat = saved;
}
