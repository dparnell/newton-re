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


void
RegisterStrokeBundleNatives(void)
{
	RegisterNativeFunction("FCountStrokes", (void*) FCountStrokes, 1);
	RegisterNativeFunction("FGetStroke", (void*) FGetStroke, 2);
	RegisterNativeFunction("FGetBundleBounds__FRC6RefVarT1", (void*) FGetBundleBounds, 1);
	RegisterNativeFunction("FCalcBundleBounds", (void*) FCalcBundleBounds, 1);
	RegisterNativeFunction("FGetStrokePointsArray", (void*) FGetStrokePointsArray, 2);
	RegisterNativeFunction("FPointsArrayToStroke", (void*) FPointsArrayToStroke, 2);
	RegisterNativeFunction("FMakeStrokeBundle", (void*) FMakeStrokeBundle, 2);
}
