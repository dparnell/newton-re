/*
	File:		recognition/Recognizer.cpp

	Contains:	TRecognizer, TRecognizerList, the click and click-event
				recognisers, TRecognitionManager.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Recognizer.h"
#include "UnitPublic.h"
#include "Commands.h"
#include "RootView.h"
#include "Rects.h"
#include "NewtonExceptions.h"

TRecognitionManager	gRecognition;			// ROM 0x0c103f50 gRecognition


/*------------------------------------------------------------------------------
	T R e c o g n i z e r
------------------------------------------------------------------------------*/

// ROM 0x00145308 __ct__11TRecognizerFv
TRecognizer::TRecognizer()
{ }


// ROM 0x00145488 Init__11TRecognizerFP7TDomainUlT2UcT2
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


// ROM 0x0014535c InitServices__11TRecognizerFUlT1
void
TRecognizer::InitServices(ULong possible, ULong enabled)
{
	fServicesPossible = possible;
	fServicesEnabled = enabled;
}


// ROM 0x00145938 Domain__11TRecognizerFv
TDomain*
TRecognizer::Domain(void)
{
	return fDomain;
}


// ROM 0x00145f40 ID__11TRecognizerFv
ULong
TRecognizer::ID(void)
{
	return fID;
}


// ROM 0x0014604c Command__11TRecognizerFv
ULong
TRecognizer::Command(void)
{
	return fCommand;
}


// ROM 0x00146054 Flags__11TRecognizerFv
ULong
TRecognizer::Flags(void)
{
	return fFlags;
}


// ROM 0x0014605c TestFlags__11TRecognizerFUc
Boolean
TRecognizer::TestFlags(UChar flags)
{
	return (fFlags & flags) != 0;
}


// ROM 0x0014607c ServicesPossible__11TRecognizerFv
ULong
TRecognizer::ServicesPossible(void)
{
	return fServicesPossible;
}


// ROM 0x0014533c ServicesEnabled__11TRecognizerFv
ULong
TRecognizer::ServicesEnabled(void)
{
	return fServicesEnabled;
}


// ROM 0x00145344 UnitConfidence__11TRecognizerFP11TUnitPublic
long
TRecognizer::UnitConfidence(TUnitPublic* /*unit*/)
{
	return 0;
}


// ROM 0x0014534c Sleep__11TRecognizerFv
void
TRecognizer::Sleep(void)
{ }


// ROM 0x00145350 WakeUp__11TRecognizerFv
void
TRecognizer::WakeUp(void)
{ }


// ROM 0x00145354 ArbitrateTime__11TRecognizerFv
ULong
TRecognizer::ArbitrateTime(void)
{
	return fArbitrateTime;
}


// ROM 0x00145368 BuildConfig__11TRecognizerFRC6RefVarP5TViewUl
void
TRecognizer::BuildConfig(RefArg /*config*/, TView* /*view*/, ULong /*flags*/)
{ }


// ROM 0x0014536c EnableArea__11TRecognizerFP8TRecAreaRC6RefVar
// NOT YET RECONSTRUCTED: when the config's inputMask has one of the
// recogniser's enabled services, its type is added to the area
// (TRecArea::AddAType with the area's hit routine and the arbitrate time).
long
TRecognizer::EnableArea(TRecArea* /*area*/, RefArg /*config*/)
{
	return 0;
}


// ROM 0x00145418 ConfigureArea__11TRecognizerFP8TRecAreaRC6RefVar
long
TRecognizer::ConfigureArea(TRecArea* /*area*/, RefArg /*config*/)
{
	return 0;
}


// ROM 0x00146074 HandleUnit__11TRecognizerFP11TUnitPublic
// ==> the command to send the view for a unit.
ULong
TRecognizer::HandleUnit(TUnitPublic* /*unit*/)
{
	return fCommand;
}


// ROM 0x001454b8 GetLearningData__11TRecognizerFP11TUnitPublic
Ref
TRecognizer::GetLearningData(TUnitPublic* /*unit*/)
{
	return NILREF;
}


// ROM 0x001454c0 DoLearning__11TRecognizerFRC6RefVarl
void
TRecognizer::DoLearning(RefArg /*data*/, long /*arg*/)
{ }


/*------------------------------------------------------------------------------
	T R e c o g n i z e r L i s t
------------------------------------------------------------------------------*/

// ROM 0x001a0140 Make__15TRecognizerListSFv
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


// ROM 0x001a01a8 IRecognizerList__15TRecognizerListFv
long
TRecognizerList::IRecognizerList(void)
{
	return IArray(sizeof(TRecognizer*), 0);
}


// ROM 0x001a01b4 AddRecognizer__15TRecognizerListFP11TRecognizer
void
TRecognizerList::AddRecognizer(TRecognizer* recognizer)
{
	*(TRecognizer**) AddEntry() = recognizer;
}


// ROM 0x001a01d8 GetRecognizer__15TRecognizerListFUl
TRecognizer*
TRecognizerList::GetRecognizer(ULong index)
{
	return *(TRecognizer**) GetEntry(index);
}


// ROM 0x001a01f8 FindRecognizer__15TRecognizerListFUl
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

// ROM 0x00036a10 OtherViewInUse__FP5TView
// NOT YET RECONSTRUCTED: whether an area of the area cache (gAreaCache)
// other than the view's is in use (its use count above 0); the host has
// no area cache.
static Boolean
OtherViewInUse(TView* /*view*/)
{
	return false;
}


// ROM 0x00036a98 ClicksOnlyArea__FP5TUnit
// NOT YET RECONSTRUCTED: whether the unit's area accepts only clicks (one
// type, 'CLIK'); the host's units have no areas.
static Boolean
ClicksOnlyArea(TUnit* /*unit*/)
{
	return false;
}


// ROM 0x00209828 OnlyStrokeWritten__FP11TStrokeUnit
// NOT YET RECONSTRUCTED: whether the controller has seen no stroke after
// the unit's (it is the last complete stroke); the host has no controller.
Boolean
OnlyStrokeWritten(TStrokeUnit* /*unit*/)
{
	return true;
}


// ROM 0x0014578c HandleUnit__16TClickRecognizerFP11TUnitPublic
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
			gRecognition.fFlag38 = 1;
	}
	return command;
}


// ROM 0x00145668 ID__16TEventRecognizerFv
ULong
TEventRecognizer::ID(void)
{
	return kClickEventUnit;
}


// ROM 0x00145674 HandleUnit__16TEventRecognizerFP11TUnitPublic
// The command for the click event, when the click's stroke is the last
// written: aeTap; aeDoubleTap and aeTapDrag only when both clicks were on
// the same view; aeHiliteClick.
ULong
TEventRecognizer::HandleUnit(TUnitPublic* unit)
{
	ULong command = 0;
	TClickEventUnit* eventUnit = (TClickEventUnit*) unit->fUnit;
	if (OnlyStrokeWritten(nil))		// (the ROM: the controller's stroke unit at eventUnit->fMinStroke)
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


// ROM 0x00145830 InstallClickRecognizer__FP19TRecognitionManager
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


// ROM 0x00145704 InstallEventRecognizer__FP19TRecognitionManager
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

// ROM 0x001a03e0 Init__19TRecognitionManagerFUc
// The recognition system started at a level: 0 none, 1 clicks and strokes,
// 2 and above the shape and word recognisers too.
// NOT YET RECONSTRUCTED: the stroke world (StrokeCentral), the areas
// (InitAreas), the controller and its arbiter and hit-test routines, the
// dictionaries.
long
TRecognitionManager::Init(UChar level)
{
	fStrokeWorld = nil;
	fController = nil;
	fArbiter = nil;
	fAreas = nil;
	fIgnoreClicksUntil = 0;
	fFlag1c = 1;
	fLevel = level;
	fModalBounds = nil;
	fRecognizers = TRecognizerList::Make();
	if (fLevel != 0)
		InitRecognizers();
	return 0;
}


// ROM 0x0019f6f4 InitRecognizers__19TRecognitionManagerFv
// The recognisers installed and the root domain made.
// NOT YET RECONSTRUCTED: the gesture and stroke recognisers (their
// domains), the shape and word recognisers of level 2, ReadDomainOptions.
long
TRecognitionManager::InitRecognizers(void)
{
	if (fLevel != 0)
	{
		InstallEventRecognizer(this);
		InstallClickRecognizer(this);
		gRootDomain = TDomain::Make(fController, kRootDomainType, (char*) "TDomain");
	}
	return 0;
}


// ROM 0x0019f7a4 EnableModalRecognition__19TRecognitionManagerFR5TRect
// Recognition confined to the bounds (a modal dialog's).
void
TRecognitionManager::EnableModalRecognition(Rect& bounds)
{
	if (fModalBounds != nil)
		ThrowMsg("Can't nest modal bounds");
	fModalBounds = new Rect;
	if (fModalBounds != nil)
		*fModalBounds = bounds;
}


// ROM 0x0019f7fc DisableModalRecognition__19TRecognitionManagerFv
void
TRecognitionManager::DisableModalRecognition(void)
{
	if (fModalBounds != nil)
		delete fModalBounds;
	fModalBounds = nil;
}


// ROM 0x0019f824 ModalRecognitionOK__19TRecognitionManagerFR5TRect
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


// ROM 0x0019f8ec IgnoreClicks__19TRecognitionManagerFUl
// Clicks ignored for the ticks.
void
TRecognitionManager::IgnoreClicks(ULong ticks)
{
	fIgnoreClicksUntil = GetTicks() + ticks;
}


// ROM 0x0019f910 SetNextClick__19TRecognitionManagerFUl
// The ignoring is over unless the time falls within the second before it
// ends.
void
TRecognitionManager::SetNextClick(ULong time)
{
	if (time <= fIgnoreClicksUntil && fIgnoreClicksUntil - time < 61)
		return;
	fIgnoreClicksUntil = 0;
}


// ROM 0x0019f934 SaveClickView__19TRecognitionManagerFP5TView
// The view clicked, and the one before it.
void
TRecognitionManager::SaveClickView(TView* view)
{
	fPrevClickView = fClickView;
	fClickView = view;
}


// ROM 0x0019f944 RemoveClickView__19TRecognitionManagerFP5TView
// A view going away is forgotten.
void
TRecognitionManager::RemoveClickView(TView* view)
{
	if (fPrevClickView == view)
		fPrevClickView = nil;
	if (fClickView == view)
		fClickView = nil;
}


// ROM 0x001a0618 Idle__19TRecognitionManagerFv
// NOT YET RECONSTRUCTED: IdleStrokes, the stroke world's IdleCompress and
// the controller's Idle.
long
TRecognitionManager::Idle(void)
{
	return 0;
}
