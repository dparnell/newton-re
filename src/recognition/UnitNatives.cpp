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

	Reconstructed from the MP2x00 US ROM (0x001ea300-0x001ea36c,
	0x0019fc50-0x001a3a10); each function cites its origin.
*/

#include "UnitPublic.h"
#include "Rects.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "Words.h"
#include "RootView.h"
#include "Interpreter.h"
#include "NewtonExceptions.h"


// ROM 0x001ea300 UnitFromRef__FRC6RefVar
// The TUnitPublic a script's unit argument stands for.
TUnitPublic*
UnitFromRef(RefArg unit)
{
	TUnitPublic* pub = (TUnitPublic*) RefToAddress(unit);
	if (pub == nil)
		ThrowMsg("nil unit");
	return pub;
}


// ROM 0x001ea338 StrokeFromRef__FRC6RefVar
// ... and its stroke's face.
TStrokePublic*
StrokeFromRef(RefArg unit)
{
	TStrokePublic* stroke = UnitFromRef(unit)->Stroke();
	if (stroke == nil)
		ThrowMsg("nil stroke");
	return stroke;
}


// ROM 0x001a37e0 FGetPoint__FRC6RefVarN21
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


// ROM 0x0019fc50 FGetPointsArray__FRC6RefVarT1
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


// ROM 0x001a0050 FGetPointsArrayXY__FRC6RefVarT1
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


// ROM 0x001a0470 FCountUnitStrokes
static Ref
FCountUnitStrokes(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->fUnit->CountStrokes());
}


// ROM 0x001a049c FGestureType
// GestureType(unit): the caret gesture's kind (0 for none).
static Ref
FGestureType(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->CaretType());
}


// ROM 0x001a083c FStrokeDone__FRC6RefVarT1
static Ref
FStrokeDone(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEBOOLEAN(StrokeFromRef(unit)->Done());
}


// ROM 0x001a0864 FStrokeBounds__FRC6RefVarT1
// StrokeBounds(unit): the unit's bounds as a bounds frame.
static Ref
FStrokeBounds(RefArg /*rcvr*/, RefArg unit)
{
	Rect bounds;
	UnitFromRef(unit)->Bounds(&bounds);
	return ToObject(bounds);
}


// ROM 0x001a0b84 FGetUnitStartTime__FRC6RefVarT1
static Ref
FGetUnitStartTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->StartTime());
}


// ROM 0x001a0ba4 FGetUnitEndTime__FRC6RefVarT1
static Ref
FGetUnitEndTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(UnitFromRef(unit)->EndTime());
}


// ROM 0x001a2888 FGetUnitDownTime
static Ref
FGetUnitDownTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(StrokeFromRef(unit)->DownTime());
}


// ROM 0x001a3320 FGetUnitUpTime
static Ref
FGetUnitUpTime(RefArg /*rcvr*/, RefArg unit)
{
	return MAKEINT(StrokeFromRef(unit)->UpTime());
}


// ROM 0x001a12cc FInkOn__FRC6RefVarT1
static Ref
FInkOn(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOn();
	return TRUEREF;
}


// ROM 0x001a1618 FInkOff__FRC6RefVarT1
// InkOff(unit): the stroke's ink taken off the screen.
static Ref
FInkOff(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOff(true);
	return TRUEREF;
}


// ROM 0x001a1d8c FInkOffUnHobbled
static Ref
FInkOffUnHobbled(RefArg /*rcvr*/, RefArg unit)
{
	StrokeFromRef(unit)->InkOff(true, false);
	return TRUEREF;
}


// ROM 0x0013feb8 FSetInkerPenSize__FRC6RefVarT1
// The pen the ink is drawn with: the size is remembered in gLastPenTip,
// which every new stroke is flagged with, and the inker is told (a
// 'newt/'inkr message carrying the size plus 12, the pen width the inker
// draws at).  ==> nil, or the error as an integer when the message could
// not be sent.
//
// DEVIATION: TInker is NOT YET RECONSTRUCTED and the host's stand-in
// (hal/host/HostTablet.h) draws no ink, so there is nobody to tell; the
// size is remembered and nil answered, as a message that got through
// would.
Ref
FSetInkerPenSize(RefArg /*rcvr*/, RefArg size)
{
	gLastPenTip = (ULong) RINT(size);
	return NILREF;
}


/*------------------------------------------------------------------------------
	T h e   t a b l e t ' s   c a l i b r a t i o n

	The four points the tablet's coordinates are mapped through live in
	the inker task, and a script reaches them by sending it a 'newt/'inkr
	RPC: 0x16 to read them, 0x17 to write them, 5 to run the calibration
	itself (the assistant's "tap the targets" page).

	DEVIATION: TInker is NOT YET RECONSTRUCTED, so the host has no inker
	port to ask and no tablet of its own to calibrate - the host's pen
	is already in the display's coordinates
	(hal/host/HostTablet.h).  Reading and setting the calibration answer
	as a machine whose inker did not reply would, and calibrating answers
	that it worked, which is what leaves the Setup assistant free to go
	on.
------------------------------------------------------------------------------*/

// ROM 0x0c101654 gInkerCalibrated
long	gInkerCalibrated = 0;


// ROM 0x0013fda4 FGetCalibration__FRC6RefVar
// GetCalibration(): the inker's 0x14-byte calibration as a 'calibration
// binary, or nil when it did not answer with one.
static Ref
FGetCalibration(RefArg /*rcvr*/)
{
	// NOT YET RECONSTRUCTED: the 'newt/'inkr 0x16 RPC to the inker
	return NILREF;
}


// ROM 0x0013fcb8 FSetCalibration__FRC6RefVarT1
// SetCalibration(binary): the inker given a calibration back, a binary
// of any other length (or nil) ignored.  ==> nil either way.
static Ref
FSetCalibration(RefArg /*rcvr*/, RefArg calibration)
{
	if (NOTNIL(calibration) && Length(calibration) == 0x14)
	{
		// NOT YET RECONSTRUCTED: the 'newt/'inkr 0x17 RPC to the inker
	}
	return NILREF;
}


// ROM 0x0014132c FIsTabletCalibrationNeeded
// IsTabletCalibrationNeeded(): true when the tablet's hardware says its
// calibration is not good.  The ROM answers nil when there is no inker
// port to ask, which is the host's answer too.
static Ref
FIsTabletCalibrationNeeded(RefArg /*rcvr*/)
{
	// NOT YET RECONSTRUCTED: CheckTabletHWCalibration 0x0014121c
	return NILREF;
}


// ROM 0x00141098 CalibrateInker__Fv
// The inker asked to calibrate the tablet ('newt/'inkr command 5, with
// the sleep time - at most ten minutes - as the RPC's own timeout, so
// the machine does not fall asleep in the middle).  When it worked the
// tablet is marked calibrated and the ROM's `savecalibration` block puts
// the new calibration in the system soup.  ==> 0, or the error.
//
// DEVIATION: there is no inker to ask on the host, and the host's pen
// needs no calibration, so it answers as a calibration that worked.
// `savecalibration` then asks GetCalibration, gets nil and saves
// nothing, which is what a machine whose inker kept quiet would do.
long
CalibrateInker(void)
{
	gInkerCalibrated = 1;
	DoBlock(RefVar(Rsavecalibration), RefVar(NILREF));
	return 0;
}


// ROM 0x001411dc FCalibrateTablet__FRC6RefVar
// CalibrateTablet(): the tablet calibrated and the whole screen redrawn
// over whatever the calibration drew on it.  ==> nil when it worked, the
// error as an integer when it did not.
static Ref
FCalibrateTablet(RefArg /*rcvr*/)
{
	long err = CalibrateInker();
	gRootView->Dirty(nil);
	return err == 0 ? NILREF : MAKEINT(err);
}

void
RegisterUnitNatives(void)
{
	RegisterNativeFunction("FSetInkerPenSize__FRC6RefVarT1", (void*) FSetInkerPenSize, 1);
	RegisterWordNatives();
	RegisterNativeFunction("FGetCalibration__FRC6RefVar", (void*) FGetCalibration, 0);
	RegisterNativeFunction("FSetCalibration__FRC6RefVarT1", (void*) FSetCalibration, 1);
	RegisterNativeFunction("FIsTabletCalibrationNeeded", (void*) FIsTabletCalibrationNeeded, 0);
	RegisterNativeFunction("FCalibrateTablet__FRC6RefVar", (void*) FCalibrateTablet, 0);
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
