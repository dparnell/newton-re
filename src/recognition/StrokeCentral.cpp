/*
	File:		recognition/StrokeCentral.cpp

	Contains:	StrokeCentral, the stroke world.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "StrokeCentral.h"
#include "StrokeQueue.h"
#include "TabletBuffer.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "Domain.h"
#include "Frames.h"

StrokeCentral	gStrokeWorld;						// ROM 0x0c1019b8 gStrokeWorld
static Boolean	gIdlingStrokes = false;				// (the ROM's byte at 0x0c1019f0) IdleStrokes is running


// NOT YET RECONSTRUCTED: TController.  DEVIATION: the host hands a new
// unit straight to the unit handler, as the ROM does when the controller
// finds it externally arbitrated (StrokeCentral::IdleStrokes below), and
// disposes it afterwards unless it is the click of the stroke in progress
// (DoneCurrentStroke lets that one go).
static void
HostClassify(TUnit* unit)
{
	TArray* list = TArray::Make(0x28, 1);
	if (list != nil)
	{
		*(TUnit**) list->GetEntry(0) = unit;
		HandleUnit(list);
		list->Dispose();
	}
}


// ROM 0x00146318 __dt__13StrokeCentralFv
StrokeCentral::~StrokeCentral()
{
	DoneFields();
}


// ROM 0x0014662c Init__13StrokeCentralFv
// The fields, the stroke queue and the tablet started; the tablet set
// collecting.
void
StrokeCentral::Init(void)
{
	InitFields();
	StrokeInit();
	xTabInit();
	gTabletCollect.collect = true;
}


// ROM 0x00146894 InitFields__13StrokeCentralFv
void
StrokeCentral::InitFields(void)
{
	fHasCurrent = false;
	fCurrentStroke = nil;
	fCurrentUnit = nil;
	fLastDownTime = 0;
	fLastUpTime = 0;
	fBlocked = 0;
	fBlockedIdles = 0;
	fFlag38 = false;
	fUnused24 = 0;
	fExpiredStrokes = TUnitList::Make();
	fNextCompressTime = TTime(0);
	fCompressGroup = nil;
	fDeferredStrokes = new RefStruct;
	*fDeferredStrokes = MakeArray(0);
	fUnused3c = 0;
	fCompressBundle = new RefStruct;
}


// ROM 0x00147198 DoneFields__13StrokeCentralFv
void
StrokeCentral::DoneFields(void)
{
	if (fExpiredStrokes != nil)
		fExpiredStrokes->Dispose();
	if (fDeferredStrokes != nil)
		delete fDeferredStrokes;
	if (fCompressBundle != nil)
		delete fCompressBundle;
	fExpiredStrokes = nil;
	fDeferredStrokes = nil;
	fCompressBundle = nil;
}


// ROM 0x001463cc IdleStrokes__Fv
// The stroke world idled, unless it is being idled already.
void
IdleStrokes(void)
{
	if (!gIdlingStrokes)
	{
		gIdlingStrokes = true;
		gStrokeWorld.IdleStrokes();
		gIdlingStrokes = false;
	}
}


// ROM 0x0014640c IdleStrokes__13StrokeCentralFv
// The strokes' idle: a block wears off after ten idles.  Then, while the
// controller is not busy and no stroke is current, the next stroke is
// taken (none while blocked) and its click unit made and classified - a
// unit the controller calls externally arbitrated is handled at once;
// the current stroke's unit is kept up to date, and a click event noted
// in the stroke becomes a click-event unit over the click (the event
// cleared from the stroke when it is still the one seen) and is
// classified; a done stroke is journalled, finished with, and
// recognition triggered - and the loop goes round for the next.
void
StrokeCentral::IdleStrokes(void)
{
	if (fBlocked > 0 && ++fBlockedIdles >= 10)
	{
		fBlockedIdles = 0;
		fBlocked = 0;
	}
	for (;;)
	{
		StrokeTime();
		if (fCurrentStroke == nil)		// (the ROM: and the controller not busy)
		{
			if (fBlocked != 0)
				return;
			TStroke* stroke = StrokeGet();
			if (stroke != nil)
			{
				StartNewStroke(stroke);
				fCurrentUnit = TClickUnit::Make(gRootDomain, 1, stroke, nil);
				if (fCurrentUnit != nil)
				{
					fCurrentUnit->SetFlags(kUnitStrokeInProgress);
					HostClassify(fCurrentUnit);		// (the ROM: gController->NewClassification, then HandleUnit when IsExternallyArbitrated)
				}
			}
		}
		if (fCurrentStroke == nil)
			return;
		IdleCurrentStroke();
		if (!fCurrentUnit->TestFlags(kClaimedUnit))
		{
			long event = fCurrentStroke->fClickEvent;
			if (event >= kTapClick && event <= kTapDragClick)
			{
				TClickEventUnit* eventUnit = TClickEventUnit::Make(gRootDomain, 1, nil);
				if (eventUnit != nil)
				{
					eventUnit->AddSub(fCurrentUnit);
					Boolean acquired = AcquireStroke(fCurrentStroke);
					if (fCurrentStroke->fClickEvent == event)
						eventUnit->ClearEvent();
					if (acquired)
						ReleaseStroke();
					HostClassify(eventUnit);		// (the ROM: gController->NewClassification)
					eventUnit->Dispose();
				}
			}
		}
		if (!fCurrentStroke->Done())
			return;
		// (the ROM: JournalRecordAStroke when journalling - NOT YET)
		DoneCurrentStroke();
		// (the ROM: gController->TriggerRecognition - NOT YET)
	}
}


// ROM 0x00147964 StartNewStroke__13StrokeCentralFP7TStroke
// The stroke made current; it is told the last stroke's down and up
// times, and its own down time becomes the last.
void
StrokeCentral::StartNewStroke(TStroke* stroke)
{
	fHasCurrent = true;
	fCurrentStroke = stroke;
	stroke->fPrevDownTime = fLastDownTime;
	stroke->fPrevUpTime = fLastUpTime;
	fLastDownTime = stroke->fDownTime;
}


// ROM 0x00147a68 DoneCurrentStroke__13StrokeCentralFv
// The current stroke's times kept as the last, the stroke and its unit
// let go (the unit's in-progress flag cleared), the time noted.
void
StrokeCentral::DoneCurrentStroke(void)
{
	fLastDownTime = fCurrentStroke->fDownTime;
	fLastUpTime = fCurrentStroke->fUpTime;
	fHasCurrent = false;
	fCurrentStroke = nil;
	fCurrentUnit->UnsetFlags(kUnitStrokeInProgress);
	fCurrentUnit->Dispose();			// DEVIATION: the ROM's controller keeps the unit (NOT YET)
	fCurrentUnit = nil;
	fNextCompressTime = GetGlobalTime();
}


// ROM 0x00147abc IdleCurrentStroke__13StrokeCentralFv
// The click unit's box brought up to the stroke's (which grows as the
// pen moves).
void
StrokeCentral::IdleCurrentStroke(void)
{
	TStroke* stroke = fCurrentStroke;
	TUnit* unit = fCurrentUnit;
	unit->fBBox.top = (short) ((stroke->fBBox.top + 0x8000) >> 16);
	unit->fBBox.left = (short) ((stroke->fBBox.left + 0x8000) >> 16);
	unit->fBBox.bottom = (short) ((stroke->fBBox.bottom + 0x8000) >> 16);
	unit->fBBox.right = (short) ((stroke->fBBox.right + 0x8000) >> 16);
}


// ROM 0x00146350 CurrentStroke__13StrokeCentralFv
TStroke*
StrokeCentral::CurrentStroke(void)
{
	return fHasCurrent ? fCurrentStroke : nil;
}


// ROM 0x00146344 InvalidateCurrentStroke__13StrokeCentralFv
void
StrokeCentral::InvalidateCurrentStroke(void)
{
	fHasCurrent = false;
}


// ROM 0x00146608 BlockStrokes__13StrokeCentralFv
void
StrokeCentral::BlockStrokes(void)
{
	fBlocked++;
}


// ROM 0x00146618 UnblockStrokes__13StrokeCentralFv
void
StrokeCentral::UnblockStrokes(void)
{
	if (fBlocked > 0)
		fBlocked--;
}


// ROM 0x0014664c FlushStrokes__13StrokeCentralFv
// Every queued stroke taken and thrown away: made a click unit whose ink
// is taken off and whose bounds are invalidated, waited for until done
// (NOT YET: Wait(1) between turns - the host's inker runs in StrokeTime),
// then disposed.  ==> whether there was any.
Boolean
StrokeCentral::FlushStrokes(void)
{
	Boolean any = false;
	for (TStroke* stroke = StrokeGet(); stroke != nil; stroke = StrokeGet())
	{
		any = true;
		TClickUnit* unit = TClickUnit::Make(gRootDomain, 1, stroke, nil);
		unit->SetFlags(kUnitStrokeInProgress);
		{
			TUnitPublic pub(unit, 0);
			pub.Stroke()->InkOff(true);
			pub.Invalidate();
		}
		while (!stroke->Done())
			StrokeTime();
		unit->Dispose();
	}
	return any;
}


// ROM 0x0014671c BeforeLastFlush__13StrokeCentralFl
// Whether the time falls before the last flush; a flush more than ten
// seconds away from the time is forgotten.
Boolean
StrokeCentral::BeforeLastFlush(long time)
{
	long flush = fLastFlushTime;
	if (flush == 0)
		return false;
	long away = flush - time;
	if (away < 0)
		away = -away;
	if (away < 601)
		return time < flush;
	fLastFlushTime = 0;
	return false;
}


// ROM 0x00146364 AddDeferredStroke__13StrokeCentralFRC6RefVarlT2
// A stroke (and two numbers) put off for later.
void
StrokeCentral::AddDeferredStroke(RefArg stroke, long a, long b)
{
	AddArraySlot(*fDeferredStrokes, stroke);
	AddArraySlot(*fDeferredStrokes, RefVar(MAKEINT(a)));
	AddArraySlot(*fDeferredStrokes, RefVar(MAKEINT(b)));
}
