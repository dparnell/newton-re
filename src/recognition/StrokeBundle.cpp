/*
	File:		recognition/StrokeBundle.cpp

	Contains:	Stroke bundles - StrokeBundle.h.

				The points of a 'stroke binary are written and read a
				byte at a time, as the ROM writes them: they are
				big-endian halfwords wherever the bundle is kept, and a
				bundle can go into a soup.
*/

#include "StrokeBundle.h"
#include "Stroke.h"
#include "UnitPublic.h"
#include "Unit.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Rects.h"
#include "Shapes.h"			// MoveTo, LineTo
#include "Transform.h"
#include "NativeFunctions.h"
#include "Interpreter.h"	// ThrowMsg
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "Recognizer.h"	// gRecognition
#include "Controller.h"
#include "Polygons.h"
#include "View.h"		// FailGetView
#include "Ports.h"		// the pen


// (a box that holds nothing yet: the recogniser's mark is a top of
// -0x8000, which TRect::Union looks for)
static void
EmptyBounds(Rect* r)
{
	r->top = (short) 0x8000;
	r->bottom = (short) 0x8000;
}


// ROM 0x001975c0 Union__5TRectFRC5TRect
// A rectangle taken into another.  A destination that holds nothing yet
// simply becomes the source.
static void
UnionInto(Rect* r, const Rect& other)
{
	if (r->top == (short) 0x8000)
	{
		*r = other;
		return;
	}
	UnionRect(&other, r, r);
}


// (the two halfwords of a stored point, big-endian)
static short
PointHalf(const UByte* p)
{
	return (short) ((p[0] << 8) | p[1]);
}

static void
SetPointHalf(UByte* p, long value)
{
	p[0] = (UByte) (value >> 8);
	p[1] = (UByte) value;
}

// (eighths of a pixel brought to whole pixels, the ROM's rounding)
static long
ToPixels(long eighths)
{
	return (eighths + 4) >> 3;
}


/*------------------------------------------------------------------------------
	A   l i s t   o f   s t r o k e s
------------------------------------------------------------------------------*/

// ROM 0x001a3420 CountTStrokes__FPP7TStroke
// How many strokes a list holds.
long
CountTStrokes(TStroke** strokes)
{
	long n = 0;
	if (strokes != nil)
		while (strokes[n] != nil)
			n++;
	return n;
}


// ROM 0x001a3448 DisposeTStrokes__FPP7TStroke
// A list of strokes and the strokes in it given back.  Dispose rather
// than IDispose: a stroke is only really freed when the last user of it
// lets go, which is what lets SplitInkAt put the same strokes in two
// lists and give all three back.
void
DisposeTStrokes(TStroke** strokes)
{
	if (strokes == nil)
		return;
	for (long i = 0; strokes[i] != nil; i++)
		strokes[i]->Dispose();
	DisposPtr((Ptr) strokes);
}


/*------------------------------------------------------------------------------
	S t r o k e s   m a d e
------------------------------------------------------------------------------*/

// ROM 0x001a2474 MakeStrokeRef__FP7TStroke
// One stroke as a 'stroke binary: a Point for each sample, the 16.16
// pixels a stroke keeps brought down to eighths.
Ref
MakeStrokeRef(TStroke* stroke)
{
	long count = stroke->Count();
	RefVar ref(AllocateBinary(RSSYMstroke, count * 4));
	UByte* p = (UByte*) BinaryData(ref);
	SamplePt* pt = stroke->GetPoint(0);
	for (long i = 0; i < count; i++, pt++, p += 4)
	{
		SetPointHalf(p, SampleY(pt) >> 13);
		SetPointHalf(p + 2, SampleX(pt) >> 13);
	}
	return ref;
}


// ROM 0x001a1f2c MakeStrokeRef__FRC6RefVarl
// The same from an array of numbers - v, h, v, h - which is how a
// script hands strokes in.  A format under two says they are pixels and
// are to be multiplied up.
Ref
MakeStrokeRef(RefArg points, long format)
{
	long count = Length(points) / 2;
	RefVar ref(AllocateBinary(RSSYMstroke, count * 4));
	UByte* p = (UByte*) BinaryData(ref);
	for (long i = 0; i < count; i++, p += 4)
	{
		long h = RINT(GetArraySlot(points, i * 2 + 1));
		long v = RINT(GetArraySlot(points, i * 2));
		if (format < 2)
		{
			h = h << 3;
			v = v << 3;
		}
		SetPointHalf(p + 2, h);
		SetPointHalf(p, v);
	}
	return ref;
}


/*------------------------------------------------------------------------------
	A   b u n d l e   a s k e d
------------------------------------------------------------------------------*/

// ROM 0x001a17ec CountStrokes__FRC6RefVar
long
CountStrokes(RefArg bundle)
{
	return Length(RefVar(GetFrameSlot(bundle, RSSYMstrokes)));
}


// ROM 0x001a1838 GetStroke__FRC6RefVarl
Ref
GetStroke(RefArg bundle, long index)
{
	return GetArraySlot(RefVar(GetFrameSlot(bundle, RSSYMstrokes)), index);
}


// ROM 0x001a19d0 CountPoints__FRC6RefVar
long
CountPoints(RefArg stroke)
{
	return Length(stroke) >> 2;
}


// ROM 0x001a19f0 GetStrokeBounds__FRC6RefVarP5TRect
// The box one stroke covers, in whole pixels.
void
GetStrokeBounds(RefArg stroke, Rect* bounds)
{
	long count = CountPoints(stroke);
	const UByte* p = (const UByte*) BinaryData(stroke);
	EmptyBounds(bounds);
	for (long i = 0; i < count; i++, p += 4)
	{
		Point pt;
		pt.v = (short) ToPixels(PointHalf(p));
		pt.h = (short) ToPixels(PointHalf(p + 2));
		UnionPt(bounds, pt);
	}
}


// ROM 0x001a188c GetBundleBounds__FRC6RefVarP5TRect
void
GetBundleBounds(RefArg bundle, Rect* bounds)
{
	long count = CountStrokes(bundle);
	EmptyBounds(bounds);
	for (long i = 0; i < count; i++)
	{
		Rect one;
		GetStrokeBounds(RefVar(GetStroke(bundle, i)), &one);
		UnionInto(bounds, one);
	}
}


// ROM 0x001a1924 CalcBundleBounds__FRC6RefVar
// ... worked out again and written back into the frame.
void
CalcBundleBounds(RefArg bundle)
{
	if (!EQRef(ClassOf(bundle), RSSYMstrokebundle))
		ThrowMsg("not a stroke bundle");
	Rect bounds;
	GetBundleBounds(bundle, &bounds);
	SetFrameSlot(bundle, RSSYMbounds, RefVar(ToObject(bounds)));
}


// ROM 0x001a1acc GetStrokePoint__FRC6RefVarlP6TPointT2
void
GetStrokePoint(RefArg stroke, long index, Point* pt, long format)
{
	const UByte* p = (const UByte*) BinaryData(stroke) + index * 4;
	if (format < 2)
	{
		pt->h = (short) ToPixels(PointHalf(p + 2));
		pt->v = (short) ToPixels(PointHalf(p));
	}
	else
	{
		pt->h = PointHalf(p + 2);
		pt->v = PointHalf(p);
	}
}


// ROM 0x001a1b88 GetStrokePointsArray__FRC6RefVarl
// Every point of a stroke as one flat array of numbers.  Above three the
// format carries two more things in its upper bytes: how far apart the
// points are to be (a point no further than that from the one kept
// before it is dropped, though the first is always kept), and whether
// they are wanted h before v rather than in the Point order.
//
// The distance is the usual cheap one - the longer side plus half the
// shorter - and it is measured from the last point *seen* rather than
// the last one kept, so a slow curve is thinned by every step being
// short rather than by the whole of it being short.
Ref
GetStrokePointsArray(RefArg stroke, long format)
{
	long count = CountPoints(stroke);
	RefVar result(MakeArray(count * 2));
	Boolean xy = false;
	long apart = 0;
	if (format > 3)
	{
		xy = (format & 0x10000) != 0;
		apart = (format >> 8) & 0xff;
		format = format & 0xff;
	}
	Boolean thin = (format & 1) == 0;

	const UByte* p = (const UByte*) BinaryData(stroke);
	long lastH = 0;
	long lastV = 0;
	long slot = 0;
	for (long i = 0; i < count; i++, p += 4)
	{
		long h;
		long v;
		if (format < 2)
		{
			h = (short) ToPixels(PointHalf(p + 2));
			v = (short) ToPixels(PointHalf(p));
		}
		else
		{
			h = PointHalf(p + 2);
			v = PointHalf(p);
		}
		long dh = h - lastH;
		if (dh < 0)
			dh = -dh;
		long dv = v - lastV;
		if (dv < 0)
			dv = -dv;
		long distance = dh < dv ? dv + (dh >> 1) : dh + (dv >> 1);
		if (!thin || distance >= apart || slot == 0)
		{
			SetArraySlot(result, slot, RefVar(MAKEINT(xy ? h : v)));
			SetArraySlot(result, slot + 1, RefVar(MAKEINT(xy ? v : h)));
			slot += 2;
		}
		lastH = h;
		lastV = v;
	}
	SetLength(result, slot);
	return result;
}


/*------------------------------------------------------------------------------
	A   b u n d l e   m a d e
------------------------------------------------------------------------------*/

// ROM 0x001a1db4 MakeStrokeBundle__FRC6RefVarl
// A bundle out of an array of point arrays.
Ref
MakeStrokeBundle(RefArg strokes, long format)
{
	RefVar bundle;
	if (!IsArray(strokes) || Length(strokes) <= 0)
		return bundle;
	long count = Length(strokes);
	RefVar list(AllocateArray(RSSYMarray, count));
	for (long i = 0; i < count; i++)
		SetArraySlot(list, i, RefVar(MakeStrokeRef(RefVar(GetArraySlot(strokes, i)), format)));
	bundle = Clone(RefVar(Rstrokebundle));
	SetFrameSlot(bundle, RSSYMstrokes, list);
	Rect bounds;
	GetBundleBounds(bundle, &bounds);
	SetFrameSlot(bundle, RSSYMbounds, RefVar(ToObject(bounds)));
	return bundle;
}


// ROM 0x00144e54 StrokeBundle__FPP11TUnitPublicP5TRect
// A bundle out of the units a recogniser has finished with, and their
// box let out by the two pixels the pen spills.  The times are the first
// unit's start and the last one's end.
Ref
StrokeBundle(TUnitPublic** units, Rect* bounds)
{
	long count = 0;
	while (units[count] != nil)
		count++;
	RefVar bundle(Clone(RefVar(Rstrokebundle)));
	RefVar list(AllocateArray(RSSYMarray, count));
	EmptyBounds(bounds);
	long i = 0;
	while (units[i] != nil)
	{
		Rect one;
		units[i]->Bounds(&one);
		UnionInto(bounds, one);
		SetArraySlot(list, i, RefVar(MakeStrokeRef(units[i]->fUnit->GetStroke(0))));
		i++;
	}
	InsetRect(bounds, -2, -2);
	SetFrameSlot(bundle, RSSYMstrokes, list);
	SetFrameSlot(bundle, RSSYMbounds, RefVar(ToObject(*bounds)));
	SetFrameSlot(bundle, RSSYMstarttime, RefVar(MAKEINT(units[0]->fUnit->StartTime())));
	SetFrameSlot(bundle, RSSYMendtime, RefVar(MAKEINT(units[i - 1]->fUnit->EndTime())));
	return bundle;
}


/*------------------------------------------------------------------------------
	A   b u n d l e   u s e d
------------------------------------------------------------------------------*/

// ROM 0x001a3494 StrokeBundleToTStrokes__FRC6RefVar
// The strokes of a bundle as the recogniser's own objects.  A stroke is
// made with its full count here - TArray::IArray takes the count as the
// number of entries the array has - because the points are written into
// it rather than added one at a time.
TStroke**
StrokeBundleToTStrokes(RefArg bundle)
{
	RefVar list(GetFrameSlot(bundle, RSSYMstrokes));
	long count = Length(list);
	TStroke** strokes = (TStroke**) NewPtrClear((count + 1) * (long) sizeof(TStroke*));
	if (strokes == nil)
		return nil;
	long i = 0;
	for (; i < count; i++)
	{
		RefVar ref(GetArraySlot(list, i));
		const UByte* p = (const UByte*) BinaryData(ref);
		ULong points = (ULong) (Length(ref) >> 2);
		TStroke* stroke = TStroke::Make(points);
		if (stroke == nil)
		{
			DisposeTStrokes(strokes);
			return nil;
		}
		SamplePt* pt = stroke->GetPoint(0);
		for (ULong n = 0; n < points; n++, pt++, p += 4)
		{
			FPoint at;
			at.x = (Fixed) ((ULong) PointHalf(p + 2) << 13);
			at.y = (Fixed) ((ULong) PointHalf(p) << 13);
			SetPoint(pt, &at);
		}
		stroke->UpdateBBox();
		stroke->EndStroke();
		strokes[i] = stroke;
	}
	strokes[i] = nil;
	return strokes;
}


// ROM 0x001a2170 DrawStrokeBundle__FRC6RefVarP5TRectT2
// A bundle drawn into the current port, stretched out of the box it was
// written in and into another.
void
DrawStrokeBundle(RefArg bundle, Rect* from, Rect* to)
{
	TTransform transform;
	transform.Setup(from, to, false);
	RefVar list(GetFrameSlot(bundle, RSSYMstrokes));
	long count = Length(list);
	for (long i = 0; i < count; i++)
	{
		RefVar ref(GetArraySlot(list, i));
		const UByte* p = (const UByte*) BinaryData(ref);
		long points = Length(ref) >> 2;
		Point at;
		at.v = (short) ToPixels(PointHalf(p));
		at.h = (short) ToPixels(PointHalf(p + 2));
		Scale(&at, transform);
		MoveTo(at.h, at.v);
		for (long n = 1; n < points; n++)
		{
			p += 4;
			at.v = (short) ToPixels(PointHalf(p));
			at.h = (short) ToPixels(PointHalf(p + 2));
			Scale(&at, transform);
			LineTo(at.h, at.v);
		}
	}
}


/*------------------------------------------------------------------------------
	T h e   N e w t o n S c r i p t   f u n c t i o n s
------------------------------------------------------------------------------*/

// (every one of them insists on being given the right kind of object)
static void
CheckBundle(RefArg bundle)
{
	if (!EQRef(ClassOf(bundle), RSSYMstrokebundle))
		ThrowMsg("not a stroke bundle");
}


// ROM 0x001a3b48 FCountStrokes
static Ref
FCountStrokes(RefArg rcvr, RefArg bundle)
{
	CheckBundle(bundle);
	return MAKEINT(CountStrokes(bundle));
}


// ROM 0x001a3bc4 FGetStroke
static Ref
FGetStroke(RefArg rcvr, RefArg bundle, RefArg index)
{
	CheckBundle(bundle);
	return GetStroke(bundle, RINT(index));
}


// ROM 0x001a3c5c FGetBundleBounds__FRC6RefVarT1
static Ref
FGetBundleBounds(RefArg rcvr, RefArg bundle)
{
	CheckBundle(bundle);
	Rect bounds;
	GetBundleBounds(bundle, &bounds);
	return ToObject(bounds);
}


// ROM 0x001a3ce4 FCalcBundleBounds
static Ref
FCalcBundleBounds(RefArg rcvr, RefArg bundle)
{
	CalcBundleBounds(bundle);
	return NILREF;
}


// ROM 0x0019fd24 FGetStrokePointsArray
// The options are either the format itself or a frame saying it in
// words: `format`, `distance` (how far apart the points are to be) and
// `order` ('xy for h before v).
static Ref
FGetStrokePointsArray(RefArg rcvr, RefArg stroke, RefArg options)
{
	if (!EQRef(ClassOf(stroke), RSSYMstroke))
		ThrowMsg("not a stroke");
	long format;
	if (ISINT(options))
		format = RVALUE(options);
	else
	{
		format = RINT(GetProtoVariable(options, RSSYMformat, nil));
		RefVar distance(GetProtoVariable(options, RSSYMdistance, nil));
		RefVar order(GetProtoVariable(options, RSSYMorder, nil));
		if (EQRef(order, RSSYMxy))
			format += 0x10000;
		if (ISINT(distance))
			format += RVALUE(distance) << 8;
	}
	return GetStrokePointsArray(stroke, format);
}


// ROM 0x0019fe88 FPointsArrayToStroke
static Ref
FPointsArrayToStroke(RefArg rcvr, RefArg points, RefArg format)
{
	return MakeStrokeRef(points, RINT(format));
}


// ROM 0x0019fec0 FMakeStrokeBundle
static Ref
FMakeStrokeBundle(RefArg rcvr, RefArg strokes, RefArg format)
{
	return MakeStrokeBundle(strokes, RINT(format));
}


// (a stroke, checked)
static void
CheckStroke(RefArg stroke)
{
	if (!EQRef(ClassOf(stroke), RSSYMstroke))
		ThrowMsg("not a stroke");
}


// ROM 0x001a3d1c FCountPoints
// CountPoints(stroke): how many samples it holds.
static Ref
FCountPoints(RefArg /*rcvr*/, RefArg stroke)
{
	CheckStroke(stroke);
	return MAKEINT(CountPoints(stroke));
}


// ROM 0x001a3d94 FGetStrokeBounds
// GetStrokeBounds(stroke): the box it covers, as a bounds frame.
static Ref
FGetStrokeBounds(RefArg /*rcvr*/, RefArg stroke)
{
	CheckStroke(stroke);
	Rect bounds;
	GetStrokeBounds(stroke, &bounds);
	return ToObject(bounds);
}


// ROM 0x001a3e18 FGetStrokePoint
// GetStrokePoint(stroke, index, point, format): the index-th sample put
// into the `point` frame's x and y slots, which is answered.  The format
// says what units the answer is in, as GetStrokePointsArray's does.
static Ref
FGetStrokePoint(RefArg /*rcvr*/, RefArg stroke, RefArg index, RefArg point, RefArg format)
{
	CheckStroke(stroke);
	long theFormat = RINT(format);
	long at = RINT(index);
	Point pt;
	GetStrokePoint(stroke, at, &pt, theFormat);
	SetFrameSlot(point, RSSYMx, RefVar(MAKEINT(pt.h)));
	SetFrameSlot(point, RSSYMy, RefVar(MAKEINT(pt.v)));
	return point;
}


// ROM 0x0019ff40 FExpandUnit
// ExpandUnit(unit): the writing under a unit as a stroke bundle.
static Ref
FExpandUnit(RefArg /*rcvr*/, RefArg unit)
{
	return ExpandUnit(UnitFromRef(unit));
}


// ROM 0x001a04bc AsPolygon__FRC6RefVar
// A stroke binary as a QuickDraw polygon in a handle of its own, its
// bounds the stroke's box and its points rounded to pixels and moved to
// the box's top left (a point that would come out negative is put at
// nought).  ==> nil when there is no memory.
Handle
AsPolygon(RefArg stroke)
{
	ULong count = (ULong) CountPoints(stroke);
	Rect bounds;
	GetStrokeBounds(stroke, &bounds);
	long size = 12 + (long) (count << 2);
	Handle h = NewHandle(size);
	SetHandleName(h, 'AsPl');
	if (h == nil)
		return nil;
	Polygon* poly = (Polygon*) *h;
	poly->polySize = (short) size;
	poly->polyBBox = bounds;
	const UByte* p = (const UByte*) BinaryData(stroke);
	for (ULong i = 0; i < count; i++, p += 4)
	{
		poly->polyPoints[i].h = (short) (((PointHalf(p + 2) + 4) >> 3) - poly->polyBBox.left);
		if (poly->polyPoints[i].h < 0)
			poly->polyPoints[i].h = 0;
		poly->polyPoints[i].v = (short) (((PointHalf(p) + 4) >> 3) - poly->polyBBox.top);
		if (poly->polyPoints[i].v < 0)
			poly->polyPoints[i].v = 0;
	}
	return h;
}


// ROM 0x001a0600 FGetPolygons
// view:GetPolygons(unit): the unit's strokes as an array of 'polygon
// binaries, their points relative to the view's top left - what
// DrawPolygons draws back.
static Ref
FGetPolygons(RefArg rcvr, RefArg unit)
{
	TView* view = FailGetView(rcvr);
	TUnitPublic* pub = UnitFromRef(unit);
	RefVar polygons(MakeArray(0));
	RefVar bundle(pub->Strokes());
	RefVar strokes(GetFrameSlot(bundle, RSSYMstrokes));
	long n = Length(strokes);
	for (long i = 0; i < n; i++)
	{
		RefVar stroke(GetArraySlot(strokes, i));
		Handle h = AsPolygon(stroke);
		if (h == nil)
			continue;
		Polygon* poly = (Polygon*) *h;
		Point delta;
		delta.h = (short) (poly->polyBBox.left - view->viewBounds.left);
		delta.v = (short) (poly->polyBBox.top - view->viewBounds.top);
		long count = PolyPointCount(poly);
		for (long j = 0; j < count; j++)
		{
			poly->polyPoints[j].h = (short) (poly->polyPoints[j].h + delta.h);
			poly->polyPoints[j].v = (short) (poly->polyPoints[j].v + delta.v);
		}
		Size size = GetHandleSize(h);
		RefVar binary(AllocateBinary(RSSYMpolygon, size));
		BlockMove(*h, BinaryData(binary), size);
		AddArraySlot(polygons, binary);
		KillPoly((PolyHandle) h);
	}
	return polygons;
}


// ROM 0x001a0450 FCountGesturePoints__FRC6RefVarT1
// CountGesturePoints(unit): how many corners the gesture's polyline has.
static Ref
FCountGesturePoints(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(CountGesturePoints(UnitFromRef(unit)));
}


// ROM 0x001a0350 FGesturePoint__FRC6RefVarN21
// GesturePoint(index, unit): that corner of the gesture, as a
// canonicalGesturePoint - x, y, and `line`, which is the gesture's angle.
static Ref
FGesturePoint(RefArg /*rcvr*/, RefArg index, RefArg unit)
{
	TUnitPublic* it = UnitFromRef(unit);
	Point at = it->GesturePoint(RINT(index));
	RefVar point(Clone(RefVar(Rcanonicalgesturepoint)));
	SetFrameSlot(point, RSSYMx, RefVar(MAKEINT(at.h)));
	SetFrameSlot(point, RSSYMy, RefVar(MAKEINT(at.v)));
	SetFrameSlot(point, RSSYMline, RefVar(MAKEINT(it->GestureAngle())));
	return point;
}


// ROM 0x001a08fc FStrokesAfterUnit
// StrokesAfterUnit(unit, ...): whether the writer has drawn anything
// since - the controller is asked whether the unit's last stroke is
// still the last complete one there is.  The ROM's function object is
// made for two arguments and the code reads only the first, so the
// second is taken and dropped here as well.
static Ref
FStrokesAfterUnit(RefArg /*rcvr*/, RefArg unit, RefArg /*ignored*/)
{
	TUnitPublic* it = UnitFromRef(unit);
	TController* controller = Recognition()->fController;
	TUnit* stroke = controller->GetIndexedStroke(it->fUnit->fMaxStroke);
	return MAKEBOOLEAN(!controller->IsLastCompleteStroke(stroke));
}


// ROM 0x00078b48 FDrawOriginal
// DrawOriginal(bundle, itemBounds): the writing a stroke bundle holds drawn
// into a box (the corrector's picture of what was written), two pixels
// thick, as large as fits without stretching it more than twice its size;
// ==> the box it was drawn in.  The bundle's bounds and the box must be
// bounds frames ("bad strokeBounds", "bad itemBounds").
//
// The box loses two pixels at the right and bottom for the thick pen.  Then
// the width that keeps the writing's shape at the box's full height is
// worked out; when that is too wide the box's width is kept and the height
// made to suit.
//
// ROM BUG, kept: that height is the box's height times the too-wide width
// divided by the box's - the ratio upside down - so it comes out taller
// than the box instead of shorter, and the box grows rather than shrinks
// in that direction (then limited to twice the writing's height, as the
// width is to twice its width).
static Ref
FDrawOriginal(RefArg /*rcvr*/, RefArg bundle, RefArg itemBounds)
{
	RefVar strokeBoundsObj(GetFrameSlotRef(bundle, RSSYMbounds));
	Rect strokeBounds, box;
	if (!FromObject(strokeBoundsObj, strokeBounds))
		ThrowMsg("bad strokeBounds");
	if (!FromObject(itemBounds, box))
		ThrowMsg("bad itemBounds");
	PenState penState;
	GetPenState(&penState);
	PenNormal();
	PenSize(2, 2);
	box.bottom -= 2;
	box.right -= 2;
	long height = (short) (box.bottom - box.top);
	long width = (short) (box.right - box.left);
	long strokeWidth = (short) (strokeBounds.right - strokeBounds.left);
	long strokeHeight = (short) (strokeBounds.bottom - strokeBounds.top);
	long drawWidth = (height * strokeWidth) / strokeHeight;
	long drawHeight = height;
	if (drawWidth > width)
	{
		drawHeight = (drawWidth * height) / width;		// (the ROM bug above: upside down)
		drawWidth = width;
	}
	if (drawWidth > strokeWidth * 2)
		drawWidth = strokeWidth * 2;
	if (drawHeight > strokeHeight * 2)
		drawHeight = strokeHeight * 2;
	InsetRect(&box, (short) ((width - drawWidth) >> 1), (short) ((height - drawHeight) >> 1));
	DrawStrokeBundle(bundle, &strokeBounds, &box);
	SetPenState(&penState);
	return ToObject(box);
}


void
RegisterStrokeBundleNatives(void)
{
	RegisterNativeFunction("FDrawOriginal", (void*) FDrawOriginal, 2);
	RegisterNativeFunction("FCountStrokes", (void*) FCountStrokes, 1);
	RegisterNativeFunction("FGetStroke", (void*) FGetStroke, 2);
	RegisterNativeFunction("FGetBundleBounds__FRC6RefVarT1", (void*) FGetBundleBounds, 1);
	RegisterNativeFunction("FCalcBundleBounds", (void*) FCalcBundleBounds, 1);
	RegisterNativeFunction("FGetStrokePointsArray", (void*) FGetStrokePointsArray, 2);
	RegisterNativeFunction("FPointsArrayToStroke", (void*) FPointsArrayToStroke, 2);
	RegisterNativeFunction("FMakeStrokeBundle", (void*) FMakeStrokeBundle, 2);
	RegisterNativeFunction("FCountPoints", (void*) FCountPoints, 1);
	RegisterNativeFunction("FGetStrokeBounds", (void*) FGetStrokeBounds, 1);
	RegisterNativeFunction("FGetStrokePoint", (void*) FGetStrokePoint, 4);
	RegisterNativeFunction("FExpandUnit", (void*) FExpandUnit, 1);
	RegisterNativeFunction("FCountGesturePoints__FRC6RefVarT1", (void*) FCountGesturePoints, 1);
	RegisterNativeFunction("FGetPolygons", (void*) FGetPolygons, 1);
	RegisterNativeFunction("FGesturePoint__FRC6RefVarN21", (void*) FGesturePoint, 2);
	RegisterNativeFunction("FStrokesAfterUnit", (void*) FStrokesAfterUnit, 2);
}
// ROM 0x001a2554 ExpandUnit__FP11TUnitPublic
// The writing under one unit as a stroke bundle: every stroke of every
// sub of it, in the order they were written, and the unit's own bounds.
// This is what the word info frame's `strokes` slot holds, and what
// ends up on the page as ink when nothing could read it.
//
// A unit with no strokes at all answers nil rather than an empty
// bundle.
Ref
ExpandUnit(TUnitPublic* unit)
{
	RefVar bundle(NILREF);
	TUnitList* strokes = unit->fUnit->GetAllStrokes();
	if (strokes != nil)
	{
		long count = strokes->Count();
		RefVar array(AllocateArray(RSSYMarray, count));
		Rect box;
		unit->Bounds(&box);
		RefVar bounds(ToObject(box));
		for (long i = 0; i < count; i++)
		{
			TUnit* sub = strokes->GetUnit((ULong) i);
			RefVar stroke(MakeStrokeRef(sub->GetStroke(0)));
			SetArraySlot(array, i, stroke);
		}
		bundle = Clone(RefVar(Rstrokebundle));
		SetFrameSlot(bundle, RSSYMbounds, bounds);
		SetFrameSlot(bundle, RSSYMstrokes, array);
		// (the list is the unit's answer, not its own: it goes back)
		strokes->Dispose();
	}
	return bundle;
}
