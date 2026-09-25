/*
	File:		recognition/ShapeEllipses.cpp

	Contains:	FindEllipses: whether a shape drawn as a curve, or closed on
				itself, is really a circle or an ellipse.

				A circle is tested for first (IsCircle): a box not much
				longer one way than the other, nearly all the points about
				as far from its middle as its mean radius, and the stroke
				going round the quadrants in order (TraceContour).  An
				ellipse is fitted next (IsEllipse): a general conic through
				the points by least squares - a 5x5 system, built by
				SetupEllipseSystem and solved by Decomp and Solve - turned
				into a centre, two radii and an angle (MakeEllipseTemplate),
				and more than half the points within a tolerance of it
				(PtsonEllipse).  Either way the fitted outline must not
				have a straight side in it, and is thrown away when it does
				not: a circle or an ellipse is drawn from its numbers.

	Reconstructed from the MP2x00 US ROM (0x00215904, 0x00220f98-0x00222130,
	and the linear algebra at 0x00125180-0x001255d0); each function cites
	its origin.
*/

#include "ShapeGeometry.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "FixedMath.h"
#include "FixedMathExtra.h"

#include <string.h>


/*------------------------------------------------------------------------------
	L i n e a r   a l g e b r a
------------------------------------------------------------------------------*/

// ROM 0x00125180 Decomp
// The n x n matrix `a` (rows `ndim` apart) decomposed into L and U in
// place, and its determinant left in `det`.  The pivot search the routine
// was written with has been compiled away - each row is swapped with
// itself, `pivots` records no exchange but the last - so a small pivot
// (under a ten-thousandth) simply leaves its column as it is.
void
Decomp(ULong n, long ndim, Fixed* a, long* pivots, Fixed* det)
{
	*det = 0x10000;
	pivots[n - 1] = 1;
	if (n == 1)
		return;
	ULong last = n - 1;
	for (ULong k = 0; k < last; k++)
	{
		pivots[k] = (long) k;
		long row = ndim * (long) k;
		Fixed pivot = a[row + k];
		*det = FixedMultiply(*det, pivot);
		if (FixedDivide(1, 10000) < ((pivot < 0) ? -pivot : pivot))
		{
			for (ULong j = k + 1; j < n; j++)
				a[row + j] = -FixedDivide(a[row + j], pivot);
			for (ULong i = k + 1; i < n; i++)
			{
				long rowI = ndim * (long) i;
				Fixed factor = a[rowI + k];
				if (FixedDivide(1, 10000) < ((factor < 0) ? -factor : factor))
					for (ULong j = k + 1; j < n; j++)
						a[rowI + j] = FixedMultiply(a[row + j], factor) + a[rowI + j];
			}
		}
	}
	*det = FixedMultiply(*det, a[ndim * (long) last + last]);
	*det = FixedMultiply(*det, (Fixed) ((ULong) pivots[last] << 16));
}


// ROM 0x0012542c Solve
// The system Decomp decomposed solved for `b` in place.  An `n` of 7, 8
// or 9 is a 5x5 system for an ellipse the caller has seen is wide, square
// or tall: the last unknown - the conic's constant, which must be
// negative for an ellipse - is forced to -24, -10 or -1 when it comes out
// positive.
void
Solve(ULong n, long ndim, Fixed* a, long* pivots, Fixed* b)
{
	long hint = 0;
	if (n >= 7)
	{
		hint = (long) n - 6;
		n = 5;
	}
	else if (n < 2)
	{
		*b = FixedDivide(*b, *a);
		return;
	}
	ULong last = n - 1;
	for (ULong k = 0; k < last; k++)
	{
		long m = pivots[k];
		Fixed t = b[m];
		b[m] = b[k];
		b[k] = t;
		for (ULong i = k + 1; i < n; i++)
			b[i] = FixedMultiply(a[ndim * (long) k + i], t) + b[i];
	}
	for (ULong step = 0; step < last; step++)
	{
		ULong k = last - step;
		Fixed x = FixedDivide(b[k], a[ndim * (long) k + k]);
		b[k] = x;
		if (n == 5 && step == 0 && x > 0)
		{
			if (hint == 1)
				b[k] = -0x180000;
			else if (hint == 2)
				b[k] = -0xa0000;
			else if (hint == 3)
				b[k] = -0x10000;
		}
		Fixed t = b[k];
		for (ULong i = 0; i < k; i++)
			b[i] = FixedMultiply(a[ndim * (long) k + i], -t) + b[i];
	}
	*b = FixedDivide(*b, *a);
}


/*------------------------------------------------------------------------------
	T h e   s t r o k e ' s   p o i n t s
------------------------------------------------------------------------------*/

// ROM 0x002220a4 GetPts__11TStrokeUnitFv
// The stroke's points as an array of FPoints (nil for no memory).
TArray*
TStrokeUnit::GetPts(void)
{
	ULong count = (ULong) fStroke->Count();
	TArray* pts = TArray::Make(sizeof(FPoint), count);
	if (pts != nil)
	{
		SamplePt* sample = fStroke->GetPoint(0);
		FPoint* pt = (FPoint*) pts->GetEntry(0);
		for (ULong i = 0; i < count; i++)
			::GetPoint(sample++, pt++);
	}
	return pts;
}


/*------------------------------------------------------------------------------
	C i r c l e s
------------------------------------------------------------------------------*/

// ROM 0x002211e4 ooops__Fv
// (a breakpoint for the debugger, called when a point is left of the
// centre; it does nothing)
static void
ooops(void)
{ }


// ROM 0x002211e8 GetQuadPoint__FP6FPointT1
// Which quarter about the centre a point is in, anticlockwise on the
// screen from the upper right: 0 upper right, 1 upper left, 2 lower left,
// 3 lower right.
static ULong
GetQuadPoint(FPoint* centre, FPoint* pt)
{
	if (centre->x <= pt->x)
		return (pt->y < centre->y) ? 0 : 3;
	ooops();
	return (pt->y < centre->y) ? 1 : 2;
}


// ROM 0x00221240 NextQuad__FUlT1Pl
// Whether moving from quarter `from` to quarter `to` goes on round the
// way the first move went (`way`: 0 not yet known, 1 anticlockwise, -1
// clockwise).
static Boolean
NextQuad(ULong from, ULong to, long* way)
{
	if (*way == 0)
	{
		if (from + 1 == to || (to == 3 && from == 0))
		{
			*way = 1;
			if (from + 1 == to)
				return true;
			return to == 0 && from == 3;
		}
		if (from - 1 != to && (from != 0 || to != 3))
			return false;
		*way = -1;
	}
	else if (*way != -1)
	{
		if (from + 1 == to)
			return true;
		return to == 0 && from == 3;
	}
	return from - 1 == to || (to == 3 && from == 0);
}


// ROM 0x00221118 TraceContour__FP6TArrayP6FPoint
// Whether the stroke goes round the centre one way, quarter after
// quarter (every other point looked at; a quarter it only touches for a
// point is not counted).
static Boolean
TraceContour(TArray* pts, FPoint* centre)
{
	ULong count = (ULong) pts->Count();
	ULong quarter = GetQuadPoint(centre, (FPoint*) pts->GetEntry(0));
	ULong run = 1;
	long way = 0;
	for (ULong i = 1; i < count; i += 2)
	{
		ULong next = GetQuadPoint(centre, (FPoint*) pts->GetEntry(i));
		if (next == quarter)
			run++;
		else if (run > 1)
		{
			if (!NextQuad(quarter, next, &way))
				return false;
			run = 1;
		}
		quarter = next;
	}
	return true;
}


// ROM 0x00221060 PtsonCircle__FP6TArrayP6FPointlPUl
// Whether more than 92% of the points are within 28% of the radius of the
// circle; the score is what is left over, 1667 for none.
static Boolean
PtsonCircle(TArray* pts, FPoint* centre, long radius, ULong* score)
{
	long on = 0;
	ULong count = (ULong) pts->Count();
	for (ULong i = 0; i < count; i++)
	{
		long off = CheapDistPoint((FPoint*) pts->GetEntry(i), centre) - radius;
		if (off < 0)
			off = -off;
		if (FixedDivide(off, radius) < 0x4800)
			on++;
	}
	Fixed fraction = FixedDivide((Fixed) (on << 16), (Fixed) (count << 16));
	if (fraction <= 0xeb85)
		return false;
	Fixed left = FixedMultiply(0x682aaab, 0x10000 - fraction);
	*score = (ULong) (long) (short) ((left + 0x8000) >> 16);
	return true;
}


// ROM 0x00220f98 IsCircle__11TStrokeUnitFP6FPointPlPUl
Boolean
TStrokeUnit::IsCircle(FPoint* centre, long* radius, ULong* score)
{
	Boolean circle = false;
	TArray* pts = GetPts();
	if (pts == nil)
		return false;
	FRect box;
	GetBBox(&box);
	long width = box.right - box.left;
	long height = box.bottom - box.top;
	long least = height;
	if (width < height)
	{
		least = width;
		width = height;
	}
	if (FixedDivide(width, least) <= 0x16000
	 && PtsonCircle(pts, centre, *radius, score)
	 && TraceContour(pts, centre))
		circle = true;
	pts->Dispose();
	return circle;
}


/*------------------------------------------------------------------------------
	E l l i p s e s
------------------------------------------------------------------------------*/

// ROM 0x00221464 SetupEllipseSystem__FP6TArraylN32PlT6
// The normal equations for the conic x^2 + a xy + b y^2 + c x + d y + e =
// 0 through the points (every n/30th of them): each point moved to the
// box's top left, scaled by `scale` (4 over the box's size) and offset by
// `offset`, and the sums of the products of its powers written into the
// 5x5 matrix and the right-hand side.
static void
SetupEllipseSystem(TArray* pts, Fixed scale, Fixed left, Fixed top, Fixed offset,
				   Fixed* matrix, Fixed* rhs)
{
	Fixed sx = 0, sy = 0, sxx = 0, syy = 0, sxxx = 0, syyy = 0, syyyy = 0;
	Fixed sxy = 0, sxxy = 0, sxxyy = 0, sxxxy = 0, sxyy = 0, sxyyy = 0;
	ULong count = (ULong) pts->Count();
	ULong step = (ULong) (long) (short) ((FixedDivide((Fixed) (count << 16), 30 << 16) + 0x10000) >> 16);
	if (step < 3)
		step = 2;
	for (ULong i = 0; i < count; i += step)
	{
		Fixed* p = (Fixed*) pts->GetEntry(i);
		Fixed x = FixedMultiply(p[0] - left, scale) + offset;
		Fixed y = FixedMultiply(p[1] - top, scale) + offset;
		Fixed xx = FixedMultiply(x, x);
		Fixed yy = FixedMultiply(y, y);
		Fixed xxx = FixedMultiply(xx, x);
		Fixed yyy = FixedMultiply(yy, y);
		sx += x;
		sy += y;
		sxx += xx;
		syy += yy;
		sxxx += xxx;
		syyy += yyy;
		syyyy = FixedMultiply(yyy, y) + syyyy;
		sxy = FixedMultiply(x, y) + sxy;
		sxxy = FixedMultiply(xx, y) + sxxy;
		sxxyy = FixedMultiply(xx, yy) + sxxyy;
		sxxxy = FixedMultiply(xxx, y) + sxxxy;
		sxyy = FixedMultiply(x, yy) + sxyy;
		sxyyy = FixedMultiply(x, yyy) + sxyyy;
	}
	Fixed samples = FixedDivide((Fixed) ((count - 1) << 16), (Fixed) (step << 16));
	matrix[0] = sxxyy;
	matrix[5] = sxyyy;
	matrix[1] = sxyyy;
	matrix[10] = sxxy;
	matrix[2] = sxxy;
	matrix[20] = sxyy;
	matrix[11] = sxyy;
	matrix[7] = sxyy;
	matrix[3] = sxyy;
	matrix[15] = sxy;
	matrix[22] = sxy;
	matrix[13] = sxy;
	matrix[4] = sxy;
	matrix[6] = syyyy;
	matrix[21] = syyy;
	matrix[8] = syyy;
	matrix[16] = syy;
	matrix[23] = syy;
	matrix[9] = syy;
	matrix[12] = sxx;
	matrix[17] = sx;
	matrix[14] = sx;
	matrix[18] = sy;
	matrix[24] = sy;
	matrix[19] = ((short) ((samples + 0x8000) >> 16) + 1) << 16;
	rhs[0] = -sxxxy;
	rhs[1] = -sxxyy;
	rhs[2] = -sxxx;
	rhs[3] = -sxxy;
	rhs[4] = -sxx;
}


// ROM 0x00221968 MakeEllipseTemplate__FP5FRectPlP6FPointN23N42
// The conic's coefficients turned into an ellipse: its centre, its two
// foci, its two radii, its long diameter and its angle in degrees.  A
// conic that is not an ellipse (or too near a degenerate one) is refused.
static Boolean
MakeEllipseTemplate(FRect* box, Fixed* c, FPoint* centre, FPoint* focus1, FPoint* focus2,
					long* radius1, long* radius2, long* diameter, long* angle)
{
	Fixed t = c[4];
	c[4] = c[3];
	c[0] = c[0] >> 1;
	c[3] = t >> 1;
	c[2] = c[2] >> 1;
	Fixed det = c[1] - FixedMultiply(c[0], c[0]);
	Fixed tiny = FixedDivide(0x10000, 10000 << 16);
	if (((det < 0) ? -det : det) < tiny)
		return false;
	Fixed k = (FixedMultiply(FixedMultiply(c[0] << 1, c[2]) - c[3], c[3]) + FixedMultiply(det, c[4]))
			- FixedMultiply(FixedMultiply(c[2], c[2]), c[1]);
	if (((k < 0) ? -k : k) < FixedDivide(0x10000, 10000 << 16))
		return false;
	if (FixedMultiply(k, c[1] + 0x10000) > 0)
		return false;
	long size = box->right - box->left;
	if (size < box->bottom - box->top)
		size = box->bottom - box->top;
	Fixed unit = FixedDivide(size, 4 << 16);
	centre->x = FixedMultiply(FixedDivide(FixedMultiply(c[0], c[3]) - FixedMultiply(c[1], c[2]), det) - 0x10000, unit) + box->left;
	centre->y = FixedMultiply(FixedDivide(FixedMultiply(c[0], c[2]) - c[3], det) - 0x10000, unit) + box->top;
	Fixed root = (FractSquareRoot(FixedMultiply(0x10000 - c[1], 0x10000 - c[1]) + FixedMultiply(4 << 16, FixedMultiply(c[0], c[0]))) + 0x40) >> 7;
	*radius1 = ((c[1] + 0x10000) - root) >> 1;
	*radius2 = (c[1] + 0x10000 + root) >> 1;
	Fixed scale = FixedDivide(k, det);
	Fixed sq1 = FixedDivide(-scale, *radius1);
	*radius1 = FixedMultiply((FractSquareRoot(sq1) + 0x40) >> 7, unit);
	Fixed sq2 = FixedDivide(-scale, *radius2);
	*radius2 = FixedMultiply((FractSquareRoot(sq2) + 0x40) >> 7, unit);
	long turn = 0;
	Fixed b = c[0];
	if (((b < 0) ? -b : b) >= FixedDivide(0x10000, 10000 << 16))
	{
		Fixed across = 0x10000 - c[1];
		if (across < 0)
			across = -across;
		turn = FixedDivide(b << 1, across);
		*angle = turn;
		if (((turn < 0) ? -turn : turn) >= FixedDivide(0xb00000, 1000 << 16))
			turn = FixedAtan2(across, b << 1) >> 1;
		else
			turn = 0;
	}
	*angle = turn;
	Fixed half = FixedMultiply((FractSquareRoot(sq1 - sq2) + 0x40) >> 7, unit);
	Fixed hx = FixedMultiply(half, (FractCos(*angle) + 0x2000) >> 14);
	Fixed hy = FixedMultiply(half, (FractSin(*angle) + 0x2000) >> 14);
	*angle = FixedMultiplyDivide(*angle, 180 << 16, 0x3243f);
	*diameter = *radius1 << 1;
	if (c[1] <= 0x10000)
	{
		focus1->x = centre->x - hy;
		focus1->y = centre->y + hx;
		focus2->x = centre->x + hy;
		focus2->y = centre->y - hx;
		*angle = -0x5a0000 - *angle;
	}
	else
	{
		focus1->x = centre->x + hx;
		focus1->y = centre->y - hy;
		focus2->x = centre->x - hx;
		focus2->y = centre->y + hy;
	}
	return true;
}


// ROM 0x00221800 PtsonEllipse__FP6TArrayP6FPointT2lPUl
// Whether more than 57% of the points (every n/30th) have distances to
// the two foci adding up to within 4% of the long diameter; the score is
// 1500 times what is left over when one and a half times the points on it
// are counted.
static Boolean
PtsonEllipse(TArray* pts, FPoint* focus1, FPoint* focus2, long diameter, ULong* score)
{
	long on = 0;
	ULong count = (ULong) pts->Count();
	ULong step = (ULong) (long) (short) ((FixedDivide((Fixed) (count << 16), 30 << 16) + 0x10000) >> 16);
	if (step < 3)
		step = 2;
	Fixed tolerance = FixedMultiplyDivide(4 << 16, diameter, 100 << 16);
	for (ULong i = 1; i < count; i += step)
	{
		FPoint pt;
		memcpy(&pt, pts->GetEntry(i), sizeof(FPoint));
		long off = CheapDistPoint(&pt, focus1) + CheapDistPoint(&pt, focus2) - diameter;
		if (off < 0)
			off = -off;
		if (off < tolerance)
			on++;
	}
	long samples = (short) ((FixedDivide((Fixed) ((count - 1) << 16), (Fixed) (step << 16)) + 0x8000) >> 16);
	if ((ULong) (count - 1) != step * (ULong) samples)
		samples++;
	ULong fraction = ((ULong) on << 16) / (ULong) samples;
	if (fraction <= 0x91eb)
		return false;
	long counted = on * 0x18000;
	if (counted > (long) ((ULong) samples << 16))
		counted = (long) ((ULong) samples << 16);
	ULong part = (ULong) counted / (ULong) samples;
	Fixed left = FixedMultiply(1500 << 16, 0x10000 - (Fixed) part);
	*score = (ULong) (long) (short) ((left + 0x8000) >> 16);
	return true;
}


// ROM 0x002212e4 IsEllipse__11TStrokeUnitFP6FPointPlN22PUl
Boolean
TStrokeUnit::IsEllipse(FPoint* centre, long* radius1, long* radius2, long* angle, ULong* score)
{
	Boolean ellipse = false;
	TArray* pts = GetPts();
	if (pts == nil)
		return false;
	FRect box;
	GetBBox(&box);
	long width = box.right - box.left;
	long height = box.bottom - box.top;
	long size = (width < height) ? height : width;
	Fixed matrix[25];
	Fixed coeffs[5];
	long pivots[5];
	Fixed det;
	SetupEllipseSystem(pts, FixedDivide(4 << 16, size), box.left, box.top, 0x10000, matrix, coeffs);
	Decomp(5, 5, matrix, pivots, &det);
	ULong kind = 8;
	if (height * 2 < width)
		kind = 7;
	else if (width * 2 < height)
		kind = 9;
	Solve(kind, 5, matrix, pivots, coeffs);
	FPoint focus1, focus2;
	long diameter;
	if (MakeEllipseTemplate(&box, coeffs, centre, &focus1, &focus2, radius1, radius2, &diameter, angle)
	 && PtsonEllipse(pts, &focus1, &focus2, diameter, score)
	 && TraceContour(pts, centre))
		ellipse = true;
	pts->Dispose();
	return ellipse;
}


/*------------------------------------------------------------------------------
	F i n d E l l i p s e s
------------------------------------------------------------------------------*/

// ROM 0x00215904 FindEllipses__FP17TGeneralShapeUnitP6GSTypePUlPl
// A circle's centre is the box's top left plus the mean radius each way,
// and its size the mean of the box's sides; an ellipse's radii must come
// to about the box's diagonal (the square of the longer diameter within
// 31% of the diagonal's).  ==> whether the shape is one, which it is only
// if its fitted outline has no straight side (two corners in a row
// farther apart than the kink distance) and no second point where it
// changes the way it bends; the outline is then thrown away.
Boolean
FindEllipses(TGeneralShapeUnit* unit, long* type, ULong* score, long* angle)
{
	Boolean found = false;
	*score = 10000;
	*angle = 0;
	*type = kShapeClosedCurve;
	TStrokeUnit* stroke = (TStrokeUnit*) unit->GetSub(0);
	FRect box;
	unit->GetBBox(&box);
	long height = (short) ((ULong) ((box.bottom - box.top) + 0x8000) >> 16);
	long width = (short) ((ULong) ((box.right - box.left) + 0x8000) >> 16);
	long radius = ((box.bottom - box.top) + (box.right - box.left)) >> 2;
	FPoint centre;
	centre.x = (box.right + box.left) / 2;
	centre.y = (box.top + box.bottom) / 2;
	long r = radius;
	if (stroke->IsCircle(&centre, &r, score))
	{
		*type = kShapeCircle;
		ShapeInterpretation* interp = unit->Interpretation();
		interp->fParams[0] = box.left + radius;
		interp->fParams[1] = box.top + radius;
		interp->fParams[2] = radius;
	}
	else
	{
		long a, b;
		if (stroke->IsEllipse(&centre, &a, &b, angle, score))
		{
			long longest = a >> 16;
			if (longest <= (b >> 16))
				longest = b >> 16;
			long diagonal = height * height + width * width;
			long off = longest * 2 * longest * 2 - diagonal;
			if (off < 0)
				off = -off;
			if (diagonal > 0x7530)
			{
				diagonal >>= 2;
				off >>= 2;
			}
			Fixed ratio = FixedDivide(off << 16, diagonal << 16);
			if (ratio >= 0 && ratio <= 0x5000)
			{
				*type = kShapeEllipse;
				ShapeInterpretation* interp = unit->Interpretation();
				interp->fParams[0] = centre.x;
				interp->fParams[1] = centre.y;
				interp->fParams[2] = a;
				interp->fParams[3] = b;
				interp->fParams[4] = *angle;
			}
		}
	}
	if (*type != kShapeClosedCurve)
	{
		TArray* shape = unit->GetGeneralShape();
		TArrayIterator iter;
		GeneralPt* prev = (GeneralPt*) shape->GetIterator(&iter);
		Boolean inflected = false;
		Boolean prevCorner = true;
		for (long i = 1; i < iter.fCount; i++)
		{
			GeneralPt* pt = (GeneralPt*) iter.GetNext();
			Boolean corner = pt->f09 == 0;
			long apart = CheapDistPoint(&prev->fPt, &pt->fPt);
			if (pt->f0a != 0)
			{
				if (inflected)
					return false;
				inflected = true;
			}
			if (prevCorner && corner && gPixMinKinkDist < apart)
				return false;
			prev = pt;
			prevCorner = corner;
		}
		found = true;
		GDisposeShape(unit->GetGeneralShape());
		unit->SetGeneralShape(nil);
	}
	return found;
}
