/*
	File:		qd/Transform.h

	Contains:	TTransform, the mapping from one rectangle onto another that
				`view:DrawShape(shape, {transform: ...})` draws through, and
				the scaler that holds the ones in force.

				A transform is two rectangles and the scale between them.
				The style frame gives them either as `[srcRect, dstRect]` or
				as `[dx, dy]` - a pair of numbers, which stands for two
				ten-by-ten rectangles offset by them, so it scales by one
				and only moves.  That is how a list draws the hilite of any
				of its rows out of one shape: the shape is the first row's,
				and the transform moves it down by the row's height
				(`protoOverview`'s `hiliter`).

				NOT YET RECONSTRUCTED: `TQDScaler` (ROM 0x00196018 -
				0x001973c8), which is what the ROM maps the drawing itself
				through - it replaces the port's regions, scales the pen and
				puts every coordinate QuickDraw is given through the stack of
				transforms.  What is here in its place keeps that stack and
				answers the offset it comes to, which is right for the
				transforms that do not scale and is what the drawing in
				`views/DrawShape.cpp` uses; a transform that does scale is
				drawn unscaled.

	Reconstructed from the MP2x00 US ROM (0x001973e8-0x00197c8c); each
	function cites its origin.
*/

#ifndef __TRANSFORM_H
#define __TRANSFORM_H

#include "Rects.h"

// the transform flags
enum
{
	kTransformSetUp		= 0x80000000,	// the two rectangles and the scale are there
	kTransformNoScale	= 0x40000000	// both scales are one: it only moves things
};

struct TTransform
{
	Fixed	fScaleH;		// +0x00  the destination's width over the source's
	Fixed	fScaleV;		// +0x04  and its height over the source's
	Rect	fSrc;			// +0x08
	Rect	fDst;			// +0x10
	ULong	fFlags;			// +0x18

	void	Setup(const Rect* src, const Rect* dst, Boolean square);	// ROM 0x001973e8 Setup__10TTransformFPC5TRectT1Uc - square: the smaller scale used for both and the destination cut back to it
};

void	Scale(Rect* rect, const TTransform& transform);		// ROM 0x001979bc Scale__5TRectFRC10TTransform
void	Scale(Point* pt, const TTransform& transform);		// ROM 0x00197bcc Scale__6TPointFRC10TTransform


// The transforms in force, innermost last.  The ROM's is `gScale`, a
// TQDScaler over a CDynamicArray of them.
class TQDScaler
{
public:
	static void		StartScaling(const TTransform& transform);	// ROM 0x00196540 StartScaling__9TQDScalerSFP10TTransformUcl (NOT YET: the scaling itself)
	static void		ReplaceScaling(const TTransform& transform);	// ROM 0x00196de8 ReplaceScaling__9TQDScalerSFP10TTransform
	static void		StopScaling(void);							// ROM 0x0019730c StopScaling__9TQDScalerSFv
	static long		GetTransformLevel(void);					// ROM 0x001973c8 GetTransformLevel__9TQDScalerSFv

	// what the transforms in force come to when none of them scales: the
	// offset the drawing is moved by (0,0 when there is none, and when one
	// of them scales - which is NOT YET)
	static Point	Offset(void);
};

#endif	/* __TRANSFORM_H */
