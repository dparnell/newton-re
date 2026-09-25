/*
	File:		recognition/ShapeDomain.cpp

	Contains:	The shape domain: its units, the grouping of strokes into
				shapes, the gravity that snaps a new shape onto the shapes
				on the page, and the recogniser.  See ShapeDomain.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ShapeDomain.h"
#include "ShapeGeometry.h"
#include "Controller.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "RecObject.h"
#include "UnitPublic.h"
#include "FixedMathExtra.h"
#include "FixedMath.h"
#include "TabletBuffer.h"
#include "NewtonGestalt.h"
#include "NewtonMemory.h"
#include "PolygonView.h"
#include "Rects.h"

#include <string.h>

extern const int	displayAngle[25];		// ShapeTables.cpp: 0 to 360 degrees in fifteens, Fixed


/*------------------------------------------------------------------------------
	T h e   d o m a i n ' s   g l o b a l s
------------------------------------------------------------------------------*/

Boolean	gCurveFlag = true;
Boolean	gSymmetryFlag = true;
Boolean	gGravityFlag = true;

FRect	gGSScreenRect;
long	gPixScreenRectInset;
long	gPixMaxContextGravity;
Boolean	gGSOffScreen;
long	gGSInkLength;
long	gGSClosed;
long	gPixMaxCollapseSize;
long	gPixMaxSmallDist;
long	gPixMaxClosedDist;
long	gPixMaxConnectDist;
long	gPixMinConnectDist;
long	gPixMinKinkDist;
long	gPixMinRLineOutTolerance;
long	gPixMaxRLineOutTolerance;
long	gPixMaxAvgLenForSmallDists;
long	gPixMinAvgLenForSmallDists;
long	gPixSomeMagicThreshold;
long	gPixLargeInitialValue;
long	gPixLowBlobThreshold;
long	gPixHighBlobThreshold;
long	gPixMaxSizeTrendSlop;
long	gPixPtOnLineSlop;
long	gSmpMinClosedShapePts;
long	gSmpMinSmallDistRun;
ContextUnitProc	gContextUnitProc;

// ROM 0x0c104d0c (unnamed) - the screen resolution the distances were last
// worked out for (horizontal, vertical), and the sampling interval the
// point counts were
static short	gGSResolutionH;
static short	gGSResolutionV;
static ULong	gGSSampleRate;

// ROM 0x0c107010 (unnamed) - where ExtractEnds leaves the two ends
static FPoint	gGSEnds[2];


// ROM 0x002105c0 SetContextUnitRoutine__FPFP5TUnitl_P9TUnitList
void
SetContextUnitRoutine(ContextUnitProc proc)
{
	gContextUnitProc = proc;
}


// ROM 0x002105d0 CheckScreenGlobals__Fv
// Everything the domain measures in pixels is a number of points at 72 to
// the inch, scaled by the screen's resolution (the mean of its two) the
// first time and whenever it changes; the two counts of samples are
// scaled the same way by how many the tablet takes a second, against 80.
// The screen itself, let out by an inset, is where a shape's points may
// go (SetGeneralPt).
void
CheckScreenGlobals(void)
{
	TUGestalt gestalt;
	TGestaltSystemInfo info;
	memset(&info, 0, sizeof(info));		// DEVIATION: a host with no such Gestalt leaves it alone
	gestalt.Gestalt(kGestalt_SystemInfo, &info, sizeof(info));
	gGSScreenRect.left = 0;
	gGSScreenRect.top = 0;
	gGSScreenRect.bottom = (Fixed) ((ULong) info.fScreenHeight << 16);
	gGSScreenRect.right = (Fixed) ((ULong) info.fScreenWidth << 16);

	if (info.fScreenResolution.h != gGSResolutionH || info.fScreenResolution.v != gGSResolutionV)
	{
		gGSResolutionH = info.fScreenResolution.h;
		gGSResolutionV = info.fScreenResolution.v;
		// (the sum is taken as a whole word and halved with the sign's
		//  rounding, as the ROM's shift-and-add does)
		long sum = (long) (int) ((unsigned int) (gGSResolutionH + gGSResolutionV) << 16);
		Fixed scale = FixedDivide(sum / 2, 72 << 16);
		gPixMaxCollapseSize = FixedMultiply(scale, 0x6ffff);
		gPixMaxSmallDist = FixedMultiply(scale, 0xffff);
		gPixMaxClosedDist = FixedMultiply(scale, 0xa0000);
		gPixMaxConnectDist = FixedMultiply(scale, 0x140000);
		gPixMinConnectDist = FixedMultiply(scale, 0x50000);
		gPixMinKinkDist = FixedMultiply(scale, 0xf0000);
		gPixMinRLineOutTolerance = FixedMultiply(scale, 0x30000);
		gPixMaxRLineOutTolerance = FixedMultiply(scale, 0x180000);
		gPixMaxAvgLenForSmallDists = FixedMultiply(scale, 0x36000);
		gPixMinAvgLenForSmallDists = FixedMultiply(scale, 0xa000);
		gPixSomeMagicThreshold = FixedMultiply(scale, 0x280000);
		gPixLargeInitialValue = FixedMultiply(scale, 0x320000);
		gPixLowBlobThreshold = FixedMultiply(scale, 0xf0000);
		gPixHighBlobThreshold = FixedMultiply(scale, 0x190000);
		gPixMaxSizeTrendSlop = (short) ((scale * 9 + 0x8000) >> 16);
		gPixPtOnLineSlop = (short) ((scale * 2 + 0x8000) >> 16);
		gPixScreenRectInset = FixedMultiply(scale, 0xa0000);
		gPixMaxContextGravity = (short) ((FixedMultiply(scale, 0xa0000) + 0x8000) >> 16);
	}

	ULong rate = GetSampleRate();
	if (rate != gGSSampleRate)
	{
		// the samples a second (the tablet's timer runs at 0x384000 a
		// second, and the rate is the ticks between two samples) against 80
		Fixed perSecond = FixedDivide((Fixed) (0x384000 / rate), 80);
		gSmpMinClosedShapePts = (short) ((perSecond * 7 + 0x8000) >> 16);
		gSmpMinSmallDistRun = (short) ((perSecond * 3 + 0x8000) >> 16);
		gGSSampleRate = rate;
	}
	InsetRectangle(&gGSScreenRect, -gPixScreenRectInset, -gPixScreenRectInset);
}


/*------------------------------------------------------------------------------
	T G e n e r a l S h a p e U n i t
------------------------------------------------------------------------------*/

// ROM 0x00216668 Make__17TGeneralShapeUnitSFP7TDomainUlP6TArray
TGeneralShapeUnit*
TGeneralShapeUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TGeneralShapeUnit* unit = new TGeneralShapeUnit;
	if (unit != nil && unit->IGeneralShapeUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x002166e0 IGeneralShapeUnit__17TGeneralShapeUnitFP7TDomainUlP6TArray
// A 'GSHP' unit with its one interpretation, and the grouping state: the
// order and directions cleared, no ends met.  (The ROM answers whatever
// was in its first register, which the one caller treats as an error
// when it is not nought; it is the unit's address, so never.)
long
TGeneralShapeUnit::IGeneralShapeUnit(TDomain* domain, ULong kind, TArray* areas)
{
	fContextID = 0;
	ISIUnit(domain, kShapeUnit, kind, areas, sizeof(ShapeInterpretation));
	fInterpCount = 0;
	fSnapped = 0;
	fSnapDist = 0;
	NewInterpretation(nil);
	fGroupInfo = (ShapeGroupInfo*) NewPtr(sizeof(ShapeGroupInfo));
	if (fGroupInfo == nil)
		return 0;
	fGroupInfo->fConnections = 0;
	for (long i = 0; i < 8; i++)
	{
		fGroupInfo->fOrder[i] = 0;
		fGroupInfo->fReversed[i] = 0;
	}
	for (long i = 0; i < 2; i++)
	{
		fGroupInfo->f50[i] = -1;
		fGroupInfo->fEnds[i].fUnit = nil;
		fGroupInfo->fEnds[i].fKind = -1;
	}
	return 0;
}


// ROM 0x00216f8c Dump__17TGeneralShapeUnitFP4TMsg
void
TGeneralShapeUnit::Dump(TMsg* /*msg*/)
{ }


// ROM 0x002167c0 GetInterpretation__17TGeneralShapeUnitFUl
UnitInterpretation*
TGeneralShapeUnit::GetInterpretation(ULong index)
{
	return index < (ULong) fInterpCount ? &fInterp.fBase : nil;
}


// ROM 0x002167d4 InterpretationCount__17TGeneralShapeUnitFv
long
TGeneralShapeUnit::InterpretationCount(void)
{
	return fInterpCount;
}


// ROM 0x002167dc AddInterpretation__17TGeneralShapeUnitFPc
// The one interpretation's first sixteen bytes (its UnitInterpretation)
// overwritten; ==> its index, nought.
long
TGeneralShapeUnit::AddInterpretation(char* interp)
{
	MoveBlock(interp, &fInterp, sizeof(UnitInterpretation));
	fInterpCount = 1;
	return 0;
}


// ROM 0x00217398 NewInterpretation__17TGeneralShapeUnitFP7TDArray
void
TGeneralShapeUnit::NewInterpretation(TDArray* shape)
{
	InitInterpretation(&fInterp.fBase, 0, 0);
	fInterp.fShape = shape;
	for (ULong i = 0; i < 6; i++)
		fInterp.fParams[i] = 0;
	fInterpCount = 1;
}


// ROM 0x0021730c GetGeneralShape__17TGeneralShapeUnitFv
TDArray*
TGeneralShapeUnit::GetGeneralShape(void)
{
	if (InterpretationCount() == 0)
		return nil;
	return ((ShapeInterpretation*) GetInterpretation(0))->fShape;
}


// ROM 0x00217354 SetGeneralShape__17TGeneralShapeUnitFP7TDArray
void
TGeneralShapeUnit::SetGeneralShape(TDArray* shape)
{
	if (InterpretationCount() == 0)
		return;
	((ShapeInterpretation*) GetInterpretation(0))->fShape = shape;
}


// ROM 0x00216f90 ContextID__17TGeneralShapeUnitFv
ULong
TGeneralShapeUnit::ContextID(void)
{
	return fContextID;
}


// ROM 0x00216f98 SetContextID__17TGeneralShapeUnitFUl
void
TGeneralShapeUnit::SetContextID(ULong id)
{
	fContextID = id;
}


// ROM 0x000dc840 GDisposeShape__FP7TDArray
// (one instruction: a jump through the array's Dispose)
void
GDisposeShape(TDArray* shape)
{
	shape->Dispose();
}


// ROM 0x002171ac DisposeFD__FP17TGeneralShapeUnit
// The grouping state given back, and with it the shapes on the page its
// ends had met - each once, though both ends may have met the same one.
void
DisposeFD(TGeneralShapeUnit* unit)
{
	if (unit->fGroupInfo == nil)
		return;
	TUnit* last = nil;
	for (ULong i = 0; i < 2; i++)
	{
		TUnit* met = unit->fGroupInfo->fEnds[i].fUnit;
		if (met != nil && met != last)
			PurgeDeep((TSIUnit*) met);
		last = met;
	}
	DisposPtr((Ptr) unit->fGroupInfo);
	unit->fGroupInfo = nil;
}


// ROM 0x00216f6c EndUnit__17TGeneralShapeUnitFv
// Ended as any unit is, and then the grouping state let go as DisposeFD
// does (the ROM has its own copy of that loop here).
void
TGeneralShapeUnit::EndUnit(void)
{
	TSIUnit::EndUnit();
	if (fGroupInfo == nil)
		return;
	TUnit* last = nil;
	for (ULong i = 0; i < 2; i++)
	{
		TUnit* met = fGroupInfo->fEnds[i].fUnit;
		if (met != nil && met != last)
			PurgeDeep((TSIUnit*) met);
		last = met;
	}
	DisposPtr((Ptr) fGroupInfo);
	fGroupInfo = nil;
}


// ROM 0x00216fa0 SizeInBytes__17TGeneralShapeUnitFv
// What a unit is, and its fitted outline (a shape, not a circle, an
// ellipse or nothing), and the grouping state with the shapes it holds.
long
TGeneralShapeUnit::SizeInBytes(void)
{
	long size = 0;
	if (InterpretationCount() != 0)
	{
		ULong type = (ULong) GetInterpretation(0)->label;
		if (type > kShapeEllipse && (type == kShapeCurve || type != kShapeNothing))
		{
			TDArray* shape = GetGeneralShape();
			if (shape != nil)
				size = shape->SizeInBytes();
		}
	}
	if (fGroupInfo != nil)
	{
		TUnit* last = nil;
		for (ULong i = 0; i < 2; i++)
		{
			TUnit* met = fGroupInfo->fEnds[i].fUnit;
			if (met != nil && met != last)
				size += met->SizeInBytes();
			last = met;
		}
		size += GetPtrSize((Ptr) fGroupInfo);
	}
	return TSIUnit::SizeInBytes() + size;
}


// ROM 0x00216e90 IDispose__17TGeneralShapeUnitFv
// The fitted outline, the subs that belong to it alone (the strokes a
// context unit was made of: flag 0x100000), the grouping state, and then
// the unit as a TSIUnit goes.
void
TGeneralShapeUnit::IDispose(void)
{
	if (InterpretationCount() != 0)
	{
		GetInterpretation(0);
		TDArray* shape = GetGeneralShape();
		if (shape != nil)
			GDisposeShape(shape);
	}
	if (TestFlags(0x100000))
	{
		ULong count = (ULong) SubCount();
		for (ULong i = 0; i < count; i++)
		{
			if (GetSub(i)->TestFlags(0x100000))
			{
				DeleteSub(i);
				i--;
				count--;
			}
		}
	}
	DisposeFD(this);
	TSIUnit::IDispose();
}


// ROM 0x00217214 DoneUsingUnit__17TGeneralShapeUnitFv
// As IDispose, but the unit stays: the outline is let go and forgotten,
// the context unit's own subs taken out and the flag cleared, and the
// interpretations and the areas let go as any unit's are.
void
TGeneralShapeUnit::DoneUsingUnit(void)
{
	if (InterpretationCount() != 0)
	{
		GetInterpretation(0);
		TDArray* shape = GetGeneralShape();
		if (shape != nil)
		{
			GDisposeShape(shape);
			SetGeneralShape(nil);
		}
	}
	if (TestFlags(0x100000))
	{
		ULong count = (ULong) SubCount();
		for (ULong i = 0; i < count; i++)
		{
			if (GetSub(i)->TestFlags(0x100000))
			{
				DeleteSub(i);
				i--;
				count--;
			}
		}
		UnsetFlags(0x100000);
	}
	DisposeFD(this);
	TSIUnit::DoneUsingUnit();
}


// ROM 0x0021680c CleanPt__FP5TabPt
// A point pulled back onto the tablet: nothing negative.
static void
CleanPt(TabPt* pt)
{
	if (pt->x < 0)
		pt->x = 0;
	if (pt->y < 0)
		pt->y = 0;
}


// A quadratic curve: its start, its control point and its end.
struct Curve
{
	FPoint		fStart;
	FPoint		fControl;
	FPoint		fEnd;
};

// ROM 0x00216d4c CurvePts__FP5CurvePlP6FPointl
// The curve drawn out into 2^depth points by cutting it in half that many
// times (de Casteljau: the halves' control points are the midpoints of the
// arms, and they meet at the midpoint of those); each half's end is a
// point, so the start of the whole is not one of them.
static void
CurvePts(Curve* curve, long* count, FPoint* pts, long depth)
{
	if (depth != 0)
	{
		Curve first, second;
		first.fStart = curve->fStart;
		first.fControl.x = (curve->fControl.x + curve->fStart.x) >> 1;
		first.fControl.y = (curve->fStart.y + curve->fControl.y) >> 1;
		second.fControl.x = (curve->fControl.x + curve->fEnd.x) >> 1;
		second.fControl.y = (curve->fControl.y + curve->fEnd.y) >> 1;
		second.fStart.x = (first.fControl.x + second.fControl.x) >> 1;
		second.fStart.y = (first.fControl.y + second.fControl.y) >> 1;
		second.fEnd = curve->fEnd;
		first.fEnd = second.fStart;
		CurvePts(&first, count, pts, depth - 1);
		CurvePts(&second, count, pts, depth - 1);
		return;
	}
	pts[(*count)++] = curve->fEnd;
}


// ROM 0x0021682c GetGSAsStroke__17TGeneralShapeUnitFv
// The shape as a stroke: an ellipse's own points, or the outline - a
// corner a point, and a corner reached through a control point a curve
// from the corner before, drawn out into 2, 4 or 8 points by how far the
// two corners are apart (under 6, under 18 pixels, or more).
TStroke*
TGeneralShapeUnit::GetGSAsStroke(void)
{
	ULong type = (ULong) GetLabel(0);
	if (type == kShapeNothing)
		return nil;
	TStroke* stroke;
	if (type == kShapeCircle || type == kShapeEllipse)
		stroke = GetEllipseAsStroke();
	else
	{
		TDArray* shape = GetGeneralShape();
		long count = 0;
		if (shape != nil)
			count = shape->Count();
		if (shape == nil || count == 0)
			return nil;
		GeneralPt prev;
		memcpy(&prev, shape->GetEntry(0), sizeof(GeneralPt));
		FPoint corner = prev.fPt;
		stroke = TStroke::Make(0);
		if (stroke == nil)
			return nil;
		TabPt pt;
		pt.x = prev.fPt.x;
		pt.y = prev.fPt.y;
		pt.z = 1;
		pt.p = 0;
		CleanPt(&pt);
		long err = stroke->AddPoint(&pt);
		for (long i = 1; err == 0 && i < count; i++)
		{
			GeneralPt cur;
			memcpy(&cur, shape->GetEntry(i), sizeof(GeneralPt));
			if (cur.fControl == 0)
			{
				if (prev.fControl == 0)
				{
					pt.x = cur.fPt.x;
					pt.y = cur.fPt.y;
					CleanPt(&pt);
					err = stroke->AddPoint(&pt);
				}
				else
				{
					Curve curve;
					curve.fStart = corner;
					curve.fControl = prev.fPt;
					curve.fEnd = cur.fPt;
					long drawn = 0;
					long apart = CheapDistPoint(&curve.fStart, &curve.fEnd);
					long depth = (apart < 0x60000) ? 1 : (apart < 0x120000) ? 2 : 3;
					long n = 1 << depth;
					FPoint pts[16];
					CurvePts(&curve, &drawn, pts, depth);
					for (long j = 0; err == 0 && j < n; j++)
					{
						pt.x = pts[j].x;
						pt.y = pts[j].y;
						CleanPt(&pt);
						err = stroke->AddPoint(&pt);
					}
				}
				if (err != 0)
					break;
				corner = cur.fPt;
			}
			prev = cur;
		}
		if (err != 0)
		{
			stroke->Dispose();
			return nil;
		}
	}
	if (stroke != nil)
		stroke->Compact();
	return stroke;
}


// ROM 0x00216abc GetEllipseAsStroke__17TGeneralShapeUnitFv
// A circle or an ellipse as 25 points, every fifteen degrees round and
// back to the start: a circle's out of its centre and its radius, an
// ellipse's out of its centre, its two radii and the angle it is turned
// through.
TStroke*
TGeneralShapeUnit::GetEllipseAsStroke(void)
{
	ShapeInterpretation* interp = (ShapeInterpretation*) GetInterpretation(0);
	TStroke* stroke = TStroke::Make(0);
	if (stroke == nil)
		return nil;
	TabPt pt;
	pt.z = 1;
	pt.p = 0;
	long err = 0;
	if (interp->fBase.label == kShapeCircle)
	{
		Fixed cx = interp->fParams[0];
		Fixed cy = interp->fParams[1];
		Fixed radius = interp->fParams[2];
		for (long i = 0; i < 25; i++)
		{
			Fixed angle = FixedMultiplyDivide(displayAngle[i], 0x3243f, 180 << 16);
			Fract s = FractSin(angle);
			Fract c = FractCos(angle);
			pt.x = FixedMultiply(radius, (c + 0x2000) >> 14) + cx;
			pt.y = cy - FixedMultiply(radius, (s + 0x2000) >> 14);
			CleanPt(&pt);
			if ((err = stroke->AddPoint(&pt)) != 0)
				break;
		}
	}
	else if (interp->fBase.label == kShapeEllipse)
	{
		Fixed cx = interp->fParams[0];
		Fixed cy = interp->fParams[1];
		Fixed a = interp->fParams[2];
		Fixed b = interp->fParams[3];
		Fixed turn = FixedMultiplyDivide(interp->fParams[4], 0x3243f, 180 << 16);
		Fixed sinTurn = (FractSin(turn) + 0x2000) >> 14;
		Fixed cosTurn = (FractCos(turn) + 0x2000) >> 14;
		for (long i = 0; i < 25; i++)
		{
			Fixed angle = FixedMultiplyDivide(displayAngle[i], 0x3243f, 180 << 16);
			Fract s = FractSin(angle);
			Fract c = FractCos(angle);
			Fixed x = FixedMultiply(a, (c + 0x2000) >> 14);
			Fixed y = FixedMultiply(b, (s + 0x2000) >> 14);
			pt.x = FixedMultiply(x, cosTurn) + cx + FixedMultiply(-y, sinTurn);
			pt.y = (cy - FixedMultiply(x, sinTurn)) + FixedMultiply(-y, cosTurn);
			CleanPt(&pt);
			if ((err = stroke->AddPoint(&pt)) != 0)
				break;
		}
	}
	else
		err = 1;
	if (err != 0)
	{
		stroke->Dispose();
		return nil;
	}
	stroke->Compact();
	return stroke;
}


// ROM 0x0021708c GetAvgLength__FP17TGeneralShapeUnit
long
GetAvgLength(TGeneralShapeUnit* unit)
{
	if (unit->InterpretationCount() == 0)
		return 0;
	Fixed length;
	ULong type = (ULong) unit->GetLabel(0);
	if (type < 3)
	{
		// (the ROM measures the box's height and width and keeps only the
		//  width)
		FRect box;
		unit->GetBBox(&box);
		length = box.right - box.left;
	}
	else
	{
		if (type == kShapeNothing)
			return 0;
		TDArray* shape = unit->GetGeneralShape();
		if (shape == nil)
			return 0;
		ULong count = (ULong) shape->Count();
		if (count < 2)
			return 0;
		Fixed total = 0;
		GeneralPt* pt = (GeneralPt*) shape->GetEntry(0);
		for (ULong i = 1; i < count; i++, pt++)
			total += CheapDistPoint(&pt->fPt, &pt[1].fPt);
		length = FixedDivide(total, (Fixed) ((count - 1) << 16));
	}
	return (short) ((ULong) (length + 0x8000) >> 16);
}


// ROM 0x00213508 PurgeDeep__FP7TSIUnit
// The unit and everything under it disposed however many hold it: each
// is marked "Cntx" and loses the flag that keeps a context unit's subs.
void
PurgeDeep(TSIUnit* unit)
{
	for (ULong i = 0; i < (ULong) unit->SubCount(); i++)
		PurgeDeep((TSIUnit*) unit->GetSub(i));
	unit->fStartTime = 'Cntx';		// (+0x1c: a marker for the debugger, over the start time)
	unit->UnsetFlags(0x100000);
	unit->Dispose();
}


// ROM 0x002142a0 PurgeDeep__FP9TUnitList
void
PurgeDeep(TUnitList* list)
{
	for (ULong i = 0; i < (ULong) list->Count(); i++)
		PurgeDeep((TSIUnit*) list->GetUnit(i));
}


/*------------------------------------------------------------------------------
	G r o u p i n g
------------------------------------------------------------------------------*/

// ROM 0x00211388 CloseDelta__FP11TStrokeUnit
long
CloseDelta(TStrokeUnit* stroke)
{
	FRect box;
	stroke->GetBBox(&box);
	long size = box.right - box.left;
	if (size < box.bottom - box.top)
		size = box.bottom - box.top;
	long delta = size / 5;
	if (delta < gPixMinConnectDist)
		delta = gPixMinConnectDist;
	else if (gPixMaxConnectDist < delta)
		delta = gPixMaxConnectDist;
	return delta;
}


// ROM 0x00210860 CheckClosed__FP11TStrokeUnit
Boolean
CheckClosed(TStrokeUnit* stroke)
{
	TStroke* s = stroke->fStroke;
	ULong count = (ULong) s->Count();
	if (count < (ULong) gSmpMinClosedShapePts)
		return false;
	long delta = CloseDelta(stroke);
	FPoint first, last;
	s->GetFPoint(0, &first);
	s->GetFPoint((long) count - 1, &last);
	return CheapDistPoint(&first, &last) < delta;
}


// ROM 0x00210e60 ExtractEnds__FP11TStrokeUnitP17TGeneralShapeUnitPP6FPoint
void
ExtractEnds(TStrokeUnit* stroke, TGeneralShapeUnit* shape, FPoint** ends)
{
	ends[0] = &gGSEnds[0];
	ends[1] = &gGSEnds[1];
	long subs = 0;
	if (shape != nil)
	{
		subs = shape->SubCount();
		stroke = (TStrokeUnit*) shape->GetSub((ULong) (subs - 1));
	}
	TStroke* s = stroke->fStroke;
	long count = s->Count();
	s->GetFPoint(0, &gGSEnds[0]);
	s->GetFPoint(count - 1, &gGSEnds[1]);
	if (shape != nil && subs > 1)
	{
		// the last stroke taken the way round it runs, and only the end
		// of it that is still free
		if (shape->fGroupInfo->fReversed[subs - 1] != 0)
		{
			s->GetFPoint(count - 1, &gGSEnds[0]);
			s->GetFPoint(0, &gGSEnds[1]);
		}
		if ((ULong) shape->fGroupInfo->fOrder[subs - 1] == (ULong) (subs - 1))
			ends[0] = nil;
		else
			ends[1] = nil;
	}
}


// ROM 0x00210f48 CheckConnect__FlPP6FPointP17TGeneralShapeUnitT3
long
CheckConnect(long mode, FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* target)
{
	long toTail[2];				// how far each end is from the target's tail (sp+0x00)
	long toHead[2];				// and from its head (sp+0x08)
	long tolHead0, tolTail0, tolHead1, tolTail1;
	long far = gPixLargeInitialValue;
	long near0 = gPixLargeInitialValue;	// what the first end already meets at, less one
	long near1 = gPixLargeInitialValue;	// and the second
	for (long i = 0; i < 2; i++)
	{
		toTail[i] = 0;
		toHead[i] = 0;
	}
	if (mode == 2)
	{
		ShapeGroupInfo* info = shape->fGroupInfo;
		if (ends[0] != nil && info->fEnds[0].fUnit != nil)
			near0 = info->fEnds[0].fDist;
		if (ends[1] != nil && info->fEnds[1].fUnit != nil)
			near1 = info->fEnds[1].fDist;
		far = CloseDelta((TStrokeUnit*) shape->GetSub(0));
	}

	// the target's head: the first stroke in its order, from whichever end
	// it runs from
	long subs = target->SubCount();
	ULong headSub = target->fGroupInfo->fOrder[0];
	TStrokeUnit* sub = (TStrokeUnit*) target->GetSub(headSub);
	long headIndex = (target->fGroupInfo->fReversed[headSub] == 0) ? 0 : sub->fStroke->Count() - 1;
	FPoint head;
	sub->fStroke->GetFPoint(headIndex, &head);
	long delta = CloseDelta(sub);
	if (delta >= far)
		delta = far;
	long near0less = near0 - 1;
	tolHead0 = (delta < near0less) ? delta : near0less;
	long near1less = near1 - 1;
	tolHead1 = (delta >= near1less) ? near1less : delta;

	// and its tail: the last stroke in its order, from its other end
	ULong tailSub = target->fGroupInfo->fOrder[subs - 1];
	sub = (TStrokeUnit*) target->GetSub(tailSub);
	long tailIndex = (target->fGroupInfo->fReversed[tailSub] == 0) ? sub->fStroke->Count() - 1 : 0;
	FPoint tail;
	sub->fStroke->GetFPoint(tailIndex, &tail);
	delta = CloseDelta(sub);
	if (delta >= far)
		delta = far;
	tolTail0 = (delta < near0less) ? delta : near0less;
	tolTail1 = (delta >= near1less) ? near1less : delta;

	// the first end to the tail and the second to the head is the stroke
	// following on; the first to the head (and the second to the tail) is
	// the stroke coming before, drawn the other way
	long reversed = 0;			// r8: the join is head to head
	long before = 0;			// sp+0x28: the stroke goes in front
	UByte met[2] = { 0, 0 };
	Boolean tried = false;
	if (ends[0] != nil)
	{
		toTail[0] = CheapDistPoint(ends[0], &tail);
		if (toTail[0] <= tolTail0)
		{
			met[0] = 1;
			if (ends[1] != nil)
			{
				toHead[1] = CheapDistPoint(ends[1], &head);
				if (toHead[1] <= tolHead1)
					met[1] = 1;
			}
			tried = true;
		}
		else
		{
			toHead[0] = CheapDistPoint(ends[0], &head);
			if (toHead[0] <= tolHead0)
			{
				met[0] = 1;
				reversed = 1;
				before = 1;
				if (ends[1] != nil)
				{
					toTail[1] = CheapDistPoint(ends[1], &tail);
					if (toTail[1] <= tolTail1)
						met[1] = 1;
				}
				tried = true;
			}
		}
	}
	if (!tried && ends[1] != nil)
	{
		toHead[1] = CheapDistPoint(ends[1], &head);
		if (toHead[1] <= tolHead1)
		{
			met[1] = 1;
			before = 1;
		}
		else
		{
			toTail[1] = CheapDistPoint(ends[1], &tail);
			if (toTail[1] <= tolTail1)
			{
				met[1] = 1;
				reversed = 1;
			}
		}
	}

	long count = met[0] + met[1];
	if (mode > 0 && count != 0)
	{
		if (mode == 2)
		{
			// the join to a shape on the page, recorded at this shape's ends
			target->fGroupInfo->fReversed[0] = (UByte) reversed;
			for (long i = 0; i < 2; i++)
			{
				if (met[i] == 0)
					continue;
				ShapeEnd* end = &shape->fGroupInfo->fEnds[i];
				if (end->fUnit == nil)
					shape->fGroupInfo->fConnections++;
				end->fUnit = target;
				end->fDist = (i == reversed) ? toTail[i] : toHead[i];
				end->fKind = 2;
				end->fPoint = (i == reversed) ? tailIndex : headIndex;
			}
		}
		else
		{
			// the stroke is to be the target's next sub: where it goes in the
			// order, and whether it runs backwards
			ShapeGroupInfo* info = target->fGroupInfo;
			info->fReversed[subs] = (UByte) reversed;
			if (before != 0)
			{
				for (long i = subs; i > 0; i--)
					info->fOrder[i] = info->fOrder[i - 1];
				info->fOrder[0] = (UByte) subs;
				if (info->fEnds[0].fUnit != nil)
					info->fEnds[0].fKind = -2;
			}
			else
			{
				info->fOrder[subs] = (UByte) subs;
				if (info->fEnds[1].fUnit != nil)
					info->fEnds[1].fKind = -2;
			}
		}
	}
	return count;
}


// ROM 0x00210cbc PtOnLine2__FP6FPointN21lPl
long
PtOnLine2(FPoint* a, FPoint* b, FPoint* pt, long slop, long* dist)
{
	Fixed tolerance = (Fixed) (slop << 16);
	long result = 1;
	Fixed nx = a->y - b->y;				// the segment's normal
	Fixed ny = b->x - a->x;
	Fixed dx = pt->x - a->x;
	Fixed dy = pt->y - a->y;
	Fixed length = FixedLength(nx, ny);
	Fixed d;
	if (length < 0x10000)
	{
		// a segment under a pixel long: the distance from its start
		d = FixedLength(dx, dy);
		if (tolerance < d)
			return 0;
	}
	else
	{
		Fixed ux = FixedDivide(nx, length);
		Fixed uy = FixedDivide(ny, length);
		d = FixedMultiply(ux, dx) + FixedMultiply(uy, dy);		// across the line
		if (d > tolerance || d < -tolerance)
			return 0;
		Fixed along = FixedMultiply(uy, dx) - FixedMultiply(ux, dy);	// and along it
		if (along < -tolerance || along > tolerance + length)
			return 0;
		if (length / 2 < along)
		{
			// nearer the far end: measured from there
			dx = pt->x - b->x;
			dy = pt->y - b->y;
			along = length - along;
			result = 2;
		}
		if (along <= tolerance)
		{
			if (along < 0)
			{
				d = FixedLength(dx, dy);
				if (tolerance < d)
					return 0;
			}
		}
		else
			result = 3;
	}
	if (dist != nil)
		*dist = (d < 0) ? -d : d;
	return result;
}


// ROM 0x00211c44 CircleParams__FP17TGeneralShapeUnitP6FPointPl
// (the centre across is the left plus half the *height*: a circle's box
// is square)
void
CircleParams(TGeneralShapeUnit* circle, FPoint* centre, long* diameter)
{
	FRect box;
	circle->GetBBox(&box);
	*diameter = box.bottom - box.top;
	centre->x = box.left + ((box.bottom - box.top) >> 1);
	centre->y = box.top + (*diameter >> 1);
}


// ROM 0x00215c08 PtOnCircle__FP6FPointP17TGeneralShapeUnitPl
Boolean
PtOnCircle(FPoint* pt, TGeneralShapeUnit* circle, long* dist)
{
	FPoint centre;
	long diameter;
	CircleParams(circle, &centre, &diameter);
	long off = CheapDistPoint(&centre, pt) - (diameter >> 1);
	if (off < 0)
		off = -off;
	long best = *dist;
	if (off < best)
		*dist = off;
	return off < best;
}


// ROM 0x00210a10 CheckPtOnShape__FPP6FPointP17TGeneralShapeUnitT2
// Each free end of `shape` against the outline of a polygon on the page
// (its first sub's stroke, joined point to point): the nearest segment
// within a tolerance wins, and says whether the end is on its line, on a
// corner, or on one of the outline's own ends (2 for an open one, 3 for a
// closed one).
void
CheckPtOnShape(FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* context)
{
	long endKind;
	switch (context->GetLabel(0))
	{
	case 4: case 9: case 10: case 11: case 12:
		endKind = 3;
		break;
	case 5: case 8:
		endKind = 2;
		break;
	default:
		return;
	}
	TStroke* outline = ((TStrokeUnit*) context->GetSub(0))->fStroke;
	long count = outline->Count();
	if (count == 0)
		return;
	SamplePt* pts = outline->GetPoint(0);
	TStrokeUnit* end = (TStrokeUnit*) shape->GetSub((ends[0] == nil) ? (ULong) (shape->SubCount() - 1) : 0);
	long delta = (end != nil) ? CloseDelta(end) : gPixMaxClosedDist;
	for (long i = 0; i < 2; i++)
	{
		if (ends[i] == nil)
			continue;
		long kind = -1;
		long point = 0;
		long tolerance = (gPixMaxClosedDist + delta) / 2;
		if (gPixMaxClosedDist < tolerance)
			tolerance = gPixMaxClosedDist;
		long best = (shape->fGroupInfo->fEnds[i].fUnit == nil) ? tolerance : shape->fGroupInfo->fEnds[i].fDist;
		FPoint prev, cur;
		GetPoint(&pts[0], &prev);
		for (long j = 1; j < count; j++)
		{
			GetPoint(&pts[j], &cur);
			long dist;
			long where = PtOnLine2(&prev, &cur, ends[i], tolerance >> 16, &dist);
			if (where != 0 && dist < best)
			{
				best = dist;
				if (where == 1)
				{
					kind = (j <= 1) ? endKind : 1;
					point = j - 1;
				}
				else if (where == 2)
				{
					kind = (count - 1 <= j) ? endKind : 1;
					point = j;
				}
				else if (where == 3)
				{
					kind = 0;
					point = j - 1;
				}
			}
			prev = cur;
		}
		if (kind >= 0)
		{
			ShapeEnd* e = &shape->fGroupInfo->fEnds[i];
			if (e->fUnit == nil)
				shape->fGroupInfo->fConnections++;
			e->fUnit = context;
			e->fDist = best;
			e->fPoint = point;
			e->fKind = kind;
		}
	}
}


// ROM 0x002108d8 CheckPtOnCircle__FPP6FPointP17TGeneralShapeUnitT2
void
CheckPtOnCircle(FPoint** ends, TGeneralShapeUnit* shape, TGeneralShapeUnit* context)
{
	if (context->GetLabel(0) != kShapeCircle)
		return;
	TStrokeUnit* end = (TStrokeUnit*) shape->GetSub((ends[0] == nil) ? (ULong) (shape->SubCount() - 1) : 0);
	long delta = gPixMaxClosedDist;
	if (end != nil)
		delta = CloseDelta(end);
	for (long i = 0; i < 2; i++)
	{
		if (ends[i] == nil)
			continue;
		ShapeEnd* e = &shape->fGroupInfo->fEnds[i];
		long dist;
		if (e->fUnit == nil)
			dist = (delta <= gPixMaxClosedDist) ? delta : gPixMaxClosedDist;
		else
			dist = e->fDist;
		if (PtOnCircle(ends[i], context, &dist))
		{
			if (e->fUnit == nil)
				shape->fGroupInfo->fConnections++;
			e->fUnit = context;
			e->fDist = dist;
			e->fKind = 4;
		}
	}
}


// ROM 0x00211c94 GetContextUnits__FP5TUnitl
TUnitList*
GetContextUnits(TUnit* unit, long whole)
{
	TUnitList* list = nil;
	if (gGravityFlag && gContextUnitProc != nil && unit != nil)
	{
		list = gContextUnitProc(unit, whole);
		if (list != nil && list->Count() == 0)
		{
			list->Dispose();
			list = nil;
		}
	}
	return list;
}


// ROM 0x002156b0 DisposeContextUnits__FP9TUnitList
void
DisposeContextUnits(TUnitList* list)
{
	if (list == nil)
		return;
	PurgeDeep(list);
	list->Dispose();
}


/*------------------------------------------------------------------------------
	T G e n e r a l S h a p e D o m a i n
------------------------------------------------------------------------------*/

// ROM 0x00215f20 Make__19TGeneralShapeDomainSFP11TController
TGeneralShapeDomain*
TGeneralShapeDomain::Make(TController* controller)
{
	TGeneralShapeDomain* domain = new TGeneralShapeDomain;
	domain->IGeneralShapeDomain(controller);
	return domain;
}


// ROM 0x00215f68 IGeneralShapeDomain__19TGeneralShapeDomainFP11TController
// 'GSHP' out of 'STRK' pieces, waiting the recognition timeout before its
// units are arbitrated, put on the controller's list.
void
TGeneralShapeDomain::IGeneralShapeDomain(TController* controller)
{
	IDomain(controller, kShapeUnit, (char*) "GeneralShape Domain");
	AddPieceType(kStrokeUnit);
	CheckScreenGlobals();
	fDelay = gRecognitionTimeout;
	fController = controller;
	*(TDomain**) controller->fDomains->AddEntry() = this;
	controller->fDomains->Compact();
}


// ROM 0x00216538 PreGroup__19TGeneralShapeDomainFP5TUnit
// Before a stroke is grouped: the shape being drawn is ended when the
// stroke will not join it - it closes on itself, or its ends do not meet
// the shape's - and so is every other shape waiting.  ==> 1 when that
// ended the shape (or there was no stroke).
long
TGeneralShapeDomain::PreGroup(TUnit* unit)
{
	if (unit == nil)
		return 1;
	long ended = 0;
	TStrokeUnit* stroke = (TStrokeUnit*) unit;
	Boolean acquired = AcquireStroke(stroke->fStroke);
	Boolean closed = CheckClosed(stroke);
	if (acquired)
		ReleaseStroke();
	TUnitList* delayed = fController->GetDelayList(this, kShapeUnit);
	if (delayed == nil)
		return 0;
	if (delayed->Count() != 0)
	{
		TGeneralShapeUnit* shape = (TGeneralShapeUnit*) delayed->GetUnit(0);
		Boolean joins = false;
		if (!closed)
		{
			acquired = AcquireStroke(stroke->fStroke);
			FPoint* ends[2];
			ExtractEnds(stroke, nil, ends);
			joins = CheckConnect(0, ends, nil, shape) != 0;
			if (acquired)
				ReleaseStroke();
		}
		if (!joins)
		{
			shape->EndSubs();
			ended = 1;
		}
		for (ULong i = 1; i < (ULong) delayed->Count(); i++)
			((TSIUnit*) delayed->GetUnit(i))->EndSubs();
	}
	delayed->Dispose();
	return ended;
}


// ROM 0x00215fd4 Group__19TGeneralShapeDomainFP5TUnitP8dInfoRec
// A stroke put into a shape.  If a shape is being drawn and the stroke's
// ends meet its free ends, the stroke becomes its next sub (the joins it
// had to the page let go where the join has moved, or all of them when
// the stroke closes the shape); otherwise that shape is ended and the
// stroke starts a new one.  A new shape then looks at the shapes on the
// page for its ends to snap to, keeping the ones it meets.  The shape is
// ended at once when it is closed, when both its ends meet something, or
// when it has eight strokes.  ==> whether it went.
long
TGeneralShapeDomain::Group(TUnit* unit, dInfoRec* /*info*/)
{
	TGeneralShapeUnit* made = nil;			// r5: a new unit, to be announced
	TUnitList* context = nil;				// r8
	long failed = 0;
	TStrokeUnit* stroke = (TStrokeUnit*) unit;
	long closed = CheckClosed(stroke);
	TUnitList* delayed = fController->GetDelayList(this, kShapeUnit);
	TGeneralShapeUnit* shape = nil;			// r4
	if (delayed == nil)
		failed = 1;
	else
	{
		if (delayed->Count() != 0)
		{
			shape = (TGeneralShapeUnit*) delayed->GetUnit(0);
			long joins = 0;
			if (closed == 0)
			{
				FPoint* ends[2];
				ExtractEnds(stroke, nil, ends);
				joins = CheckConnect(1, ends, nil, shape);
			}
			if (joins == 0)
			{
				shape->EndSubs();
				shape = nil;
			}
			else
			{
				if (shape->AddSub(unit) == -1)
				{
					delayed->Dispose();
					return 0;
				}
				// both ends joined: the shape is closed
				closed = (joins == 2);
				for (long i = 0; i < 2; i++)
				{
					ShapeEnd* end = &shape->fGroupInfo->fEnds[i];
					if (end->fUnit != nil && (closed || end->fKind == -2))
					{
						PurgeDeep((TSIUnit*) end->fUnit);
						end->fUnit = nil;
						end->fKind = -1;
						shape->fGroupInfo->fConnections--;
					}
				}
			}
			for (ULong i = 1; i < (ULong) delayed->Count(); i++)
				((TSIUnit*) delayed->GetUnit(i))->EndSubs();
		}
		delayed->Dispose();

		if (closed == 0 && (context = GetContextUnits(unit, 0)) != nil)
		{
			if (shape == nil)
			{
				TAreaList* areas = unit->GetAreas();
				shape = TGeneralShapeUnit::Make(this, unit->fKind + 1, (TArray*) areas);
				if (areas != nil)
					areas->Dispose();
				if (shape == nil || shape->AddSub(unit) == -1)
				{
					DisposeContextUnits(context);
					return 0;
				}
				shape->NewInterpretation(nil);
				made = shape;
			}
			FPoint* ends[2];
			ExtractEnds(nil, shape, ends);
			long keep[2] = { -1, -1 };
			TArrayIterator iter;
			TGeneralShapeUnit** entry = (TGeneralShapeUnit**) context->GetIterator(&iter);
			for (long i = 0; i < iter.fCount; i++)
			{
				TGeneralShapeUnit* other = *entry;
				long type = other->GetLabel(0);
				if (type == 5 || type == 7 || type == 8 || type == 13)
				{
					other->GetSub(0);
					CheckConnect(2, ends, shape, other);
				}
				CheckPtOnShape(ends, shape, other);
				CheckPtOnCircle(ends, shape, other);
				for (long e = 0; e < 2; e++)
					if (shape->fGroupInfo->fEnds[e].fUnit == other)
						keep[e] = i;
				entry = (TGeneralShapeUnit**) iter.GetNext();
			}
			// the shapes met are taken off the list, so they live on in the
			// shape's ends (the later index first, and the same one once)
			long connections = shape->fGroupInfo->fConnections;
			if (connections != 0)
			{
				if (connections == 2)
				{
					if (keep[0] < keep[1])
					{
						long t = keep[0];
						keep[0] = keep[1];
						keep[1] = t;
					}
					else if (keep[1] == keep[0])
						keep[1] = -1;
				}
				for (long e = 0; e < 2; e++)
					if (keep[e] >= 0)
						context->Delete((ULong) keep[e]);
			}
			DisposeContextUnits(context);
			context = nil;
		}

		if (shape == nil)
		{
			TAreaList* areas = unit->GetAreas();
			shape = TGeneralShapeUnit::Make(this, unit->fKind + 1, (TArray*) areas);
			if (areas != nil)
				areas->Dispose();
			if (shape == nil || shape->AddSub(unit) == -1)
				return 0;
			shape->NewInterpretation(nil);
			made = shape;
		}
		if (made != nil)
		{
			made->SetLabel(0, kShapeGrouping);
			fController->NewGroup(made);
		}
		ULong subs = (ULong) shape->SubCount();
		if (closed != 0 || shape->fGroupInfo->fConnections == 2 || subs > 7)
		{
			shape->EndSubs();
			shape->SetScore(0, 1000);
			if (closed != 0)
				shape->SetLabel(0, kShapeClosedCurve);
		}
		ShapeGroupInfo* info = shape->fGroupInfo;
		if (info->fConnections > 0)
		{
			long dist;
			if (info->fConnections == 2)
				dist = (info->fEnds[0].fDist < info->fEnds[1].fDist) ? info->fEnds[0].fDist : info->fEnds[1].fDist;
			else if (info->fEnds[0].fUnit == nil)
				dist = info->fEnds[1].fDist;
			else
				dist = info->fEnds[0].fDist;
			shape->fSnapped = 1;
			shape->fSnapDist = dist;
		}
	}
	if (context != nil)
		DisposeContextUnits(context);
	return failed == 0;
}


// ROM 0x002113f0 Classify__19TGeneralShapeDomainFP5TUnit
// A finished shape fitted and tidied.  The key points come first
// (FindKeyPoints), which say what the shape is; a curve, or a closed
// curve, may yet turn out to be an ellipse (FindEllipses); anything else
// with corners is tidied by its symmetries - equations written for what it
// looks like it should be, solved, and the answers put back
// (FindEquations, SolveEquations, PlugNewVals).  Two answers of the
// solver mean the shape is really something else: 0x80000000 a type 10,
// 0x40000000 a type 11, each with no angle.  A score better than 300
// counts only for a type 4; anything else not that good is 10000.
// Finally the shape is snapped onto whatever its ends met on the page
// (SnapPtToLC), or its trends evened out (GlobalTrends), and the
// grouping state's first byte left as whether it moved.
void
TGeneralShapeDomain::Classify(TUnit* unit)
{
	TGeneralShapeUnit* shape = (TGeneralShapeUnit*) unit;
	CheckScreenGlobals();
	long type = shape->GetLabel(0);
	ULong score = 10000;
	// DEVIATION: the ROM leaves the angle as whatever was on its stack
	// until FindEquations or FindEllipses sets it, and a type 4 or 5 the
	// equations do not set it for is given that; the host starts it at
	// nought.
	long angle = 0;
	long values[75];
	values[0] = 0;
	FindKeyPoints(shape, &type, &score);
	if (type != kShapeNone)
	{
		EqSystem system;
		system.fCount = 0;
		Boolean ellipses = false;
		if (type != kShapeNothing)
		{
			if (type == kShapeCurve)
				ellipses = gGSClosed != 0;
			else
			{
				Boolean solvable = false;
				if (gSymmetryFlag)
					solvable = FindEquations(shape, values, &system, &type, &score, &angle);
				if (type == kShapeClosedCurve)
					ellipses = true;
				else if (type == kShapeCurve)
					ellipses = gGSClosed != 0;
				else if (type != kShapeNothing && type != kShapeGrouping
					  && solvable && SolveEquations(&system, values))
					PlugNewVals(shape, values, &system);
			}
		}
		if (ellipses && gCurveFlag)
		{
			long eType;
			ULong eScore;
			long eAngle;
			if (FindEllipses(shape, &eType, &eScore, &eAngle))
			{
				type = eType;
				score = (eScore <= 300) ? 300 : eScore;
				angle = eAngle;
			}
		}
		ReleaseEqs(&system);
	}
	if (values[0] == (long) 0x80000000)
	{
		type = 10;
		angle = 0;
	}
	else if (values[0] == 0x40000000)
	{
		type = 11;
		angle = 0;
	}
	if (score < 300 && type != kShapeClosed)
		score = 10000;
	shape->SetLabel(0, (ULong) type);
	shape->SetScore(0, score);
	shape->SetAngle(0, angle);
	long snapped = 0;
	if (type != kShapeNothing && type != kShapeNone && type != kShapeCurve)
	{
		if (shape->fGroupInfo->fConnections != 0)
		{
			SnapPtToLC(shape);
			snapped = 1;
		}
		else
			GlobalTrends(shape, &snapped);
	}
	shape->fGroupInfo->fOrder[0] = 0;
	if (snapped != 0)
		shape->fGroupInfo->fOrder[0] = 0xff;
	shape->EndUnit();
	fController->NewClassification(unit);
}


/*------------------------------------------------------------------------------
	T h e   r e c o g n i s e r
------------------------------------------------------------------------------*/

// ROM 0x00144530 HandleUnit__16TShapeRecognizerFP11TUnitPublic
ULong
TShapeRecognizer::HandleUnit(TUnitPublic* unit)
{
	ULong command = Command();
	ULong type = unit->ShapeType();
	if (type == kShapeNone || type == kShapeNothing)
		command = 0;
	return command;
}


// ROM 0x0014456c InstallShapeRecognizer__FP19TRecognitionManager
// The shape domain made, and its units answered with aeShape (0x11) once
// the recognition timeout has passed.  Its service is vShapesAllowed.
void
InstallShapeRecognizer(TRecognitionManager* manager)
{
	TGeneralShapeDomain* domain = TGeneralShapeDomain::Make(manager->fController);
	TRecognizer* recognizer = new TShapeRecognizer;
	recognizer->Init(domain, domain->fType, 0x11, kRecognizerIsWriting, 1);
	recognizer->InitServices(0x10000, 0x10000);
	*(TRecognizer**) manager->fRecognizers->AddEntry() = recognizer;
}


/*------------------------------------------------------------------------------
	S h a p e s   o n   t h e   p a g e
------------------------------------------------------------------------------*/

// ROM 0x002230e8 SetTabPt__FP8SamplePtP5TabPt
// A tablet point packed: x and y to eighths of a pixel (nothing
// negative), the pressure's top two bits over x's and its bottom bit over
// y's, and the flag's second bit over y's top.
void
SetTabPt(SamplePt* sample, TabPt* pt)
{
	Fixed x = (pt->x < 1) ? 0 : pt->x;
	sample->fX = (UShort) ((sample->fX & 0xc000) | ((x >> 13) & 0x3fff));
	Fixed y = (pt->y < 1) ? 0 : pt->y;
	sample->fY = (UShort) ((sample->fY & 0xc000) | ((y >> 13) & 0x3fff));
	ULong z = (ULong) pt->z;
	if ((long) z > 6)
		z = 7;
	sample->fX = (UShort) ((sample->fX & 0x3fff) | ((z & 6) << 13));
	sample->fY = (UShort) ((sample->fY & 0x3fff) | ((z & 1) << 14));
	sample->fY = (UShort) ((sample->fY & 0x7fff) | ((pt->p & 2) << 14));
}


// ROM 0x00145d18 PrepStrokeForRecognition__FP7TStroke
// A stroke made rather than written, made to look written: its box, a
// down and up two ticks ago and now, marked done and compacted (and its
// box a fixed unit bigger again).
void
PrepStrokeForRecognition(TStroke* stroke)
{
	stroke->UpdateBBox();
	ULong now = GetTicks();
	stroke->fUpTime = now;
	stroke->fDownTime = now - 2;
	stroke->SetFlags(kStrokeDone);
	stroke->Compact();
	stroke->fBBox.right++;
	stroke->fBBox.bottom++;
}


// ROM 0x00145d48 MakeStroke__FP6TPointl6TPoint
// A stroke through the points, moved by the offset, at pressure 1.
TStroke*
MakeStroke(const Point* pts, long count, Point offset)
{
	TStroke* stroke = TStroke::Make((ULong) count);
	if (stroke != nil)
	{
		for (long i = 0; i < count; i++)
		{
			TabPt pt;
			pt.x = (Fixed) ((ULong) (UShort) (pts[i].h + offset.h) << 16);
			pt.y = (Fixed) ((ULong) (UShort) (pts[i].v + offset.v) << 16);
			pt.z = 1;
			pt.p = 0;
			SetTabPt(stroke->GetPoint(i), &pt);
		}
	}
	return stroke;
}


// ROM 0x00145c1c MakeStrokeUnit__FP7TStrokeP6TArrayl
// A stroke unit in the stroke domain over the stroke, with the context id,
// at the controller's next stroke number.
TStrokeUnit*
MakeStrokeUnit(TStroke* stroke, TArray* areas, long contextID)
{
	PrepStrokeForRecognition(stroke);
	TDomain* domain = gRecognition.fRecognizers->FindRecognizer(kStrokeUnit)->Domain();
	TStrokeUnit* unit = TStrokeUnit::Make(domain, 2, stroke, areas);
	if (unit != nil)
	{
		unit->SetContextID((ULong) contextID);
		unit->fMinStroke = gController->fNextStroke;
		unit->fMaxStroke = gController->fNextStroke;
	}
	return unit;
}


// ROM 0x00145cc8 MakeStrokeUnit__FP6TPointlP6TArray6TPointT2
TStrokeUnit*
MakeStrokeUnit(const Point* pts, long count, TArray* areas, Point offset, long contextID)
{
	TStrokeUnit* unit = nil;
	TStroke* stroke = MakeStroke(pts, count, offset);
	if (stroke != nil && (unit = MakeStrokeUnit(stroke, areas, contextID)) == nil)
		stroke->Dispose();
	return unit;
}


// ROM 0x001445fc MakeGeneralShape__FP11TUnitPublicP12PolygonShapeRC5TRectl
TGeneralShapeUnit*
MakeGeneralShape(TUnitPublic* /*unit*/, PolygonShape* shape, const Rect& box, long contextID)
{
	Point offset;
	offset.v = box.top;
	offset.h = box.left;
	TStrokeUnit* stroke = MakeStrokeUnit(shape->fPoints, shape->fCount, nil, offset, 0);
	if (stroke == nil)
		return nil;
	stroke->SetFlags(0x100000);
	TDomain* domain = gRecognition.fRecognizers->FindRecognizer(kShapeUnit)->Domain();
	TGeneralShapeUnit* unit = TGeneralShapeUnit::Make(domain, 0, nil);
	if (unit == nil)
	{
		stroke->Dispose();
		return nil;
	}
	unit->SetContextID((ULong) contextID);
	unit->AddSub(stroke);
	unit->SetFlags(0x100000);
	unit->NewInterpretation(nil);
	long type = shape->fVerb;
	if (type == 1 || type == 6)
		type = kShapeClosed;
	else if (type == 2 || type == 7)
		type = kShapeOpen;
	unit->SetLabel(0, (ULong) type);
	if (shape->fVerb == kShapeCircle)
	{
		// a circle: its centre, and its radius as the box's half height
		Point mid = MidPoint(box);
		ShapeInterpretation* interp = unit->Interpretation();
		interp->fParams[0] = (Fixed) ((ULong) (UShort) mid.h << 16);
		interp->fParams[1] = (Fixed) ((ULong) (UShort) mid.v << 16);
		interp->fParams[2] = (Fixed) ((ULong) (UShort) (mid.v - box.top) << 16);
	}
	return unit;
}
