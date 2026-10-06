/*
	File:		recognition/EdgeList.cpp

	Contains:	The gesture domain, its units and the three shape tests
				(EdgeList.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The corner finder itself has no debug symbol, so it is cited
	`(unnamed)`.
*/

#include "EdgeList.h"
#include "Controller.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "RecObject.h"
#include "Angles.h"
#include "FixedMathExtra.h"
#include "Recognizer.h"
#include "host/RomBugs.h"

#include <string.h>

TDomain*	gEdgeListDomain = nil;

// the flag a stroke's sample point carries when the corner finder has
// chosen it (the same bit the stroke code uses for a kept point)
enum { kCornerSample = 2 };


/* -------------------------------------------------------------------------------
	The corner finder
------------------------------------------------------------------------------- */

// ROM 0x0020e3f8 (unnamed)
// The points between first and last marked as corners, recursively.
//
// The chord from the first point to the last gives a pair of axes - one
// along it, one across it, both scaled so that their length is a twelfth
// of the chord's - and every point in between is measured against both.
// Along each axis the walk keeps the furthest the run has got (max), how
// far it has come back since (min), and where the turn was; when a point
// goes further back than the last turn did, the axis is flipped and the
// search starts again the other way, which is what lets a stroke that
// doubles back be split at its own extremes.  The axis whose swing is
// widest, and wider than the threshold, gives the split, and the two
// halves are done again; when neither is, the two ends are the corners.
static void
SplitCorners(SamplePt* points, long first, long last, Fixed threshold, long* count)
{
	long split = -1;
	SamplePt* firstPt = points + first;
	SamplePt* lastPt = points + last;
	if (last - first > 1)
	{
		FPoint a, b, p;
		GetPoint(firstPt, &a);
		GetPoint(lastPt, &b);
		Fixed chord = FixedLength(b.x - a.x, b.y - a.y);
		if (chord < kFix1)
			chord = kFix1;
		Fixed twelfth = chord / 12;
		Fixed nx = FixedDivide(a.y - b.y, twelfth);
		Fixed ny = FixedDivide(b.x - a.x, twelfth);
		if ((nx < 0 ? -nx : nx) < kFix1 && (ny < 0 ? -ny : ny) < kFix1)
			nx = kFix1;								// a chord of no length at all: across is x
		Fixed norm = FixedLength(nx, ny);
		Fixed acrossBase = FixedMultiply(nx, a.x) + FixedMultiply(ny, a.y);
		Fixed alongBase = FixedMultiply(ny, a.x) - FixedMultiply(nx, a.y);

		long acrossMax = 0, acrossMin = 0, acrossPrev = 0;
		long acrossBest = first, acrossAt = first;
		Boolean acrossFlipped = false;
		long alongMax = 0, alongMin = 0, alongPrev = 0;
		long alongBest = first, alongAt = first;
		Boolean alongFlipped = false;

		for (long i = first + 1; i <= last; i++)
		{
			GetPoint(points + i, &p);

			long d = (FixedMultiply(nx, p.x) + FixedMultiply(ny, p.y)) - acrossBase;
			if (acrossFlipped)
				d = -d;
			if (acrossMax < d)
			{
				acrossMin += d - acrossMax;
				acrossMax = d;
				acrossAt = i;
			}
			else if (d < acrossMin)
			{
				acrossMin = d;
				acrossBest = acrossAt;
				if (d < acrossPrev)
				{
					acrossFlipped = !acrossFlipped;
					acrossMin = (acrossPrev - acrossMax) - d;
					acrossPrev = -acrossMax;
					acrossMax = -d;
					acrossAt = i;
				}
			}

			d = (FixedMultiply(ny, p.x) - FixedMultiply(nx, p.y)) - alongBase;
			if (alongFlipped)
				d = -d;
			if (alongMax < d)
			{
				alongMin += d - alongMax;
				alongMax = d;
				alongAt = i;
			}
			else if (d < alongMin)
			{
				alongMin = d;
				alongBest = alongAt;
				if (d < alongPrev)
				{
					alongFlipped = !alongFlipped;
					alongMin = (alongPrev - alongMax) - d;
					alongPrev = -alongMax;
					alongMax = -d;
					alongAt = i;
				}
			}
		}

		if (acrossMax - acrossMin <= FixedMultiply(threshold, norm))
			acrossBest = first;
		if (alongMax - alongMin <= FixedMultiply(threshold, norm))
			alongBest = first;
		if ((first < acrossBest && (alongBest == first || alongMax - alongMin < acrossMax - acrossMin))
			|| (acrossBest = alongBest, first < alongBest))
			split = acrossBest;

		if (split != -1)
		{
			SplitCorners(points, first, split, threshold, count);
			SplitCorners(points, split, last, threshold, count);
			return;
		}
	}
	if (!TestFlag(firstPt, kCornerSample))
	{
		SetFlag(firstPt, kCornerSample);
		(*count)++;
	}
	if (!TestFlag(lastPt, kCornerSample))
	{
		SetFlag(lastPt, kCornerSample);
		(*count)++;
	}
}


// ROM 0x0020ebe4 Collapse2__FP7TDArray
// The corner list tidied, in two passes.  The first drops a corner that
// is within seven units of the one before - keeping whichever of the two
// turns more sharply, when there is a corner on either side to compare -
// and the second drops a corner whose two edges are within about nine
// degrees of each other, which is a straight line with a kink in it.
//
// Both walk the array through a pointer taken once at the start: Delete
// shuffles the entries down but never moves the block, so the pointer
// stays good.
void
Collapse2(TDArray* corners)
{
	ULong count = (ULong) corners->fCount;
	FPoint* points = (FPoint*) corners->GetEntry(0);
	if (count <= 2)
		return;

	ULong i = 1;
	while (i < count)
	{
		FPoint* cur = points + i;
		FPoint* prev = cur - 1;
		if ((ULong) CheapDistPoint(prev, cur) < 0x70000)
		{
			count--;
			Boolean deleteThis = true;
			if (i < count)
			{
				if (i > 1)
				{
					long before = PtsToAngleR(prev, cur - 2);
					long here = PtsToAngleR(cur, prev);
					long after = PtsToAngleR(cur + 1, cur);
					long turnBefore = here - before;
					long turnAfter = after - here;
					NORM(&turnBefore);
					NORM(&turnAfter);
					if (turnBefore < 0)
						turnBefore = -turnBefore;
					if (turnAfter < 0)
						turnAfter = -turnAfter;
					deleteThis = turnAfter <= turnBefore;
				}
				else
					deleteThis = false;
				if (!deleteThis)
					i--;					// the one before goes instead
			}
			corners->Delete(i);
			i--;
			if ((long) i < 1)
				i = 0;
		}
		i++;
	}

	long slope = PtsToAngleR(points + 1, points);
	i = 2;
	while (i < count)
	{
		long next = PtsToAngleR(points + i, points + i - 1);
		long turn = next - slope;
		NORM(&turn);
		if (turn < 0)
			turn = -turn;
		if (turn < 0xa0d9)					// about nine degrees
		{
			corners->Delete(i - 1);
			count--;
			i--;
			next = PtsToAngleR(points + i, points + i - 1);
		}
		i++;
		slope = next;
	}
}


/* -------------------------------------------------------------------------------
	The shape tests
------------------------------------------------------------------------------- */

// ROM 0x001f95ac Signum__Fl
long
Signum(long value)
{
	if (value < 0)
		return -1;
	return value > 0 ? 1 : 0;
}


// ROM 0x0021edf0 Interpolate__FP6FPointT1lT1
// The point that far along the line from a to b.
void
Interpolate(const FPoint* a, const FPoint* b, long distance, FPoint* result)
{
	Fixed t = FixedDivide(distance, CheapDistPoint(a, b));
	result->x = a->x + FixedMultiply(b->x - a->x, t);
	result->y = a->y + FixedMultiply(b->y - a->y, t);
}


// ROM 0x0021f170 InitTurnData__FP8TurnData
void
InitTurnData(TurnData* turns)
{
	turns->fCount = 0;
}


// ROM 0x0021f17c NewTurn__FP8TurnData6FPointT2
void
NewTurn(TurnData* turns, FPoint start, FPoint end)
{
	turns->fStart = start;
	turns->fEnd = end;
}


// ROM 0x0021f1a8 ExtendTurn__FP8TurnData6FPoint
void
ExtendTurn(TurnData* turns, FPoint end)
{
	turns->fEnd = end;
}


// ROM 0x0021f1b4 EndTurn__FP8TurnData6FPoint
// The turn closed off.  Its slope joins the list only when it is more
// than six units long (rounded to whole units first), so the wobbles
// between the real strokes of a scrub are not counted.
void
EndTurn(TurnData* turns, FPoint end)
{
	turns->fEnd = end;
	Fixed length = CheapDistPoint(&turns->fStart, &turns->fEnd);
	if (((length + 0x8000) >> 16) * kFix1 < 0x60001)
		return;
	turns->fSlopes[turns->fCount] = GetSlope(&turns->fEnd, &turns->fStart);
	turns->fCount++;
}


// ROM 0x0021f218 ValidTurnSequence__FP8TurnData
// Whether the turns all point much the same way: every slope is measured
// against the first, and the spread between the largest and the smallest
// must be less than half a turn.  A scrub goes back and forth along one
// line; a scribble wanders.
Boolean
ValidTurnSequence(TurnData* turns)
{
	long lowest = 0;
	long highest = 0;
	for (ULong i = 1; i < (ULong) turns->fCount; i++)
	{
		long delta = DeltaAngle(turns->fSlopes[0], turns->fSlopes[i]);
		if (delta < lowest)
			lowest = delta;
		else if (highest < delta)
			highest = delta;
	}
	return highest - lowest < kHalfTurnDegrees;
}


// ROM 0x0021f0fc TestLine__FP7TDArrayP18UnitInterpretation
// Two corners and nothing in between: a line, at the slope of the two.
Boolean
TestLine(TDArray* corners, UnitInterpretation* interp)
{
	if (corners->fCount != 2)
		return false;
	interp->angle = GetSlope((FPoint*) corners->GetEntry(0), (FPoint*) corners->GetEntry(1));
	interp->label = kGestureLine;
	interp->score = 0;
	return true;
}


// ROM 0x0021ee64 TestCarets__FP7TDArrayP18UnitInterpretation
// Three or four corners meeting at a point.  The two arms must be within
// a factor of two of each other in length, or - with only three corners -
// the longer one is split so that they are.  The angle between them and
// the direction the point faces (the mid angle) then say which of the
// caret family it is; a fourth corner makes it one of the two-stroke
// forms, and its last arm has to come off the axis squarely for the
// closed one.
Boolean
TestCarets(TDArray* corners, UnitInterpretation* interp)
{
	long count = corners->fCount;
	if (count != 3 && count != 4)
		return false;
	FPoint p0, p1, p2;
	memcpy(&p0, corners->GetEntry(0), sizeof(FPoint));
	memcpy(&p1, corners->GetEntry(1), sizeof(FPoint));
	memcpy(&p2, corners->GetEntry(2), sizeof(FPoint));
	long first = CheapDistPoint(&p0, &p1);
	long second = CheapDistPoint(&p1, &p2);
	if (second < first >> 1)
		return false;
	if (first < second >> 1)
	{
		if (count == 4)
			return false;
		// ROM BUG (fixed): Interpolate's third argument is a distance
		// along the line, and what it is given here is the ratio of the
		// two arms - at most a half, where the distance wanted is the
		// length of the first arm.  The new corner therefore lands all but
		// on top of p1 rather than a first arm's length along the second,
		// and the four-corner tests below see a first arm and a stub.  The
		// fix gives it the first arm's length, so the second arm is split
		// into one as long as the first and the rest.
		FPoint split;
		Interpolate(&p1, &p2, RomBugFixed() ? first : FixedDivide(first, second), &split);
		corners->Add();
		memcpy(corners->GetEntry(2), &split, sizeof(FPoint));
		memcpy(corners->GetEntry(3), &p2, sizeof(FPoint));
		p2 = split;
		count = 4;
	}

	long inSlope = GetSlope(&p0, &p1);
	long outSlope = GetSlope(&p2, &p1);
	long turn = DeltaAngle(inSlope, outSlope);
	long mid = MidAngle(inSlope, outSlope);
	interp->angle = mid;
	long sharpness = turn < 0 ? -turn : turn;
	long label;

	if (sharpness <= 0x6e0000)					// the arms meet at 110 degrees or less
	{
		Boolean tooShallow = sharpness < 0x460000 ? true : mid < 0x730000;
		if (!tooShallow && mid <= 0x9b0000 && count == 3)
		{
			label = kGestureCaretFlat;			// 70..110 degrees, leaning over: 115..155
			goto found;
		}
	}
	if (sharpness <= 0x780000)					// 120 degrees
	{
		long facing = mid < 0 ? -mid : mid;
		if (facing >= 0xa00000 && count == 3)	// pointing within 20 degrees of straight up
		{
			label = kGestureCaret;
			goto found;
		}
	}
	if (sharpness > 0x640000)					// more than 100 degrees: not a caret at all
		return false;
	if (count != 3)
	{
		FPoint p3;
		memcpy(&p3, corners->GetEntry(3), sizeof(FPoint));
		long tail = GetSlope(&p2, &p3);
		long off = DeltaAngle(tail, mid);
		long offMagnitude = off < 0 ? -off : off;
		if (offMagnitude <= 0x910000)			// 145 degrees
		{
			if (turn < 0)
				off = -off;
			long square = DeltaAngle(off, 0x5a0000);
			if (square < 0)
				square = -square;
			if (square > 0x13ffff)				// the tail is not square to the axis
				return false;
			label = kGestureCaret4;
		}
		else
			label = kGestureCaret4Open;
		goto found;
	}
	label = kGestureCaret;

found:
	interp->label = label;
	interp->score = 0;
	return true;
}


// ROM 0x0021ebb8 TestScrub__FP7TDArrayP5FRectP18UnitInterpretation
// A zig-zag: five to thirty-nine corners whose turns alternate.  The
// edges' slopes are taken first, along with the average edge length - a
// first edge shorter than half the average is treated as no turn at all,
// so a scrub that starts with a little hook still counts.  Then the
// corners are walked: while the turn keeps going the same way the run is
// extended, and when it changes the run is closed off as a turn.  A turn
// of more than 110 degrees counts towards the three that a scrub needs; a
// run that accumulates more than 180 degrees is a loop, and gives up.
// Finally the turns must all point much the same way (ValidTurnSequence).
//
// (The bounding box is taken but never looked at - the ROM passes it and
// does nothing with it.)
Boolean
TestScrub(TDArray* corners, FRect* /*bbox*/, UnitInterpretation* interp)
{
	ULong count = (ULong) corners->fCount;
	if (count < 5 || count > 39)
		return false;

	TurnData turns;
	long slopes[40];
	InitTurnData(&turns);
	FPoint* points = (FPoint*) corners->GetEntry(0);
	long total = 0;
	long firstEdge = 0;
	for (ULong i = 1; i < count; i++)
	{
		slopes[i] = GetSlope(points + i, points + i - 1);
		total += CheapDistPoint(points + i - 1, points + i);
		if (i == 1)
			firstEdge = total;
	}
	long average = (long) ((ULong) total / (count - 1));

	ULong sharpTurns = 0;
	long run = DeltaAngle(slopes[1], slopes[2]);
	if (firstEdge < average >> 1)
		run = 0;
	long direction = Signum(run);
	NewTurn(&turns, points[0], points[2]);

	ULong i = 3;
	long turn = run;
	for (; i < count; i++)
	{
		long delta = DeltaAngle(slopes[i - 1], slopes[i]);
		long sign = Signum(delta);
		long magnitude = delta < 0 ? -delta : delta;
		if (magnitude > 0xa9ffff && direction == sign)
		{
			// more than 170 degrees the same way is the other way round
			sign = -direction;
			delta = -delta;
		}
		if (direction == sign)
		{
			delta += turn;
			ExtendTurn(&turns, points[i]);
		}
		else
		{
			long closed = turn < 0 ? -turn : turn;
			if (closed > 0x6e0000)
				sharpTurns++;
			EndTurn(&turns, points[i - 1]);
			NewTurn(&turns, points[i - 2], points[i]);
		}
		long accumulated = delta < 0 ? -delta : delta;
		if (accumulated > kHalfTurnDegrees)
			return false;						// it has turned right round: a loop
		turn = delta;
		direction = sign;
	}

	long closed = turn < 0 ? -turn : turn;
	if (closed > 0x6e0000)
		sharpTurns++;
	EndTurn(&turns, points[i - 1]);
	if (sharpTurns < 3 || !ValidTurnSequence(&turns))
		return false;
	interp->label = kGestureScrub;
	interp->score = 0;
	return true;
}


/* -------------------------------------------------------------------------------
	T E d g e L i s t U n i t
------------------------------------------------------------------------------- */

// ROM 0x0020ed90 Make__13TEdgeListUnitSFP7TDomainUlP6TArray
TSIUnit*
TEdgeListUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TEdgeListUnit* unit = new TEdgeListUnit;
	if (unit != nil && unit->IEdgeListUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x0020ee08 IEdgeListUnit__13TEdgeListUnitFP7TDomainUlP6TArray
// A 'SCRB' unit with one interpretation of its own - the ROM says it has
// one before there is anything in it - and no corners yet.
long
TEdgeListUnit::IEdgeListUnit(TDomain* domain, ULong kind, TArray* areas)
{
	InitInterpretation(&fInterp, 0, 0);
	fInterpCount = 1;
	long err = ISIUnit(domain, kEdgeListDomainType, kind, areas, sizeof(UnitInterpretation));
	fCorners = nil;
	return err;
}


// ROM 0x0020ee70 SetInterpretation__13TEdgeListUnitFP7TDArray
void
TEdgeListUnit::SetInterpretation(TDArray* corners)
{
	corners->Clone();
	fCorners = corners;
	EndUnit();
}


// ROM 0x0020effc GetCorners__13TEdgeListUnitFv
TDArray*
TEdgeListUnit::GetCorners(void)
{
	return fCorners;
}


// ROM 0x0020f004 InterpretationCount__13TEdgeListUnitFv
long
TEdgeListUnit::InterpretationCount(void)
{
	return fInterpCount;
}


// ROM 0x0020f00c AddInterpretation__13TEdgeListUnitFPc
// There is only ever the one, so adding overwrites it.
long
TEdgeListUnit::AddInterpretation(char* interp)
{
	memmove(&fInterp, interp, sizeof(UnitInterpretation));
	fInterpCount = 1;
	return 0;
}


// ROM 0x0020f03c GetInterpretation__13TEdgeListUnitFUl
UnitInterpretation*
TEdgeListUnit::GetInterpretation(ULong /*index*/)
{
	return &fInterp;
}


// ROM 0x0020eea0 EndUnit__13TEdgeListUnitFv
// No more to come: the corners and the interpretations' parameters give
// their spare slots back.
void
TEdgeListUnit::EndUnit(void)
{
	TDArray* corners = GetCorners();
	if (corners != nil)
		corners->Compact();
	EndSubs();
	long count = InterpretationCount();
	for (long i = 0; i < count; i++)
	{
		TRecObject* param = GetParam(i);
		if (param != nil)
			((TArray*) param)->Compact();
	}
	CompactInterpretations();
}


// ROM 0x0020efb4 SizeInBytes__13TEdgeListUnitFv
long
TEdgeListUnit::SizeInBytes(void)
{
	TDArray* corners = GetCorners();
	long size = TSIUnit::SizeInBytes();
	if (corners != nil)
		size += corners->SizeInBytes();
	return size;
}


// ROM 0x0020ef80 DoneUsingUnit__13TEdgeListUnitFv
void
TEdgeListUnit::DoneUsingUnit(void)
{
	TDArray* corners = GetCorners();
	if (corners != nil)
		corners->Dispose();
	fCorners = nil;
	if (fHasInterps == 1)
		for (long i = InterpretationCount() - 1; i >= 0; i--)
			DeleteInterpretation(i);
	fHasInterps = 0;
	if (fAreas != nil)
		fAreas->Dispose();
	fAreas = nil;
	UnsetFlags(kAreaListUnit);
}


// ROM 0x0020ef54 IDispose__13TEdgeListUnitFv
void
TEdgeListUnit::IDispose(void)
{
	TDArray* corners = GetCorners();
	if (corners != nil)
		corners->Dispose();
	if (fSubKind == kSubList)
		fSubs->Dispose();
	if (fHasInterps == 1)
		for (long i = InterpretationCount() - 1; i >= 0; i--)
			DeleteInterpretation(i);
	fSubKind = kNoSubs;
	fHasInterps = 0;
	SetAreas(nil);
	delete this;
}


// ROM 0x0020eed0 Dump__13TEdgeListUnitFP4TMsg
// (NOT YET RECONSTRUCTED: TMsg, the recogniser's debug log.  The ROM
// writes "EdgeList: ", the unit's own line and the number of corners.)
void
TEdgeListUnit::Dump(TMsg* /*msg*/)
{ }


/* -------------------------------------------------------------------------------
	T E d g e L i s t D o m a i n
------------------------------------------------------------------------------- */

// ROM 0x0020e83c Make__15TEdgeListDomainSFP11TController
TDomain*
TEdgeListDomain::Make(TController* controller)
{
	TEdgeListDomain* domain = new TEdgeListDomain;
	if (domain != nil)
		domain->IEdgeListDomain(controller);
	return domain;
}


// ROM 0x0020e884 IEdgeListDomain__15TEdgeListDomainFP11TController
// 'SCRB', made out of 'STRK' pieces, and put on the controller's list
// (as the stroke domain does, rather than through RegisterDomain).
void
TEdgeListDomain::IEdgeListDomain(TController* controller)
{
	IDomain(controller, kEdgeListDomainType, (char*) "EdgeList");
	AddPieceType(kStrokeUnit);
	fController = controller;
	*(TDomain**) controller->fDomains->AddEntry() = this;
	controller->fDomains->Compact();
}


// ROM 0x0020e8d4 Dispose__15TEdgeListDomainFv
// Nothing: the gesture domain is made once and lives as long as the
// recogniser does.
void
TEdgeListDomain::Dispose(void)
{ }


// ROM 0x0020ea1c Group__15TEdgeListDomainFP5TUnitP8dInfoRec
// Each stroke on its own: a 'SCRB' unit with the piece as its only sub.
long
TEdgeListDomain::Group(TUnit* piece, dInfoRec* /*info*/)
{
	TAreaList* areas = piece->GetAreas();
	TSIUnit* unit = TEdgeListUnit::Make(this, piece->fKind + 1, areas);
	if (areas != nil)
		areas->Dispose();
	if (unit == nil)
		return 0;
	unit->AddSub(piece);
	unit->EndSubs();
	fController->NewGroup(unit);
	return 1;
}


// ROM 0x0020e8d8 Classify__15TEdgeListDomainFP5TUnit
// The shape of the stroke worked out and the unit labelled with it, or
// the unit marked invalid and let go.  Nothing is tried at all while
// another stroke is still being written (OnlyStrokeWritten), because a
// gesture is only a gesture when it stands alone, and a polyline of more
// than fifty corners is not going to be one either.
void
TEdgeListDomain::Classify(TUnit* unit)
{
	TStrokeUnit* sub = (TStrokeUnit*) ((TSIUnit*) unit)->GetSub(0);
	Boolean recognised = false;
	if (OnlyStrokeWritten(sub))
	{
		FindCorners(unit);
		TDArray* corners = ((TEdgeListUnit*) unit)->GetCorners();
		if (corners != nil && (ULong) corners->fCount < 50)
		{
			UnitInterpretation interp;
			InitInterpretation(&interp, 0, 0);
			FRect bbox;
			unit->GetBBox(&bbox);
			Collapse2(corners);
			if (TestLine(corners, &interp)
				|| TestCarets(corners, &interp)
				|| TestScrub(corners, &bbox, &interp))
			{
				((TSIUnit*) unit)->AddInterpretation((char*) &interp);
				recognised = true;
			}
		}
	}
	if (!recognised)
	{
		unit->SetFlags(kInvalidUnit);
		unit->DoneUsingUnit();
	}
	unit->Invalidate();
	fController->NewClassification(unit);
}


// ROM 0x0020eaa8 FindCorners__15TEdgeListDomainFP5TUnit
// The stroke's points run through SplitCorners at a threshold of four,
// and the ones it marked collected into an array of FPoints that the
// unit keeps.
void
TEdgeListDomain::FindCorners(TUnit* unit)
{
	TStroke* stroke = ((TStrokeUnit*) ((TSIUnit*) unit)->GetSub(0))->fStroke;
	ULong count = (ULong) stroke->fCount;
	SamplePt* pt = stroke->GetPoint(0);
	for (ULong i = 0; i < count; i++)
		UnsetFlag(pt++, kCornerSample);

	long cornerCount = 0;
	SplitCorners(stroke->GetPoint(0), 0, (long) count - 1, 4 * kFix1, &cornerCount);

	TDArray* corners = TDArray::Make(sizeof(FPoint), cornerCount);
	if (corners == nil)
		return;
	pt = stroke->GetPoint(0);
	ULong n = 0;
	for (ULong i = 0; i < count; i++, pt++)
		if (TestFlag(pt, kCornerSample))
		{
			FPoint p;
			GetPoint(pt, &p);
			corners->SetEntry(n++, (char*) &p);
		}
	((TEdgeListUnit*) unit)->SetInterpretation(corners);
	corners->Dispose();
}
