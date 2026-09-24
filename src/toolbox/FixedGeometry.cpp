/*
	File:		toolbox/FixedGeometry.cpp

	Contains:	Points and rectangles in 16.16 fixed point - see
				FixedGeometry.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "FixedGeometry.h"
#include "FixedMath.h"


// ROM 0x000bd934 SetFixedPoint
void
SetFixedPoint(FPoint* point, Fixed x, Fixed y)
{
	point->x = x;
	point->y = y;
}


// ROM 0x000bd93c CopyFixedPoint
void
CopyFixedPoint(FPoint* dest, const FPoint* src)
{
	dest->x = src->x;
	dest->y = src->y;
}


// ROM 0x000bda74 SubtractFixedPoints
void
SubtractFixedPoints(FPoint* out, const FPoint* a, const FPoint* b)
{
	out->x = a->x - b->x;
	out->y = a->y - b->y;
}


// ROM 0x000bd950 FixedRectSize
void
FixedRectSize(FPoint* size, const FRect* rect)
{
	size->x = rect->right - rect->left;
	size->y = rect->bottom - rect->top;
}


// ROM 0x000bdaf8 SetFixedRect
void
SetFixedRect(FRect* rect, Fixed left, Fixed top, Fixed right, Fixed bottom)
{
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}


// ROM 0x000bdb04 CopyFixedRect
void
CopyFixedRect(FRect* dest, const FRect* src)
{
	dest->top = src->top;
	dest->left = src->left;
	dest->bottom = src->bottom;
	dest->right = src->right;
}


// ROM 0x000bda98 ValidFixedRect
// Its edges the right way round.  An empty one is valid, which is what
// lets a bounding box start at nothing.
Boolean
ValidFixedRect(const FRect* rect)
{
	if (rect->top > rect->bottom)
		return false;
	return rect->right >= rect->left;
}


// ROM 0x000bdac8 EmptyFixedRect
// Nothing at all - all four noughts, not merely zero area.
Boolean
EmptyFixedRect(const FRect* rect)
{
	return rect->top == 0 && rect->bottom == 0
		&& rect->left == 0 && rect->right == 0;
}


// ROM 0x000bd974 OrFixedRect
// `dest` grown to take `src` in as well.  A destination that is not a
// rectangle yet - upside down, or nothing at all - simply becomes the
// source, which is how a bounding box is started from its first stroke
// without anyone having to say so; and a source that is nothing is
// passed over, so an empty stroke does not drag a box to the origin.
void
OrFixedRect(FRect* dest, const FRect* src)
{
	if (!ValidFixedRect(dest) || EmptyFixedRect(dest))
	{
		CopyFixedRect(dest, src);
		return;
	}
	if (!ValidFixedRect(src) || EmptyFixedRect(src))
		return;

	if (dest->top > src->top)
		dest->top = src->top;
	if (dest->left > src->left)
		dest->left = src->left;
	if (dest->bottom < src->bottom)
		dest->bottom = src->bottom;
	if (dest->right < src->right)
		dest->right = src->right;
}


// ROM 0x000bda18 XYFixedScaleFixedRect
// Each edge multiplied by the scale of its own axis.
void
XYFixedScaleFixedRect(FRect* rect, Fixed xScale, Fixed yScale)
{
	rect->top = FixedMultiply(rect->top, yScale);
	rect->bottom = FixedMultiply(rect->bottom, yScale);
	rect->left = FixedMultiply(rect->left, xScale);
	rect->right = FixedMultiply(rect->right, xScale);
}
