/*
	File:		recognition/Unit.cpp

	Contains:	TUnit, TUnitList, TTypeList, TSIUnit, TStrokeUnit,
				TClickUnit, TClickEventUnit.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Unit.h"
#include "Areas.h"
#include "Domain.h"
#include <string.h>

// NOT YET RECONSTRUCTED: the controller.  AddSub and EndSubs lower its
// next-event time (TController +0x20) to the unit's expiry so the delayed
// unit is arbitrated on time; the host has no controller yet.
static void
NoteUnitExpiry(TUnit* unit)
{
	(void) (GetTicks() + unit->fDelay);
}


/*------------------------------------------------------------------------------
	F i x e d   r e c t a n g l e s
------------------------------------------------------------------------------*/

// ROM 0x001a3fa8 FixRect
// A pixel rectangle as Fixed.
void
FixRect(FRect* dst, const Rect* src)
{
	dst->top = (Fixed) src->top * 0x10000;
	dst->left = (Fixed) src->left * 0x10000;
	dst->bottom = (Fixed) src->bottom * 0x10000;
	dst->right = (Fixed) src->right * 0x10000;
}


// ROM 0x001a41d0 AddRect
// dst grown to hold src - or set to it when this is the first.
void
AddRect(const FRect* src, FRect* dst, Boolean first)
{
	if (first)
		*dst = *src;
	else
	{
		if (src->top < dst->top)
			dst->top = src->top;
		if (src->bottom > dst->bottom)
			dst->bottom = src->bottom;
		if (src->left < dst->left)
			dst->left = src->left;
		if (src->right > dst->right)
			dst->right = src->right;
	}
}


/*------------------------------------------------------------------------------
	T U n i t
------------------------------------------------------------------------------*/

// ROM 0x0022ccb0 __ct__5TUnitFv
TUnit::TUnit()
{ }


// ROM 0x0022dea4 __dt__5TUnitFv
TUnit::~TUnit()
{ }


// ROM 0x0022e2bc IUnit__5TUnitFP7TDomainUlT2P6TArray
// A unit of a type in a domain, started now, with no strokes yet, delayed
// as the domain says, lying in the areas.  ==> 0.
long
TUnit::IUnit(TDomain* domain, ULong type, ULong kind, TArray* areas)
{
	fDomain = domain;
	fType = type;
	fFlags = 0;
	NamePtr((char*) this, type);
	fPriority = 0;
	fStartTime = GetTicks();
	fDuration = 0;
	fElapsed = 0;
	fKind = (UChar) kind;
	SetDelay(domain != nil ? domain->fDelay : 0);
	fUsers = 0;
	fAreas = nil;
	SetAreas((TAreaList*) areas);
	fSubRange = 0xffff;
	fMinStroke = 0;
	fMaxStroke = 0;
	FRect empty;
	SetRectangleEmpty(&empty);
	SetBBox(&empty);
	return 0;
}


// ROM 0x0022e52c Dispose__5TUnitFv
// One user fewer; the unit goes when none is left.
void
TUnit::Dispose(void)
{
	if (Release())
		IDispose();
}


// ROM 0x0022e560 IDispose__5TUnitFv
// The areas let go and the object deleted.
void
TUnit::IDispose(void)
{
	SetAreas(nil);
	delete this;
}


// ROM 0x0022e0d8 Dump__5TUnitFP4TMsg
// NOT YET RECONSTRUCTED: TMsg, the debugging message buffer.
void
TUnit::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0022e070 DumpName__5TUnitFP4TMsg
void
TUnit::DumpName(TMsg* /*msg*/)
{ }


// ROM 0x0022e594 SizeInBytes__5TUnitFv
long
TUnit::SizeInBytes(void)
{
	long size = 0;
	if (fAreas != nil)
		size = fAreas->SizeInBytes();
	return TRecObject::SizeInBytes() + size;
}


// ROM 0x0022e5e4 Clone__5TUnitFv
void
TUnit::Clone(void)
{
	fUsers++;
}


// ROM 0x0022e5f4 Release__5TUnitFv
// ==> whether no user is left.
Boolean
TUnit::Release(void)
{
	fUsers--;
	return fUsers < 0;
}


// ROM 0x0022dee4 SubCount__5TUnitFv
long
TUnit::SubCount(void)
{
	return 0;
}


// ROM 0x0022deec InterpretationCount__5TUnitFv
long
TUnit::InterpretationCount(void)
{
	return 0;
}


// ROM 0x0022def4 GetBestInterpretation__5TUnitFv
long
TUnit::GetBestInterpretation(void)
{
	return -1;
}


// ROM 0x0022defc GetAreas__5TUnitFv
// The areas as a list - the list itself (cloned) when there are several,
// a new list of the one area otherwise; nil when there is none.
TAreaList*
TUnit::GetAreas(void)
{
	TAreaList* list = nil;
	if (fAreas != nil)
	{
		if (TestFlags(kAreaListUnit))
		{
			list = (TAreaList*) fAreas;
			list->Clone();
		}
		else
		{
			list = TAreaList::Make();
			if (list != nil && list->AddArea((TRecArea*) fAreas) != 0)
			{
				list->Dispose();
				list = nil;
			}
		}
	}
	return list;
}


// ROM 0x0022df78 SetAreas__5TUnitFP9TAreaList
// The old areas let go; one area is kept by itself (cloned), several as
// the list (cloned).
void
TUnit::SetAreas(TAreaList* areas)
{
	if (fAreas != nil)
	{
		TestFlags(kAreaListUnit);
		fAreas->Dispose();
	}
	fAreas = nil;
	UnsetFlags(kAreaListUnit);
	if (areas == nil)
		return;
	if (areas->Count() > 1)
	{
		areas->Clone();
		fAreas = areas;
		fFlags |= kAreaListUnit;
	}
	else if (areas->Count() == 1)
	{
		TRecArea* area = areas->GetArea(0);
		area->Clone();
		fAreas = area;
	}
}


// ROM 0x0022e014 GetArea__5TUnitFv
// The area - the last of the list when there are several.
TRecArea*
TUnit::GetArea(void)
{
	if (fAreas == nil)
		return nil;
	if (!TestFlags(kAreaListUnit))
		return (TRecArea*) fAreas;
	TAreaList* list = (TAreaList*) fAreas;
	return list->GetArea(list->Count() - 1);
}


// ROM 0x0022e054 SetDelay__5TUnitFUl
// The delay before arbitration, in ticks, at most 255; the unit is flagged
// delayed while it is not 0.
void
TUnit::SetDelay(ULong delay)
{
	if (delay > 0xff)
		delay = 0xff;
	fDelay = (UChar) delay;
	if (delay != 0)
		fFlags |= kDelayedUnit;
	else
		fFlags &= ~kDelayedUnit;
}


// ROM 0x0022e210 MarkUnit__5TUnitFP9TUnitListUl
// The unit added to the list and flagged.  ==> 0, or 1 for no memory.
long
TUnit::MarkUnit(TUnitList* list, ULong flags)
{
	long err = list->AddUnit(this);
	SetFlags(flags);
	return err;
}


// ROM 0x0022e248 ClaimUnit__5TUnitFP9TUnitList
void
TUnit::ClaimUnit(TUnitList* list)
{
	MarkUnit(list, kClaimedUnit);
}


// ROM 0x0022e254 Invalidate__5TUnitFv
void
TUnit::Invalidate(void)
{
	fFlags |= kInvalidatedUnit;
}


// ROM 0x0022e58c DoneUsingUnit__5TUnitFv
// The areas let go.
void
TUnit::DoneUsingUnit(void)
{
	if (fAreas != nil)
	{
		TestFlags(kAreaListUnit);
		fAreas->Dispose();
	}
	fAreas = nil;
	UnsetFlags(kAreaListUnit);
}


// ROM 0x0022e25c CountStrokes__5TUnitFv
long
TUnit::CountStrokes(void)
{
	return 0;
}


// ROM 0x0022e264 GetStroke__5TUnitFUl
TStroke*
TUnit::GetStroke(ULong /*index*/)
{
	return nil;
}


// ROM 0x0022e26c GetAllStrokes__5TUnitFv
TUnitList*
TUnit::GetAllStrokes(void)
{
	return nil;
}


// ROM 0x0022e274 OwnsStroke__5TUnitFv
Boolean
TUnit::OwnsStroke(void)
{
	return false;
}


// ROM 0x0022e27c ContextID__5TUnitFv
ULong
TUnit::ContextID(void)
{
	return 0;
}


// ROM 0x0022e284 SetContextID__5TUnitFUl
void
TUnit::SetContextID(ULong /*id*/)
{ }


// ROM 0x0022e288 SetBBox__5TUnitFP5FRect
// The box, rounded to pixels.
void
TUnit::SetBBox(FRect* box)
{
	fBBox.top = (short) ((box->top + 0x8000) >> 16);
	fBBox.left = (short) ((box->left + 0x8000) >> 16);
	fBBox.bottom = (short) ((box->bottom + 0x8000) >> 16);
	fBBox.right = (short) ((box->right + 0x8000) >> 16);
}


// ROM 0x0022e298 GetBBox__5TUnitFP5FRect
FRect*
TUnit::GetBBox(FRect* box)
{
	FixRect(box, &fBBox);
	return box;
}


// ROM 0x0022e454 MarkStrokes__FP5TUnitPcl
// A count, per stroke index from base, of the times the stroke is under
// the unit: a unit whose stroke range is one stroke, or all its own
// (fSubRange == the range's length), counts its range; otherwise its subs
// are walked.
void
MarkStrokes(TUnit* unit, char* marks, long base)
{
	long first = unit->fMinStroke;
	long last = unit->fMaxStroke;
	long count = last - first + 1;
	if (count == 1)
	{
		marks[first - base]++;
		return;
	}
	if ((UShort) count == unit->fSubRange)
	{
		for (long i = first; i <= last; i++)
			marks[i - base]++;
		return;
	}
	long subs = unit->SubCount();
	for (long i = 0; i < subs; i++)
		MarkStrokes(((TSIUnit*) unit)->GetSub(i), marks, base);
}


// ROM 0x0022e374 CountStrokes__FP5TUnit
// The distinct strokes under a unit: those of its range that its subs
// touch.
long
CountStrokes(TUnit* unit)
{
	if (unit == nil)
		return 0;
	long first = unit->fMinStroke;
	long count = unit->fMaxStroke - first + 1;
	if (count == 1)
		return 1;
	char local[0x20];
	char* marks = local;
	Handle h = nil;
	if (count > 0x20)
	{
		h = MakeHandle(count);
		NameHandle(h, 'temp');
		if (h == nil)
			return 0;
		marks = *h;
	}
	memset(marks, 0, count);
	MarkStrokes(unit, marks, first);
	long strokes = 0;
	for (long i = 0; i < count; i++)
		if (marks[i] != 0)
			strokes++;
	if (h != nil)
		DeleteHandle(h);
	return strokes;
}


/*------------------------------------------------------------------------------
	T U n i t L i s t
------------------------------------------------------------------------------*/

// ROM 0x0022ccf0 Make__9TUnitListSFv
TUnitList*
TUnitList::Make(void)
{
	TUnitList* list = new TUnitList;
	if (list != nil)
	{
		list->fData = nil;
		if (list->IUnitList() != 0)
		{
			list->Dispose();
			list = nil;
		}
	}
	return list;
}


// ROM 0x0022cd58 IUnitList__9TUnitListFv
long
TUnitList::IUnitList(void)
{
	long err = IArray(sizeof(TUnit*), 0);
	NameHandle(fData, 'dDta');
	return err;
}


// ROM 0x0022ce74 Dump__9TUnitListFP4TMsg
void
TUnitList::Dump(TMsg* msg)
{
	for (ULong i = 0; i < (ULong) fCount; i++)
		GetUnit(i)->Dump(msg);
}


// ROM 0x0022cd64 Purge__9TUnitListFv
// Every unit disposed (the list itself keeps its entries).
void
TUnitList::Purge(void)
{
	for (ULong i = 0; i < (ULong) fCount; i++)
		GetUnit(i)->Dispose();
}


// ROM 0x0022cdac AddUnit__9TUnitListFP5TUnit
// ==> true for no memory.
Boolean
TUnitList::AddUnit(TUnit* unit)
{
	TUnit** entry = (TUnit**) AddEntry();
	if (entry != nil)
		*entry = unit;
	return entry == nil;
}


// ROM 0x0022cddc AddUnique__9TUnitListFP5TUnit
// The unit added unless it is there already.  ==> true for no memory.
Boolean
TUnitList::AddUnique(TUnit* unit)
{
	ULong count = fCount;
	if (count != 0)
	{
		TUnit** entry = (TUnit**) GetEntry(0);
		for (ULong i = 0; i < count; i++, entry++)
			if (*entry == unit)
				return false;
	}
	TUnit** entry = (TUnit**) AddEntry();
	if (entry != nil)
		*entry = unit;
	return entry == nil;
}


// ROM 0x0022ce4c GetUnit__9TUnitListFUl
TUnit*
TUnitList::GetUnit(ULong index)
{
	TUnit** entry = (TUnit**) GetEntry(index);
	return entry != nil ? *entry : nil;
}


/*------------------------------------------------------------------------------
	T T y p e L i s t
------------------------------------------------------------------------------*/

// ROM 0x0022ca94 Make__9TTypeListSFv
TTypeList*
TTypeList::Make(void)
{
	TTypeList* list = new TTypeList;
	if (list != nil)
	{
		list->fData = nil;
		if (list->ITypeList() != 0)
		{
			list->Dispose();
			list = nil;
		}
	}
	return list;
}


// ROM 0x0022cafc ITypeList__9TTypeListFv
long
TTypeList::ITypeList(void)
{
	long err = IDArray(sizeof(ULong), 0);
	Compact();
	return err;
}


// ROM 0x0022cc4c Dump__9TTypeListFP4TMsg
void
TTypeList::Dump(TMsg* /*msg*/)
{ }


// ROM 0x0022cb34 AddType__9TTypeListFUl
Boolean
TTypeList::AddType(ULong type)
{
	ULong* entry = (ULong*) AddEntry();
	if (entry != nil)
		*entry = type;
	return entry == nil;
}


// ROM 0x0022cb64 AddUnique__9TTypeListFUl
Boolean
TTypeList::AddUnique(ULong type)
{
	if (fCount != 0)
	{
		ULong* entry = (ULong*) GetEntry(0);
		for (ULong i = 0; i < (ULong) fCount; i++, entry++)
			if (*entry == type)
				return false;
	}
	ULong* entry = (ULong*) AddEntry();
	if (entry != nil)
		*entry = type;
	return entry == nil;
}


// ROM 0x0022cbd8 FindType__9TTypeListFUl
// ==> the type's index, -1 for not there.
ULong
TTypeList::FindType(ULong type)
{
	for (ULong i = 0; i < (ULong) fCount; i++)
		if (GetType(i) == type)
			return i;
	return (ULong) -1;
}


// ROM 0x0022cc2c GetType__9TTypeListFUl
ULong
TTypeList::GetType(ULong index)
{
	return *(ULong*) GetEntry(index);
}


/*------------------------------------------------------------------------------
	T S I U n i t
------------------------------------------------------------------------------*/

// ROM 0x0021d070 InitInterpretation__FP18UnitInterpretationUlT2
// An interpretation with no label, the worst score, and - when count is
// not 0 - a TArray of count entries of elementSize as its parameter.
// ==> 1, or 0 when the array could not be made.
long
InitInterpretation(UnitInterpretation* interp, ULong elementSize, ULong count)
{
	long ok = 1;
	interp->label = -1;
	interp->score = 10000;
	interp->angle = 0;
	TArray* param = nil;
	if (count != 0)
	{
		param = TArray::Make(elementSize, count);
		if (param == nil)
			ok = 0;
	}
	interp->param = param;
	return ok;
}


// ROM 0x0021ca70 __ct__7TSIUnitFv
TSIUnit::TSIUnit()
{ }


// ROM 0x0021d528 ISIUnit__7TSIUnitFP7TDomainUlT2P6TArrayT2
// No subs, no interpretations yet (their size kept for when the list is
// made).
long
TSIUnit::ISIUnit(TDomain* domain, ULong type, ULong kind, TArray* areas, ULong interpSize)
{
	IUnit(domain, type, kind, areas);
	fSubKind = kNoSubs;
	fSubs = nil;
	fHasInterps = 0;
	fInterpSize = interpSize;
	return 0;
}


// ROM 0x0021d440 Dump__7TSIUnitFP4TMsg
void
TSIUnit::Dump(TMsg* msg)
{
	TUnit::Dump(msg);
}


// ROM 0x0021da24 SizeInBytes__7TSIUnitFv
long
TSIUnit::SizeInBytes(void)
{
	long size = 0;
	ULong count = InterpretationCount();
	for (ULong i = 0; i < count; i++)
	{
		TRecObject* param = GetParam(i);
		if (param != nil)
			size += param->SizeInBytes();
	}
	if (fSubKind == kSubList)
		size += fSubs->SizeInBytes();
	if (fHasInterps == 1)
		size += fInterps->SizeInBytes();
	return TUnit::SizeInBytes() + size;
}


// ROM 0x0021d728 IDispose__7TSIUnitFv
// The sub list (not the subs) and the interpretations gone, then the
// unit's own.
void
TSIUnit::IDispose(void)
{
	if (fSubKind == kSubList)
		fSubs->Dispose();
	if (fHasInterps == 1)
		for (long i = InterpretationCount() - 1; i >= 0; i--)
			DeleteInterpretation(i);
	fSubKind = kNoSubs;
	fHasInterps = 0;
	TUnit::IDispose();
}


// ROM 0x0021dbb8 SubCount__7TSIUnitFv
long
TSIUnit::SubCount(void)
{
	if (fSubKind == kOneSub)
		return 1;
	if (fSubKind == kSubList && fSubs != nil)
		return ((TDArray*) fSubs)->Count();
	return 0;
}


// ROM 0x0021cab0 AddSub__7TSIUnitFP5TUnit
// A sub added: the first is kept in place, the second makes the list.  The
// unit's box grows to the sub's, its start time and duration cover the
// sub's, its elapsed time is now, its stroke range takes the sub's in, and
// it is delayed again as its domain says; a sub that is flagged as passed
// on passes the flag up.  ==> the sub's index, -1 for no memory.
long
TSIUnit::AddSub(TUnit* sub)
{
	long index;
	if (fSubKind == kNoSubs)
	{
		fSubKind = kOneSub;
		index = 0;
		fSubs = sub;
	}
	else if (fSubKind == kOneSub)
	{
		TDArray* list = TDArray::Make(sizeof(TUnit*), 2);
		if (list == nil)
			return -1;
		list->SetEntry(0, (const char*) &fSubs);
		list->SetEntry(1, (const char*) &sub);
		fSubKind = kSubList;
		index = 1;
		fSubs = list;
	}
	else if (fSubKind == kSubList)
	{
		TDArray* list = (TDArray*) fSubs;
		index = list->Add();
		if (index == -1)
			return -1;
		list->SetEntry(index, (const char*) &sub);
	}
	else
		return -1;

	FRect subBox, box;
	sub->GetBBox(&subBox);
	GetBBox(&box);
	AddRect(&subBox, &box, index == 0);
	SetBBox(&box);
	if (index == 0)
	{
		fStartTime = sub->fStartTime;
		fDuration = (UShort) (sub->fStartTime + sub->fDuration - fStartTime);
	}
	else
	{
		ULong start = fStartTime < sub->fStartTime ? fStartTime : sub->fStartTime;
		fStartTime = start;
		ULong end = fStartTime + fDuration;
		ULong subEnd = sub->fStartTime + sub->fDuration;
		if (subEnd < end)
			subEnd = end;
		fDuration = (UShort) (subEnd - start);
	}
	fElapsed = (UShort) (GetTicks() - fStartTime);
	if (fDomain != nil && fDomain->fDelay != 0)
	{
		SetFlags(kDelayedUnit);
		SetDelay(fDomain->fDelay);
	}
	if (fSubRange == 0xffff)
	{
		fSubRange = 0;
		fMinStroke = sub->fMinStroke;
		fMaxStroke = sub->fMaxStroke;
	}
	else
	{
		if (sub->fMinStroke < fMinStroke)
			fMinStroke = sub->fMinStroke;
		if (sub->fMaxStroke > fMaxStroke)
			fMaxStroke = sub->fMaxStroke;
	}
	NoteUnitExpiry(this);
	if (sub->TestFlags(kPassedOnUnit))
		SetFlags(kPassedOnUnit);
	return index;
}


// ROM 0x0021cdb8 GetSub__7TSIUnitFUl
TUnit*
TSIUnit::GetSub(ULong index)
{
	if (fSubKind == kOneSub)
		return index == 0 ? (TUnit*) fSubs : nil;
	if (fSubKind == kSubList)
		return *(TUnit**) ((TDArray*) fSubs)->GetEntry(index);
	return nil;
}


// ROM 0x0021ce14 DeleteSub__7TSIUnitFUl
// The sub taken out and disposed; a list left with one sub folds back to
// keeping it in place.
void
TSIUnit::DeleteSub(ULong index)
{
	TUnit* sub;
	if (fSubKind == kOneSub)
	{
		if (index != 0)
			return;
		sub = (TUnit*) fSubs;
		fSubKind = kNoSubs;
	}
	else if (fSubKind == kSubList)
	{
		sub = GetSub(index);
		TDArray* list = (TDArray*) fSubs;
		list->Delete(index);
		if (list->Count() == 1)
		{
			TUnit* last = *(TUnit**) list->GetEntry(0);
			list->Dispose();
			fSubs = last;
			fSubKind = kOneSub;
		}
	}
	else
		return;
	if (sub != nil)
		sub->Dispose();
}


// ROM 0x0021cedc MarkUnit__7TSIUnitFP9TUnitListUl
// The unit flagged and its subs marked; a unit with no subs is put in the
// list itself.  ==> 0, or 1 for no memory.
long
TSIUnit::MarkUnit(TUnitList* list, ULong flags)
{
	SetFlags(flags);
	long count = SubCount();
	for (long i = 0; i < count; i++)
	{
		long err = GetSub(i)->MarkUnit(list, flags);
		if (err != 0)
			return err;
	}
	if (count == 0)
		return list->AddUnit(this);
	return 0;
}


// ROM 0x0021cf7c ClaimUnit__7TSIUnitFP9TUnitList
void
TSIUnit::ClaimUnit(TUnitList* list)
{
	MarkUnit(list, kClaimedUnit);
}


// ROM 0x0021cf88 GetSubsCopy__7TSIUnitFv
// The subs as a list: the list itself (cloned), or a new one of the single
// sub; nil for none.
TDArray*
TSIUnit::GetSubsCopy(void)
{
	if (fSubKind == kOneSub)
	{
		TDArray* list = TDArray::Make(sizeof(TUnit*), 1);
		if (list != nil)
			*(TRecObject**) list->GetEntry(0) = fSubs;
		return list;
	}
	if (fSubKind == kSubList)
	{
		((TDArray*) fSubs)->Clone();
		return (TDArray*) fSubs;
	}
	return nil;
}


// ROM 0x0021d000 CloseInterpList__7TSIUnitFv
// The list disposed; its element size stays for the next one.
void
TSIUnit::CloseInterpList(void)
{
	ULong size = fInterps->ElementSize();
	fInterps->Dispose();
	fInterpSize = size;
	fHasInterps = 0;
}


// ROM 0x0021d030 OpenInterpList__7TSIUnitFv
// ==> 0, or -1 for no memory.
long
TSIUnit::OpenInterpList(void)
{
	TDArray* list = TDArray::Make(fInterpSize, 1);
	if (list == nil)
		return -1;
	fInterps = list;
	fHasInterps = 1;
	return 0;
}


// ROM 0x0021d0c8 InterpretationCount__7TSIUnitFv
long
TSIUnit::InterpretationCount(void)
{
	if (fHasInterps == 1 && fInterps != nil)
		return fInterps->Count();
	return 0;
}


// ROM 0x0021d0f4 InterpretationReuse__7TSIUnitFUlN21
// The interpretations cut back to count; when there are fewer, the list
// is sized to count and - when paramCount is not 0 - the new ones are set
// up fresh, each with a paramCount-entry parameter array of paramSize.
long
TSIUnit::InterpretationReuse(ULong count, ULong paramSize, ULong paramCount)
{
	ULong have = InterpretationCount();
	if (count < have)
		for (long i = have - 1; i >= (long) count; i--)
			DeleteInterpretation(i);
	if (fHasInterps == 0)
	{
		if (count == 0)
			return 0;
		OpenInterpList();
	}
	if (fHasInterps == 1 && count == 0)
		CloseInterpList();
	else if (count != 0)
	{
		if (paramCount == 0)
			fInterps->Reuse(count);
		else
		{
			have = InterpretationCount();
			if (have < count)
			{
				fInterps->Reuse(count);
				for ( ; have < count; have++)
				{
					UnitInterpretation interp;
					InitInterpretation(&interp, paramSize, paramCount);
					*GetInterpretation(have) = interp;
				}
			}
		}
	}
	return 0;
}


// ROM 0x0021d240 AddInterpretation__7TSIUnitFPc
// ==> the new interpretation's index, -1 for no memory.
long
TSIUnit::AddInterpretation(char* interp)
{
	long index;
	if (fHasInterps == 0)
		index = OpenInterpList();
	else if (fHasInterps == 1)
		index = fInterps->Add();
	else
		return -1;
	if (index != -1)
		fInterps->SetEntry(index, interp);
	return index;
}


// ROM 0x0021d2b8 GetInterpretation__7TSIUnitFUl
UnitInterpretation*
TSIUnit::GetInterpretation(ULong index)
{
	if (fHasInterps != 0)
		return (UnitInterpretation*) fInterps->GetEntry(index);
	return nil;
}


// ROM 0x0021d2d8 DeleteInterpretation__7TSIUnitFUl
// The interpretation's parameter disposed and the entry deleted; the list
// closes when it empties.  ==> 1.
long
TSIUnit::DeleteInterpretation(ULong index)
{
	TRecObject* param = GetParam(index);
	if (param != nil)
		param->Dispose();
	fInterps->Delete(index);
	if (fInterps->Count() == 0)
		CloseInterpList();
	return 1;
}


// ROM 0x0021d338 InsertInterpretation__7TSIUnitFUl
// A slot opened at the index.  ==> its index, -1 for no memory.
long
TSIUnit::InsertInterpretation(ULong index)
{
	if (fHasInterps == 0)
		return OpenInterpList();
	if (fHasInterps == 1)
		return fInterps->Insert(index);
	return -1;
}


// ROM 0x0021d384 LockInterpretations__7TSIUnitFv
char*
TSIUnit::LockInterpretations(void)
{
	if (fHasInterps == 0)
		return nil;
	return fInterps->Lock();
}


// ROM 0x0021d398 UnlockInterpretations__7TSIUnitFv
void
TSIUnit::UnlockInterpretations(void)
{
	if (fHasInterps != 0)
		fInterps->Unlock();
}


// ROM 0x0021d3ac CompactInterpretations__7TSIUnitFv
void
TSIUnit::CompactInterpretations(void)
{
	if (fHasInterps != 0)
		fInterps->Compact();
}


// ROM 0x0021d3c4 GetBestInterpretation__7TSIUnitFv
// The labelled interpretation with the lowest score (below 10000); -1 for
// none.
long
TSIUnit::GetBestInterpretation(void)
{
	long best = -1;
	long bestScore = 10000;
	ULong count = InterpretationCount();
	for (ULong i = 0; i < count; i++)
	{
		UnitInterpretation* interp = GetInterpretation(i);
		if (interp->label != -1 && interp->score < bestScore)
		{
			bestScore = interp->score;
			best = i;
		}
	}
	return best;
}


// ROM 0x0021d568 GetLabel__7TSIUnitFUl
long
TSIUnit::GetLabel(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	return interp != nil ? interp->label : 0;
}


// ROM 0x0021d590 GetScore__7TSIUnitFUl
long
TSIUnit::GetScore(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	return interp != nil ? interp->score : 0;
}


// ROM 0x0021d5b8 GetAngle__7TSIUnitFUl
long
TSIUnit::GetAngle(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	return interp != nil ? interp->angle : 0;
}


// ROM 0x0021d5e0 GetParam__7TSIUnitFUl
TRecObject*
TSIUnit::GetParam(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	return interp != nil ? interp->param : nil;
}


// ROM 0x0021d608 SetLabel__7TSIUnitFUlT1
void
TSIUnit::SetLabel(ULong index, ULong label)
{
	UnitInterpretation* interp = GetInterpretation(index);
	if (interp != nil)
		interp->label = label;
}


// ROM 0x0021d630 SetScore__7TSIUnitFUlT1
void
TSIUnit::SetScore(ULong index, ULong score)
{
	UnitInterpretation* interp = GetInterpretation(index);
	if (interp != nil)
		interp->score = score;
}


// ROM 0x0021d658 SetAngle__7TSIUnitFUll
void
TSIUnit::SetAngle(ULong index, long angle)
{
	UnitInterpretation* interp = GetInterpretation(index);
	if (interp != nil)
		interp->angle = angle;
}


// ROM 0x0021d680 CheckInterpretationIndex__7TSIUnitFUl
Boolean
TSIUnit::CheckInterpretationIndex(ULong index)
{
	return index < (ULong) InterpretationCount();
}


// ROM 0x0021d6c0 CountStrokes__7TSIUnitFv
// The subs' strokes together.
long
TSIUnit::CountStrokes(void)
{
	long strokes = 0;
	ULong count = SubCount();
	for (ULong i = 0; i < count; i++)
		strokes += GetSub(i)->CountStrokes();
	return strokes;
}


// ROM 0x0021d7a4 GetStroke__7TSIUnitFUl
// The index-th stroke, counting across the subs.
TStroke*
TSIUnit::GetStroke(ULong index)
{
	if (index >= (ULong) CountStrokes())
		return nil;
	if (index == 0)
		return GetSub(0)->GetStroke(0);
	ULong subs = SubCount();
	ULong base = 0;
	for (ULong i = 0; i < subs; i++)
	{
		TUnit* sub = GetSub(i);
		ULong count = sub->CountStrokes();
		if (index < base + count)
			return sub->GetStroke(index - base);
		base += count;
	}
	return nil;
}


// ROM 0x0021d870 GetAllStrokes__7TSIUnitFv
// The units under this one that own strokes: the tree is walked down a
// level at a time - each level's units' subs make the next - until a
// level owns its strokes.  ==> the list, nil for no memory.
TUnitList*
TSIUnit::GetAllStrokes(void)
{
	Boolean ownsStrokes = false;
	TUnitList* level = TUnitList::Make();
	TUnitList* next = TUnitList::Make();
	if (level == nil || next == nil || level->AddUnit(this))
	{
		if (next != nil)
			next->Dispose();
		if (level != nil)
			level->Dispose();
		return nil;
	}
	for (;;)
	{
		long count = level->Count();
		for (long i = 0; i < count; i++)
		{
			TSIUnit* unit = (TSIUnit*) level->GetUnit(i);
			long subs = unit->SubCount();
			for (long j = 0; j < subs; j++)
			{
				TUnit* sub = unit->GetSub(j);
				ownsStrokes = sub->OwnsStroke();
				if (next->AddUnique(sub))
				{
					next->Dispose();
					level->Dispose();
					return nil;
				}
			}
		}
		level->Clear();
		if (ownsStrokes || next->Count() == 0)
			break;
		TUnitList* swap = level;
		level = next;
		next = swap;
	}
	level->Dispose();
	return next;
}


// ROM 0x0021d9c0 DoneUsingUnit__7TSIUnitFv
// The interpretations and the areas let go.
void
TSIUnit::DoneUsingUnit(void)
{
	if (fHasInterps == 1)
		for (long i = InterpretationCount() - 1; i >= 0; i--)
			DeleteInterpretation(i);
	fHasInterps = 0;
	TUnit::DoneUsingUnit();
}


// ROM 0x0021dadc EndSubs__7TSIUnitFv
// No more subs: the delay is dropped and the list compacted.  ==> 0.
long
TSIUnit::EndSubs(void)
{
	SetDelay(0);
	if (fSubKind == kSubList)
		((TDArray*) fSubs)->Compact();
	NoteUnitExpiry(this);
	return 0;
}


// ROM 0x0021db3c EndUnit__7TSIUnitFv
// The subs ended, the interpretations' parameter arrays compacted.
void
TSIUnit::EndUnit(void)
{
	EndSubs();
	ULong count = InterpretationCount();
	for (ULong i = 0; i < count; i++)
	{
		TArray* param = (TArray*) GetParam(i);
		if (param != nil)
			param->Compact();
	}
	CompactInterpretations();
}


/*------------------------------------------------------------------------------
	T S t r o k e U n i t
------------------------------------------------------------------------------*/

// ROM 0x00221ec8 Make__11TStrokeUnitSFP7TDomainUlP7TStrokeP6TArray
TStrokeUnit*
TStrokeUnit::Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas)
{
	TStrokeUnit* unit = new TStrokeUnit;
	if (unit != nil && unit->IStrokeUnit(domain, kind, stroke, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x00221f50 IStrokeUnit__11TStrokeUnitFP7TDomainUlP7TStrokeP6TArray
// A 'STRK' unit over the stroke: its box, from its down time to its up
// time; 16-byte interpretations.
long
TStrokeUnit::IStrokeUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas)
{
	long err = ISIUnit(domain, kStrokeUnit, kind, areas, sizeof(UnitInterpretation));
	fContextID = 0;
	fStroke = stroke;
	SetBBox(&stroke->fBBox);
	fStartTime = stroke->fDownTime;
	fDuration = (UShort) (stroke->fUpTime - stroke->fDownTime);
	return err;
}


// ROM 0x00220f94 Dump__11TStrokeUnitFP4TMsg
void
TStrokeUnit::Dump(TMsg* /*msg*/)
{ }


// ROM 0x00221fd0 IDispose__11TStrokeUnitFv
// The stroke goes with the unit while the unit is still a 'STRK' (a
// domain that retypes the unit takes the stroke over).
void
TStrokeUnit::IDispose(void)
{
	if (fType == kStrokeUnit)
		fStroke->Dispose();
	TSIUnit::IDispose();
}


// ROM 0x00220f28 CountStrokes__11TStrokeUnitFv
long
TStrokeUnit::CountStrokes(void)
{
	return 1;
}


// ROM 0x00220f30 GetStroke__11TStrokeUnitFUl
TStroke*
TStrokeUnit::GetStroke(ULong /*index*/)
{
	return fStroke;
}


// ROM 0x00220f50 GetAllStrokes__11TStrokeUnitFv
// A list of the unit itself.
TUnitList*
TStrokeUnit::GetAllStrokes(void)
{
	TUnitList* list = TUnitList::Make();
	if (list != nil)
	{
		if (!list->AddUnique(this))
			return list;
		list->Dispose();
	}
	return nil;
}


// ROM 0x00220f38 OwnsStroke__11TStrokeUnitFv
Boolean
TStrokeUnit::OwnsStroke(void)
{
	return true;
}


// ROM 0x00220f40 ContextID__11TStrokeUnitFv
ULong
TStrokeUnit::ContextID(void)
{
	return fContextID;
}


// ROM 0x00220f48 SetContextID__11TStrokeUnitFUl
void
TStrokeUnit::SetContextID(ULong id)
{
	fContextID = id;
}


/*------------------------------------------------------------------------------
	T C l i c k U n i t
------------------------------------------------------------------------------*/

// ROM 0x0021f518 Make__10TClickUnitSFP7TDomainUlP7TStrokeP6TArray
TClickUnit*
TClickUnit::Make(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas)
{
	TClickUnit* unit = new TClickUnit;
	if (unit != nil && unit->IClickUnit(domain, kind, stroke, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x0021f5a0 IClickUnit__10TClickUnitFP7TDomainUlP7TStrokeP6TArray
// A 'CLIK' unit over the stroke of the pen-down: its box so far, started
// at its down time, no duration.
long
TClickUnit::IClickUnit(TDomain* domain, ULong kind, TStroke* stroke, TArray* areas)
{
	long err = IUnit(domain, kClickUnit, kind, areas);
	fStroke = stroke;
	SetBBox(&stroke->fBBox);
	fStartTime = stroke->fDownTime;
	fDuration = 0;
	return err;
}


// ROM 0x0021f28c Dump__10TClickUnitFP4TMsg
void
TClickUnit::Dump(TMsg* msg)
{
	TUnit::Dump(msg);
}


// ROM 0x0021f640 IDispose__10TClickUnitFv
// The stroke let out of the inker's buffer and disposed.
void
TClickUnit::IDispose(void)
{
	fStroke->UnsetFlags(kBufferedStroke);
	UnbufferStroke(fStroke);
	fStroke->Dispose();
	TUnit::IDispose();
}


// ROM 0x0021f608 MarkUnit__10TClickUnitFP9TUnitListUl
// The stroke let out of the inker's buffer as the unit is marked.
long
TClickUnit::MarkUnit(TUnitList* list, ULong flags)
{
	fStroke->UnsetFlags(kBufferedStroke);
	long err = list->AddUnit(this);
	SetFlags(flags);
	return err;
}


// ROM 0x0021f2e4 CountStrokes__10TClickUnitFv
long
TClickUnit::CountStrokes(void)
{
	return 1;
}


// ROM 0x0021f2ec GetStroke__10TClickUnitFUl
TStroke*
TClickUnit::GetStroke(ULong /*index*/)
{
	return fStroke;
}


// ROM 0x0021f2f4 OwnsStroke__10TClickUnitFv
Boolean
TClickUnit::OwnsStroke(void)
{
	return true;
}


/*------------------------------------------------------------------------------
	T C l i c k E v e n t U n i t
------------------------------------------------------------------------------*/

// ROM 0x0021f2fc Make__15TClickEventUnitSFP7TDomainUlP6TArray
TClickEventUnit*
TClickEventUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TClickEventUnit* unit = new TClickEventUnit;
	if (unit != nil && unit->IClickEventUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x0021f374 IClickEventUnit__15TClickEventUnitFP7TDomainUlP6TArray
// A 'CEVT' unit with no interpretations; its event is read from its sub
// (a click unit) when first asked for.
long
TClickEventUnit::IClickEventUnit(TDomain* domain, ULong kind, TArray* areas)
{
	ISIUnit(domain, kClickEventUnit, kind, areas, 0);
	fEvent = -1;
	return 0;
}


// ROM 0x0021f3b4 Event__15TClickEventUnitFv
// The click event the sub's stroke carries, read once.
long
TClickEventUnit::Event(void)
{
	if (fEvent == -1)
	{
		TClickUnit* click = (TClickUnit*) GetSub(0);
		fEvent = click->fStroke->fClickEvent;
	}
	return fEvent;
}


// ROM 0x0021f3f8 ClearEvent__15TClickEventUnitFv
// The event kept in the unit and marked processed in the stroke, so the
// stroke world does not make another unit of it.
void
TClickEventUnit::ClearEvent(void)
{
	fEvent = Event();
	TClickUnit* click = (TClickUnit*) GetSub(0);
	click->fStroke->fClickEvent = kProcessedClick;
}


// ROM 0x0021f434 Dump__15TClickEventUnitFP4TMsg
void
TClickEventUnit::Dump(TMsg* msg)
{
	TSIUnit::Dump(msg);
}
