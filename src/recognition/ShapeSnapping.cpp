/*
	File:		recognition/ShapeSnapping.cpp

	Contains:	How a new shape is fitted to the shapes already on the page
				(the context units, which GetContextUnits asks the view
				for).

				SnapPtToLC moves a shape's loose ends onto whatever they
				were found to touch while grouping: onto a corner or an
				end of another shape, or along a side to where the new
				line crosses it (SnapPtToLine), or onto a circle - across
				to its edge, or round to the point where the new line is
				its tangent, and when a line joins two circles of one size,
				tangent to both (SnapPtToCircle, CircleTan).

				GlobalTrends does the rest for an upright square or a
				circle that touches nothing: its centre across, its centre
				down and its size are each clustered with those of the
				squares and circles on the page (TTrend), and each is moved
				onto the value it clusters with - so shapes drawn in a row
				line up and come out the same size.

	Reconstructed from the MP2x00 US ROM (0x00211684-0x0021227c and
	CircleTan at 0x00215c64); each function cites its origin.
*/

#include "ShapeGeometry.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "FixedMath.h"
#include "Angles.h"


static inline long
WAbs(long v)
{
	return v < 0 ? (long) (int32_t) (0u - (uint32_t) v) : v;
}

// A 16.16 value rounded to a whole number, as a short.
static inline long
RoundShort(long v)
{
	return (short) ((uint32_t) (v + 0x8000) >> 16);
}


// ROM 0x00211684 GlobalTrends__FP17TGeneralShapeUnitPl
// See the file's head.  *snapped is set when the shape was looked at
// (whether or not anything moved).
void
GlobalTrends(TGeneralShapeUnit* unit, long* snapped)
{
	*snapped = 0;
	long type = unit->GetLabel(0);
	if (unit->GetAngle(0) != 0)
		return;
	if (type != kShapeSquare && type != kShapeCircle)
		return;
	TUnitList* context = GetContextUnits(unit, 1);
	if (context == nil)
		return;
	TTrend* sizes = TTrend::Make(1);
	TTrend* across = TTrend::Make(1);
	TTrend* down = TTrend::Make(1);
	FPoint centre;
	long size;
	if (sizes == nil || across == nil || down == nil)
		goto failed;
	{
		TArrayIterator iter;
		TGeneralShapeUnit** entry = (TGeneralShapeUnit**) ((TArray*) context)->GetIterator(&iter);
		for (long i = 0; i < iter.fCount; i++)
		{
			TGeneralShapeUnit* other = *entry;
			long otherType = other->GetLabel(0);
			if (otherType == kShapeSquare)
			{
				// ROM BUG: a turned square is skipped without moving on, so
				// the rest of the list is that same square, skipped again
				if (other->GetAngle(0) != 0)
					continue;
			}
			else if (otherType != kShapeCircle)
			{
				entry = (TGeneralShapeUnit**) iter.GetNext();
				continue;
			}
			CircleParams(other, &centre, &size);
			if (across->AddToTrend(RoundShort(centre.x), nil, 1)
			 || down->AddToTrend(RoundShort(centre.y), nil, 1)
			 || sizes->AddToTrend(RoundShort(size), nil, 1))
				goto failed;
			entry = (TGeneralShapeUnit**) iter.GetNext();
		}

		CircleParams(unit, &centre, &size);
		long nearX, nearY, nearSize;
		across->fTolerance = RoundShort(size / 6);
		if (across->AddToTrend(RoundShort(centre.x), &nearX, 0))
			goto failed;
		down->fTolerance = RoundShort(size / 6);
		if (down->AddToTrend(RoundShort(centre.y), &nearY, 0))
			goto failed;
		long slop = RoundShort(size / 6);
		if (slop >= gPixMaxSizeTrendSlop)
			slop = gPixMaxSizeTrendSlop;
		sizes->fTolerance = slop;
		if (sizes->AddToTrend(RoundShort(size), &nearSize, 0))
			goto failed;

		// the values found are taken from the first shape that has them
		Boolean foundX = false, foundY = false, foundSize = false;
		entry = (TGeneralShapeUnit**) ((TArray*) context)->GetIterator(&iter);
		for (long i = 0; i < iter.fCount; i++)
		{
			TGeneralShapeUnit* other = *entry;
			long otherType = other->GetLabel(0);
			if (otherType == kShapeSquare || otherType == kShapeCircle)
			{
				FPoint c;
				long s;
				CircleParams(other, &c, &s);
				if (!foundX && nearX == RoundShort(c.x))
				{
					foundX = true;
					centre.x = c.x;
				}
				if (!foundY && nearY == RoundShort(c.y))
				{
					foundY = true;
					centre.y = c.y;
				}
				if (!foundSize && nearSize == RoundShort(s))
				{
					foundSize = true;
					size = s;
				}
			}
			if (foundY && foundX && foundSize)
				break;
			entry = (TGeneralShapeUnit**) iter.GetNext();
		}
		DisposeContextUnits(context);
		*snapped = 1;
		if (type == kShapeSquare)
		{
			TDArray* shape = unit->GetGeneralShape();
			shape->CutToIndex(0);
			FPoint pt;
			pt.x = centre.x - (size >> 1);
			pt.y = centre.y - (size >> 1);
			InitGeneralPt(shape, 0, pt);
			pt.y = pt.y + size;
			InitGeneralPt(shape, 1, pt);
			pt.x = pt.x + size;
			InitGeneralPt(shape, 2, pt);
			pt.y = pt.y - size;
			InitGeneralPt(shape, 3, pt);
			pt.x = pt.x - size;
			InitGeneralPt(shape, 4, pt);
		}
		else if (type == kShapeCircle)
		{
			ShapeInterpretation* interp = unit->Interpretation();
			interp->fParams[0] = centre.x;
			interp->fParams[1] = centre.y;
			interp->fParams[2] = (long) ((uint32_t) size >> 1);
		}
		goto done;
	}

failed:
	if (context != nil)
		DisposeContextUnits(context);
done:
	if (sizes != nil)
		sizes->Dispose();
	if (across != nil)
		across->Dispose();
	if (down != nil)
		down->Dispose();
}


// ROM 0x00211d00 SnapPtToLC__FP17TGeneralShapeUnit
// Each loose end that touched another shape moved onto it - except a
// line both of whose ends touched one shape at neighbouring points, which
// is taken to be tracing that shape's side and is left alone.
void
SnapPtToLC(TGeneralShapeUnit* unit)
{
	TDArray* shape = unit->GetGeneralShape();
	if (shape == nil)
		return;
	long n = shape->Count();
	TGeneralShapeUnit* met[2] = { nil, nil };		// (the ROM's are what its stack held)
	long metType[2] = { 0, 0 };
	for (long i = 0; i < 2; i++)
	{
		TUnit* u = unit->fGroupInfo->fEnds[i].fUnit;
		if (u != nil)
		{
			met[i] = (TGeneralShapeUnit*) u;
			metType[i] = met[i]->GetLabel(0);
		}
	}
	ShapeGroupInfo* info = unit->fGroupInfo;
	if (info->fConnections == 2 && n == 2 && met[1] == met[0] && metType[0] != 0)
	{
		long count = ((TStrokeUnit*) met[0]->GetSub(0))->fStroke->Count();
		long p0 = info->fEnds[0].fPoint;
		long p1 = info->fEnds[1].fPoint;
		if (p0 == p1)
			return;
		long k1 = info->fEnds[1].fKind;
		if (k1 >= 1 && p0 + 1 == p1)
			return;
		long k0 = info->fEnds[0].fKind;
		if (k0 >= 1 && p0 - 1 == p1)
			return;
		if (k1 == 3 && p0 == count - 2)
			return;
		if (k0 == 3 && p1 == count - 2)
			return;
	}
	for (long i = 0; i < 2; i++)
	{
		if (unit->fGroupInfo->fEnds[i].fUnit == nil)
			continue;
		GeneralPt* end;
		GeneralPt* next;
		if (i == 0)
		{
			end = (GeneralPt*) shape->GetEntry(0);
			next = (GeneralPt*) shape->GetEntry(1);
		}
		else
		{
			end = (GeneralPt*) shape->GetEntry(n - 1);
			next = (GeneralPt*) shape->GetEntry(n - 2);
		}
		TStroke* outline = ((TStrokeUnit*) met[i]->GetSub(0))->fStroke;
		if (metType[i] != kShapeCircle)
			SnapPtToLine(unit, i, end, next, outline);
		else if (SnapPtToCircle(unit, i, &end->fPt, &next->fPt, met[i]))
			return;
	}
}


// ROM 0x00211f1c SnapPtToLine__FP17TGeneralShapeUnitlP9GeneralPtT3P7TStroke
// End `which` moved onto the outline it touched: onto the corner or end
// it met; or, on a side, to where the line through the end and its
// neighbour crosses that side (for a straight line that misses, the line
// from its drawn end nearer this one), or to either end of the side if
// that is nearer still - but only within gPixMaxClosedDist.  An end whose
// neighbour is a curve's control point is moved along the side instead,
// as far from its start as it was.
void
SnapPtToLine(TGeneralShapeUnit* unit, long which, GeneralPt* end, GeneralPt* next, TStroke* outline)
{
	ShapeEnd* e = &unit->fGroupInfo->fEnds[which];
	long index = e->fPoint;
	long kind = e->fKind;
	if (kind < 0)
		return;
	FPoint a;
	outline->GetFPoint(index, &a);
	if (kind > 0)
	{
		end->fPt = a;
		return;
	}
	if ((ULong) outline->Count() <= (ULong) (index + 1))
		return;
	FPoint b;
	outline->GetFPoint(index + 1, &b);
	if (next->fControl)
	{
		long d = CheapDistPoint(&end->fPt, &a);
		FPoint v;
		v.x = b.x - a.x;
		v.y = b.y - a.y;
		ScaleToSize(&v, d);
		end->fPt.x = a.x + v.x;
		end->fPt.y = a.y + v.y;
		return;
	}
	long best = 0x7fffffff;
	FPoint cross;
	IntersectLine(&cross, &end->fPt, &next->fPt, &a, &b);
	if (PtOnLine2(&a, &b, &cross, gPixPtOnLineSlop, nil))
		best = CheapDistPoint(&end->fPt, &cross);
	else if (unit->GetLabel(0) == kShapeLine)
	{
		TStroke* drawn = ((TStrokeUnit*) unit->GetSub(0))->fStroke;
		long count = drawn->Count();
		FPoint first, last;
		GetPoint(drawn->GetPoint(0), &first);
		GetPoint(drawn->GetPoint(0) + (count - 1), &last);
		long d0 = CheapDistPoint(&end->fPt, &first);
		long d1 = CheapDistPoint(&end->fPt, &last);
		FPoint from = (d0 < d1) ? first : last;
		IntersectLine(&cross, &from, &next->fPt, &a, &b);
		if (PtOnLine2(&a, &b, &cross, gPixPtOnLineSlop, nil))
			best = CheapDistPoint(&end->fPt, &cross);
	}
	long da = CheapDistPoint(&end->fPt, &a);
	long db = CheapDistPoint(&end->fPt, &b);
	long limit = gPixMaxClosedDist + 1;
	if (limit > best)
	{
		limit = best;
		end->fPt = cross;
	}
	if (limit > da)
	{
		limit = da;
		end->fPt = a;
	}
	if (limit > db)
		end->fPt = b;
}


// ROM 0x002121c0 SnapPtToCircle__FP17TGeneralShapeUnitlP6FPointT3T1
// End `which` moved onto the circle it touched (CircleTan); a straight
// line whose other end touched another circle is made tangent to both
// when they are one size.  True if the line was moved whole.
Boolean
SnapPtToCircle(TGeneralShapeUnit* unit, long which, FPoint* end, FPoint* next, TGeneralShapeUnit* circle)
{
	long dist = unit->fGroupInfo->fEnds[which].fDist;
	long type = unit->GetLabel(0);
	TGeneralShapeUnit* other = nil;
	long otherDist = 0;
	if (which == 0 && unit->fGroupInfo->fConnections == 2 && type == kShapeLine)
	{
		other = (TGeneralShapeUnit*) unit->fGroupInfo->fEnds[1].fUnit;
		if (other->GetLabel(0) == kShapeCircle && circle != other)
			otherDist = unit->fGroupInfo->fEnds[1].fDist;
		else
			other = nil;
	}
	return CircleTan(circle, other, end, next, dist, otherDist);
}


// ROM 0x00215c64 CircleTan__FP17TGeneralShapeUnitT1P6FPointT3lT5
// An end moved onto a circle.  Within `dist` of the circle's upright or
// level through the centre, to the top or bottom, left or right of it.
// Otherwise, when the line from the end to its neighbour is within 8
// degrees of being a tangent, to the point where it is one (the side of
// the circle nearer the end) - and when `other` is a circle of the same
// size, to where the line tangent to both leaves this one, the neighbour
// moved to where it meets the other (and the answer is true).  Otherwise
// straight in or out to the edge.
Boolean
CircleTan(TGeneralShapeUnit* circle, TGeneralShapeUnit* other, FPoint* end, FPoint* next, long dist, long /*otherDist*/)
{
	FRect box;
	circle->GetBBox(&box);
	long r = (box.bottom - box.top) >> 1;
	FPoint centre;
	centre.x = box.left + r;
	centre.y = box.top + r;
	long direction = PtsToAngle(end, next, 0x10000);
	if (WAbs(centre.x - end->x) < dist)
	{
		end->x = centre.x;
		end->y = (end->y >= centre.y) ? centre.y + r : centre.y - r;
	}
	else if (WAbs(centre.y - end->y) < dist)
	{
		end->x = (end->x >= centre.x) ? centre.x + r : centre.x - r;
		end->y = centre.y;
	}
	else
	{
		long d = PtsToAngle(end, &centre, 0x10000) - direction;
		NORMD(&d);
		d = WAbs(d);
		if (WAbs(d - 0x5a0000) < 0x80000)
		{
			long kind = 1;
			long otherR = 0;
			// (the ROM's other centre is what its registers held when
			// there is no other circle; it is used only for a circle of
			// no size)
			FPoint otherCentre = { 0, 0 };
			if (other != nil)
			{
				other->GetBBox(&box);
				otherR = (box.bottom - box.top) >> 1;
				otherCentre.x = box.left + otherR;
				otherCentre.y = box.top + otherR;
			}
			FPoint v, step = { 0, 0 };
			if (r == otherR)
			{
				v.x = otherCentre.y - centre.y;
				v.y = centre.x - otherCentre.x;
				step.x = otherCentre.x - centre.x;
				step.y = otherCentre.y - centre.y;
				kind = 2;
			}
			else
			{
				v.x = next->y - end->y;
				v.y = end->x - next->x;
			}
			ScaleToSize(&v, r);
			FPoint p;
			p.x = centre.x + v.x;
			p.y = centre.y + v.y;
			if (CheapDistPoint(&p, end) > gPixMaxClosedDist)
			{
				p.x = centre.x - v.x;
				p.y = centre.y - v.y;
			}
			*end = p;
			if (kind != 2)
				return false;
			next->x = end->x + step.x;
			next->y = end->y + step.y;
			return true;
		}
	}
	FPoint v;
	v.x = end->x - centre.x;
	v.y = end->y - centre.y;
	ScaleToSize(&v, r);
	end->x = centre.x + v.x;
	end->y = centre.y + v.y;
	return false;
}
