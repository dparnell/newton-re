// ShapeDomain test: the shape domain's units and grouping, over the
// standalone heap.  No OS is booted, so CheckScreenGlobals would find no
// screen; the distances are set here as a 72 dpi screen gives them (every
// scale one) and the sample counts as 80 samples a second gives them.

#include "ShapeDomain.h"
#include "ShapeGeometry.h"
#include "Controller.h"
#include "Domain.h"
#include "Unit.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "memory/host/KernelHeap.h"
#include "FixedMath.h"
#include "RecObject.h"
#include "NewtonMemory.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


static void
SetDistances(void)
{
	gPixMaxClosedDist = F(10);
	gPixMaxConnectDist = F(20);
	gPixMinConnectDist = F(5);
	gPixLargeInitialValue = F(50);
	gSmpMinClosedShapePts = 7;
	gSmpMinSmallDistRun = 3;
}

// What CheckScreenGlobals works out for a 100 dpi screen sampled 80 times
// a second (it asks the name server, which this test does not start).
static void
SetScreenDistances(void)
{
	Fixed scale = FixedDivide(F(100), F(72));
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
	gSmpMinClosedShapePts = 7;
	gSmpMinSmallDistRun = 3;
	gGSScreenRect.left = -gPixScreenRectInset;
	gGSScreenRect.top = -gPixScreenRectInset;
	gGSScreenRect.right = F(320) + gPixScreenRectInset;
	gGSScreenRect.bottom = F(480) + gPixScreenRectInset;
}

// A stroke of points spaced about 2 pixels apart along a polyline.
static TStroke*
PolylineStroke(const long* corners, long count)
{
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	for (long c = 0; c + 1 < count; c++)
	{
		long x0 = corners[2 * c], y0 = corners[2 * c + 1];
		long x1 = corners[2 * c + 2], y1 = corners[2 * c + 3];
		long dx = x1 - x0, dy = y1 - y0;
		long len = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
		long steps = len / 2;
		for (long k = 0; k < steps; k++)
		{
			pt.x = F(x0) + (Fixed) (((long long) F(dx) * k) / steps);
			pt.y = F(y0) + (Fixed) (((long long) F(dy) * k) / steps);
			stroke->AddPoint(&pt);
		}
	}
	pt.x = F(corners[2 * count - 2]);
	pt.y = F(corners[2 * count - 1]);
	stroke->AddPoint(&pt);
	stroke->fDownTime = 1000;
	stroke->fUpTime = 1010;
	stroke->EndStroke();
	stroke->UpdateBBox();
	return stroke;
}

// A shape unit of one stroke through the corners, fitted.
static TGeneralShapeUnit*
Fitted(TDomain* domain, const long* corners, long count, long* type, ULong* score)
{
	TGeneralShapeUnit* unit = TGeneralShapeUnit::Make(domain, 3, nil);
	TStrokeUnit* stroke = TStrokeUnit::Make(domain, 2, PolylineStroke(corners, count), nil);
	unit->AddSub(stroke);
	unit->fGroupInfo->fOrder[0] = 0;
	FindKeyPoints(unit, type, score);
	return unit;
}

static void
DumpShape(TGeneralShapeUnit* unit)
{
	TDArray* shape = unit->GetGeneralShape();
	if (shape == nil)
		return;
	for (long i = 0; i < shape->Count(); i++)
	{
		GeneralPt* p = (GeneralPt*) shape->GetEntry(i);
		printf("    %2ld: %7.2f %7.2f%s %d %d\n", i, p->fPt.x / 65536.0, p->fPt.y / 65536.0,
			   p->fControl ? " (control)" : "", p->f09, p->f0a);
	}
}

// The key points of a line, an L and a closed triangle.
static void
TestKeyPoints(TDomain* domain)
{
	SetScreenDistances();
	static const long line[] = { 50, 50, 150, 60 };
	long type = kShapeGrouping;
	ULong score = 0;
	TGeneralShapeUnit* unit = Fitted(domain, line, 2, &type, &score);
	printf("  line: type %ld score %lu\n", type, (unsigned long) score);
	DumpShape(unit);
	TDArray* shape = unit->GetGeneralShape();
	EXPECT(type == kShapeGrouping && score == 1000);
	EXPECT(shape != nil && shape->Count() == 2);
	if (shape != nil && shape->Count() == 2)
	{
		GeneralPt* a = (GeneralPt*) shape->GetEntry(0);
		GeneralPt* b = (GeneralPt*) shape->GetEntry(1);
		EXPECT(a->fPt.x == F(50) && a->fPt.y == F(50) && b->fPt.x == F(150) && b->fPt.y == F(60));
	}
	unit->Dispose();

	static const long ell[] = { 50, 50, 50, 150, 130, 150 };
	type = kShapeGrouping;
	unit = Fitted(domain, ell, 3, &type, &score);
	printf("  L: type %ld score %lu\n", type, (unsigned long) score);
	DumpShape(unit);
	shape = unit->GetGeneralShape();
	EXPECT(shape != nil && shape->Count() == 3);
	if (shape != nil && shape->Count() == 3)
	{
		GeneralPt* corner = (GeneralPt*) shape->GetEntry(1);
		EXPECT(corner->fPt.x == F(50) && corner->fPt.y == F(150) && corner->fControl == 0);
	}
	unit->Dispose();

	static const long triangle[] = { 100, 50, 160, 150, 40, 150, 100, 52 };
	type = kShapeClosedCurve;			// Group marks a stroke that closes on itself so
	unit = Fitted(domain, triangle, 4, &type, &score);
	printf("  triangle: type %ld score %lu\n", type, (unsigned long) score);
	DumpShape(unit);
	shape = unit->GetGeneralShape();
	EXPECT(type != kShapeNothing);
	EXPECT(shape != nil && shape->Count() == 4);		// three corners and back to the first
	unit->Dispose();

	// half a circle: a curve, so its points include control points
	long arc[2 * 13];
	static const short c[] = { 100, 97, 87, 71, 50, 26, 0, -26, -50, -71, -87, -97, -100 };
	static const short sn[] = { 0, 26, 50, 71, 87, 97, 100, 97, 87, 71, 50, 26, 0 };
	for (long k = 0; k < 13; k++)
	{
		arc[2 * k] = 150 + (60 * c[k]) / 100;
		arc[2 * k + 1] = 200 - (60 * sn[k]) / 100;
	}
	type = kShapeGrouping;
	unit = Fitted(domain, arc, 13, &type, &score);
	printf("  arc: type %ld score %lu\n", type, (unsigned long) score);
	DumpShape(unit);
	shape = unit->GetGeneralShape();
	long controls = 0;
	for (long i = 0; shape != nil && i < shape->Count(); i++)
		if (((GeneralPt*) shape->GetEntry(i))->fControl)
			controls++;
	EXPECT(type != kShapeNothing && controls > 0);
	unit->Dispose();
}

static TStroke*
StrokeOf(const long* xy, long count)
{
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	for (long i = 0; i < count; i++)
	{
		pt.x = F(xy[2 * i]);
		pt.y = F(xy[2 * i + 1]);
		stroke->AddPoint(&pt);
	}
	stroke->fDownTime = 1000;
	stroke->fUpTime = 1010;
	stroke->EndStroke();
	stroke->UpdateBBox();
	return stroke;
}


// PtOnLine2: near the start, near the end, on the line between, or not.
static void
TestPtOnLine(void)
{
	FPoint a = { F(10), F(10) };
	FPoint b = { F(50), F(10) };
	long dist = -1;
	FPoint p = { F(11), F(12) };
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 1 && dist == F(2));
	p.x = F(49);
	p.y = F(9);
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 2 && dist == F(1));
	p.x = F(30);
	p.y = F(13);
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 3 && dist == F(3));
	p.y = F(20);
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 0);			// too far off the line
	p.x = F(60);
	p.y = F(10);
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 0);			// past the end
	p.x = F(8);
	EXPECT(PtOnLine2(&a, &b, &p, 4, &dist) == 1 && dist == F(2));	// a little before the start
}


// A stroke closes when its ends are within a fifth of its size (held
// between the connect distances) and it has enough points.
static void
TestClosed(TDomain* domain)
{
	static const long box[] = { 10, 10, 40, 10, 40, 40, 10, 40, 10, 12, 11, 11, 11, 10 };
	TStrokeUnit* closed = TStrokeUnit::Make(domain, 2, StrokeOf(box, 7), nil);
	EXPECT(CloseDelta(closed) == F(6));				// a fifth of 30
	EXPECT(CheckClosed(closed));
	static const long line[] = { 10, 10, 20, 12, 30, 14, 40, 16, 50, 18, 60, 20, 70, 22 };
	TStrokeUnit* open = TStrokeUnit::Make(domain, 2, StrokeOf(line, 7), nil);
	EXPECT(CloseDelta(open) == F(12));
	EXPECT(!CheckClosed(open));
	static const long few[] = { 10, 10, 40, 10, 10, 11 };
	TStrokeUnit* tooFew = TStrokeUnit::Make(domain, 2, StrokeOf(few, 3), nil);
	EXPECT(!CheckClosed(tooFew));					// three points are not a shape
	closed->Dispose();
	open->Dispose();
	tooFew->Dispose();
}


// The unit's one interpretation, and a shape drawn out as a stroke: two
// corners, then a curve through a control point to a third.
static void
TestUnit(TDomain* domain)
{
	TGeneralShapeUnit* unit = TGeneralShapeUnit::Make(domain, 3, nil);
	EXPECT(unit != nil && unit->fType == kShapeUnit);
	EXPECT(unit->InterpretationCount() == 1 && unit->GetInterpretation(1) == nil);
	EXPECT(unit->fGroupInfo != nil && unit->fGroupInfo->fConnections == 0);
	EXPECT(unit->fGroupInfo->fEnds[0].fKind == -1 && unit->fGroupInfo->fEnds[1].fUnit == nil);

	TDArray* shape = TDArray::Make(sizeof(GeneralPt), 0);
	GeneralPt pt;
	memset(&pt, 0, sizeof(pt));
	pt.fPt.x = F(10); pt.fPt.y = F(10);
	*(GeneralPt*) shape->AddEntry() = pt;
	pt.fPt.x = F(40);
	*(GeneralPt*) shape->AddEntry() = pt;
	pt.fPt.y = F(40); pt.fControl = 1;			// the control point
	*(GeneralPt*) shape->AddEntry() = pt;
	pt.fPt.x = F(10); pt.fControl = 0;
	*(GeneralPt*) shape->AddEntry() = pt;
	unit->SetGeneralShape(shape);
	EXPECT(unit->GetGeneralShape() == shape);
	unit->SetLabel(0, kShapeOpen);
	EXPECT(unit->GetLabel(0) == kShapeOpen);
	TStroke* stroke = unit->GetGSAsStroke();
	EXPECT(stroke != nil);
	if (stroke != nil)
	{
		// 2 corners, then the curve (from 40,10 through 40,40 to 10,40:
		// 30 apart in each direction, so 8 points)
		EXPECT(stroke->Count() == 2 + 8);
		FPoint p;
		stroke->GetFPoint(0, &p);
		EXPECT(p.x == F(10) && p.y == F(10));
		stroke->GetFPoint(9, &p);
		EXPECT(p.x == F(10) && p.y == F(40));		// the curve ends at its end
		stroke->GetFPoint(5, &p);
		EXPECT(p.x > F(31) && p.x < F(33) && p.y > F(31) && p.y < F(33));	// its middle, a quarter of the way in
		stroke->Dispose();
	}
	long average = GetAvgLength(unit);
	EXPECT(average == 30);				// three sides of 30 (the control point counts as a corner here)
	if (average != 30)
		fprintf(stderr, "average length %ld\n", average);

	// a circle: 25 points round its centre
	unit->SetLabel(0, kShapeCircle);
	unit->Interpretation()->fParams[0] = F(100);
	unit->Interpretation()->fParams[1] = F(100);
	unit->Interpretation()->fParams[2] = F(20);
	stroke = unit->GetGSAsStroke();
	EXPECT(stroke != nil && stroke->Count() == 25);
	if (stroke != nil)
	{
		FPoint p;
		stroke->GetFPoint(0, &p);
		EXPECT(p.x >= F(119) && p.x <= F(121) && p.y >= F(99) && p.y <= F(101));	// at 0 degrees: to the right
		stroke->GetFPoint(6, &p);
		EXPECT(p.x >= F(99) && p.x <= F(101) && p.y >= F(79) && p.y <= F(81));		// at 90: up
		stroke->Dispose();
	}
	unit->Dispose();
}


// Two strokes: the second's start meets the first's end, so it joins
// following on; one whose end meets the first's start joins in front.
static void
TestConnect(TDomain* domain)
{
	static const long first[] = { 10, 10, 20, 10, 30, 10, 40, 10 };
	static const long follows[] = { 41, 11, 41, 20, 41, 30, 41, 40 };
	TGeneralShapeUnit* shape = TGeneralShapeUnit::Make(domain, 3, nil);
	TStrokeUnit* one = TStrokeUnit::Make(domain, 2, StrokeOf(first, 4), nil);
	shape->AddSub(one);
	shape->fGroupInfo->fOrder[0] = 0;
	TStrokeUnit* two = TStrokeUnit::Make(domain, 2, StrokeOf(follows, 4), nil);
	FPoint* ends[2];
	ExtractEnds(two, nil, ends);
	EXPECT(ends[0]->x == F(41) && ends[1]->y == F(40));
	EXPECT(CheckConnect(0, ends, nil, shape) == 1);	// only asked: nothing recorded
	EXPECT(CheckConnect(1, ends, nil, shape) == 1);
	EXPECT(shape->fGroupInfo->fOrder[1] == 1 && shape->fGroupInfo->fReversed[1] == 0);

	// one that ends at the first stroke's start goes in front of it
	static const long leads[] = { 10, 40, 10, 30, 10, 20, 10, 11 };
	TGeneralShapeUnit* other = TGeneralShapeUnit::Make(domain, 3, nil);
	other->AddSub(TStrokeUnit::Make(domain, 2, StrokeOf(first, 4), nil));
	TStrokeUnit* three = TStrokeUnit::Make(domain, 2, StrokeOf(leads, 4), nil);
	ExtractEnds(three, nil, ends);
	EXPECT(CheckConnect(1, ends, nil, other) == 1);
	EXPECT(other->fGroupInfo->fOrder[0] == 1 && other->fGroupInfo->fOrder[1] == 0);

	// far away: nothing
	static const long away[] = { 100, 100, 120, 100 };
	TStrokeUnit* four = TStrokeUnit::Make(domain, 2, StrokeOf(away, 2), nil);
	ExtractEnds(four, nil, ends);
	EXPECT(CheckConnect(0, ends, nil, shape) == 0);

	shape->Dispose();
	other->Dispose();
	two->Dispose();
	three->Dispose();
	four->Dispose();
}


// A stroke round an ellipse (rx, ry about the centre, turned through
// `turn` degrees), drawn from the right going anticlockwise on the screen
// and back to just short of where it started.
static TStroke*
EllipseStroke(double cx, double cy, double rx, double ry, double turn)
{
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	double t = turn * 3.14159265358979 / 180.0;
	for (long k = 0; k <= 72; k++)
	{
		double a = k * 2 * 3.14159265358979 / 72 * 0.99;
		double x = rx * cos(a), y = -ry * sin(a);
		pt.x = (Fixed) ((cx + x * cos(t) - y * sin(t)) * 65536.0);
		pt.y = (Fixed) ((cy + x * sin(t) + y * cos(t)) * 65536.0);
		stroke->AddPoint(&pt);
	}
	stroke->fDownTime = 1000;
	stroke->fUpTime = 1010;
	stroke->EndStroke();
	stroke->UpdateBBox();
	return stroke;
}

static TGeneralShapeUnit*
Round(TDomain* domain, TStroke* stroke, long* type, ULong* score, long* angle, Boolean* found)
{
	TGeneralShapeUnit* unit = TGeneralShapeUnit::Make(domain, 3, nil);
	TStrokeUnit* sub = TStrokeUnit::Make(domain, 2, stroke, nil);
	unit->AddSub(sub);
	unit->fGroupInfo->fOrder[0] = 0;
	*type = kShapeClosedCurve;
	FindKeyPoints(unit, type, score);
	*found = FindEllipses(unit, type, score, angle);
	return unit;
}

// A drawn circle is found to be a circle, and a drawn ellipse an ellipse.
static void
TestEllipses(TDomain* domain)
{
	SetScreenDistances();
	long type, angle;
	ULong score;
	Boolean found;
	TGeneralShapeUnit* unit = Round(domain, EllipseStroke(160, 200, 50, 50, 0), &type, &score, &angle, &found);
	ShapeInterpretation* interp = unit->Interpretation();
	printf("  circle: found %d type %ld score %lu centre %.1f %.1f radius %.1f\n", found, type,
		   (unsigned long) score, interp->fParams[0] / 65536.0, interp->fParams[1] / 65536.0, interp->fParams[2] / 65536.0);
	EXPECT(found && type == kShapeCircle);
	EXPECT(interp->fParams[2] > F(45) && interp->fParams[2] < F(55));
	EXPECT(unit->GetGeneralShape() == nil);		// drawn from its numbers now
	unit->SetLabel(0, (ULong) type);		// (as Classify does)
	TStroke* drawn = unit->GetGSAsStroke();
	EXPECT(drawn != nil && drawn->Count() == 25);
	if (drawn != nil)
		drawn->Dispose();
	unit->Dispose();

	unit = Round(domain, EllipseStroke(160, 200, 80, 35, 0), &type, &score, &angle, &found);
	interp = unit->Interpretation();
	printf("  ellipse: found %d type %ld score %lu centre %.1f %.1f radii %.1f %.1f angle %.1f\n", found, type,
		   (unsigned long) score, interp->fParams[0] / 65536.0, interp->fParams[1] / 65536.0,
		   interp->fParams[2] / 65536.0, interp->fParams[3] / 65536.0, interp->fParams[4] / 65536.0);
	EXPECT(found && type == kShapeEllipse);
	unit->Dispose();
}


// One equation: coefficient c on variable v, nought everywhere else.
static void
AddEquation(EqSystem* system, long v, Fixed c)
{
	Equation* eq = &system->fEqs[system->fCount++];
	eq->fN = system->fN;
	eq->fKind = 0;
	eq->fCoeffs = MakeHandle(0x94);
	memset(*eq->fCoeffs, 0, 0x94);
	((Fixed*) *eq->fCoeffs)[v] = c;
}

// The solver: two edges drawn nearly level and nearly upright, asked to be
// exactly that, come out level and upright and still covering the box
// they covered.
static void
TestSolver(void)
{
	EqSystem system;
	system.fN = 4;
	system.fCount = 0;
	AddEquation(&system, 2, F(1));		// the first edge's dy is nought
	AddEquation(&system, 3, F(1));		// the second edge's dx is nought
	long values[75];
	values[0] = 0;
	values[1] = F(100); values[2] = F(8);
	values[3] = F(6); values[4] = F(80);
	Boolean solved = SolveEquations(&system, values);
	printf("  solver: %d  (%.2f %.2f) (%.2f %.2f)\n", solved, values[1] / 65536.0, values[2] / 65536.0,
		   values[3] / 65536.0, values[4] / 65536.0);
	EXPECT(solved);
	EXPECT(values[2] > -F(1) && values[2] < F(1));
	EXPECT(values[3] > -F(1) && values[3] < F(1));
	EXPECT(values[1] > F(104) && values[1] < F(108));	// the box was 106 across
	EXPECT(values[4] > F(86) && values[4] < F(90));		// and 88 down
	ReleaseEqs(&system);

	// Minimize alone: (x1 - 3)^2 + (x2 + 2)^2 as a quadratic over [1, x1, x2]
	MixFunc saved = currFunction;
	Bilinear scratch;
	EXPECT(!InitFunction(2, &currFunction, &scratch));
	Fixed* r0 = (Fixed*) *currFunction.fQuad.fRows[0];
	Fixed* r1 = (Fixed*) *currFunction.fQuad.fRows[1];
	Fixed* r2 = (Fixed*) *currFunction.fQuad.fRows[2];
	r0[0] = F(13); r0[1] = -F(6); r0[2] = F(4);
	r1[1] = F(1); r2[2] = F(1);
	currGradient = (MixGradEl*) NewPtr(sizeof(MixGradEl) * 37);
	InitGradient(currGradient);
	EXPECT(!FindGradient(&currFunction, currGradient));
	long x[3] = { 0, 0, 0 };
	long iterations, least;
	EXPECT(Minimize(x, 2, 0x200, &iterations, &least, TheFunction, TheGradient));
	printf("  minimize: %ld iterations, least %.4f at %.3f %.3f\n", iterations, least / 65536.0, x[1] / 65536.0, x[2] / 65536.0);
	EXPECT(x[1] > F(3) - 0x2000 && x[1] < F(3) + 0x2000);
	EXPECT(x[2] > -F(2) - 0x2000 && x[2] < -F(2) + 0x2000);
	ReleaseGradient(currGradient);
	DisposPtr((Ptr) currGradient);
	currGradient = nil;
	ReleaseBilin(&scratch);
	ReleaseBilin(&currFunction.fQuad);
	currFunction = saved;
}


// A trend of angles: the values near 0 and the values near 90 each come
// together into a cluster, and a value between is in neither.
static void
TestTrend(void)
{
	TTrend* trend = TTrend::Make(7);
	EXPECT(trend != nil);
	long angles[] = { 0, 90, 3, 88, 92, 1, 45 };
	for (unsigned k = 0; k < sizeof(angles) / sizeof(angles[0]); k++)
		EXPECT(!trend->AddToTrend(angles[k], nil, 1));
	printf("  trend: %ld clusters:", trend->Count());
	for (long i = 0; i < trend->Count(); i++)
		printf(" [%ld..%ld mean %ld n %ld]", trend->At(i)->fMin, trend->At(i)->fMax, trend->At(i)->fMean, trend->At(i)->fCount);
	printf(" spread %.3f\n", trend->fSpread / 65536.0);
	EXPECT(trend->Count() == 3);
	EXPECT(trend->FindCluster(2) == 0);
	EXPECT(trend->FindCluster(45) == 1);
	EXPECT(trend->FindCluster(91) == 2);
	EXPECT(trend->FindCluster(60) == -1);
	long found;
	trend->AddToTrend(89, &found, 0);		// looked up only
	EXPECT(found == 90);					// the value the cluster started with
	EXPECT(trend->At(2)->fCount == 3);
	trend->Dispose();
}


// The tidying Classify does after FindKeyPoints: the equations found,
// solved and put back.  values[0] is what PlugNewVals says of a
// four-sided shape.
static void
Tidied(TDomain* domain, const char* name, const long* corners, long count, long startType,
	   long* type, long* angle, long* values, TGeneralShapeUnit** result)
{
	ULong score = 0;
	*type = startType;
	TGeneralShapeUnit* unit = Fitted(domain, corners, count, type, &score);
	values[0] = 0;
	*angle = 0;
	EqSystem system;
	system.fCount = 0;
	Boolean solvable = FindEquations(unit, values, &system, type, &score, angle);
	Boolean solved = false;
	if (solvable)
	{
		solved = SolveEquations(&system, values);
		if (solved)
			PlugNewVals(unit, values, &system);
	}
	ReleaseEqs(&system);
	printf("  %s: type %ld score %lu angle %.1f solvable %d solved %d values[0] %lx\n", name, *type,
		   (unsigned long) score, *angle / 65536.0, solvable, solved, (unsigned long) values[0]);
	DumpShape(unit);
	*result = unit;
}

static void
TestEquations(TDomain* domain)
{
	SetScreenDistances();
	long type, angle;
	long values[75];
	TGeneralShapeUnit* unit;

	static const long box[] = { 50, 50, 152, 53, 149, 131, 48, 128, 51, 52 };
	Tidied(domain, "box", box, 5, kShapeClosedCurve, &type, &angle, values, &unit);
	TDArray* shape = unit->GetGeneralShape();
	EXPECT(shape != nil && shape->Count() == 5);
	if (shape != nil && shape->Count() == 5)
	{
		// level and upright sides
		for (long k = 0; k < 4; k++)
		{
			GeneralPt* a = (GeneralPt*) shape->GetEntry(k);
			GeneralPt* b = (GeneralPt*) shape->GetEntry(k + 1);
			long dx = (b->fPt.x - a->fPt.x) >> 16, dy = (b->fPt.y - a->fPt.y) >> 16;
			EXPECT(dx == 0 || dy == 0);
		}
	}
	unit->Dispose();

	static const long line[] = { 50, 50, 150, 54 };
	Tidied(domain, "line", line, 2, kShapeGrouping, &type, &angle, values, &unit);
	EXPECT(type == kShapeLine);
	shape = unit->GetGeneralShape();
	if (shape != nil && shape->Count() == 2)
		EXPECT(((GeneralPt*) shape->GetEntry(1))->fPt.y == ((GeneralPt*) shape->GetEntry(0))->fPt.y);
	unit->Dispose();

	static const long triangle[] = { 100, 50, 160, 150, 40, 150, 100, 52 };
	Tidied(domain, "triangle", triangle, 4, kShapeClosedCurve, &type, &angle, values, &unit);
	EXPECT(type == kShapeTriangle);
	unit->Dispose();
}


int
main()
{
	InitHostStandaloneHeap();
	SetDistances();
	gController = TController::Make();
	// (not through Make, which asks the name server about the screen)
	TGeneralShapeDomain* domain = new TGeneralShapeDomain;
	domain->IDomain(gController, kShapeUnit, (char*) "GeneralShape Domain");
	domain->AddPieceType(kStrokeUnit);
	TestPtOnLine();
	TestClosed(domain);
	TestUnit(domain);
	TestConnect(domain);
	TestKeyPoints(domain);
	TestEllipses(domain);
	TestSolver();
	TestTrend();
	TestEquations(domain);
	if (failures != 0)
	{
		fprintf(stderr, "test_ShapeDomain: %d failures\n", failures);
		return 1;
	}
	printf("test_ShapeDomain: all tests passed\n");
	return 0;
}
