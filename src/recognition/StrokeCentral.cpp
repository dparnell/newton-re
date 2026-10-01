/*
	File:		recognition/StrokeCentral.cpp

	Contains:	StrokeCentral, the stroke world.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Journal.h"
#include <stdio.h>
#include "StrokeCentral.h"
#include "StrokeQueue.h"
#include "TabletBuffer.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "Domain.h"
#include "Controller.h"
#include "Arbiter.h"
#include "Frames.h"
#include "InkGroups.h"
#include "OSErrors.h"
#include "StrokeBundle.h"
#include "RecConfig.h"
#include "Commands.h"
#include "Application.h"
#include "RootView.h"
#include "View.h"
#include "WRecDomain.h"
#include "Locale.h"
#include "Interpreter.h"
#include "RSSymbols.h"

#include <string.h>

StrokeCentral	gStrokeWorld;						// ROM 0x0c1018cc gStrokeWorld


// A unit handed to the unit handler on its own, which is what the ROM
// does for a click the controller finds externally arbitrated: the
// handler takes a list of arbitration matches, and the unit is the first
// word of one.
static void
HandleOneUnit(TUnit* unit)
{
	TArray* list = TArray::Make(sizeof(BestMatch), 1);
	if (list != nil)
	{
		memset(list->GetEntry(0), 0, sizeof(BestMatch));
		((BestMatch*) list->GetEntry(0))->fUnit = unit;
		HandleUnit(list);
		list->Dispose();
	}
}


// ROM 0x001447c4 __dt__13StrokeCentralFv
StrokeCentral::~StrokeCentral()
{
	DoneFields();
}


// ROM 0x00144790 __ct__13StrokeCentralFv
StrokeCentral*
StrokeCentral::New(void)
{
	StrokeCentral* world = new StrokeCentral;
	if (world != nil)
		world->InitFields();
	return world;
}


// ROM 0x00144ad8 Init__13StrokeCentralFv
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


// ROM 0x00144d40 InitFields__13StrokeCentralFv
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
	fGroupCount = 0;
	fExpiredStrokes = TUnitList::Make();
	fNextCompressTime = TTime(0);
	fCompressGroup = nil;
	fDeferredStrokes = new RefStruct;
	*fDeferredStrokes = MakeArray(0);
	fExpireProc = nil;
	fCompressBundle = new RefStruct;
}


// ROM 0x00145644 DoneFields__13StrokeCentralFv
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


// ROM 0x00144878 IdleStrokes__Fv
// The stroke world idled, unless it is being idled already - then only
// the pen's time kept (StrokeTime).  The flag is the stroke world's own
// (+0x38), which SaveRecognitionState puts aside and clears: that is what
// lets a modal dialog opened by a tap (from inside this idle) take taps of
// its own in its fork.
void
IdleStrokes(void)
{
	if (gStrokeWorld.fFlag38)
	{
		StrokeTime();
		return;
	}
	gStrokeWorld.fFlag38 = true;
	gStrokeWorld.IdleStrokes();
	gStrokeWorld.fFlag38 = false;
}


// ROM 0x001448b8 IdleStrokes__13StrokeCentralFv
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
		if (!gController->CheckBusy() && fCurrentStroke == nil)
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
					gController->NewClassification(fCurrentUnit);
					if (gController->IsExternallyArbitrated(fCurrentUnit))
						HandleOneUnit(fCurrentUnit);
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
					gController->NewClassification(eventUnit);
				}
			}
		}
		if (!fCurrentStroke->Done())
			return;
		if (gJournallingState == 1)
			JournalRecordAStroke(fCurrentStroke);
		DoneCurrentStroke();
		gController->TriggerRecognition();
	}
}


// ROM 0x00145e10 StartNewStroke__13StrokeCentralFP7TStroke
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


// ROM 0x00145f14 DoneCurrentStroke__13StrokeCentralFv
// The current stroke's times kept as the last, the stroke and its unit
// let go (the unit's in-progress flag cleared), the time noted.  The
// unit itself is not disposed: the controller has it on its piece list
// and its clean-up is what takes it away.
void
StrokeCentral::DoneCurrentStroke(void)
{
	fLastDownTime = fCurrentStroke->fDownTime;
	fLastUpTime = fCurrentStroke->fUpTime;
	fHasCurrent = false;
	fCurrentStroke = nil;
	fCurrentUnit->UnsetFlags(kUnitStrokeInProgress);
	fCurrentUnit = nil;					// (the controller holds it now)
	fNextCompressTime = GetGlobalTime();
}


// ROM 0x00145f68 IdleCurrentStroke__13StrokeCentralFv
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


// ROM 0x001447fc CurrentStroke__13StrokeCentralFv
TStroke*
StrokeCentral::CurrentStroke(void)
{
	return fHasCurrent ? fCurrentStroke : nil;
}


// ROM 0x001447f0 InvalidateCurrentStroke__13StrokeCentralFv
void
StrokeCentral::InvalidateCurrentStroke(void)
{
	fHasCurrent = false;
}


// ROM 0x00144ab4 BlockStrokes__13StrokeCentralFv
void
StrokeCentral::BlockStrokes(void)
{
	fBlocked++;
}


// ROM 0x00144ac4 UnblockStrokes__13StrokeCentralFv
void
StrokeCentral::UnblockStrokes(void)
{
	if (fBlocked > 0)
		fBlocked--;
}


// ROM 0x00144af8 FlushStrokes__13StrokeCentralFv
// Every queued stroke taken and thrown away: made a click unit whose ink
// is taken off and whose bounds are invalidated, waited for until the pen
// has finished it, then disposed.  ==> whether there was any.
//
// The wait between turns is the ROM's and it matters: this loop runs in
// the application's task, and without it nothing else gets the processor
// - not the inker that reads the pen's samples, and on a host not the
// kernel either, because a task that never enters it is never preempted
// (kernel/host/TaskRuntime.h).  A pen still down would then hold the
// machine for good.
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
		{
			StrokeTime();
			Wait(1);
		}
		unit->Dispose();
	}
	return any;
}


// ROM 0x00144bc8 BeforeLastFlush__13StrokeCentralFl
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


// ROM 0x00144810 AddDeferredStroke__13StrokeCentralFRC6RefVarlT2
// A stroke (and two numbers) put off for later.
void
StrokeCentral::AddDeferredStroke(RefArg stroke, long a, long b)
{
	AddArraySlot(*fDeferredStrokes, stroke);
	AddArraySlot(*fDeferredStrokes, RefVar(MAKEINT(a)));
	AddArraySlot(*fDeferredStrokes, RefVar(MAKEINT(b)));
}


// ROM 0x001456a0 UpdateStroke__FP11TStrokeUnitP5FRect
// A waiting stroke inked again when the rectangle updated meets its box
// (grown by the pen's width) - always, when its stroke lacks flag
// 0x08000000.
void
UpdateStroke(TStrokeUnit* unit, FRect* rect)
{
	FRect box;
	unit->GetBBox(&box);
	long size = RINT(GetPreference(RSSYMuserpensize));
	InsetRectangle(&box, -(size << 16), -(size << 16));
	if (unit->fStroke->TestFlags(0x08000000) && !SectRectangle(&box, &box, rect))
		return;
	unit->fStroke->Draw();
}


// ROM 0x00145564 UpdateStrokesInList__FP9TUnitListP5FRect
// Each stroke unit of the list inked again where the update meets it.
void
UpdateStrokesInList(TUnitList* list, FRect* rect)
{
	for (ULong i = 0; i < list->Count(); i++)
		UpdateStroke((TStrokeUnit*) list->GetUnit(i), rect);
}


// ROM 0x001455bc UpdateCompressGroup__13StrokeCentralFP5FRect
// The strokes waiting to become ink - the expired ones, and the ones the
// ink grouping holds - inked again where an update painted over them
// (TRecognitionManager::Update, from the root view's PostDraw).
void
StrokeCentral::UpdateCompressGroup(FRect* rect)
{
	if (fExpiredStrokes->Count() > 0)
		UpdateStrokesInList(fExpiredStrokes, rect);
	if (fCompressGroup == nil)
		return;
	GroupDataStruct* data = IGLockGroupData(fCompressGroup);
	TStrokeUnit** units;
	ULong count;
	IGGetStrokesQueue(data, &units, &count);
	for (ULong i = 0; i < count; i++)
		if (units[i] != nil)
			UpdateStroke(units[i], rect);
	IGUnlockGroupData(fCompressGroup);
}


// ROM 0x001454fc IdleCompress__13StrokeCentralFv
// With no stroke current, once the compress time has come the expired
// strokes are compressed into ink.
void
StrokeCentral::IdleCompress(void)
{
	if (gStrokeWorld.CurrentStroke() != nil)
		return;
	static const Int64 zero = { 0, 0 };
	if (CompCompare(&fNextCompressTime.time, &zero) != 0)
	{
		TTime now = GetGlobalTime();
		if (CompCompare(&now.time, &fNextCompressTime.time) >= 0)
			ExpireAll();
	}
}


// ROM 0x00144df8 AddExpiredStroke__13StrokeCentralFP11TStrokeUnit
// A stroke nobody claimed: held (one more user), grouped with the others
// into the compress group - the words the segmenter settles compressed
// into ink there and then - and the compress time set half a second on.
void
StrokeCentral::AddExpiredStroke(TStrokeUnit* unit)
{
	unit->Clone();
	IGGroupAndCompressStrokes((ULong) this, unit, gRecognitionLetterSpacing, false, &fCompressGroup);
	fNextCompressTime = TimeFromNow(500 * kMilliseconds);
}


// ROM 0x00144cd8 ExpireAll__13StrokeCentralFv
// Whatever is waiting in the compress group settled and compressed into
// ink; with no expired stroke left the compress time is cleared.
void
StrokeCentral::ExpireAll(void)
{
	if (fCompressGroup != nil)
		IGGroupAndCompressStrokes((ULong) this, nil, gRecognitionLetterSpacing, true, &fCompressGroup);
	if (fExpiredStrokes->Count() == 0)
		fNextCompressTime = TTime(0);
}


// ROM 0x00144c6c IGCompressGroup__FUlPP11TStrokeUnit
// The ink grouping's way back to the stroke world.
void
IGCompressGroup(ULong strokeWorld, TStrokeUnit** units)
{
	((StrokeCentral*) strokeWorld)->IGCompressGroup(units);
}


// ROM 0x00144c70 IGGetCompressBufSize__FUl
// The most strokes one piece of ink is made of (less one).
ULong
IGGetCompressBufSize(ULong strokeWorld)
{
	return 0x28;
}


// ROM 0x00144c78 IGCompressGroup__13StrokeCentralFPP11TStrokeUnit
// A word's strokes put in the expired strokes as the group and
// compressed.
void
StrokeCentral::IGCompressGroup(TStrokeUnit** units)
{
	ULong n;
	for (n = 0; units[n] != nil; n++)
		fExpiredStrokes->AddUnit(units[n]);
	fGroupCount = n;
	CompressGroup();
}


// ROM 0x00144cc8 WRecEndInkStrokeGroup__FPP11TStrokeUnit
// A group of strokes the word recogniser gave up on, compressed as the
// ink grouping's would be.
void
WRecEndInkStrokeGroup(TStrokeUnit** units)
{
	gStrokeWorld.IGCompressGroup(units);
}


// ROM 0x00145030 ExpireUsingCommand__FPP11TUnitPublic
// A group of strokes as ink for the view under them: an aeInkWord
// command when the view reads ink words, aeRawInk when it does not, the
// strokes as a stroke bundle; the screen under them made to be redrawn.
// When the recogniser ran out of memory since, the writer is warned (at
// most once a day, and only as the preferences ask).
static void
ExpireUsingCommand(TUnitPublic** units)
{
	TView* view = units[0]->FindView(0x1fffe00);
	if (view == nil)
		return;
	RefVar config(BuildRecConfig(view, view->fFlags & 0x1ffff00));
	config = GetVariable(config, RSSYMdoinkwordrecognition, nil, 0);
	RefVar cmd(MakeCommand(ISNIL(config) ? aeRawInk : aeInkWord, view, 0));
	Rect bounds;
	RefVar bundle(StrokeBundle(units, &bounds));
	CommandSetFrameParameter(cmd, bundle);
	gApplication->DispatchCommand(cmd);
	AdjustForInk(&bounds);
	gRootView->SmartInvalidate(bounds);
	if (0 < gRecMemErrCount && (gRecInkNotifyFlags & 1) != 0)
	{
		RefVar last(GetPreference(RSSYMlastrecmemwarning));
		ULong lastDay = ISINT(last) ? (ULong) RINT(last) : 0;
		ULong today = (ULong) (uint32_t) RealClock() / 0x5a0;
		if (lastDay < today)
		{
			RefVar quiet((gRecInkNotifyFlags & 2) == 0 ? TRUEREF : NILREF);
			NSCallGlobalFn(RSSYMrecognitionmemorywarning, quiet);
			SetPreference(RSSYMlastrecmemwarning, RefVar(MAKEINT(today)));
		}
		gRecMemErrCount = 0;
	}
}


// ROM 0x00145210 ExpireGroup__13StrokeCentralFPP11TUnitPublic
// A group of expired strokes handed on - to the expire proc as a stroke
// bundle, or as ink to the view under them - unless the arbiter is
// waiting (a Throw of an 'evt.ex' reported); with no expire proc the
// screen is brought up to date.
void
StrokeCentral::ExpireGroup(TUnitPublic** units)
{
	if (!gArbiter->fWaiting)
	{
		newton_try
		{
			if (fExpireProc == nil)
				ExpireUsingCommand(units);
			else
			{
				Rect bounds;
				RefVar bundle(StrokeBundle(units, &bounds));
				fExpireProc(*fCompressBundle, bundle);
			}
		}
		newton_catch("evt.ex")
		{
			// DEVIATION: the ROM's ExceptionNotify is the application
			// layer's (newt/Notebook.h), below which recognition sits
			SafeExceptionNotify(CurrentException());
		}
		end_try;
	}
	if (fExpireProc == nil)
	{
		gApplication->fNewUndoBatch = true;
		gRootView->Update(nil);
	}
	gRecognition.fAfterWriting = true;
}


// ROM 0x0014532c CompressGroup__13StrokeCentralFv
// The group's strokes (the first fGroupCount expired strokes) given faces,
// their ink taken off and the group expired; then every one let go and
// the expired strokes emptied of them - a Throw on the way is passed on
// after the letting go.
void
StrokeCentral::CompressGroup(void)
{
	TUnitPublic** units = (TUnitPublic**) operator new((fGroupCount + 1) * sizeof(TUnitPublic*));
	if (units == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	for (ULong i = 0; i < fGroupCount; i++)
		units[i] = nil;
	unwind_protect
	{
		ULong i;
		for (i = 0; i < fGroupCount; i++)
		{
			units[i] = new TUnitPublic(fExpiredStrokes->GetUnit(i), 0);
			if (units[i] == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			units[i]->Stroke()->InkOff(false);
		}
		units[i] = nil;
		if (fGroupCount != 0)
			ExpireGroup(units);
	}
	on_unwind
	{
		for (ULong i = 0; i < fGroupCount; i++)
		{
			TUnit* unit = fExpiredStrokes->GetUnit(i);
			*(TUnit**) fExpiredStrokes->GetEntry(i) = nil;
			unit->Dispose();
			if (units[i] != nil)
				delete units[i];
		}
		fExpiredStrokes->DeleteEntries(0, fGroupCount);
		fExpiredStrokes->Compact();
		fGroupCount = 0;
		operator delete(units);
	}
	end_unwind;
}


#pragma mark - saving the state

// ROM 0x001457fc SaveRecognitionState__13StrokeCentralFPUc
StrokeCentralState*
StrokeCentral::SaveRecognitionState(UChar* failed)
{
	*failed = false;
	StrokeCentralState* state = new StrokeCentralState;
	if (state == nil)
	{
		*failed = true;
		return state;
	}
	state->fHasCurrent = fHasCurrent;
	state->fCurrentStroke = fCurrentStroke;
	state->fCurrentUnit = fCurrentUnit;
	state->fLastDownTime = fLastDownTime;
	state->fLastUpTime = fLastUpTime;
	state->fDeferredStrokes = fDeferredStrokes;
	state->fGroupCount = fGroupCount;
	state->fExpiredStrokes = fExpiredStrokes;
	state->fNextCompressTime = fNextCompressTime;
	state->fCompressGroup = fCompressGroup;
	state->fFlag38 = fFlag38;
	state->fExpireProc = fExpireProc;
	state->fCompressBundle = fCompressBundle;
	InitFields();
	return state;
}


// ROM 0x00145b6c RestoreRecognitionState__13StrokeCentralFUl
// The click unit being made meanwhile let go (its 0x4000000 flag taken
// off), the fields done with and the saved ones put back.
void
StrokeCentral::RestoreRecognitionState(StrokeCentralState* state)
{
	if (state == nil)
		return;
	if (fCurrentUnit != nil)
		fCurrentUnit->UnsetFlags(0x4000000);
	DoneFields();
	fHasCurrent = state->fHasCurrent;
	fCurrentStroke = state->fCurrentStroke;
	fCurrentUnit = state->fCurrentUnit;
	fLastDownTime = state->fLastDownTime;
	fLastUpTime = state->fLastUpTime;
	fDeferredStrokes = state->fDeferredStrokes;
	fGroupCount = state->fGroupCount;
	fExpiredStrokes = state->fExpiredStrokes;
	fNextCompressTime = state->fNextCompressTime;
	fCompressGroup = state->fCompressGroup;
	fFlag38 = state->fFlag38;
	fExpireProc = state->fExpireProc;
	fCompressBundle = state->fCompressBundle;
	delete state;
}
