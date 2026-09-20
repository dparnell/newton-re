/*
	File:		qd/Transform.cpp

	Contains:	TTransform and the scaler's transform stack (Transform.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Transform.h"
#include "Ports.h"
#include "FixedMath.h"

#include <string.h>


// ROM 0x001973e8 Setup__10TTransformFPC5TRectT1Uc
// The mapping from one rectangle onto another: the scale each way, and
// the two rectangles kept to map from and to.  With `square` the smaller
// of the two scales is used for both and the destination is cut back to
// what that leaves, so that nothing is stretched out of shape.
void
TTransform::Setup(const Rect* src, const Rect* dst, Boolean square)
{
	if (src == nil)
	{
		fFlags &= ~kTransformSetUp;
		return;
	}
	fSrc = *src;
	fDst = *dst;
	fScaleH = FixedDivide(ToFixed(fDst.right - fDst.left), ToFixed(fSrc.right - fSrc.left));
	fScaleV = FixedDivide(ToFixed(fDst.bottom - fDst.top), ToFixed(fSrc.bottom - fSrc.top));
	if (square)
	{
		if (fScaleH < fScaleV)
		{
			fScaleV = fScaleH;
			fDst.bottom = fDst.top + RoundFixed(ScaleFixed(fScaleH, fSrc.bottom - fSrc.top));
		}
		else
		{
			fScaleH = fScaleV;
			fDst.right = fDst.left + RoundFixed(ScaleFixed(fScaleV, fSrc.right - fSrc.left));
		}
	}
	fFlags |= kTransformSetUp;
	if (fScaleH == ToFixed(1) && fScaleV == ToFixed(1))
		fFlags |= kTransformNoScale;
	else
		fFlags &= ~kTransformNoScale;
}


// ROM 0x00197bcc Scale__6TPointFRC10TTransform
// A point moved from the source rectangle into the destination.
void
Scale(Point* pt, const TTransform& transform)
{
	if ((transform.fFlags & kTransformSetUp) == 0)
		return;
	pt->h = (short) (transform.fDst.left + RoundFixed(ScaleFixed(transform.fScaleH, pt->h - transform.fSrc.left)));
	pt->v = (short) (transform.fDst.top + RoundFixed(ScaleFixed(transform.fScaleV, pt->v - transform.fSrc.top)));
}


// ROM 0x001979bc Scale__5TRectFRC10TTransform
// A rectangle's two corners, each moved as a point is.
void
Scale(Rect* rect, const TTransform& transform)
{
	Scale((Point*) rect, transform);
	if ((transform.fFlags & kTransformSetUp) == 0)
		return;
	rect->right = (short) (transform.fDst.left + RoundFixed(ScaleFixed(transform.fScaleH, rect->right - transform.fSrc.left)));
	rect->bottom = (short) (transform.fDst.top + RoundFixed(ScaleFixed(transform.fScaleV, rect->bottom - transform.fSrc.top)));
}


/*------------------------------------------------------------------------------
	T h e   t r a n s f o r m s   i n   f o r c e

	NOT YET RECONSTRUCTED: TQDScaler itself, which is what the ROM puts
	the drawing through.  Here the stack is kept and its offset answered;
	`views/DrawShape.cpp` moves what it draws by that instead.
------------------------------------------------------------------------------*/

enum { kMaxTransforms = 8 };			// (the ROM's is a CDynamicArray, unbounded)

static TTransform	gTransforms[kMaxTransforms];
static long			gTransformCount = 0;


// ROM 0x00196540 StartScaling__9TQDScalerSFP10TTransformUcl
void
TQDScaler::StartScaling(const TTransform& transform)
{
	if (gTransformCount < kMaxTransforms)
		gTransforms[gTransformCount] = transform;
	gTransformCount++;
}


// ROM 0x00196de8 ReplaceScaling__9TQDScalerSFP10TTransform
// The innermost transform changed for another - a style frame set again
// at the same level.
void
TQDScaler::ReplaceScaling(const TTransform& transform)
{
	if (gTransformCount > 0 && gTransformCount <= kMaxTransforms)
		gTransforms[gTransformCount - 1] = transform;
}


// ROM 0x0019730c StopScaling__9TQDScalerSFv
void
TQDScaler::StopScaling(void)
{
	if (gTransformCount > 0)
		gTransformCount--;
}


// ROM 0x001973c8 GetTransformLevel__9TQDScalerSFv
long
TQDScaler::GetTransformLevel(void)
{
	return gTransformCount;
}


// What the transforms in force come to.  A transform that does not scale
// moves everything by the distance between its two rectangles' corners,
// and transforms one inside another add up; one that does scale is NOT
// YET, and is answered as no offset at all rather than the wrong one.
Point
TQDScaler::Offset(void)
{
	Point offset;
	offset.h = 0;
	offset.v = 0;
	long count = gTransformCount < kMaxTransforms ? gTransformCount : kMaxTransforms;
	for (long i = 0; i < count; i++)
	{
		const TTransform& t = gTransforms[i];
		if ((t.fFlags & (kTransformSetUp | kTransformNoScale)) != (kTransformSetUp | kTransformNoScale))
			continue;
		offset.h = (short) (offset.h + (t.fDst.left - t.fSrc.left));
		offset.v = (short) (offset.v + (t.fDst.top - t.fSrc.top));
	}
	return offset;
}
