/*
	File:		recognition/Controller.cpp

	Contains:	TController, the recogniser's engine (Controller.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Controller.h"
#include "Domain.h"
#include "Arbiter.h"
#include "Stroke.h"
#include "StrokeCentral.h"
#include "StrokeQueue.h"

TController*	gController = nil;					// ROM 0x0c10187c gController


// ROM 0x0020c4b4 ClickInProgress__FP5TUnit
// Whether a unit is the click of the stroke the pen is still writing.
Boolean
ClickInProgress(TUnit* unit)
{
	return unit->fType == kClickUnit && unit->TestFlags(kUnitStrokeInProgress);
}


// ROM 0x0021c68c UnitsHitSameArea__FP5TUnitT1
// Whether two units were written in the same view.  The areas themselves
// are not compared - two strokes in one view may have been given areas
// built at different times - but the view they stand for is.
Boolean
UnitsHitSameArea(TUnit* a, TUnit* b)
{
	TRecArea* areaA = a->GetArea();
	TRecArea* areaB = b->GetArea();
	return areaA != nil && areaB != nil && areaA->fViewId == areaB->fViewId && areaA->fViewId != 0;
}


// ROM 0x0020b58c TimeOutSubs__FP7TSIUnit
// A unit's subs, and their subs, made ready at once.
void
TimeOutSubs(TSIUnit* unit)
{
	long count = unit->SubCount();
	for (long i = 0; i < count; i++)
	{
		TSIUnit* sub = (TSIUnit*) unit->GetSub(i);
		sub->SetDelay(0);
		if (sub->SubCount() != 0)
			TimeOutSubs(sub);
	}
}


// ROM 0x0020ac84 HandleAreaSwitched__FP7TDomainPPc
// A domain has just been given another area's parameter block.  Every
// unit of its own making that is still waiting (delayed, and neither
// claimed nor invalidated) and whose area wants a *different* block is
// made ready now - it cannot be left to be worked on with parameters that
// are no longer its own.
void
HandleAreaSwitched(TDomain* domain, Handle params)
{
	ULong type = domain->fType;
	TArrayIterator iter;
	TUnit** entry = (TUnit**) domain->fController->fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *entry;
		if (unit->fType == type && unit->fDomain == domain
			&& unit->TestFlags(kDelayedUnit) && !unit->TestFlags(kClaimedUnit | kInvalidatedUnit))
		{
			if (unit->GetArea()->GetInfoFor(type, false) != params)
				unit->SetDelay(0);
		}
	}
}


// ROM 0x00209e84 Make__11TControllerSFv
TController*
TController::Make(void)
{
	TController* controller = new TController;
	if (controller != nil)
		controller->IController();
	return controller;
}


// ROM 0x00209ec4 InitControllerState__FP11TController
// The lists a controller starts and starts again with: the pieces, the
// units, the group queue, and the four passes all not due.
static Boolean
InitControllerState(TController* controller)
{
	controller->fFlags = 0;
	controller->fPieces = TUnitList::Make();
	controller->fUnits = TUnitList::Make();
	controller->fGroupQ = TArray::Make(sizeof(GroupEntry), 0);
	controller->fClassifyTime = 0xffffffff;
	controller->fGroupTime = 0xffffffff;
	controller->fArbitrateTime = 0xffffffff;
	controller->fCleanUpTime = 0xffffffff;
	return controller->fPieces == nil || controller->fUnits == nil || controller->fGroupQ == nil;
}


// ROM 0x0020a7a8 IController__11TControllerFv
void
TController::IController(void)
{
	InitControllerState(this);
	fDomains = TArray::Make(sizeof(TDomain*), 0);
	// (the ROM leaves fArbiter as it found it: TArbiter::Make registers
	// itself before anything reads it)
	fArbiter = nil;
	fNextStroke = 0;
	fHitTest = nil;
	fExpireStroke = nil;
	fInArea = false;
	fAreaCount = 0;
	fAreaDone = 0;
	fAreaArg = 0;
	fArea = nil;
	fSavedHitTest = nil;
	fSavedExpire = nil;
	fAreaHandler = nil;
}


// ROM 0x0020b1e8 Dispose__11TControllerFv
void
TController::Dispose(void)
{
	fPieces->Purge();
	fPieces->Dispose();
	fUnits->Purge();
	fUnits->Dispose();
	TArrayIterator iter;
	TDomain** entry = (TDomain**) fDomains->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (TDomain**) iter.GetNext())
		(*entry)->Dispose();
	fDomains->Dispose();
	fGroupQ->Dispose();
	delete this;
}


// ROM 0x00209f34 RegisterDomain__11TControllerFP7TDomain
void
TController::RegisterDomain(TDomain* domain)
{
	domain->fController = this;
	*(TDomain**) fDomains->AddEntry() = domain;
	fDomains->Compact();
}


// ROM 0x00209f70 RegisterArbiter__11TControllerFP8TArbiter
void
TController::RegisterArbiter(TArbiter* arbiter)
{
	fArbiter = arbiter;
}


// ROM 0x0020c6fc GetTypedDomain__11TControllerFUl
TDomain*
TController::GetTypedDomain(ULong type)
{
	TArrayIterator iter;
	TDomain** entry = (TDomain**) fDomains->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (TDomain**) iter.GetNext())
	{
		if ((*entry)->fType == type)
			return *entry;
	}
	return nil;
}


// ROM 0x0020c554 Initialize__11TControllerFv
// Every domain given its distance from the strokes: the stroke domains
// are 2, the domains that take *their* type are 3, and so on outwards.
// The walk is a breadth-first one over the piece types, with two lists
// swapped at each step - the domains found last time, and the ones their
// types lead to.  Nothing stops a domain being reached twice, so a domain
// in two rings keeps the larger number.
void
TController::Initialize(void)
{
	TArray* thisRing = TArray::Make(sizeof(TDomain*), 0);
	TArray* nextRing = TArray::Make(sizeof(TDomain*), 0);

	TArrayIterator iter;
	TDomain** entry = (TDomain**) fDomains->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (TDomain**) iter.GetNext())
	{
		if ((*entry)->fType == kStrokeUnit)
		{
			*(TDomain**) thisRing->AddEntry() = *entry;
			(*entry)->fLevel = 2;
		}
	}

	long level = 2;
	while (thisRing->Count() != 0)
	{
		nextRing->CutToIndex(0);
		level++;
		TArrayIterator ringIter;
		TDomain** ringEntry = (TDomain**) thisRing->GetIterator(&ringIter);
		for (ULong i = 0; i < (ULong) ringIter.fCount; i++, ringEntry = (TDomain**) ringIter.GetNext())
		{
			ULong type = (*ringEntry)->fType;
			TArrayIterator allIter;
			TDomain** all = (TDomain**) fDomains->GetIterator(&allIter);
			for (ULong j = 0; j < (ULong) allIter.fCount; j++, all = (TDomain**) allIter.GetNext())
			{
				if ((*all)->fPieceTypes->FindType(type) != (ULong) -1)
				{
					*(TDomain**) nextRing->AddEntry() = *all;
					(*all)->fLevel = level;
				}
			}
		}
		TArray* swap = thisRing;
		thisRing = nextRing;
		nextRing = swap;
	}
	thisRing->Dispose();
	nextRing->Dispose();
}


// ROM 0x0020bdf4 SignalMemoryError__11TControllerFv
// The controller is out of memory: it cannot be trusted to hold anything
// more, so a clean-up is asked for at once, and the arbiter - if it has
// something waiting - is asked to arbitrate first so that what has been
// worked out so far still reaches the views.
void
TController::SignalMemoryError(void)
{
	SetFlags(kControllerError);
	fCleanUpTime = GetTicks();
	if (fArbiter->fWaiting)
	{
		fArbitrateTime = GetTicks();
		fArbiter->fArbitrateNow = true;
	}
}


// ROM 0x0020be3c ControllerError__11TControllerFv
Boolean
TController::ControllerError(void)
{
	return (fFlags & kControllerError) != 0;
}


// ROM 0x0020bc68 CheckBusy__11TControllerFv
Boolean
TController::CheckBusy(void)
{
	return TestFlags(kControllerBusy);
}


// ROM 0x0020ab08 TriggerRecognition__11TControllerFv
void
TController::TriggerRecognition(void)
{
	ULong now = GetTicks();
	fClassifyTime = now;
	fGroupTime = now;
	fArbitrateTime = now;
	fCleanUpTime = now;
}


// The answer Idle and NextIdleTime share: how long to wait before the
// next pass falls due, in milliseconds, or -1 for "nothing to wait for".
// With no pass due and a click still being written the answer is also -1
// (the pen itself will wake the recogniser); with no pass due, no click
// and pieces left over, something has gone wrong and the controller says
// so.  A modal arbiter (one that has been told to arbitrate now) turns
// any wait into no wait at all.
long
TController::NextIdleTime(void)
{
	ULong next = fClassifyTime;
	if (fGroupTime <= next)
		next = fGroupTime;
	if (fArbitrateTime <= next)
		next = fArbitrateTime;
	if (fCleanUpTime < next)
		next = fCleanUpTime;

	ULong now = GetTicks();
	if (next == 0xffffffff)
	{
		ULong count = fPieces->Count();
		if (count == 0)
			return -1;
		for (ULong i = 0; i < count; i++)
		{
			if (ClickInProgress(fPieces->GetUnit(i)))
				return -1;
		}
		SignalMemoryError();
		return 0;
	}

	if (fArbiter->fWaiting
		&& fArbitrateTime == 0xffffffff && fClassifyTime == 0xffffffff && fGroupTime == 0xffffffff)
	{
		// the arbiter is waiting on units that will never come: let it go
		fArbiter->fArbitrateNow = true;
	}
	else if (!fArbiter->fArbitrateNow)
	{
		if ((long) (next - now) < 1)
			return 0;
		return (long) ((next - now) * 1000 / 60);
	}
	gController->fArbitrateTime = GetTicks();
	return 0;
}


// ROM 0x0020aa04 Idle__11TControllerFv
// The four passes, each run when its time has come (inside
// RecognizeInArea every pass runs every time).  An error in any of them
// stops the rest and throws the state away.
long
TController::Idle(void)
{
	Boolean internal = TestFlags(kControllerInternal);
	if (ControllerError())
		CleanupAfterError();
	else
	{
		Boolean done = false;
		if (internal || fGroupTime <= GetTicks())
		{
			DoGroup();
			if (ControllerError())
				done = true;
		}
		if (!done && (internal || fClassifyTime <= GetTicks()))
		{
			DoClassify();
			if (ControllerError())
				done = true;
		}
		if (!done && (internal || fArbitrateTime <= GetTicks()))
		{
			DoArbitration();
			if (ControllerError())
				done = true;
		}
		if (!done && (internal || fCleanUpTime <= GetTicks()))
		{
			CleanUp();
			if (ControllerError())
				done = true;
		}
		if (done)
			CleanupAfterError();
	}
	return NextIdleTime();
}


// ROM 0x0020ac60 DoArbitration__11TControllerFv
void
TController::DoArbitration(void)
{
	fArbiter->DoArbitration();
	fArbitrateTime = 0xffffffff;
}


// ROM 0x0020b688 CleanUp__11TControllerFv
void
TController::CleanUp(void)
{
	fArbiter->CleanUp();
	fPieces->Compact();
	fUnits->Compact();
	fGroupQ->Compact();
	fCleanUpTime = 0xffffffff;
}


// ROM 0x0020ad54 DoGroup__11TControllerFv
// Every entry of the group queue offered to its domain.  An entry whose
// piece has gone (nil) or has been claimed is dropped; an entry the
// domain does not take yet (Group answers 0) is kept, written back at the
// front of the queue, and the queue cut to what was kept.
//
// The two counters are the ROM's guard against a domain that never takes
// anything: once two 'STXR' or two 'CLIK' entries have been kept the rest
// of the queue is copied down without being offered at all, and the group
// pass is made due again straight away rather than waiting.
void
TController::DoGroup(void)
{
	long kept = 0;
	ULong strxr = 0;
	ULong clik = 0;
	TArrayIterator iter;
	GroupEntry* entry = (GroupEntry*) fGroupQ->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++)
	{
		if (entry->fPiece == nil || entry->fPiece->TestFlags(kClaimedUnit))
		{
			entry = (GroupEntry*) iter.GetNext();
			continue;
		}
		if ((entry->fPiece->fType == 'STXR' && ++strxr >= 2)
			|| (entry->fPiece->fType == kClickUnit && ++clik >= 2))
		{
			*(GroupEntry*) fGroupQ->GetEntry(kept++) = *(GroupEntry*) fGroupQ->GetEntry(i);
			entry = (GroupEntry*) iter.GetNext();
			continue;
		}

		TDomain* domain = entry->fDomain;
		if (entry->fParams != domain->fParameters)
		{
			domain->SetParameters(entry->fParams);
			entry = (GroupEntry*) iter.GetCur();
			entry->fDomain->fParameters = entry->fParams;
			HandleAreaSwitched(entry->fDomain, entry->fParams);
		}
		if (entry->fDomain->Group(entry->fPiece, entry->fInfo) == 0)
		{
			*(GroupEntry*) fGroupQ->GetEntry(kept++) = *(GroupEntry*) fGroupQ->GetEntry(i);
		}
		if (ControllerError())
			break;
		entry = (GroupEntry*) iter.GetNext();
	}
	fGroupQ->CutToIndex(kept);
	fGroupTime = (strxr >= 2 || clik >= 2) ? GetTicks() : 0xffffffff;
}


// ROM 0x0020af60 DoClassify__11TControllerFv
// Every unit handed to the domain that made it, which turns it into a
// piece for the domains above (TDomain::Classify is NewClassification).
// A unit that is still waiting is left, and the classify pass asked for
// again at the time its delay runs out; one whose delay has run out but
// whose area has seen something written since is given a new delay by
// NoEventsWithinDelay.  Afterwards the units that were handed on are
// taken off the list.  ==> whether a unit had no area, which is a
// memory error.
Boolean
TController::DoClassify(void)
{
	Boolean noArea = false;
	fClassifyTime = 0xffffffff;
	Boolean internal = TestFlags(kControllerInternal);

	TArrayIterator iter;
	TUnit** slot = (TUnit**) fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kUnitDoneOrPassed))
			continue;
		TDomain* domain = unit->fDomain;
		Boolean classify = false;
		if (unit->fDelay == 0)
		{
			if (unit->fPriority != 0)
			{
				// it has been put behind another unit: one pass each time
				unit->fPriority--;
			}
			else
				classify = true;
		}
		else if (!internal && !NoEventsWithinDelay(unit, 0))
		{
			// still waiting: come back when its delay is up
			ULong ready = unit->fStartTime + unit->fDuration + unit->fDelay;
			if (ready < fClassifyTime)
				fClassifyTime = ready;
		}
		else if (!unit->TestFlags(kInvalidatedUnit))
			classify = true;

		if (classify)
		{
			TRecArea* area = unit->GetArea();
			if (area == nil)
			{
				noArea = true;
				break;
			}
			Handle params = area->GetInfoFor(unit->fType, false);
			if (params != nil && domain->fParameters != params)
			{
				domain->SetParameters(params);
				domain->fParameters = params;
			}
			domain->Classify(unit);
			if (ControllerError())
				break;
		}
	}

	// the units that have gone on to be pieces are off the unit list
	slot = (TUnit**) fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kUnitClassified))
		{
			unit->Dispose();
			fUnits->Delete(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	if (noArea)
		SignalMemoryError();
	return noArea;
}


// ROM 0x0020a38c NewClassification__11TControllerFP5TUnit
// A unit offered as a *piece* for the domains above it: it joins the
// piece list, its areas are worked out (the hit-test routine), and one
// group-queue entry is made for each domain the area runs over pieces of
// its type.  Types the area arbitrates (arbitrate time other than 2) also
// go to the arbiter, which is what decides between the units the domains
// will make of it.
//
// A piece whose areas come out empty is nobody's: it and everything built
// over it are marked claimed and a clean-up asked for.
// ==> 1 for no memory.
ULong
TController::NewClassification(TUnit* piece)
{
	TAreaList* areas = nil;
	ULong wasQueued = fGroupQ->Count();
	ULong failed = (piece == nil);

	if (failed == 0)
	{
		piece->SetFlags(kUnitClassified);
		failed = fPieces->AddUnit(piece);
	}
	if (failed == 0)
	{
		// a piece that is on the unit list as well is now held twice
		TArrayIterator iter;
		TUnit** slot = (TUnit**) fUnits->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
		{
			if (*slot == piece)
			{
				piece->Clone();
				break;
			}
		}

		if (piece->fSubRange == 0xffff)
		{
			// nothing under it: it is a stroke of its own
			piece->fMinStroke = fNextStroke;
			piece->fMaxStroke = fNextStroke;
			fNextStroke = fNextStroke + 1;
		}
		piece->fSubRange = CountStrokes(piece);

		areas = piece->GetAreas();
		if (areas == nil)
			areas = TAreaList::Make();
		if (areas == nil)
			failed = 1;
	}
	if (failed == 0)
	{
		ULong hit = 0;
		if (fHitTest != nil)
			hit = fHitTest(piece, areas);
		if (hit == 0)
			piece->SetAreas(areas);
		areas->Dispose();
		areas = piece->GetAreas();

		if (areas == nil || areas->Count() == 0)
		{
			MarkUnits(piece, kClaimedUnit);
			fCleanUpTime = GetTicks();
		}
		else
		{
			TRecArea* area = piece->GetArea();
			if (area == nil)
				failed = 1;
			else
				failed = QueuePiece(piece, area);
		}
	}

	if (failed != 0)
		SignalMemoryError();
	if (areas != nil)
		areas->Dispose();

	ULong now = GetTicks();
	if (wasQueued < (ULong) fGroupQ->Count())
		fGroupTime = now;
	if (fArbiter->Pending()->Count() != 0)
		fArbitrateTime = now;
	return failed;
}


// The tail of NewClassification and of RegroupSub: the group-queue and
// arbiter entries an area asks for a piece of this type.
ULong
TController::QueuePiece(TUnit* piece, TRecArea* area)
{
	if (area->fDomains->Count() == 0)
	{
		// no domain wants it at all - a stroke with nowhere to go
		if (piece->fType == kStrokeUnit)
			return 1;
	}
	else
	{
		TArrayIterator iter;
		Assoc* assoc = (Assoc*) area->fDomains->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
		{
			Assoc copy = *assoc;
			if (piece->fType == copy.fType)
			{
				GroupEntry* entry = (GroupEntry*) fGroupQ->AddEntry();
				if (entry == nil)
					return 1;
				entry->fPiece = piece;
				entry->fDomain = copy.fDomain;
				entry->fInfo = (dInfoRec*) copy.fInfo;
				entry->fParams = copy.fParams;
			}
		}
	}

	TArrayIterator iter;
	Assoc* assoc = (Assoc*) area->fTypes->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
	{
		Assoc copy = *assoc;
		if (piece->fType == copy.fType && copy.fArbitrateTime != kArbitrateExternally)
		{
			BestMatch* entry = (BestMatch*) fArbiter->Pending()->AddEntry();
			if (entry == nil)
				return 1;
			entry->fUnit = piece;
			entry->fState = 0;
			entry->fArbitrateTime = copy.fArbitrateTime;
			entry->fAssoc = copy;
		}
	}
	return 0;
}


// ROM 0x0020a724 IsExternallyArbitrated__11TControllerFP5TUnit
// Whether the area a unit was written in handles its type itself rather
// than through the arbiter (arbitrate time 2) - what makes a click reach
// its view while the pen is still down.
Boolean
TController::IsExternallyArbitrated(TUnit* unit)
{
	TRecArea* area = unit->GetArea();
	if (area == nil || area->fTypes->Count() == 0)
		return false;
	TArrayIterator iter;
	Assoc* assoc = (Assoc*) area->fTypes->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
	{
		if (assoc->fType == unit->fType && assoc->fArbitrateTime == kArbitrateExternally)
			return true;
	}
	return false;
}


// ROM 0x0020a970 NewGroup__11TControllerFP5TUnit
// A unit a domain has just made.  It joins the unit list with the delay
// its domain asks for, and the classify pass is asked for at the time
// that delay runs out.
void
TController::NewGroup(TUnit* unit)
{
	ULong delay = 0;
	if (unit->fDelay == 0)
	{
		delay = unit->fDomain->fDelay;
		unit->SetDelay(delay);
	}
	if (fUnits->AddUnit(unit))
	{
		SignalMemoryError();
		return;
	}
	if (unit->TestFlags(kUnitDoneOrPassed))
		return;
	ULong ready = unit->fStartTime + unit->fDuration + delay;
	if (ready < fClassifyTime)
		fClassifyTime = ready;
}


// ROM 0x0020a804 RegroupUnclaimedSubs__11TControllerFP5TUnit
void
TController::RegroupUnclaimedSubs(TUnit* unit)
{
	long count = unit->SubCount();
	for (long i = 0; i < count; i++)
	{
		TUnit* sub = ((TSIUnit*) unit)->GetSub(i);
		if (!sub->TestFlags(kClaimedUnit))
			RegroupSub(unit, sub);
	}
}


// ROM 0x0020a880 RegroupSub__11TControllerFP5TUnitT1
// A sub put back on the group queue for the domain that made the unit it
// was part of: the unit did not work out, so its pieces are offered
// again.
void
TController::RegroupSub(TUnit* unit, TUnit* sub)
{
	Boolean failed = false;
	TRecArea* area = unit->GetArea();
	if (area != nil)
	{
		TArrayIterator iter;
		Assoc* assoc = (Assoc*) area->fDomains->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
		{
			Assoc copy = *assoc;
			if (sub->fType == copy.fType && unit->fDomain == copy.fDomain)
			{
				GroupEntry* entry = (GroupEntry*) fGroupQ->AddEntry();
				if (entry == nil)
				{
					failed = true;
					break;
				}
				entry->fPiece = sub;
				entry->fDomain = copy.fDomain;
				entry->fInfo = (dInfoRec*) copy.fInfo;
				entry->fParams = copy.fParams;
			}
		}
	}
	if (failed)
		SignalMemoryError();
	else
		fGroupTime = GetTicks();
}


// ROM 0x0020b604 TimeOut__11TControllerFUl
// Every unit of a type, and everything under it, made ready at once.
void
TController::TimeOut(ULong type)
{
	TArrayIterator iter;
	TUnit** slot = (TUnit**) fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TSIUnit* unit = (TSIUnit*) *slot;
		if (!unit->TestFlags(kUnitDone) && unit->fType == type)
		{
			unit->SetDelay(0);
			TimeOutSubs(unit);
		}
	}
}


// ROM 0x0020b2a0 NoEventsWithinDelay__11TControllerFP5TUnitUl
// Whether a unit's delay has gone by with nothing written over it: the
// question that decides whether a word is finished or the pen is about to
// add another stroke to it.
//
// Nothing counts as an event unless it falls inside the delay and hits
// the same view.  A click that is still being written is not an event by
// itself - the pen may be drawing a stroke that belongs to this unit -
// but it is a reason to wait, and once that stroke is long enough to be a
// stroke of its own (more than fifty points) the domain is asked whether
// it would take it; if it would, the unit waits for it properly.
// ==> true when nothing is in the way.
Boolean
TController::NoEventsWithinDelay(TUnit* unit, ULong arg)
{
	ULong delay = unit->fDelay;
	if (delay == 0)
		return true;

	ULong from = unit->fStartTime + unit->fDuration;
	ULong until = from + delay;
	if (GetTicks() <= until)
		return false;

	Boolean busy = false;
	TUnit* pending = nil;
	TUnit* event = nil;

	TArrayIterator iter;
	TUnit** slot = (TUnit**) fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* other = *slot;
		ULong when = other->fStartTime;
		if (when > from && when < until && UnitsHitSameArea(unit, other))
		{
			if (other->fType != kClickUnit)
			{
				event = other;
				break;
			}
			if (pending == nil && !((TClickUnit*) other)->fStroke->Done())
				pending = other;
		}
		if (when > until)
			break;
	}
	if (event == nil)
	{
		slot = (TUnit**) fUnits->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
		{
			TUnit* other = *slot;
			ULong when = other->fStartTime;
			if (when > from && when < until && UnitsHitSameArea(unit, other))
			{
				event = other;
				break;
			}
			if (when > until)
				break;
		}
	}
	if (event != nil)
		busy = true;
	else if (CheckStrokeQueueEvents(from, delay))
		busy = true;

	if (event != nil || pending != nil)
	{
		TDomain* domain = unit->fDomain;
		ULong newDelay = 0xff;
		if (pending != nil && domain->PreGroup(nil) != 0
			&& domain->fPieceTypes->FindType(kStrokeUnit) != (ULong) -1)
		{
			TStroke* stroke = ((TClickUnit*) pending)->fStroke;
			if (stroke->fCount > 0x31)
			{
				// long enough to be a stroke: ask the domain for real
				stroke->Clone();
				TStrokeUnit* trial = TStrokeUnit::Make(gStrokeDomain, 2, stroke, nil);
				if (trial != nil)
				{
					trial->AddSub(pending);
					trial->EndSubs();
					busy = (domain->PreGroup(trial) == 0);
					if (busy)
						unit->SetDelay(0xff);
					trial->Dispose();
				}
				return !busy;
			}
			newDelay = (GetTicks() - from) + (0x32 - stroke->fCount) * 60 / 60;
		}
		busy = true;
		unit->SetDelay(newDelay);
	}
	return !busy;
}


// ROM 0x0020b6dc GetUList__11TControllerFP7TDomainUlN22
// The units of a type (and, when one is named, of a domain) that have all
// of one set of flags and none of another.
TUnitList*
TController::GetUList(TDomain* domain, ULong type, ULong has, ULong hasNot)
{
	TUnitList* list = TUnitList::Make();
	Boolean failed = (list == nil);
	if (!failed)
	{
		TArrayIterator iter;
		TUnit** slot = (TUnit**) fUnits->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
		{
			TUnit* unit = *slot;
			if (unit->fType == type && (domain == nil || unit->fDomain == domain)
				&& unit->TestFlags(has) && !unit->TestFlags(hasNot))
			{
				if (list->AddUnit(unit))
				{
					failed = true;
					break;
				}
			}
		}
		if (!failed)
			list->Compact();
	}
	if (failed)
	{
		SignalMemoryError();
		if (list != nil)
			list->Dispose();
		list = nil;
	}
	return list;
}


// ROM 0x0020b7ec GetDelayList__11TControllerFP7TDomainUl
TUnitList*
TController::GetDelayList(TDomain* domain, ULong type)
{
	return GetUList(domain, type, kDelayedUnit, kClaimedUnit | kInvalidatedUnit);
}


// ROM 0x0020b80c GetIndexedStroke__11TControllerFUl
TUnit*
TController::GetIndexedStroke(ULong index)
{
	TArrayIterator iter;
	TUnit** slot = (TUnit**) fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->fMinStroke == index && unit->fMaxStroke == index && unit->fType == kStrokeUnit)
			return unit;
	}
	return nil;
}


// ROM 0x0020b940 DeletePiece__11TControllerFl
void
TController::DeletePiece(long index)
{
	fPieces->GetUnit(index)->Dispose();
	fPieces->Delete(index);
}


// ROM 0x0020b97c DeleteUnit__11TControllerFl
void
TController::DeleteUnit(long index)
{
	fUnits->GetUnit(index)->Dispose();
	fUnits->Delete(index);
}


// ROM 0x0020b9b8 MarkUnits__11TControllerFP5TUnitUl
// A unit and everything built over it given a flag.  The unit marks
// itself and its own subs (MarkUnit) into a list; then every piece or
// unit that has one of those as a sub is marked in its turn, into a
// second list; the two lists are swapped and the round run again until a
// round marks nothing new.
void
TController::MarkUnits(TUnit* unit, ULong flags)
{
	Boolean failed = false;
	TUnitList* marked = TUnitList::Make();
	TUnitList* found = nil;
	if (marked == nil)
		failed = true;
	else if (unit->MarkUnit(marked, flags) != 0)
		failed = true;
	else
	{
		found = TUnitList::Make();
		if (found == nil)
			failed = true;
	}

	long count = failed ? 0 : marked->Count();
	while (count != 0)
	{
		TArray* lists[2];
		lists[0] = fPieces;
		lists[1] = fUnits;
		for (long which = 0; which < 2 && !failed; which++)
		{
			TArrayIterator iter;
			TUnit** slot = (TUnit**) lists[which]->GetIterator(&iter);
			for (long i = 0; i < iter.fCount && !failed; i++, slot = (TUnit**) iter.GetNext())
			{
				TUnit* over = *slot;
				long subs = over->SubCount();
				for (long s = 0; s < subs; s++)
				{
					TUnit* sub = ((TSIUnit*) over)->GetSub(s);
					for (long d = 0; d < count; d++)
					{
						if (marked->GetUnit(d) == sub)
						{
							over->SetFlags(flags);
							failed = found->AddUnique(over);
							s = subs;
							break;
						}
					}
					if (failed)
						break;
				}
			}
		}
		if (failed)
			break;
		marked->Clear();
		count = found->Count();
		TUnitList* swap = marked;
		marked = found;
		found = swap;
	}

	if (failed)
		SignalMemoryError();
	if (marked != nil)
		marked->Dispose();
	if (found != nil)
		found->Dispose();
}


// ROM 0x0020bd9c CleanGroupQ__11TControllerFP5TUnit
void
TController::CleanGroupQ(TUnit* unit)
{
	TArrayIterator iter;
	GroupEntry* entry = (GroupEntry*) fGroupQ->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry++)
	{
		if (entry->fPiece == unit)
			entry->fPiece = nil;
	}
}


// ROM 0x0020bd94 SetExpireStrokeRoutine__11TControllerFPFP5TUnit_v
void
TController::SetExpireStrokeRoutine(void (*routine)(TUnit*))
{
	fExpireStroke = routine;
}


// ROM 0x0021c7c4 SetHitTestRoutine__11TControllerFPFP5TUnitP6TArray_Ul
void
TController::SetHitTestRoutine(ULong (*routine)(TUnit*, TArray*))
{
	fHitTest = routine;
}


// ROM 0x0020c090 GetClickInProgress__11TControllerFv
TUnit*
TController::GetClickInProgress(void)
{
	ULong count = fPieces->Count();
	for (ULong i = 0; i < count; i++)
	{
		TUnit* unit = fPieces->GetUnit(i);
		if (ClickInProgress(unit))
			return unit;
	}
	return nil;
}


// ROM 0x0020c0e8 IsLastCompleteStroke__11TControllerFP5TUnit
// Whether nothing has been written since this unit's stroke - either it
// is the last stroke there is, or the only thing after it is the click
// the pen is making now.
Boolean
TController::IsLastCompleteStroke(TUnit* unit)
{
	ULong next = unit->fMinStroke + 1;
	if (next == fNextStroke)
		return true;
	TUnit* click = GetClickInProgress();
	return click != nil && next == click->fMinStroke;
}


// ROM 0x0020beb0 ExpireAllStrokes__11TControllerFv
// Every stroke piece nobody claimed given to the expire routine (the
// stroke world's, which keeps it as ink).  Past fifty strokes they are
// marked as late, which is what stops the ones that have piled up being
// grouped all over again.
void
TController::ExpireAllStrokes(void)
{
	TArrayIterator iter;
	TUnit** slot = (TUnit**) fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->fType == kStrokeUnit && !unit->TestFlags(kClaimedUnit) && fExpireStroke != nil)
		{
			if (iter.fCount > 0x32)
				unit->SetFlags(kUnitLateStroke);
			fExpireStroke(unit);
		}
	}
}


// ROM 0x0020c140 CleanUpUnits__11TControllerFUc
// Everything thrown away.  `all` false keeps the click being written and
// the stroke pieces, and the stroke units among the units; `all` true
// keeps only the click being written, which is never disposed either way.
// CleanupAfterError calls it twice with the strokes expired in between,
// so that the ink of what was written is handed on before the strokes
// themselves go.
void
TController::CleanUpUnits(Boolean all)
{
	long kept = 0;
	ULong count = fPieces->Count();
	for (ULong i = 0; i < count; i++)
	{
		TUnit* unit = fPieces->GetUnit(i);
		if (!all && (ClickInProgress(unit) || unit->fType == kStrokeUnit))
			*(TUnit**) fPieces->GetEntry(kept++) = unit;
		else if (!ClickInProgress(unit))
			unit->Dispose();
	}
	fPieces->CutToIndex(kept);

	kept = 0;
	count = fUnits->Count();
	for (ULong i = 0; i < count; i++)
	{
		TUnit* unit = fUnits->GetUnit(i);
		if (!all && unit->fType == kStrokeUnit)
			*(TUnit**) fUnits->GetEntry(kept++) = unit;
		else
			unit->Dispose();
	}
	fUnits->CutToIndex(kept);
}


// ROM 0x0020c354 ClearArbiter__11TControllerFv
void
TController::ClearArbiter(void)
{
	for (long i = 0; i < kArbiterListCount; i++)
		fArbiter->fLists[i]->Clear();
	for (long i = 0; i < kArbiterListCount; i++)
		fArbiter->fLists[i]->Compact();
}


// ROM 0x0020c444 ClearController__11TControllerFv
void
TController::ClearController(void)
{
	fPieces->Clear();
	fUnits->Clear();
	fGroupQ->Clear();
	fPieces->Compact();
	fUnits->Compact();
	fGroupQ->Compact();
}


// ROM 0x0020be44 CleanupAfterError__11TControllerFv
// Out of memory: everything is let go and the controller started again.
// The click the pen is writing survives - it is put back as a new piece -
// so that the stroke in hand is not lost with the rest.
void
TController::CleanupAfterError(void)
{
	TUnit* click = GetClickInProgress();
	ClearArbiter();
	CleanUpUnits(false);
	ExpireAllStrokes();
	CleanUpUnits(true);
	ClearController();
	UnsetFlags(0xffffffff);
	if (click != nil)
		NewClassification(click);
}


// ROM 0x0021c7cc BuildGTypes__11TControllerFP8TRecArea
// The domains an area must run, worked out from the unit types its
// recognisers take.
//
// A recogniser asks for a type ('STRK', 'WORD', ...); the domain of that
// type is found among the controller's, and what that domain needs is its
// *piece* types - so the area must also run whatever makes those, and so
// on down until nothing new is found.  Each round takes the types found
// last time, looks up the domains of those types and writes their piece
// types (paired with the domain that wants them) into the area's
// `fDomains`; the two lists are swapped and the round run again.  The
// area's level is the furthest any of those domains stood from the
// strokes, which is how many rounds of arbitration it will take.
void
TController::BuildGTypes(TRecArea* area)
{
	area->fDomains->Clear();
	TTypeAssoc* wanted = area->fTypes->Copy();
	if (wanted == nil)
		return;
	TTypeAssoc* needed = TTypeAssoc::Make();
	if (needed == nil)
	{
		wanted->Dispose();
		return;
	}

	ULong maxLevel = 0;
	while (wanted->Count() != 0)
	{
		TArrayIterator iter;
		TDomain** slot = (TDomain**) fDomains->GetIterator(&iter);
		for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TDomain**) iter.GetNext())
		{
			TDomain* domain = *slot;
			for (ULong j = 0; j < (ULong) wanted->Count(); j++)
			{
				if (domain->fType != wanted->GetAssoc(j)->fType)
					continue;
				if (maxLevel < (ULong) domain->fLevel)
					maxLevel = domain->fLevel;
				TArrayIterator pieceIter;
				ULong* piece = (ULong*) domain->fPieceTypes->GetIterator(&pieceIter);
				for (ULong k = 0; k < (ULong) pieceIter.fCount; k++, piece = (ULong*) pieceIter.GetNext())
				{
					if (domain->fType == *piece)
						continue;			// a domain that takes its own type would not end
					Assoc assoc;
					memset(&assoc, 0, sizeof(assoc));
					assoc.fType = *piece;
					assoc.fDomain = domain;
					needed->AddAssoc(&assoc);
				}
			}
		}
		area->fDomains->MergeAssoc(needed);
		wanted->CutToIndex(0);
		TTypeAssoc* swap = wanted;
		wanted = needed;
		needed = swap;
	}
	area->fMaxLevel = maxLevel;
	wanted->Dispose();
	needed->Dispose();
}
// ROM 0x0020c4f4 SetDomainDelays__FP11TControllerUl
// Every domain that waits at all is made to wait the writer's timeout.
// A domain whose delay is already nought - the strokes' and the clicks',
// which are ready the moment the pen lifts - is left alone.
void
SetDomainDelays(TController* controller, ULong delay)
{
	ULong count = (ULong) controller->fDomains->Count();
	for (ULong i = 0; i < count; i++)
	{
		TDomain* domain = *(TDomain**) controller->fDomains->GetEntry(i);
		if (domain->fDelay != 0)
			domain->fDelay = delay;
	}
}
