/*
	File:		recognition/Areas.cpp

	Contains:	TRecArea and TAreaList.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Areas.h"
#include "Domain.h"
#include "OSErrors.h"


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
	area->fUnused10 = 0;
	return area;
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
			&& assoc->fUnknown10 == entry->fUnknown10)
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
