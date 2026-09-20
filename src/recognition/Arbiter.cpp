/*
	File:		recognition/Arbiter.cpp

	Contains:	TArbiter (Arbiter.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Arbiter.h"
#include "Controller.h"

TArbiter*	gArbiter = nil;						// ROM 0x0c101880 gArbiter


// ROM 0x00207d2c IArbiter__8TArbiterFP11TController
// The seven lists: four of arbitration entries and three of bare unit
// pointers.  The first two and the last three are TDArrays - entries are
// taken out of the middle of them - and the two in between are plain
// arrays, cut back rather than unpicked.
long
TArbiter::IArbiter(TController* controller)
{
	fController = controller;
	fLists[kArbiterPending] = TDArray::Make(sizeof(ArbiterEntry), 0);
	fLists[kArbiterActive] = TDArray::Make(sizeof(ArbiterEntry), 0);
	fLists[kArbiterGathered] = TArray::Make(sizeof(ArbiterEntry), 0);
	fLists[kArbiterWinners] = TArray::Make(sizeof(ArbiterEntry), 0);
	fLists[kArbiterUnitsA] = TDArray::Make(sizeof(TUnit*), 0);
	fLists[kArbiterUnitsB] = TDArray::Make(sizeof(TUnit*), 0);
	fLists[kArbiterUnitsC] = TDArray::Make(sizeof(TUnit*), 0);
	fArbitrateNow = false;
	fWaiting = false;
	fUnused24 = 0;
	return 0;
}


// ROM 0x00206bf0 Make__8TArbiterSFP11TController
TArbiter*
TArbiter::Make(TController* controller)
{
	TArbiter* arbiter = new TArbiter;
	if (arbiter != nil)
	{
		arbiter->IArbiter(controller);
		controller->RegisterArbiter(arbiter);
	}
	return arbiter;
}


// ROM 0x002085ec DoArbitration__8TArbiterFv
// NOT YET RECONSTRUCTED: the deciding.  The ROM gathers the entries whose
// units are all present (GatherUnits, AllUnitsPresent), settles between
// the units written over the same strokes (ArbitrateUnits, and
// ArbitrateGraphicsWords for a word drawn as a shape), claims the winners
// for their recognisers and marks the losers.  With none of that here the
// arbitration decides nothing and the entries simply stay pending.
void
TArbiter::DoArbitration(void)
{
	fArbitrateNow = false;
}


// ROM 0x00207de0 CleanUp__8TArbiterFv
// What the arbitration left behind.  A claimed unit's own entries are
// dropped; the subs of a claimed unit that was not invalidated are
// offered to the domains again (the strokes of a word that lost may still
// make something else); and the claimed units and pieces are taken out of
// the controller's two lists - a claimed stroke piece that was marked
// invalid first goes to the expire routine, which is what leaves it on
// the screen as ink.  A click the pen is still writing is never touched.
void
TArbiter::CleanUp(void)
{
	TArrayIterator iter;

	ArbiterEntry* entry = (ArbiterEntry*) fLists[kArbiterActive]->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, entry = (ArbiterEntry*) iter.GetNext())
	{
		if (entry->fUnit->TestFlags(kClaimedUnit))
		{
			((TDArray*) fLists[kArbiterActive])->Delete(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	TUnit** slot = (TUnit**) fController->fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !unit->TestFlags(kInvalidatedUnit))
			fController->RegroupUnclaimedSubs(unit);
	}

	slot = (TUnit**) fController->fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !ClickInProgress(unit) && !unit->TestFlags(kInvalidatedUnit))
			fController->RegroupUnclaimedSubs(unit);
	}

	slot = (TUnit**) fController->fUnits->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit))
		{
			fController->CleanGroupQ(unit);
			fController->DeleteUnit(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	slot = (TUnit**) fController->fPieces->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, slot = (TUnit**) iter.GetNext())
	{
		TUnit* unit = *slot;
		if (unit->TestFlags(kClaimedUnit) && !ClickInProgress(unit))
		{
			fController->CleanGroupQ(unit);
			if (unit->fType == kStrokeUnit && unit->TestFlags(kInvalidUnit)
				&& fController->fExpireStroke != nil)
				fController->fExpireStroke(unit);
			fController->DeletePiece(i);
			iter.RemoveCurrent();
			i--;
		}
	}

	for (long i = 0; i < kArbiterListCount; i++)
		fLists[i]->Compact();
}
