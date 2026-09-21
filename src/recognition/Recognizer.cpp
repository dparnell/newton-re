/*
	File:		recognition/Recognizer.cpp

	Contains:	TRecognizer, TRecognizerList, the click and click-event
				recognisers, TRecognitionManager.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Recognizer.h"
#include "Areas.h"
#include "Words.h"
#include "UnitPublic.h"
#include "Commands.h"
#include "RootView.h"
#include "Rects.h"
#include "NewtonExceptions.h"
#include "StrokeCentral.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "Controller.h"
#include "Arbiter.h"
#include "Domain.h"
#include "EdgeList.h"
#include "StrokeQueue.h"
#include "UserTasks.h"

TRecognitionManager	gRecognition;			// ROM 0x0c106e88 gRecognition


/*------------------------------------------------------------------------------
	T R e c o g n i z e r
------------------------------------------------------------------------------*/

// ROM 0x001437b4 __ct__11TRecognizerFv
TRecognizer::TRecognizer()
{ }


// ROM 0x00143934 Init__11TRecognizerFP7TDomainUlT2UcT2
// The recogniser set up; no services yet.
void
TRecognizer::Init(TDomain* domain, ULong id, ULong command, UChar flags, ULong arbitrateTime)
{
	fDomain = domain;
	fID = id;
	fCommand = command;
	fFlags = flags;
	fArbitrateTime = arbitrateTime;
	InitServices(0, 0);
}


// ROM 0x00143808 InitServices__11TRecognizerFUlT1
void
TRecognizer::InitServices(ULong possible, ULong enabled)
{
	fServicesPossible = possible;
	fServicesEnabled = enabled;
}


// ROM 0x00143de4 Domain__11TRecognizerFv
TDomain*
TRecognizer::Domain(void)
{
	return fDomain;
}


// ROM 0x001443ec ID__11TRecognizerFv
ULong
TRecognizer::ID(void)
{
	return fID;
}


// ROM 0x001444f8 Command__11TRecognizerFv
ULong
TRecognizer::Command(void)
{
	return fCommand;
}


// ROM 0x00144500 Flags__11TRecognizerFv
ULong
TRecognizer::Flags(void)
{
	return fFlags;
}


// ROM 0x00144508 TestFlags__11TRecognizerFUc
Boolean
TRecognizer::TestFlags(UChar flags)
{
	return (fFlags & flags) != 0;
}


// ROM 0x00144528 ServicesPossible__11TRecognizerFv
ULong
TRecognizer::ServicesPossible(void)
{
	return fServicesPossible;
}


// ROM 0x001437e8 ServicesEnabled__11TRecognizerFv
ULong
TRecognizer::ServicesEnabled(void)
{
	return fServicesEnabled;
}


// ROM 0x001437f0 UnitConfidence__11TRecognizerFP11TUnitPublic
long
TRecognizer::UnitConfidence(TUnitPublic* /*unit*/)
{
	return 0;
}


// ROM 0x001437f8 Sleep__11TRecognizerFv
void
TRecognizer::Sleep(void)
{ }


// ROM 0x001437fc WakeUp__11TRecognizerFv
void
TRecognizer::WakeUp(void)
{ }


// ROM 0x00143800 ArbitrateTime__11TRecognizerFv
ULong
TRecognizer::ArbitrateTime(void)
{
	return fArbitrateTime;
}


// ROM 0x00143814 BuildConfig__11TRecognizerFRC6RefVarP5TViewUl
void
TRecognizer::BuildConfig(RefArg /*config*/, TView* /*view*/, ULong /*flags*/)
{ }


// ROM 0x00143818 EnableArea__11TRecognizerFP8TRecAreaRC6RefVar
// The recogniser asked whether it wants anything written here.  If the
// configuration's inputMask has any of the services the recogniser
// provides, its unit type is added to the area, answered through the
// unit handler and arbitrated at the time the recogniser was installed
// with.
long
TRecognizer::EnableArea(TRecArea* area, RefArg config)
{
	ULong mask = RINT(RefVar(GetVariable(config, RSSYMinputmask, nil, 0)));
	if ((ServicesEnabled() & mask) != 0)
		area->AddAType(ID(), gRecognition.fUnitHandler, ArbitrateTime(), nil);
	return 0;
}


// ROM 0x001438c4 ConfigureArea__11TRecognizerFP8TRecAreaRC6RefVar
long
TRecognizer::ConfigureArea(TRecArea* /*area*/, RefArg /*config*/)
{
	return 0;
}


// ROM 0x00144520 HandleUnit__11TRecognizerFP11TUnitPublic
// ==> the command to send the view for a unit.
ULong
TRecognizer::HandleUnit(TUnitPublic* /*unit*/)
{
	return fCommand;
}


// ROM 0x00143964 GetLearningData__11TRecognizerFP11TUnitPublic
Ref
TRecognizer::GetLearningData(TUnitPublic* /*unit*/)
{
	return NILREF;
}


// ROM 0x0014396c DoLearning__11TRecognizerFRC6RefVarl
void
TRecognizer::DoLearning(RefArg /*data*/, long /*arg*/)
{ }


/*------------------------------------------------------------------------------
	T R e c o g n i z e r L i s t
------------------------------------------------------------------------------*/

// ROM 0x0019de84 Make__15TRecognizerListSFv
TRecognizerList*
TRecognizerList::Make(void)
{
	TRecognizerList* list = new TRecognizerList;
	if (list != nil)
	{
		list->fData = nil;
		if (list->IRecognizerList() != 0)
		{
			list->Dispose();
			list = nil;
		}
	}
	return list;
}


// ROM 0x0019deec IRecognizerList__15TRecognizerListFv
long
TRecognizerList::IRecognizerList(void)
{
	return IArray(sizeof(TRecognizer*), 0);
}


// ROM 0x0019def8 AddRecognizer__15TRecognizerListFP11TRecognizer
void
TRecognizerList::AddRecognizer(TRecognizer* recognizer)
{
	*(TRecognizer**) AddEntry() = recognizer;
}


// ROM 0x0019df1c GetRecognizer__15TRecognizerListFUl
TRecognizer*
TRecognizerList::GetRecognizer(ULong index)
{
	return *(TRecognizer**) GetEntry(index);
}


// ROM 0x0019df3c FindRecognizer__15TRecognizerListFUl
// The recogniser whose id is the unit type; nil for none.
TRecognizer*
TRecognizerList::FindRecognizer(ULong id)
{
	ULong count = fCount;
	TRecognizer** entry = (TRecognizer**) GetEntry(0);
	for (ULong i = 0; i < count; i++, entry++)
		if ((*entry)->ID() == id)
			return *entry;
	return nil;
}


/*------------------------------------------------------------------------------
	T h e   c l i c k   r e c o g n i s e r s
------------------------------------------------------------------------------*/

// ROM 0x0020bf58 OnlyStrokeWritten__FP11TStrokeUnit
// Whether this stroke is the only one in hand: no other unclaimed
// 'STRK' piece is waiting in the controller, and no further stroke
// arrived in the queue within 255 ticks of this one's pen-up.  Nothing
// that has to stand alone - a gesture, a click - is acted on while
// another stroke is still about.
Boolean
OnlyStrokeWritten(TStrokeUnit* unit)
{
	TArrayIterator iter;
	TUnit** entry = (TUnit**) gController->fPieces->GetIterator(&iter);
	for (long i = 0; i < iter.fCount; i++)
	{
		TUnit* piece = *entry;
		if (piece->fType == kStrokeUnit && !piece->TestFlags(kClaimedUnit) && piece != (TUnit*) unit)
			return false;
		entry = (TUnit**) iter.GetNext();
	}
	if (unit == nil)
		return true;
	return !CheckStrokeQueueEvents(unit->fStroke->fUpTime, 0xff);
}


// ROM 0x00143c38 HandleUnit__16TClickRecognizerFP11TUnitPublic
// The view under the click found (and remembered as the click view);
// aeClick unless another view's area is in use or clicks are being
// ignored - a click on a clicks-only area then notes that one was
// swallowed.
ULong
TClickRecognizer::HandleUnit(TUnitPublic* unit)
{
	TUnit* theUnit = unit->fUnit;
	ULong command = Command();
	TView* view = unit->FindView(ServicesEnabled());
	gRecognition.SaveClickView(view);
	if (OtherViewInUse(view) || gRecognition.fIgnoreClicksUntil != 0)
	{
		command = 0;
		if (ClicksOnlyArea(theUnit))
			gRecognition.fClickSwallowed = true;
	}
	return command;
}


// ROM 0x00143b14 ID__16TEventRecognizerFv
ULong
TEventRecognizer::ID(void)
{
	return kClickEventUnit;
}


// ROM 0x00143b20 HandleUnit__16TEventRecognizerFP11TUnitPublic
// The command for the click event, when the click's stroke is the last
// written: aeTap; aeDoubleTap and aeTapDrag only when both clicks were on
// the same view; aeHiliteClick.
ULong
TEventRecognizer::HandleUnit(TUnitPublic* unit)
{
	ULong command = 0;
	TClickEventUnit* eventUnit = (TClickEventUnit*) unit->fUnit;
	if (OnlyStrokeWritten((TStrokeUnit*) gController->GetIndexedStroke(eventUnit->fMinStroke)))
	{
		switch (eventUnit->Event())
		{
		case kTapClick:
			command = aeTap;
			break;
		case kDoubleTapClick:
			if (gRecognition.fPrevClickView == gRecognition.fClickView)
				command = aeDoubleTap;
			break;
		case kHiliteClick:
			command = aeHiliteClick;
			break;
		case 5:
			if (gRecognition.fPrevClickView == gRecognition.fClickView)
				command = aeTapDrag;
			break;
		}
	}
	return command;
}


// ROM 0x00143cdc InstallClickRecognizer__FP19TRecognitionManager
// The click recogniser: 'CLIK' units, aeClick, flags 10, arbitrate time 2,
// the vClickable service.
void
InstallClickRecognizer(TRecognitionManager* manager)
{
	TRecognizer* recognizer = new TClickRecognizer;
	recognizer->Init(nil, kClickUnit, aeClick, kRecognizerStrokeBounds | kRecognizerArbitrated, 2);
	recognizer->InitServices(vClickable, vClickable);
	*(TRecognizer**) manager->fRecognizers->AddEntry() = recognizer;
}


// ROM 0x00143bb0 InstallEventRecognizer__FP19TRecognitionManager
// The click-event recogniser: 'CEVT' units, aeTap, flags 10, no arbitrate
// time, the vGesturesAllowed service.
void
InstallEventRecognizer(TRecognitionManager* manager)
{
	TRecognizer* recognizer = new TEventRecognizer;
	recognizer->Init(nil, kClickEventUnit, aeTap, kRecognizerStrokeBounds | kRecognizerArbitrated, 0);
	recognizer->InitServices(vGesturesAllowed, vGesturesAllowed);
	*(TRecognizer**) manager->fRecognizers->AddEntry() = recognizer;
}


/*------------------------------------------------------------------------------
	T R e c o g n i t i o n M a n a g e r
------------------------------------------------------------------------------*/

// ROM 0x00143a00 HandleUnit__16TScrubRecognizerFP11TUnitPublic
// The command a recognised gesture asks for.  A gesture written within
// half a second of the stroke before it, while the last thing handled
// went to the word recogniser, is not one: somebody is writing.  The
// rest waits out the fifth of a second after the pen came up in which
// another stroke could still arrive, and gives up if one did.  A
// gesture whose bounds are tiny is a tap after all.
ULong
TScrubRecognizer::HandleUnit(TUnitPublic* unit)
{
	TUnit* theUnit = unit->fUnit;
	TStroke* stroke = theUnit->GetStroke(0);
	if (gRecognition.fAfterWriting && stroke->fDownTime <= stroke->fPrevUpTime + 30)
		return 0;
	long label = ((TSIUnit*) theUnit)->GetInterpretation(0)->label;
	long wait = 20 - (long) (GetTicks() - stroke->fUpTime);
	if (wait > 0)
		::Sleep(wait * kTicksToTimeUnits);		// (the global: TRecognizer has a Sleep of its own)
	if (!OnlyStrokeWritten((TStrokeUnit*) gController->GetIndexedStroke(theUnit->fMinStroke)))
		return 0;
	if (unit->IsTap())
		return aeTap;
	switch (label)
	{
	case kGestureScrub:		return aeScrub;
	case kGestureCaret:
	case kGestureCaret4:
	case kGestureCaret4Open:
	case kGestureCaretFlat:	return aeCaret;
	case kGestureLine:		return aeLine;
	default:				return 0;
	}
}


// ROM 0x00143970 InstallGestureRecognizer__FP19TRecognitionManager
// The gesture recogniser: the edge-list domain made, and its 'SCRB'
// units answered with aeScrub - the command the recogniser's HandleUnit
// then narrows to whichever gesture it turned out to be.  Its service
// is vGesturesAllowed.
void
InstallGestureRecognizer(TRecognitionManager* manager)
{
	gEdgeListDomain = TEdgeListDomain::Make(manager->fController);
	TRecognizer* recognizer = new TScrubRecognizer;
	recognizer->Init(gEdgeListDomain, gEdgeListDomain->fType, aeScrub,
		kRecognizerArbitrated, kArbitrateAtOnce);
	recognizer->InitServices(vGesturesAllowed, vGesturesAllowed);
	*(TRecognizer**) manager->fRecognizers->AddEntry() = recognizer;
}


// ROM 0x00143d64 InstallStrokeRecognizer__FP19TRecognitionManager
// The stroke recogniser: the stroke domain made, and 'STRK' units
// answered with aeStroke (viewStrokeScript) as soon as they are ready.
// Its service is vStrokesAllowed, so a view that asks for raw strokes is
// the only one it is enabled in.
void
InstallStrokeRecognizer(TRecognitionManager* manager)
{
	gStrokeDomain = TStrokeDomain::Make(manager->fController);
	TRecognizer* recognizer = new TRecognizer;
	recognizer->Init(gStrokeDomain, gStrokeDomain->fType, aeStroke,
		kRecognizerStrokeBounds | kRecognizerArbitrated, kArbitrateAtOnce);
	recognizer->InitServices(vStrokesAllowed, vStrokesAllowed);
	*(TRecognizer**) manager->fRecognizers->AddEntry() = recognizer;
}


// ROM 0x0019e124 Init__19TRecognitionManagerFUc
// The recognition system started at a level: 0 none, 1 clicks and
// strokes, 2 shapes and words as well.  The stroke world, the area
// cache, the controller and the arbiter are made, the controller is told
// how to find a unit's areas and what to do with a stroke nobody wanted,
// and then the recognisers are installed and the domains ordered.
//
// (InitializeParagraphCompression, which the ROM calls here, is started
// from outside instead - see TNotebook::InitToolbox.)
//
// NOT YET RECONSTRUCTED: SetContextUnitRoutine(HandleGetContextUnits).
long
TRecognitionManager::Init(UChar level)
{
	fStrokeWorld = nil;
	fController = nil;
	fArbiter = nil;
	fAreas = nil;
	fIgnoreClicksUntil = 0;
	fAfterWriting = true;
	fLevel = level;
	fModalBounds = nil;
	fRecognizers = TRecognizerList::Make();
	if (fLevel != 0)
	{
		gStrokeWorld.Init();
		fStrokeWorld = &gStrokeWorld;
		InitAreas();
		fAreas = (TRecObject*) gAreaCache;
		gController = TController::Make();
		fController = gController;
		gArbiter = TArbiter::Make(fController);
		fArbiter = gArbiter;
		fController->SetHitTestRoutine(GetAreasHit);
		fController->SetExpireStrokeRoutine(HandleExpiredStroke);
	}
	if (fLevel > 1)
		InitDictionaries();
	if (fLevel != 0)
	{
		InitRecognizers();
		fController->Initialize();
	}
	return 0;
}


// ROM 0x0019d438 InitRecognizers__19TRecognitionManagerFv
// The recognisers installed and the root domain made.  The ROM installs
// the gesture, click-event, stroke and click recognisers at any level,
// and the shape, word and WRec ones above level 1.
// NOT YET RECONSTRUCTED: the gesture recogniser and its edge-list domain,
// the three of level 2, ReadDomainOptions.
long
TRecognitionManager::InitRecognizers(void)
{
	if (fLevel != 0)
	{
		InstallGestureRecognizer(this);
		InstallEventRecognizer(this);
		InstallStrokeRecognizer(this);
		InstallClickRecognizer(this);
		gRootDomain = TDomain::Make(fController, kRootDomainType, (char*) "TDomain");
	}
	return 0;
}


// ROM 0x0019d4e8 EnableModalRecognition__19TRecognitionManagerFR5TRect
// Recognition confined to the bounds (a modal dialog's).
void
TRecognitionManager::EnableModalRecognition(Rect& bounds)
{
	if (fModalBounds != nil)
		ThrowMsg((char*) "Can't nest modal bounds");
	fModalBounds = new Rect;
	if (fModalBounds != nil)
		*fModalBounds = bounds;
}


// ROM 0x0019d540 DisableModalRecognition__19TRecognitionManagerFv
void
TRecognitionManager::DisableModalRecognition(void)
{
	if (fModalBounds != nil)
		delete fModalBounds;
	fModalBounds = nil;
}


// ROM 0x0019d568 ModalRecognitionOK__19TRecognitionManagerFR5TRect
// Whether a unit's bounds' centre lies within the modal bounds; the popup
// is closed when it does not.
Boolean
TRecognitionManager::ModalRecognitionOK(Rect& bounds)
{
	if (fModalBounds == nil)
		return true;
	Boolean ok = PtInRect(MidPoint(bounds), fModalBounds);
	if (!ok && gRootView->fPopup != nil)
		gRootView->SetPopup(nil, true);
	return ok;
}


// ROM 0x0019d630 IgnoreClicks__19TRecognitionManagerFUl
// Clicks ignored for the ticks.
void
TRecognitionManager::IgnoreClicks(ULong ticks)
{
	fIgnoreClicksUntil = GetTicks() + ticks;
}


// ROM 0x0019d654 SetNextClick__19TRecognitionManagerFUl
// The ignoring is over unless the time falls within the second before it
// ends.
void
TRecognitionManager::SetNextClick(ULong time)
{
	if (time <= fIgnoreClicksUntil && fIgnoreClicksUntil - time < 61)
		return;
	fIgnoreClicksUntil = 0;
}


// ROM 0x0019d678 SaveClickView__19TRecognitionManagerFP5TView
// The view clicked, and the one before it.
void
TRecognitionManager::SaveClickView(TView* view)
{
	fPrevClickView = fClickView;
	fClickView = view;
}


// ROM 0x0019d688 RemoveClickView__19TRecognitionManagerFP5TView
// A view going away is forgotten.
void
TRecognitionManager::RemoveClickView(TView* view)
{
	if (fPrevClickView == view)
		fPrevClickView = nil;
	if (fClickView == view)
		fClickView = nil;
}


// ROM 0x0019e35c Idle__19TRecognitionManagerFv
// When started: the strokes idled, the stroke world's ink compressed, the
// controller idled (NOT YET RECONSTRUCTED: TController::Idle).
long
TRecognitionManager::Idle(void)
{
	if (fLevel != 0)
	{
		IdleStrokes();
		fStrokeWorld->IdleCompress();
		fController->Idle();
	}
	return 0;
}


// ROM 0x0019e394 NextIdle__19TRecognitionManagerFv
// When to idle next: when started, the stroke world's compress time, or
// the controller's next idle time (in milliseconds from now) when that
// is earlier (NOT YET RECONSTRUCTED: TController::NextIdleTime - none);
// zero when there is nothing to wait for.
TTime
TRecognitionManager::NextIdle(void)
{
	TTime next(0);
	if (fLevel != 0)
		next = fStrokeWorld->fNextCompressTime;
	return next;
}
