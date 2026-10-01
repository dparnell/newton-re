/*
	File:		qd/Polygons.h

	Contains:	Recording shapes into regions and polygons.  OpenRgn opens a
				point buffer in the QuickDraw globals (and the port's
				rgnSave); the lines drawn until CloseRgn (StdLine, DoLine,
				the ovals through PutOval) write their inversion points
				into it - PutLine: a pair per row where the edge steps -
				and CloseRgn sorts, culls and packs them into the region.
				OpenPoly likewise collects the lines' end points into a
				Polygon (polySave), which ClosePoly sizes and bounds; a
				polygon is drawn by the verbs (FramePoly, PaintPoly, ...)
				through StdPoly: framed as its lines (FrPoly), else as the
				region of its outline (DrawPoly).

	Reconstructed from the MP2x00 US ROM (0x002f77dc-0x002f78dc,
	0x002f88e4-0x002f8b0c, 0x003354d0-0x003359b8, 0x0034103c,
	0x0034142c); each function cites its origin.
*/

#ifndef __POLYGONS_H
#define __POLYGONS_H

#ifndef __SHAPES_H
#include "Shapes.h"
#endif

// regions
void		OpenRgn(void);
void		CloseRgn(RgnHandle rgn);
void		PutLine(Point from, Point to, Handle points, long* offset, long* limit);	// the line's inversion points appended
void		DoLine(Point to);							// a line from the pen (drawn, or recorded into the open region/polygon)

// polygons
PolyHandle	OpenPoly(void);
void		ClosePoly(void);
void		KillPoly(PolyHandle poly);
void		OffsetPoly(PolyHandle poly, long dh, long dv);
void		MapPoly(PolyHandle poly, const Rect* src, const Rect* dst);
void		FramePoly(PolyHandle poly);
void		PaintPoly(PolyHandle poly);
void		ErasePoly(PolyHandle poly);
void		InvertPoly(PolyHandle poly);
void		FillPoly(PolyHandle poly, PatternHandle pattern);
void		CallPoly(GrafVerb verb, PolyHandle poly);
void		StdPoly(GrafVerb verb, PolyHandle poly);
void		FrPoly(PolyHandle poly, long mode);			// the outline as lines
void		DrawPoly(PolyHandle poly, long mode, PatternHandle pattern);	// the inside as a region

// the ROM's Count: how many points (the size less the 12 bytes in front)
inline long	PolyPointCount(const Polygon* poly)		{ return (long) ((ULong32) ((long) poly->polySize - 12) >> 2); }	// ROM 0x00197d18 Count__FP7Polygon

#endif	/* __POLYGONS_H */
