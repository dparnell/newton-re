/*
	File:		recognition/Areas.cpp

	Contains:	TRecArea and TAreaList.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Areas.h"


/*------------------------------------------------------------------------------
	T R e c A r e a
------------------------------------------------------------------------------*/

// ROM 0x00219a7c Make__8TRecAreaSFUlT1
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


// ROM 0x00219b30 Dispose__8TRecAreaFv
// One user fewer; the area goes when none is left.
void
TRecArea::Dispose(void)
{
	if (Release())
		IDispose();
}


// ROM 0x00219c5c IDispose__8TRecAreaFv
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


// ROM 0x0021a090 Dump__8TRecAreaFP4TMsg
void
TRecArea::Dump(TMsg* /*msg*/)
{ }


// ROM 0x00219fd4 SizeInBytes__8TRecAreaFv
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


// ROM 0x00219f4c Clone__8TRecAreaFv
void
TRecArea::Clone(void)
{
	fUsers++;
}


// ROM 0x00219fb8 Release__8TRecAreaFv
// ==> whether no user is left.
Boolean
TRecArea::Release(void)
{
	return --fUsers < 0;
}


/*------------------------------------------------------------------------------
	T A r e a L i s t
------------------------------------------------------------------------------*/

// ROM 0x00219d68 Make__9TAreaListSFv
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


// ROM 0x00219dd0 IAreaList__9TAreaListFv
long
TAreaList::IAreaList(void)
{
	long err = IArray(sizeof(TRecArea*), 0);
	NameHandle(fData, 'dDta');
	return err;
}


// ROM 0x00219ddc Dispose__9TAreaListFv
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


// ROM 0x00219e3c IDispose__9TAreaListFv
void
TAreaList::IDispose(void)
{
	TArray::IDispose();
}


// ROM 0x00219e40 Clone__9TAreaListFv
// One more user of the list and of each area.
void
TAreaList::Clone(void)
{
	ULong count = fCount;
	for (ULong i = 0; i < count; i++)
		GetArea(i)->Clone();
	fUsers++;
}


// ROM 0x00219e88 GetArea__9TAreaListFUl
TRecArea*
TAreaList::GetArea(ULong index)
{
	return *(TRecArea**) GetEntry(index);
}


// ROM 0x00219ea8 AddArea__9TAreaListFP8TRecArea
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


// ROM 0x00219ef8 FindMatchingView__9TAreaListFUl
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


// ROM 0x00219fac GetMergedArea__9TAreaListFv
// The merged area is the last.
TRecArea*
TAreaList::GetMergedArea(void)
{
	return GetArea(fCount - 1);
}
