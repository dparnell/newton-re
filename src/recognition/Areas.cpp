/*
	File:		recognition/Areas.cpp

	Contains:	TRecArea and TAreaList.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Areas.h"
#include "Domain.h"
#include "Controller.h"
#include "Recognizer.h"
#include "RecConfig.h"
#include "UnitPublic.h"
#include "RootView.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"


/*------------------------------------------------------------------------------
	T R e c A r e a
------------------------------------------------------------------------------*/

// ROM 0x0021c1ac Make__8TRecAreaSFUlT1
// An area for a view's recognition flags; nothing in it yet.
// NOT YET RECONSTRUCTED: the two TTypeAssocs it is made with.
TRecArea*
TRecArea::Make(ULong viewFlags, ULong flags)
{
	TRecArea* area = new TRecArea;
	if (area == nil)
		return nil;
	area->fFlags = flags;
	area->fTypes = nil;
	area->fViewFlags = viewFlags;
	area->fDomains = nil;
	area->fArbitrateNow = 0;
	for (long i = 0; i < 3; i++)
		area->fDictionaries[i] = nil;
	area->fViewId = 0;
	area->fUsers = 0;
	area->fMaxLevel = 0;
	area->fTypes = TTypeAssoc::Make();
	if (area->fTypes != nil)
	{
		area->fDomains = TTypeAssoc::Make();
		if (area->fDomains != nil)
			return area;
	}
	area->Dispose();
	return nil;
}


// ROM 0x0021c74c AddAType__8TRecAreaFUlPFP6TArray_UlT1P8dInfoRec
// A unit type the area takes, with the routine its winning units are
// handed to, when they are arbitrated, and the domain's own record for
// it.  The domain itself is left nil: which domains that implies is
// worked out afterwards by TController::BuildGTypes.  An arbitrate time
// of 1 - decide as soon as the unit is ready - is counted, because the
// arbiter asks how many of them an area has.
void
TRecArea::AddAType(ULong type, AreaHandler handler, ULong arbitrateTime, dInfoRec* info)
{
	Assoc assoc;
	memset(&assoc, 0, sizeof(assoc));
	assoc.fType = type;
	assoc.fInfo = info;
	assoc.fHandler = handler;
	assoc.fArbitrateTime = arbitrateTime;
	long before = fTypes->Count();
	fTypes->AddAssoc(&assoc);
	if (arbitrateTime == kArbitrateAtOnce && before < fTypes->Count())
		fArbitrateNow++;
}


// ROM 0x0021c260 Dispose__8TRecAreaFv
// One user fewer; the area goes when none is left.
void
TRecArea::Dispose(void)
{
	if (Release())
		IDispose();
}


// ROM 0x0021c288 GetInfoFor__8TRecAreaFUlUc
// The parameter block the area runs a domain with, found by the domain's
// *own* type rather than the piece type the entry is for.  With `make`
// the block is built the first time it is asked for: the domain is asked
// how big it is (DomainParameter selector 0) and then to fill it in
// (selector 1).  The iterator is stepped after the handle is made,
// because making it can move the array's data.
Handle
TRecArea::GetInfoFor(ULong type, Boolean make)
{
	TArrayIterator iter;
	Assoc* assoc = (Assoc*) fDomains->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
	{
		if (assoc->fDomain->fType != type)
			continue;
		if (assoc->fParams != nil)
			return assoc->fParams;
		if (!make)
			return nil;
		TDomain* domain = assoc->fDomain;
		ULong size = 0;
		domain->DomainParameter(0, (ULong) &size, 0);
		Handle params = nil;
		if (size != 0)
		{
			params = MakeHandle(size);
			NameHandle(params, 'info');
			if (params != nil)
				domain->DomainParameter(1, 0, (ULong) params);
			assoc = (Assoc*) iter.GetNext();
		}
		assoc->fParams = params;
		return params;
	}
	return nil;
}


// ROM 0x0021c400 ParamsAllSet__8TRecAreaFUl
// The domain told that its parameters are complete: it is asked to work
// out what it can from them (InvalParameters) and to set up whatever it
// keeps per area (ConfigureSubDomain).
void
TRecArea::ParamsAllSet(ULong type)
{
	TArrayIterator iter;
	Assoc* assoc = (Assoc*) fDomains->GetIterator(&iter);
	for (ULong i = 0; i < (ULong) iter.fCount; i++, assoc = (Assoc*) iter.GetNext())
	{
		if (assoc->fDomain->fType == type)
		{
			assoc->fDomain->InvalParameters();
			assoc->fDomain->ConfigureSubDomain(this);
			return;
		}
	}
}


// ROM 0x0021c38c IDispose__8TRecAreaFv
// The associations and chains disposed with it.
void
TRecArea::IDispose(void)
{
	if (fTypes != nil)
		((TRecObject*) fTypes)->Dispose();
	if (fDomains != nil)
		((TRecObject*) fDomains)->Dispose();
	for (long i = 0; i < 3; i++)
		if (fDictionaries[i] != nil)
			((TRecObject*) fDictionaries[i])->Dispose();
	delete this;
}


// ROM 0x0021c7c0 Dump__8TRecAreaFP4TMsg
void
TRecArea::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0021c704 SizeInBytes__8TRecAreaFv
long
TRecArea::SizeInBytes(void)
{
	long size = 0;
	if (fTypes != nil)
		size += ((TRecObject*) fTypes)->SizeInBytes();
	if (fDomains != nil)
		size += ((TRecObject*) fDomains)->SizeInBytes();
	return TRecObject::SizeInBytes() + size;
}


// ROM 0x0021c67c Clone__8TRecAreaFv
void
TRecArea::Clone(void)
{
	fUsers++;
}


// ROM 0x0021c6e8 Release__8TRecAreaFv
// ==> whether no user is left.
Boolean
TRecArea::Release(void)
{
	return --fUsers < 0;
}


/*------------------------------------------------------------------------------
	T A r e a L i s t
------------------------------------------------------------------------------*/

// ROM 0x0021c498 Make__9TAreaListSFv
TAreaList*
TAreaList::Make(void)
{
	TAreaList* list = new TAreaList;
	if (list == nil)
		return nil;
	list->fData = nil;
	if (list->IAreaList() != 0)
	{
		list->Dispose();
		list = nil;
	}
	return list;
}


// ROM 0x0021c500 IAreaList__9TAreaListFv
long
TAreaList::IAreaList(void)
{
	long err = IArray(sizeof(TRecArea*), 0);
	NameHandle(fData, 'dDta');
	return err;
}


// ROM 0x0021c50c Dispose__9TAreaListFv
// The areas released; the list goes when no user is left.
void
TAreaList::Dispose(void)
{
	ULong count = fCount;
	for (ULong i = 0; i < count; i++)
		GetArea(i)->Dispose();
	if (TArray::Release())
		IDispose();
}


// ROM 0x0021c56c IDispose__9TAreaListFv
void
TAreaList::IDispose(void)
{
	TArray::IDispose();
}


// ROM 0x0021c570 Clone__9TAreaListFv
// One more user of the list and of each area.
void
TAreaList::Clone(void)
{
	ULong count = fCount;
	for (ULong i = 0; i < count; i++)
		GetArea(i)->Clone();
	fUsers++;
}


// ROM 0x0021c5b8 GetArea__9TAreaListFUl
TRecArea*
TAreaList::GetArea(ULong index)
{
	return *(TRecArea**) GetEntry(index);
}


// ROM 0x0021c5d8 AddArea__9TAreaListFP8TRecArea
// The area cloned and added.  ==> 0, or 1 for no memory.
long
TAreaList::AddArea(TRecArea* area)
{
	TRecArea** entry = (TRecArea**) AddEntry();
	if (entry == nil)
		return 1;
	Compact();
	entry = (TRecArea**) GetEntry(fCount - 1);		// DEVIATION: the host's handles may move when compacted
	area->Clone();
	*entry = area;
	return 0;
}


// ROM 0x0021c628 FindMatchingView__9TAreaListFUl
// Whether an area stands for the view.
Boolean
TAreaList::FindMatchingView(ULong viewId)
{
	ULong count = fCount;
	for (ULong i = 0; i < count; i++)
		if (GetArea(i)->fViewId == viewId)
			return true;
	return false;
}


// ROM 0x0021c6dc GetMergedArea__9TAreaListFv
// The merged area is the last.
TRecArea*
TAreaList::GetMergedArea(void)
{
	return GetArea(fCount - 1);
}

/*------------------------------------------------------------------------------
	T T y p e A s s o c
------------------------------------------------------------------------------*/

// ROM 0x0022c778 Make__10TTypeAssocSFv
TTypeAssoc*
TTypeAssoc::Make(void)
{
	TTypeAssoc* assoc = new TTypeAssoc;
	if (assoc != nil && assoc->ITypeAssoc() != noErr)
	{
		assoc->Dispose();
		assoc = nil;
	}
	return assoc;
}


// ROM 0x0022c7e0 ITypeAssoc__10TTypeAssocFv
// An array of Assoc records, grown a chunk at a time like any other; the
// handle is named so that a heap dump says what it is.
long
TTypeAssoc::ITypeAssoc(void)
{
	long err = IArray(sizeof(Assoc), 0);
	NameHandle(fData, 'Datd');
	return err;
}


// ROM 0x0022c7ec IDispose__10TTypeAssocFv
// The parameter blocks that belong to the entries go with them: the domain
// is told first (DomainParameter with selector 3), then the handle is
// freed.  A block someone else owns (fSharedParams) is left alone.
void
TTypeAssoc::IDispose(void)
{
	ULong count = (ULong) fCount;
	for (ULong i = 0; i < count; i++)
	{
		Assoc* assoc = GetAssoc(i);
		if (assoc->fParams != nil && !assoc->fSharedParams)
		{
			assoc->fDomain->DomainParameter(3, 0, 0);
			DisposeHandle(assoc->fParams);
		}
	}
	TArray::IDispose();
}


// ROM 0x0022c878 Copy__10TTypeAssocFv
TTypeAssoc*
TTypeAssoc::Copy(void)
{
	TTypeAssoc* copy = new TTypeAssoc;
	if (copy != nil)
		CopyInto(copy);
	return copy;
}


// ROM 0x0022c8d0 AddAssoc__10TTypeAssocFP5Assoc
// Sorted by type.  An entry that matches this one - the same type, domain,
// and the two words that go with them - is already there and its index is
// the answer; otherwise a slot is opened where the order wants it.
ULong
TTypeAssoc::AddAssoc(const Assoc* assoc)
{
	ULong at = 0;
	ULong count = (ULong) fCount;
	while (at < count)
	{
		Assoc* entry = GetAssoc(at);
		if (assoc->fType < entry->fType)
			break;
		if (assoc->fType == entry->fType
			&& (ULong) assoc->fDomain == (ULong) entry->fDomain
			&& assoc->fInfo == entry->fInfo
			&& assoc->fHandler == entry->fHandler)
			return at;
		at++;
	}
	at = Insert(at);
	if (at != (ULong) -1)
		*GetAssoc(at) = *assoc;
	return at;
}


// ROM 0x0022c998 MergeAssoc__10TTypeAssocFP10TTypeAssoc
// Another area's associations added to ours - what happens when a unit
// lies in more than one area and the merged one has to take both.
void
TTypeAssoc::MergeAssoc(TTypeAssoc* other)
{
	ULong count = (ULong) other->fCount;
	for (ULong i = 0; i < count; i++)
	{
		Assoc entry = *other->GetAssoc(i);
		AddAssoc(&entry);
	}
}


// ROM 0x0022ca18 GetAssoc__10TTypeAssocFUl
Assoc*
TTypeAssoc::GetAssoc(ULong index)
{
	return (Assoc*) GetEntry(index);
}


// ROM 0x0022ca20 Dump__10TTypeAssocFP4TMsg
void
TTypeAssoc::Dump(TMsg* /*msg*/)
{ }

// ROM 0x0c1008a0 gAreaCache
TArray*	gAreaCache = nil;


// ROM 0x0003485c PurgeAreaCache__Fv
// Every area in the cache let go, then the array emptied and its spare
// slots given back.  A script asks for this when it has changed something
// the areas were built from, so that they are built again.
void
PurgeAreaCache(void)
{
	if (gAreaCache == nil)
		return;		// (NOT YET RECONSTRUCTED: nothing builds the cache, so it is always this)
	for (ULong i = 0; i < (ULong) gAreaCache->Count(); i++)
	{
		TRecArea** entry = (TRecArea**) gAreaCache->GetEntry(i);
		(*entry)->Dispose();
	}
	gAreaCache->Clear();
	gAreaCache->Compact();
}


#pragma mark - the area cache

// ROM 0x00036294 GetElapsedTicks__FUl
// How long ago something was.  (The ROM tests whether the clock has
// wrapped and then subtracts either way, which comes to the same thing:
// unsigned arithmetic wraps with it.)
static ULong
GetElapsedTicks(ULong since)
{
	return GetTicks() - since;
}


// ROM 0x00034834 InitAreas__Fv
void
InitAreas(void)
{
	gAreaCache = TDArray::Make(sizeof(AreaCacheEntry), 0);
}


// ROM 0x0003495c SetUpArea__FP8TRecAreaRC6RefVar
// Every recogniser asked whether it wants anything written here: each
// one whose enabled services meet the configuration's inputMask adds its
// unit type to the area (TRecognizer::EnableArea).
void
SetUpArea(TRecArea* area, RefArg config)
{
	ULong count = gRecognition.fRecognizers->Count();
	for (ULong i = 0; i < count; i++)
		gRecognition.fRecognizers->GetRecognizer(i)->EnableArea(area, config);
}


// ROM 0x000349bc ConfigureArea__FP8TRecAreaRC6RefVar
// And then each one asked to set itself up in the area it has just been
// enabled in - which for the word recognisers is where their dictionary
// chains are built.
void
ConfigureArea(TRecArea* area, RefArg config)
{
	ULong count = gRecognition.fRecognizers->Count();
	for (ULong i = 0; i < count; i++)
		gRecognition.fRecognizers->GetRecognizer(i)->ConfigureArea(area, config);
}


// ROM 0x00035484 MakeArea__FP11TControllerP5TViewUlRC6RefVar
// The area for a view: its recognition configuration built (RecConfig.h),
// the recognisers enabled in it, the domains that implies worked out, and
// the recognisers configured.
TRecArea*
MakeArea(TController* controller, TView* view, ULong flags, RefArg config)
{
	RefVar built(config);
	TRecArea* area = TRecArea::Make(0, 0);
	if (area != nil)
	{
		built = (view == nil) ? BuildRCProto(nil, built) : BuildRecConfig(view, flags);
		SetUpArea(area, built);
		controller->BuildGTypes(area);
		ConfigureArea(area, built);
	}
	return area;
}


// ROM 0x00035434 MakeArea__FP11TControllerP5TViewUl
TRecArea*
MakeArea(TController* controller, TView* view, ULong flags)
{
	return MakeArea(controller, view, flags, RefVar(NILREF));
}


// ROM 0x00035674 FindMatchingArea__FP5TViewUl
// The cached area for a view and an input mask, or a new one.  Every look
// also ages the cache: a line untouched for ten seconds is let go, which
// is what makes a preference changed while nothing is being written take
// effect.
TRecArea*
FindMatchingArea(TView* view, ULong inputMask)
{
	TRecArea* found = nil;
	for (ULong i = 0; i < (ULong) gAreaCache->Count(); i++)
	{
		AreaCacheEntry* entry = (AreaCacheEntry*) gAreaCache->GetEntry(i);
		if (entry->fInputMask == inputMask && entry->fArea->fViewId == view->fId)
		{
			entry->fLastUsed = GetTicks();
			found = entry->fArea;
			break;
		}
	}
	for (ULong i = 0; i < (ULong) gAreaCache->Count(); i++)
	{
		AreaCacheEntry* entry = (AreaCacheEntry*) gAreaCache->GetEntry(i);
		if (GetElapsedTicks(entry->fLastUsed) > 600)
		{
			entry->fArea->Dispose();
			((TDArray*) gAreaCache)->Delete(i);
			i--;
		}
	}
	if (found == nil)
	{
		gRecognition.fUnitHandler = HandleUnit;		// (the journal's HandleReplayUnit is NOT YET)
		found = MakeArea(gController, view, inputMask);
		if (found == nil)
			return nil;
		AreaCacheEntry entry;
		entry.fArea = found;
		entry.fInputMask = inputMask;
		entry.fLastUsed = GetTicks();
		memcpy(gAreaCache->AddEntry(), &entry, sizeof(entry));
		gAreaCache->Compact();
	}
	found->fViewId = view->fId;
	return found;
}


// ROM 0x00036aa4 TryGetAreasHit__FP5TUnitP6TArray
// Which areas a piece lies in: the view under it that takes what it is
// (TUnitPublic::FindView over the recogniser's required mask), and that
// view's area for its input mask.  ==> whether the areas were put on the
// unit here, in which case the controller leaves them alone.
//
// A view that takes no writing at all answers an input mask of zero and
// gets no area, so the piece is nobody's and the controller claims it;
// a click there also closes any popup that was open, which is how tapping
// outside a menu dismisses it.
ULong
TryGetAreasHit(TUnit* unit, TArray* areas)
{
	TUnitPublic pub(unit, nil);
	ULong made = 0;
	TView* view = pub.FindView(pub.RequiredMask());
	ULong inputMask = pub.InputMask();
	if (inputMask == 0)
	{
		if (pub.GetType() == kClickUnit)
		{
			if (gRootView->fPopup != nil)
				gRootView->SetPopup(nil, true);
			gInhibitPopup = false;
		}
		pub.Cleanup();
	}
	else if (!((TAreaList*) areas)->FindMatchingView(view->fId))
	{
		if (areas->Count() != 0)
		{
			TAreaList* list = TAreaList::Make();
			if (list != nil)
			{
				areas = list;
				made = 1;
			}
		}
		TRecArea* area = FindMatchingArea(view, inputMask);
		if (area != nil)
			((TAreaList*) areas)->AddArea(area);
		if (made != 0)
		{
			unit->SetAreas((TAreaList*) areas);
			areas->Dispose();
		}
	}
	return made;
}


// ROM 0x00036bc8 GetAreasHit__FP5TUnitP6TArray
// The same under an exception handler: an `evt.ex` out of a view's
// scripts is reported rather than thrown at the recogniser.
ULong
GetAreasHit(TUnit* unit, TArray* areas)
{
	ULong made = 0;
	newton_try
	{
		made = TryGetAreasHit(unit, areas);
	}
	newton_catch_all
	{
		if (Subexception(CurrentException()->name, "evt.ex"))
			SafeExceptionNotify(CurrentException());
		else
			NextHandler(&_info);
	}
	end_try;
	return made;
}


// ROM 0x00036960 OtherViewInUse__FP5TView
// Whether somebody else's writing is still in hand: an area in the cache
// that stands for another view and is still being used by a unit.
Boolean
OtherViewInUse(TView* view)
{
	ULong id = (view != nil) ? view->fId : 0;
	for (ULong i = 0; i < (ULong) gAreaCache->Count(); i++)
	{
		AreaCacheEntry* entry = (AreaCacheEntry*) gAreaCache->GetEntry(i);
		if (entry->fArea->fViewId != id && entry->fArea->fUsers > 0)
			return true;
	}
	return false;
}


// ROM 0x000369e8 ClicksOnlyArea__FP5TUnit
// Whether the only thing the unit's area takes is clicks.
//
// DEVIATION: the ROM reads the area without looking, because a unit only
// ever reaches a recogniser through the controller, which gives it one.
// The host's stroke world still hands its units straight to HandleUnit
// (StrokeCentral.h), so they have no areas at all until the arbiter can
// decide; an arealess unit is not a clicks-only one.
Boolean
ClicksOnlyArea(TUnit* unit)
{
	TRecArea* area = unit->GetArea();
	if (area == nil)
		return false;
	TTypeAssoc* types = area->fTypes;
	return types->Count() == 1 && types->GetAssoc(0)->fType == kClickUnit;
}
