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
#include "WordList.h"
#include "Rects.h"
#include "Frames.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "Words.h"
#include "Controller.h"
#include "Learning.h"
#include "TabletBuffer.h"
#include "RecConfig.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "WordInfo.h"
#include "CompMath.h"
#include "RootView.h"
#include "View.h"
#include "StrokeCentral.h"
#include "Recognizer.h"
#include "Areas.h"
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

// ROM 0x00144c2c FBlockStrokes
// BlockStrokes(): the stroke world told to hold the strokes back.  The
// count nests, and IdleStrokes gives up after ten idles of it, so a
// caller that forgets to unblock does not stop the pen for good.
static Ref
FBlockStrokes(RefArg /*rcvr*/)
{
	gStrokeWorld.BlockStrokes();
	return NILREF;
}


// ROM 0x00144c4c FUnblockStrokes
// UnblockStrokes(): one BlockStrokes taken back (never below none).
static Ref
FUnblockStrokes(RefArg /*rcvr*/)
{
	gStrokeWorld.UnblockStrokes();
	return NILREF;
}


// ROM 0x00144c04 FFlushStrokes
// FlushStrokes(): the strokes waiting in the queue thrown away - each
// made into a click whose ink is taken off, so nothing of them is left
// on the screen either.  ==> true when there was anything to throw away.
static Ref
FFlushStrokes(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gStrokeWorld.FlushStrokes());
}

// ROM 0x0019d1a0 FPurgeAreaCache
// PurgeAreaCache(): the recogniser's cache of the areas it built for the
// views emptied, so that the next stroke builds them again - what a
// script calls when it has changed something they were built from.
static Ref
FPurgeAreaCache(RefArg /*rcvr*/)
{
	PurgeAreaCache();
	return NILREF;
}

// ROM 0x0019d1b8 FRecSettingsChanged
// RecSettingsChanged(): what a script calls when it has changed the
// recognition settings - the areas built from them are thrown away, and
// the caret is taken away if the view it is in no longer wants one.
static Ref
FRecSettingsChanged(RefArg /*rcvr*/)
{
	PurgeAreaCache();
	gRootView->CheckForCaretRemoval();
	return NILREF;
}

// ROM 0x001a030c FOtherViewInUse
// OtherViewInUse(view) - whether somebody else's writing is still in
// hand.  A nil view asks about no view at all, which is how a script
// asks whether *anybody* is using one.
static Ref
FOtherViewInUse(RefArg /*rcvr*/, RefArg view)
{
	TView* theView = nil;
	if (NOTNIL(view))
		theView = FailGetView(view);
	return MAKEBOOLEAN(OtherViewInUse(theView));
}


// ROM 0x001a0ac4 FGetEditArray__FRC6RefVarT1
// GetEditArray(unit): the readings of a unit as an array of strings -
// what the corrector puts in its list.  nil when the unit has no word
// list of its own (it was never read as words).
static Ref
FGetEditArray(RefArg /*rcvr*/, RefArg unit)
{
	RefVar words;
	TWordList* list = UnitFromRef(unit)->MakeWordList(true, false);
	if (list != nil)
	{
		words = MakeStringArray(list);
		delete list;
	}
	return words;
}


// ROM 0x0019d5d8 FModalRecognitionOn
// ModalRecognitionOn(bounds): writing taken only inside that rectangle,
// which is what a modal dialog does while it is up.
static Ref
FModalRecognitionOn(RefArg /*rcvr*/, RefArg bounds)
{
	Rect box;
	if (!FromObject(bounds, box))
		ThrowMsg("bad modal rect");
	gRecognition.EnableModalRecognition(box);
	return NILREF;
}


// ROM 0x0019d624 FModalRecognitionOff
// ModalRecognitionOff(): and the whole screen again.
static Ref
FModalRecognitionOff(RefArg /*rcvr*/)
{
	gRecognition.DisableModalRecognition();
	return NILREF;
}


// ROM 0x00168340 FTriggerWordRecognition
// TriggerWordRecognition(): the writing that is waiting read now rather
// than when the pen has been still long enough - the stroke units are
// timed out at once.
static Ref
FTriggerWordRecognition(RefArg /*rcvr*/)
{
	gController->TimeOut('STXR');		// the type the word domain's pieces carry
	return NILREF;
}


// ROM 0x0019e0a0 FFinishRecognizing__FRC6RefVar
// FinishRecognizing(): the recogniser idled until it has nothing left -
// no pieces to group, no units to classify, and nothing due.  A script
// calls it when it wants what has been written to have been read before
// it goes on.
static Ref
FFinishRecognizing(RefArg /*rcvr*/)
{
	gRecognition.Idle();
	for (;;)
	{
		if (gController->fUnits->Count() == 0 && gController->fPieces->Count() == 0)
		{
			static const Int64 zero = { 0, 0 };
			TTime next = gRecognition.NextIdle();
			if (CompCompare(&next.time, &zero) == 0)
				return NILREF;
		}
		gRecognition.Idle();
	}
}


// ROM 0x001a0bc4 FGetTrainingData__FRC6RefVarT1
// GetTrainingData(unit): the same frame WordUnitToWordInfo answers.
// The ROM's two functions are the same code; the training data a
// handwriting engine would keep is in that frame or nowhere.
static Ref
FGetTrainingData(RefArg /*rcvr*/, RefArg unit)
{
	TUnitPublic* it = UnitFromRef(unit);
	if (ISNIL(it->fWordInfo->ref))
		it->fWordInfo->ref = MakeWordInfo(it);
	return it->fWordInfo->ref;
}


// ROM 0x001a0cbc FDisposeTrainingData__FRC6RefVarT1
// DisposeTrainingData(data): nil.  The ROM's function answers nil and
// does nothing - the training data belongs to the unit and goes with it.
static Ref
FDisposeTrainingData(RefArg /*rcvr*/, RefArg /*data*/)
{
	return NILREF;
}

// ROM 0x001aa7c8 FAddAutoAdd
// AddAutoAdd(word): a word added to the writer's dictionaries on their
// behalf.  ==> whether it was added.
//
// BUG (the ROM's, kept in Learning.cpp): AddAutoAdd answers true for a
// word it has just taken back out again.
static Ref
FAddAutoAdd(RefArg /*rcvr*/, RefArg word)
{
	return MAKEBOOLEAN(AddAutoAdd(GetCString(word)));
}


// ROM 0x001aa7f0 FRemoveAutoAdd
// RemoveAutoAdd(word): and taken back out.
static Ref
FRemoveAutoAdd(RefArg /*rcvr*/, RefArg word)
{
	RemoveAutoAdd(GetCString(word));
	return NILREF;
}


// ROM 0x00202d44 FTabletBufferEmpty
// TabletBufferEmpty(): whether the tablet has anything waiting to be
// read - both the inker and the stroker are caught up with the writer.
static Ref
FTabletBufferEmpty(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(TabletBufferEmpty());
}


// ROM 0x0003540c FBuildRecConfig
// BuildRecConfig(view): the recognition configuration a view would be
// read under, built as the recogniser builds it.
static Ref
FBuildRecConfig(RefArg /*rcvr*/, RefArg view)
{
	TView* theView = FailGetView(view);
	return BuildRecConfig(theView, theView->fFlags & 0x01ffff00);
}


// ROM 0x000353ec PrepRecConfig
// view:PrepRecConfig(config) - a configuration frame readied for use.
// One that already has a `_parent` is taken as it is; anything else is
// wrapped in a clone of protoRecConfig with the frame as its `_proto`,
// and given a `_parent`: the writer's own `_recogSettings` expanded over
// `vars.userConfiguration` when the view has them, else that frame
// alone.
static Ref
FPrepRecConfig(RefArg rcvr, RefArg config)
{
	TView* view = FailGetView(rcvr);
	if (FrameHasSlot(config, RSSYM_parent))
		return config;
	RefVar prepared(Clone(RefVar(Rprotorecconfig)));
	SetFrameSlot(prepared, RSSYM_proto, config);
	RefVar settings(GetFrameSlotRef(RefVar(gVarFrame), RSSYMuserconfiguration));
	RefVar mine;
	if (view != nil)
		mine = view->GetVar(RSSYM_recogsettings);
	if (ISNIL(mine))
		SetFrameSlot(prepared, RSSYM_parent, settings);
	else
	{
		mine = NSCallGlobalFn(RefVar(RSSYMexpandsettings), mine);
		SetFrameSlot(mine, RSSYM_proto, settings);
		SetFrameSlot(prepared, RSSYM_parent, mine);
	}
	return prepared;
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
	RegisterNativeFunction("FGetEditArray__FRC6RefVarT1", (void*) FGetEditArray, 1);
	RegisterNativeFunction("FAddAutoAdd", (void*) FAddAutoAdd, 1);
	RegisterNativeFunction("FRemoveAutoAdd", (void*) FRemoveAutoAdd, 1);
	RegisterNativeFunction("FTabletBufferEmpty", (void*) FTabletBufferEmpty, 0);
	RegisterNativeFunction("FBuildRecConfig", (void*) FBuildRecConfig, 1);
	RegisterNativeFunction("PrepRecConfig", (void*) FPrepRecConfig, 1);
	RegisterNativeFunction("FModalRecognitionOn", (void*) FModalRecognitionOn, 1);
	RegisterNativeFunction("FModalRecognitionOff", (void*) FModalRecognitionOff, 0);
	RegisterNativeFunction("FTriggerWordRecognition", (void*) FTriggerWordRecognition, 0);
	RegisterNativeFunction("FFinishRecognizing__FRC6RefVar", (void*) FFinishRecognizing, 0);
	RegisterNativeFunction("FGetTrainingData__FRC6RefVarT1", (void*) FGetTrainingData, 1);
	RegisterNativeFunction("FDisposeTrainingData__FRC6RefVarT1", (void*) FDisposeTrainingData, 1);
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
	RegisterNativeFunction("FBlockStrokes", (void*) FBlockStrokes, 0);
	RegisterNativeFunction("FUnblockStrokes", (void*) FUnblockStrokes, 0);
	RegisterNativeFunction("FFlushStrokes", (void*) FFlushStrokes, 0);
	RegisterNativeFunction("FPurgeAreaCache", (void*) FPurgeAreaCache, 0);
	RegisterNativeFunction("FRecSettingsChanged", (void*) FRecSettingsChanged, 0);
	RegisterNativeFunction("FOtherViewInUse", (void*) FOtherViewInUse, 1);
	RegisterNativeFunction("FReadCursiveOptions__FRC6RefVar", (void*) FReadCursiveOptions, 0);
}
