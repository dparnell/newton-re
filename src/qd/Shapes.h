/*
	File:		qd/Shapes.h

	Contains:	Lines, ovals, round rectangles and arcs.  A line is drawn
				from the pen's location with the pen's size, mode and
				pattern (LineTo, Line; StdLine records into an open polygon
				or region - Polygons.h); ovals and round rectangles are one
				shape - an oval of the corners' width and height inside the
				rectangle (OvalRec: the ROM's incremental rasteriser in 16.16
				and 64-bit arithmetic, InitOval/BumpOval, PutOval writing
				the rows' end points), drawn by DrawArc as a region: the
				whole shape, or, framed, the shape less the same shape inset
				by the pen.  Arcs of less than a full turn are NOT YET
				RECONSTRUCTED (the ROM's DrawArc clips the oval's rows by the
				angles' slopes; the host draws nothing for them).

	Reconstructed from the MP2x00 US ROM (0x002aa908-0x002aaaf8,
	0x002f7664-0x002f77dc, 0x002f7fc0, 0x00320550-0x00320bd0,
	0x00344d5c-0x00344f78); each function cites its origin.
*/

#ifndef __SHAPES_H
#define __SHAPES_H

#ifndef __DRAW_H
#include "Draw.h"
#endif
#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// ROM: the oval rasteriser's state (0x34 bytes)
struct OvalRec
{
	long		fTop;			// +0x00  the rectangle's top and bottom
	long		fBottom;		// +0x04
	long		fRow;			// +0x08  the row in the curve: 1 - height, then by two
	long		fD;				// +0x0c  the decision term the accumulator is stepped to
	Int64		fA;				// +0x10  the accumulator and its first and second differences
	Int64		fB;				// +0x18
	Int64		fC;				// +0x20
	Fixed		fLeft;			// +0x28  the row's ends, 16.16
	Fixed		fRight;			// +0x2c
	Fixed		fStep;			// +0x30  half a pixel
};

void	InitOval(const Rect* r, OvalRec* oval, long ovalWidth, long ovalHeight);
void	BumpOval(OvalRec* oval, long y);					// the ends for row y (within the curved rows)
void	PutOval(const Rect* r, long ovalWidth, long ovalHeight, Handle points, long* offset, long* limit);	// the shape's change points appended
RgnHandle	OvalRgn(const Rect* r, long ovalWidth, long ovalHeight);	// host: the shape as a new region (nil for no memory)

// lines
void	LineTo(long h, long v);
void	Line(long dh, long dv);
void	StdLine(Point to);
void	DrawLine(Point from, Point to);

// ovals
void	FrameOval(const Rect* r);
void	PaintOval(const Rect* r);
void	EraseOval(const Rect* r);
void	InvertOval(const Rect* r);
void	FillOval(const Rect* r, PatternHandle pattern);
void	CallOval(GrafVerb verb, const Rect* r);
void	StdOval(GrafVerb verb, Rect* r);

// round rectangles: the corners are quarters of an oval ovalWidth by ovalHeight
void	FrameRoundRect(const Rect* r, long ovalWidth, long ovalHeight);
void	PaintRoundRect(const Rect* r, long ovalWidth, long ovalHeight);
void	EraseRoundRect(const Rect* r, long ovalWidth, long ovalHeight);
void	InvertRoundRect(const Rect* r, long ovalWidth, long ovalHeight);
void	FillRoundRect(const Rect* r, long ovalWidth, long ovalHeight, PatternHandle pattern);
void	CallRRect(GrafVerb verb, const Rect* r, long ovalWidth, long ovalHeight);
void	StdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight);

// arcs: from startAngle (degrees clockwise from twelve o'clock) through arcAngle
void	FrameArc(const Rect* r, long startAngle, long arcAngle);
void	PaintArc(const Rect* r, long startAngle, long arcAngle);
void	EraseArc(const Rect* r, long startAngle, long arcAngle);
void	InvertArc(const Rect* r, long startAngle, long arcAngle);
void	FillArc(const Rect* r, long startAngle, long arcAngle, PatternHandle pattern);
void	CallArc(GrafVerb verb, const Rect* r, long startAngle, long arcAngle);
void	StdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle);
void	DrawArc(const Rect* r, Boolean framed, long ovalWidth, long ovalHeight, long mode, PatternHandle pattern, long startAngle, long arcAngle);

#endif	/* __SHAPES_H */
