/*
	File:		toolbox/FixedGeometry.h

	Contains:	Points and rectangles in 16.16 fixed point.

				QuickDraw works in whole pixels (`qd/Rects.h`); the
				recognition system works in fractions of one, because a
				stroke is sampled in eighths of a pixel and the
				handwriting engine scales everything by the tablet's
				resolution.  These are the ten routines it measures with:
				the ROM keeps them together in one small block, and
				everything from the stroke lists up uses them.

				`FRect` and `FPoint` are the DDK's (NewtonTypes.h): four
				and two `Fixed` respectively, in the order left, top,
				right, bottom.

	Reconstructed from the MP2x00 US ROM (0x000bd934-0x000bdb28); each
	function cites its origin.
*/

#ifndef __FIXEDGEOMETRY_H
#define __FIXEDGEOMETRY_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

void	SetFixedPoint(FPoint* point, Fixed x, Fixed y);				// ROM 0x000bd934 SetFixedPoint
void	CopyFixedPoint(FPoint* dest, const FPoint* src);			// ROM 0x000bd93c CopyFixedPoint
void	SubtractFixedPoints(FPoint* out, const FPoint* a, const FPoint* b);	// ROM 0x000bda74 SubtractFixedPoints

// How wide and how tall, as a point.
void	FixedRectSize(FPoint* size, const FRect* rect);				// ROM 0x000bd950 FixedRectSize
void	SetFixedRect(FRect* rect, Fixed left, Fixed top, Fixed right, Fixed bottom);	// ROM 0x000bdaf8 SetFixedRect
void	CopyFixedRect(FRect* dest, const FRect* src);				// ROM 0x000bdb04 CopyFixedRect
// Whether its edges are the right way round, and whether it is nothing
// at all - which is not the same question: a rectangle of four noughts
// is valid *and* empty, and that is the one a bounding box starts as.
Boolean	ValidFixedRect(const FRect* rect);							// ROM 0x000bda98 ValidFixedRect
Boolean	EmptyFixedRect(const FRect* rect);							// ROM 0x000bdac8 EmptyFixedRect
// `dest` grown to take `src` in as well.
void	OrFixedRect(FRect* dest, const FRect* src);					// ROM 0x000bd974 OrFixedRect
void	XYFixedScaleFixedRect(FRect* rect, Fixed xScale, Fixed yScale);	// ROM 0x000bda18 XYFixedScaleFixedRect

#endif	/* __FIXEDGEOMETRY_H */
