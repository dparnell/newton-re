/*
	File:		recognition/UnitNatives.cpp

	Contains:	The NewtonScript functions over units and strokes: the unit
				a viewClickScript, viewStrokeScript or viewGestureScript
				gets is the address of a TUnitPublic (UnitFromRef), and its
				stroke's face a TStrokePublic (StrokeFromRef).  GetPoint,
				GetPointsArray/XY, StrokeDone, StrokeBounds, InkOn/InkOff,
				CountUnitStrokes, GetUnitStartTime/EndTime/DownTime/UpTime,
				GestureType.  RegisterUnitNatives binds them.

				NOT YET RECONSTRUCTED: GesturePoint and CountGesturePoints
				(the gesture unit's points), the stroke bundle functions
				(GetStroke, CountStrokes, GetStrokePoint..., MakeStrokeBundle,
				CompressStrokes, ExpandInk, the ink of the paragraphs),
				StrokesAfterUnit (the controller), the word functions
				(GetWordArray, GetScoreArray, GetTrainingData...).

	Reconstructed from the MP2100 D ROM (0x001ec718-0x001ec784,
	0x001a21d0-0x001a5f90); each function cites its origin.
*/

#include "UnitPublic.h"
#include "Rects.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"


// ROM 0x001ec718 UnitFromRef__FRC6RefVar
// The TUnitPublic a script's unit argument stands for.
TUnitPublic*
UnitFromRef(RefArg unit)
{
	TUnitPublic* pub = (TUnitPublic*) RefToAddress(unit);
	if (pub == nil)
		ThrowMsg("nil unit");
	return pub;
}


// ROM 0x001ec750 StrokeFromRef__FRC6RefVar
// ... and its stroke's face.
TStrokePublic*
StrokeFromRef(RefArg unit)
{
	TStrokePublic* stroke = UnitFromRef(unit)->Stroke();
	if (stroke == nil)
		ThrowMsg("nil stroke");
	return stroke;
}


// ROM 0x001a5d60 FGetPoint__FRC6RefVarN21
// GetPoint(which, unit): which 0 the first point's x, 1 its y, 4 the last
// point's x, 5 its y, 6 the first point as a {x, y} frame, 8 the last;
// anything else 0.
static Ref
FGetPoint(RefArg /*rcvr*/, RefArg which, RefArg unit)
{
	TStrokePublic* stroke = StrokeFromRef(unit);
	long selector = RINT(which);
	long value = 0;
	RefVar result;
	switch (selector)
	{
	case 0:
		value = stroke->FirstPoint().h;
		break;
	case 1:
		value = stroke->FirstPoint().v;
		break;
	case 4:
		value = stroke->FinalPoint().h;
		break;
	case 5:
		value = stroke->FinalPoint().v;
		break;
	case 6:
	case 8:
		{
			Point pt = selector == 6 ? stroke->FirstPoint() : stroke->FinalPoint();
			result = Clone(RefVar(Rcanonicalpoint));
			SetFrameSlot(result, RSSYMx, RefVar(MAKEINT(pt.h)));
			SetFrameSlot(result, RSSYMy, RefVar(MAKEINT(pt.v)));
		}
		break;
	}
	if (ISNIL(result))
		result = MAKEINT(value);
	return result;
}


// ROM 0x001a21d0 FGetPointsArray__FRC6RefVarT1
// GetPointsArray(unit): the stroke's points as a flat array of
// coordinates, each point's v then its h (the tablet's order).
static Ref
FGetPointsArray(RefArg /*rcvr*/, RefArg unit)
{
	TStrokePublic* stroke = StrokeFromRef(unit);
	long count = stroke->Size() * 2;
	RefVar array(MakeArray(count));
	for (long slot = 0, i = 0; slot < count; slot += 2, i++)
	{
		Point pt = stroke->GetPoint(i);
		SetArraySlotRef(array, slot, MAKEINT(pt.v));
		SetArraySlotRef(array, slot + 1, MAKEINT(pt.h));
	}
	return array;
}


// ROM 0x001a25d0 FGetPointsArrayXY__FRC6RefVarT1
// GetPointsArrayXY(unit): ... each point's x then its y.
static Ref
FGetPointsArrayXY(RefArg /*rcvr*/, RefArg unit)
{
	TStrokePublic* stroke = StrokeFromRef(unit);
	long count = stroke->Size() * 2;
	RefVar array(MakeArray(count));
	for (long slot = 0, i = 0; slot < count; slot += 2, i++)
	{
		Point pt = stroke->GetPoint(i);
		SetArraySlotRef(array, slot, MAKEINT(pt.h));
		SetArraySlotRef(array, slot + 1, MAKEINT(pt.v));
	}
	return array;
}


// ROM 0x001a29f0 FCountUnitStrokes
static Ref
FCountUnitStrokes(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->fUnit->CountStrokes());
}


// ROM 0x001a2a1c FGestureType
// GestureType(unit): the caret gesture's kind (0 for none).
static Ref
FGestureType(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->CaretType());
}


// ROM 0x001a2dbc FStrokeDone__FRC6RefVarT1
static Ref
FStrokeDone(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEBOOLEAN(StrokeFromRef(unit)->Done());
}


// ROM 0x001a2de4 FStrokeBounds__FRC6RefVarT1
// StrokeBounds(unit): the unit's bounds as a bounds frame.
static Ref
FStrokeBounds(RefArg /*rcvr*/, RefArg unit)
{
	Rect bounds;
	UnitFromRef(unit)->Bounds(&bounds);
	return ToObject(bounds);
}


// ROM 0x001a3104 FGetUnitStartTime__FRC6RefVarT1
static Ref
FGetUnitStartTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->StartTime());
}


// ROM 0x001a3124 FGetUnitEndTime__FRC6RefVarT1
static Ref
FGetUnitEndTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->EndTime());
}


// ROM 0x001a4e08 FGetUnitDownTime
static Ref
FGetUnitDownTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(StrokeFromRef(unit)->DownTime());
}


// ROM 0x001a58a0 FGetUnitUpTime
static Ref
FGetUnitUpTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(StrokeFromRef(unit)->UpTime());
}


// ROM 0x001a384c FInkOn__FRC6RefVarT1
static Ref
FInkOn(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOn();
	return TRUEREF;
}


// ROM 0x001a3b98 FInkOff__FRC6RefVarT1
// InkOff(unit): the stroke's ink taken off the screen.
static Ref
FInkOff(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOff(true);
	return TRUEREF;
}


// ROM 0x001a430c FInkOffUnHobbled
static Ref
FInkOffUnHobbled(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOff(true, false);
	return TRUEREF;
}


void
RegisterUnitNatives(void)
{
	RegisterNativeFunction("FGetPoint__FRC6RefVarN21", (void*) FGetPoint, 2);
	RegisterNativeFunction("FGetPointsArray__FRC6RefVarT1", (void*) FGetPointsArray, 1);
	RegisterNativeFunction("FGetPointsArrayXY__FRC6RefVarT1", (void*) FGetPointsArrayXY, 1);
	RegisterNativeFunction("FCountUnitStrokes", (void*) FCountUnitStrokes, 1);
	RegisterNativeFunction("FGestureType", (void*) FGestureType, 1);
	RegisterNativeFunction("FStrokeDone__FRC6RefVarT1", (void*) FStrokeDone, 1);
	RegisterNativeFunction("FStrokeBounds__FRC6RefVarT1", (void*) FStrokeBounds, 1);
	RegisterNativeFunction("FGetUnitStartTime__FRC6RefVarT1", (void*) FGetUnitStartTime, 1);
	RegisterNativeFunction("FGetUnitEndTime__FRC6RefVarT1", (void*) FGetUnitEndTime, 1);
	RegisterNativeFunction("FGetUnitDownTime", (void*) FGetUnitDownTime, 1);
	RegisterNativeFunction("FGetUnitUpTime", (void*) FGetUnitUpTime, 1);
	RegisterNativeFunction("FInkOn__FRC6RefVarT1", (void*) FInkOn, 1);
	RegisterNativeFunction("FInkOff__FRC6RefVarT1", (void*) FInkOff, 1);
	RegisterNativeFunction("FInkOffUnHobbled", (void*) FInkOffUnHobbled, 1);
}
