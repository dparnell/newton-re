/*
	File:		recognition/Recognizer.cpp

	Contains:	TRecognizer, TRecognizerList, the click and click-event
				recognisers, TRecognitionManager.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Recognizer.h"
#include <stdio.h>
#include <stdlib.h>
#include "WRecDomain.h"
#include "WordInfo.h"
#include "Words.h"			// gWordID
#include "Dictionaries.h"	// InitDictionaries
#include "RecConfig.h"
#include "Areas.h"
#include "Controller.h"
#include "StrokeQueue.h"	// gDoubleTapInterval
#include "Entries.h"		// EntryChange, EntryValid
#include "Protocols.h"
#include "NewtonTime.h"
#include "Locale.h"			// GetPreference
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
#include "ShapeDomain.h"		// SetContextUnitRoutine
#include "WordRecognizer.h"	// InstallWordRecognizer
#include "WordRecog.h"		// FragmentLigatures
#include "NewtonGestalt.h"
#include "ROMConstants.h"
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


/*------------------------------------------------------------------------------
	T h e   w o r d   r e c o g n i s e r
------------------------------------------------------------------------------*/

// ROM 0x0c101688 gRecInkNotifyFlags / 0x0c101684 gLastInkWordWarning
// Both start at zero and nothing in the ROM's own code ever writes the
// flags - they are there for a patch or a diagnostic build to turn the
// warnings on with.  Bit 1 warns about the recogniser running out of
// memory, bit 4 warns the writer the first time something is left as
// ink, and bit 8 decides which of the two warnings the script shows.
ULong	gRecInkNotifyFlags = 0;
ULong	gLastInkWordWarning = 0;

// How many minutes the real-time clock counts to a day.
enum { kMinutesPerDay = 24 * 60 };


// ROM 0x00143dec GetInkCommand__FRC6RefVar
// Which command a view is sent for writing that was not read: the view
// under the middle of the writing is asked what it wants.  A view whose
// recognition configuration has `doInkWordRecognition` takes the
// writing as an ink *word* - something that sits in a line of text and
// may be recognised later - and any other view takes it as raw ink, to
// be drawn where it was written.  ==> 0 when there is no writing or no
// view under it.
ULong
GetInkCommand(RefArg wordInfo)
{
	ULong command = 0;
	RefVar strokes(GetFrameSlot(wordInfo, RSSYMstrokes));
	if (ISNIL(strokes))
		return command;
	Rect box;
	FromObject(RefVar(GetFrameSlot(strokes, RSSYMbounds)), box);
	TView* view = gRootView->FindView(MidPoint(box), vAnythingAllowed, nil);
	if (view == nil)
		return command;
	RefVar config(BuildRecConfig(view, view->fFlags & vRecognitionAllowed));
	command = ISNIL(RefVar(GetVariable(config, RSSYMdoinkwordrecognition, nil, 0)))
			  ? (ULong) aeRawInk : (ULong) aeInkWord;
	return command;
}


// ROM 0x00143f00 WordRecognizerHandleUnit__FP11TRecognizerP11TUnitPublic
// What a word unit that has won its arbitration comes to.  The base
// line is worked out first, because everything that lays the writing
// out wants it.  Then:
//
//   - a unit small enough to be a tap is a tap, whatever was written in
//     it;
//   - a unit the engine could not read is ink, and which ink command
//     the view gets is `GetInkCommand`'s business;
//   - anything else is a word, and the recogniser's own command
//     (aeWord) stands.
//
// The two warnings hang off the same call because this is the one place
// that knows both that something was left as ink and that the
// recogniser has been running out of memory.  Each is shown at most
// once a day.
ULong
WordRecognizerHandleUnit(TRecognizer* recognizer, TUnitPublic* unit)
{
	ULong command = recognizer->Command();
	unit->SetWordBase();
	if (unit->IsTap())
		command = aeTap;
	else if (recognizer->UnitConfidence(unit) == kWRecInk)
	{
		RefVar info(unit->WordInfo());
		SetWordInfoFlags(info, kWordInfoIsInk);
		command = GetInkCommand(info);
		if ((gRecInkNotifyFlags & 4) != 0)
		{
			ULong today = RealClock() / kMinutesPerDay;
			if (today > gLastInkWordWarning)
			{
				NSCallGlobalFn(RSSYMrecognitioninkwordwarning,
							   RefVar((gRecInkNotifyFlags & 8) != 0 ? TRUEREF : NILREF));
				gLastInkWordWarning = today;
			}
		}
	}
	if (gRecMemErrCount != 0 && (gRecInkNotifyFlags & 1) != 0)
	{
		RefVar warned(GetPreference(RSSYMlastrecmemwarning));
		long day = ISINT((Ref) warned) ? RINT(warned) : 0;
		// having already told the writer today, the errors are forgotten
		if (RealClock() / kMinutesPerDay == (ULong) day)
			gRecMemErrCount = 0;
	}
	return command;
}


// ROM 0x00144238 UnitConfidence__15TWRecRecognizerFP11TUnitPublic
long
TWRecRecognizer::UnitConfidence(TUnitPublic* unit)
{
	return ((TWRecDomain*) Domain())->UnitConfidence((TSIUnit*) unit->fUnit);
}


// ROM 0x00144260 Sleep__15TWRecRecognizerFv
void
TWRecRecognizer::Sleep(void)
{
	((TWRecDomain*) Domain())->Sleep();
}


// ROM 0x00144280 WakeUp__15TWRecRecognizerFv
void
TWRecRecognizer::WakeUp(void)
{
	((TWRecDomain*) Domain())->WakeUp();
}


// ROM 0x00144174 HandleUnit__15TWRecRecognizerFP11TUnitPublic
// One instruction in the ROM: a branch straight to the shared handler,
// which the Airus word recogniser uses as well.
ULong
TWRecRecognizer::HandleUnit(TUnitPublic* unit)
{
	return WordRecognizerHandleUnit(this, unit);
}


// ROM 0x00144178 ConfigureArea__15TWRecRecognizerFP8TRecAreaRC6RefVar
// An area the word domain is running over, set up from its recognition
// configuration: the engine's own parameter block for the area is made
// if it has none, filled in from the configuration, and the area's
// three dictionary chains are built alongside it.  The domain is then
// told its parameters are complete.
//
// An area this domain is not running over has nothing to set up.
long
TWRecRecognizer::ConfigureArea(TRecArea* area, RefArg config)
{
	if (!DomainOn(area, ID()))
		return 0;
	TWRecDomain* domain = (TWRecDomain*) Domain();
	Handle info = area->GetInfoFor(kWRecDomainType, true);
	domain->ConfigureArea(config, (ULong) info);
	TDictChain* chains[kAreaDictChains];
	BuildChains(chains, config);
	for (long i = 0; i < kAreaDictChains; i++)
		area->fDictionaries[i] = chains[i];
	area->ParamsAllSet(kWRecDomainType);
	return 0;
}


// ROM 0x00144380 GetIDFromRef__FRC6RefVar
// A four-character type out of a four-character string, a byte from
// each character.  ==> 0 for anything else.
ULong
GetIDFromRef(RefArg spec)
{
	if (!IsString(spec))
		return 0;
	const UniChar* text = GetCString(spec);
	if (Ustrlen(text) != 4)
		return 0;
	return ((ULong) text[0] << 24) | ((ULong) (text[1] & 0xff) << 16)
		   | ((ULong) (text[2] & 0xff) << 8) | (ULong) (text[3] & 0xff);
}


// ROM 0x001442a0 (unnamed) - SetWordRecognizer
// Which word recogniser is in use.  Only one may be: the one in use has
// its services turned off and is put to sleep, and the one taking over
// has its services turned back on and is woken.  `gWordID` holds the
// unit type of whichever it is, which is how everything else - the word
// list, the arbiter, the natives - knows which units carry readings.
// The area cache is purged because the services a recogniser offers are
// what areas are built from.  ==> whether there is now one in use.
Boolean
SetWordRecognizer(ULong id)
{
	Boolean set = false;
	TRecognizer* wanted = gRecognition.fRecognizers->FindRecognizer(id);
	TRecognizer* current = gRecognition.fRecognizers->FindRecognizer(gWordID);
	if (current != nil)
	{
		current->InitServices(current->ServicesPossible(), 0);
		current->Sleep();
		gWordID = 0;
	}
	if (wanted != nil)
	{
		wanted->InitServices(wanted->ServicesPossible(), wanted->ServicesPossible());
		gWordID = id;
		wanted->WakeUp();
		set = true;
	}
	PurgeAreaCache();
	return set;
}


// ROM 0x001443f4 FUseWRec
// UseWRec("XRWR"): the word recogniser named by its four characters put
// in use.  ==> true when it is (including when it already was), nil
// when there is no such recogniser.
Ref
FUseWRec(RefArg /*rcvr*/, RefArg name)
{
	if (!IsString(name))
		return NILREF;
	ULong id = GetIDFromRef(name);
	if (id == gWordID)
		return TRUEREF;
	return SetWordRecognizer(id) ? TRUEREF : NILREF;
}


// ROM 0x001b5bb4 RegisterWRec__Fv
// NOT YET RECONSTRUCTED: the ROM's own handwriting engine (the CIC
// library's) registering itself as an implementation of TWRecognizer.
// With nothing registered, InstallWRecRecognizer finds no engine and
// installs no recogniser - which is also what a host that has not
// supplied an engine of its own wants.
void
RegisterWRec(void)
{
}


// ROM 0x00144094 InstallWRecRecognizer__FP19TRecognitionManager
// The recogniser that drives a handwriting engine through the
// TWRecognizer protocol.  The engine registers itself first, unless the
// `inhibitBaseRomWRecRegistration` preference says to leave the ROM's
// own alone and let something else supply one; if there is no engine at
// all there is nothing to install.
//
// It is installed asleep.  Nothing wakes it until `SetWordRecognizer`
// puts it in use, which is what stops two word recognisers reading the
// same writing.
void
InstallWRecRecognizer(TRecognitionManager* manager)
{
	if (ISNIL(RefVar(GetPreference(RSSYMinhibitbaseromwrecregistration))))
		RegisterWRec();
	if (ClassInfoByName("TWRecognizer", nil, 0) == nil)
		return;
	TDomain* domain = TWRecDomain::Make(manager->fController);
	if (domain == nil)
		return;
	TWRecRecognizer* recognizer = new TWRecRecognizer;
	recognizer->Init(domain, domain->fType, aeWord, kRecognizerIsWriting, 1);
	recognizer->InitServices(kWRecServices, 0);
	manager->fRecognizers->AddRecognizer(recognizer);
	recognizer->Sleep();
}


/*------------------------------------------------------------------------------
	T h e   w r i t e r ' s   p r e f e r e n c e s
------------------------------------------------------------------------------*/

// ROM 0x0c10184c gLetterSetSelection / 0x0c101850 gRecognitionTimeout /
// 0x0c101858 gRecognitionLetterSpacing / 0x0c101868 gUseBigTrainingData
//
// What the writer has asked for, read out of the preferences at boot
// and whenever they are changed.
long	gLetterSetSelection = 2;
ULong	gRecognitionTimeout = 0x28;
long	gRecognitionLetterSpacing = 5;
Boolean	gUseBigTrainingData = false;


// ROM 0x0019cc04 GetDefaultedPreference__FRC6RefVarl
// A preference, and the default written down as the writer's own when
// there is none.  The configuration is a soup entry, so it is told it
// has changed.
long
GetDefaultedPreference(RefArg slot, long deflt)
{
	RefVar value(GetPreference(slot));
	if (NOTNIL(value))
		return RINT(value);
	RefVar config(GetFrameSlotRef(RefVar(gVarFrame), RSSYMuserconfiguration));
	SetVariable(config, slot, RefVar(MAKEINT(deflt)));
	// (DEVIATION: the ROM tells the entry it has changed without
	//  looking, the configuration always being one on a machine that has
	//  booted.  A host program running part of the system may have a
	//  plain frame there, and telling a frame it has changed throws.)
	if (EntryValid(config))
		EntryChange(config);
	return deflt;
}


// ROM 0x0019cfd8 FReadCursiveOptions__FRC6RefVar
// ReadCursiveOptions(): the recognition preferences read and put into
// force.  The Prefs slip calls it whenever the writer changes one of
// them, and the boot calls it through ReadDomainOptions.
//
// The timeout is how long the recogniser waits after the pen stops
// before it decides the writing is finished, in sixtieths of a second,
// and it is kept between a quarter of a second and a second.  Every
// domain that waits at all is made to wait that long, and the interval
// two taps have to fall within to count as a double tap is worked out
// from it: half way between a quarter of a second and the timeout.  The
// letter spacing is stored the other way up from the way it is asked
// for - nine less what the writer chose - because the recogniser wants
// how *close* letters may be, and the slip offers how far apart.
//
// The letter set says which of the two word recognisers is in use
// (SetUpRosetta, SetUpParaGraph).
Ref
FReadCursiveOptions(RefArg /*rcvr*/)
{
	gLetterSetSelection = GetDefaultedPreference(RSSYMlettersetselection, 2);
	SetUpRosetta(gLetterSetSelection);
	SetUpParaGraph(gLetterSetSelection);

	gRecognitionTimeout = (ULong) GetDefaultedPreference(RSSYMtimeoutcursiveoption, 0x28);
	if (gRecognitionTimeout < 0xf)
		gRecognitionTimeout = 0xf;
	else if (gRecognitionTimeout > 0x3c)
		gRecognitionTimeout = 0x3c;
	SetDomainDelays(gController, gRecognitionTimeout);
	gDoubleTapInterval = ((gRecognitionTimeout - 0xf) >> 1) + 0xf;

	gRecognitionLetterSpacing = 9 - GetDefaultedPreference(RSSYMletterspacecursiveoption, 4);

	// the configuration's own input mask, which every area built from it
	// starts with: strokes and gestures, plus whatever the
	// text-recognition options add
	RefVar prefs(GetFrameSlotRef(RefVar(gVarFrame), RSSYMuserconfiguration));
	SetFrameSlot(prefs, RSSYMinputmask,
				 RefVar(MAKEINT(BuildInputMask(prefs, vStrokesAllowed | vGesturesAllowed, true))));

	// the language the dictionaries are read in: 8 when the locale names
	// one, 1 when it does not
	gEnabledLanguage = NOTNIL(RefVar(GetLocaleSlot(RSSYMenabledlanguage))) ? 8 : 1;

	// ... and the words it reads against, which the locale may replace
	ReadDictPrefs();

	RefVar config(GetFrameSlotRef(RefVar(gVarFrame), RSSYMuserconfiguration));
	gSaveWordTrainingData =
		NOTNIL(RefVar(GetProtoVariable(config, RSSYMlearningenabledoption, nil)));
	gUseBigTrainingData =
		NOTNIL(RefVar(GetProtoVariable(config, RSSYMbiglearningenabled, nil)));

	// the areas were built from the old answers
	PurgeAreaCache();
	return NILREF;
}


// ROM 0x0019ccd4 SetUpRosetta__FUl
// Letter set 2, printed writing: Rosetta put in use, writing read a word
// at a time, and whether its engine cuts ligatures apart - the
// `doFragmentation` preference, and when that is 'default whether the
// processor runs faster than 90 MHz.  The Handwriting Recognition slip
// is told which recognisers there are to choose among
// (`_recognizerUserChoices` on the root view, its text choices Rosetta's).
void
SetUpRosetta(ULong letterSet)
{
	if (letterSet != 2)
		return;
	RefVar name(MakeString("WREC"));
	FUseWRec(RefVar(), name);
	SetPreference(RSSYMcurrentwordrecognizer, name);
	SetPreference(RSSYMlineatatime, RefVar());
	RefVar fragment(GetPreference(RSSYMdofragmentation));
	if (EQ(fragment, RSSYMdefault))
	{
		TUGestalt gestalt;
		TGestaltSystemInfo info;
		fragment = (gestalt.Gestalt(kGestalt_SystemInfo, &info, sizeof(info)) == noErr
					&& (long) info.fCpuSpeed > 0x5a0000) ? TRUEREF : NILREF;
	}
	FragmentLigatures = NOTNIL(fragment);
	if (gRootView != nil)
	{
		RefVar choices(Clone(RefVar(Rrecognizeruserchoices)));
		RefVar recognizers(Clone(RefVar(GetFrameSlotRef(choices, RSSYMrecognizers))));
		SetFrameSlot(choices, RSSYMrecognizers, recognizers);
		RefVar text(Clone(RefVar(GetFrameSlotRef(recognizers, RSSYMtext))));
		SetFrameSlot(recognizers, RSSYMtext, text);
		SetFrameSlot(text, RSSYMchoices, RefVar(Rrosettachoices));
		gRootView->SetContextSlot(RSSYM_recognizeruserchoices, choices);
	}
}


// ROM 0x0019cf24 SetUpParaGraph__FUl
// Any other letter set: the cursive recogniser put in use, writing read
// a line at a time.
void
SetUpParaGraph(ULong letterSet)
{
	if (letterSet == 2)
		return;
	RefVar name(MakeString("XRWR"));
	FUseWRec(RefVar(), name);
	SetPreference(RSSYMcurrentwordrecognizer, name);
	SetPreference(RSSYMlineatatime, RefVar(TRUEREF));
	if (gRootView != nil)
		gRootView->SetContextSlot(RSSYM_recognizeruserchoices, RefVar(Rrecognizeruserchoices));
}


// ROM 0x001a0cc4 DoIndexedLearning__FUlRC6RefVarT1
// What the writer settled on handed to the recogniser that read it, by
// unit type, so that it reads the same writing better next time.
void
DoIndexedLearning(ULong id, RefArg data, ULong which)
{
	if (ISNIL(data))
		return;
	gRecognition.fRecognizers->FindRecognizer(id)->DoLearning(data, (long) which);
}


// ROM 0x0019d1e0 ReadDomainOptions
// What the boot calls: the cursive options and nothing else.
Ref
ReadDomainOptions(void)
{
	FReadCursiveOptions(RefVar(NILREF));
	return NILREF;
}


// ROM 0x0019d338 GetCommand__FUl
ULong
GetCommand(ULong type)
{
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(type);
	if (recognizer == nil)
		return 0;
	return recognizer->Command();
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
		SetContextUnitRoutine(HandleGetContextUnits);
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
	if (fLevel >= 2)
	{
		InstallShapeRecognizer(this);
		InstallWordRecognizer(this);
		InstallWRecRecognizer(this);
	}
	// the writer's recognition preferences put into force
	ReadDomainOptions();
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


#pragma mark - saving the state

// what TRecognitionManager::SaveRecognitionState puts aside (0x14 bytes
// in the ROM)
struct RecognitionState
{
	ULong				fUnused3c;
	ULong				fIgnoreClicksUntil;
	Boolean				fAfterWriting;
	Boolean				fClickSwallowed;
	StrokeCentralState*	fStrokeWorld;
	ControllerState*	fController;
};


// ROM 0x0019e21c SaveRecognitionState__19TRecognitionManagerFPUc
// The manager's own fields, the stroke world's and the controller's put
// aside and started again, so that what a modal dialog's fork recognises
// has nothing to do with what was going on when it opened; the area cache
// is purged either way.
RecognitionState*
TRecognitionManager::SaveRecognitionState(UChar* failed)
{
	*failed = false;
	RecognitionState* state = new RecognitionState;
	if (state == nil)
		*failed = true;
	else
	{
		state->fUnused3c = fUnused3c;
		state->fIgnoreClicksUntil = fIgnoreClicksUntil;
		state->fAfterWriting = fAfterWriting;
		state->fClickSwallowed = fClickSwallowed;
		UChar strokesFailed, controllerFailed;
		state->fStrokeWorld = gStrokeWorld.SaveRecognitionState(&strokesFailed);
		state->fController = ::SaveRecognitionState(gController, &controllerFailed);
		if (strokesFailed || controllerFailed)
			*failed = true;
		fIgnoreClicksUntil = 0;
		fAfterWriting = false;
		fClickSwallowed = false;
		fPrevClickView = nil;
		fClickView = nil;
	}
	PurgeAreaCache();
	return state;
}


// ROM 0x0019e2e0 RestoreRecognitionState__19TRecognitionManagerFUl
// ... put back (the click views forgotten), and the area cache purged
// again (the ROM has PurgeAreaCache 0x0003485c inline).
void
TRecognitionManager::RestoreRecognitionState(RecognitionState* state)
{
	if (state != nil)
	{
		fUnused3c = state->fUnused3c;
		fIgnoreClicksUntil = state->fIgnoreClicksUntil;
		fAfterWriting = state->fAfterWriting;
		fClickSwallowed = state->fClickSwallowed;
		gStrokeWorld.RestoreRecognitionState(state->fStrokeWorld);
		::RestoreRecognitionState(gController, state->fController);
		fPrevClickView = nil;
		fClickView = nil;
		delete state;
	}
	PurgeAreaCache();
}
