/*
	File:		qd/Rects.h

	Contains:	QuickDraw's rectangles and points: the DDK's Rect (top,
				left, bottom, right shorts) and Point (v, h) made, moved,
				inset, intersected, united, tested and mapped between
				coordinate systems.  Newton's QuickDraw is Apple's,
				rewritten in C for the ARM (src/qd/README.md); the drawing
				calls (FrameRect, PaintRect, ...) go through the port's
				procs and are NOT YET RECONSTRUCTED.

	Reconstructed from the MP2x00 US ROM (0x00335308-0x003354d0,
	0x0033ffb0-0x00340f60); each function cites its origin.
*/

#ifndef __RECTS_H
#define __RECTS_H

#ifndef __NEWTQD_H
#include "NewtQD.h"
#endif

// left, top, right, bottom - the Macintosh order
void	SetRect(Rect* r, long left, long top, long right, long bottom);
void	SetEmptyRect(Rect* r);
void	OffsetRect(Rect* r, long dh, long dv);
void	InsetRect(Rect* r, long dh, long dv);
void	Pt2Rect(Point a, Point b, Rect* r);
Boolean	PtInRect(Point pt, const Rect* r);
Boolean	EmptyRect(const Rect* r);
Boolean	EqualRect(const Rect* a, const Rect* b);
Boolean	SectRect(const Rect* a, const Rect* b, Rect* result);		// ==> whether they intersect (result empty when not)
long	CoveredBy(const Rect* r, const Rect* other);		// ROM 0x001976d8 CoveredBy__5TRectCFRC5TRect - how much of r (per cent) their intersection covers
void	UnionRect(const Rect* a, const Rect* b, Rect* result);		// an empty one ignored
void	JoinRect(const Rect* a, const Rect* b, Rect* result);		// the bounds of both (an empty one ignored)
Boolean	RSect(Rect* result, long count, const Rect* first, ...);	// the intersection of count rects

// mapping between coordinate systems
long	MapCoord(long x, long srcStart, long srcSize, long dstStart, long dstSize);
void	MapPt(Point* pt, const Rect* src, const Rect* dst);
void	MapRect(Rect* r, const Rect* src, const Rect* dst);
void	ScalePt(Point* pt, const Rect* src, const Rect* dst);

long	CheapDistance(const Point& a, const Point& b);					// ROM 0x001991c4 CheapDistance__FRC6TPointT1 - the longer axis plus half the shorter

inline Point	MakePoint(long h, long v)		{ Point p; p.v = (short) v; p.h = (short) h; return p; }
inline Point	MidPoint(const Rect& r)			{ return MakePoint((r.left + r.right) / 2, (r.top + r.bottom) / 2); }		// ROM 0x00197884 MidPoint__5TRectCFv
inline Boolean	EqualPt(Point a, Point b)		{ return a.v == b.v && a.h == b.h; }

#endif	/* __RECTS_H */
