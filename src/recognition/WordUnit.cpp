/*
	File:		recognition/WordUnit.cpp

	Contains:	The units the word recognisers work in - WordUnit.h.
*/

#include "WordUnit.h"
#include "Domain.h"
#include "WRecDomain.h"
#include "Unicode.h"
#include "NewtErrors.h"

#include <string.h>


/*------------------------------------------------------------------------------
	T S t d W o r d U n i t
------------------------------------------------------------------------------*/

// ROM 0x0021f67c IStdWordUnit__12TStdWordUnitFP7TDomainUlP6TArrayT2
// A word unit takes its type from the domain that makes it.
long
TStdWordUnit::IStdWordUnit(TDomain* domain, ULong kind, TArray* areas, ULong interpSize)
{
	return ISIUnit(domain, domain->fType, kind, areas, interpSize);
}


// ROM 0x0021fb40 Dump__12TStdWordUnitFP4TMsg
void
TStdWordUnit::Dump(TMsg* msg)
{
}


// ROM 0x0021f9c8 SizeInBytes__12TStdWordUnitFv
// What a TSIUnit takes, and the words on top of it.
long
TStdWordUnit::SizeInBytes(void)
{
	long size = 0;
	ULong count = (ULong) InterpretationCount();
	for (ULong i = 0; i < count; i++)
	{
		Handle string = GetString(i);
		if (string != nil)
			size += SizeOfHandle(string);
	}
	return TSIUnit::SizeInBytes() + size;
}


// ROM 0x0021fc28 DeleteInterpretation__12TStdWordUnitFUl
// The interpretation's parameter is a handle holding its word, not the
// recogniser object a TSIUnit's is, so it is given back as one.  The
// label is put out of the way first, so that nothing picks the
// interpretation as the best one on the way out.
long
TStdWordUnit::DeleteInterpretation(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	if (interp == nil)
		return 0;
	if (interp->param != nil)
		DeleteHandle((Handle) interp->param);
	interp->label = -1;
	TSIUnit::DeleteInterpretation(index);
	return 1;
}


// ROM 0x0021f93c GetParam__12TStdWordUnitFUl
// A word unit's interpretations have no parameter object: what would be
// there is the word.
TRecObject*
TStdWordUnit::GetParam(ULong index)
{
	return nil;
}


// ROM 0x0021f998 EndUnit__12TStdWordUnitFv
void
TStdWordUnit::EndUnit(void)
{
	EndSubs();
	CompactInterpretations();
}


// ROM 0x0021fb44 AddWordInterpretation__12TStdWordUnitFv
// One added at the end: the ROM asks to insert at 0x7fff, which is past
// any real count, and the insert clamps it.
long
TStdWordUnit::AddWordInterpretation(void)
{
	return InsertWordInterpretation(0x7fff);
}


// ROM 0x0021fb54 InsertWordInterpretation__12TStdWordUnitFUl
// An interpretation with an empty word: the handle its word lives in is
// made first, so that nothing is inserted when there is no memory for
// it.  ==> the index it went in at, -1 for no memory.
long
TStdWordUnit::InsertWordInterpretation(ULong index)
{
	Handle string = MakeHandle(0x14);
	NameHandle(string, kWordStringHandleName);
	if (string == nil)
		return -1;
	long at = InsertInterpretation(index);
	UnitInterpretation* interp = GetInterpretation((ULong) at);
	if (interp == nil)
		return -1;
	LockInterpretations();
	InitInterpretation(interp, 0, 0);
	*(UniChar*) *string = 0;
	// (the list may have moved: the interpretation is asked for again)
	GetInterpretation((ULong) at)->param = (TRecObject*) string;
	UnlockInterpretations();
	return at;
}


// ROM 0x0021fad0 SetCharWordString__12TStdWordUnitFUlPc
// The word set from an eight-bit string, which is what a recogniser
// that works in characters rather than in Unicode hands over.
void
TStdWordUnit::SetCharWordString(ULong index, const char* str)
{
	Handle string = GetString(index);
	if (string == nil)
		return;
	long length = (long) strlen(str);
	if (ResizeHandle(string, (length + 1) * (long) sizeof(UniChar)) != noErr)
	{
		*(UniChar*) *string = 0;
		return;
	}
	// (the ROM asks the 'unicode frame's own converter when it has been
	//  installed, and widens the bytes itself when it has not)
	ConvertToUnicode(str, (UniChar*) *string, kMacRomanEncoding, length);
	((UniChar*) *string)[length] = 0;
}


// ROM 0x0021fa6c SetWordString__12TStdWordUnitFUlPUs
// ... and from Unicode.  ==> the word, nil when there was no room for
// it (and the word is then empty rather than half written).
UniChar*
TStdWordUnit::SetWordString(ULong index, const UniChar* str)
{
	Handle string = GetString(index);
	if (string == nil)
		return nil;
	if (ResizeHandle(string, Ustrlen(str) * (long) sizeof(UniChar)
							 + (long) sizeof(UniChar)) != noErr)
	{
		*(UniChar*) *string = 0;
		return nil;
	}
	UniChar* out = (UniChar*) *string;
	UniChar* at = out;
	UniChar c;
	do
	{
		c = *str++;
		*at++ = c;
	}
	while (c != 0);
	return out;
}


// ROM 0x0021fa44 GetString__12TStdWordUnitFUl
Handle
TStdWordUnit::GetString(ULong index)
{
	UnitInterpretation* interp = GetInterpretation(index);
	return interp == nil ? nil : (Handle) interp->param;
}


// ROM 0x0021f944 SetParam__12TStdWordUnitFP18UnitInterpretationUlPc
void
TStdWordUnit::SetParam(UnitInterpretation* interp, ULong elementSize, char* data)
{
}


// ROM 0x0021f948 GetWordBase__12TStdWordUnitFP6FPointT1Ul
// Where the word stands, as far as the unit itself can say: the bottom
// edge of its box, from its left to its right.  A recogniser that has
// measured the writing answers better.
void
TStdWordUnit::GetWordBase(FPoint* left, FPoint* right, ULong index)
{
	FRect box;
	GetBBox(&box);
	left->x = box.left;
	left->y = box.bottom;
	right->x = box.right;
	right->y = box.bottom;
}


// ROM 0x0021f988 GetWordSlant__12TStdWordUnitFUl
long
TStdWordUnit::GetWordSlant(ULong index)
{
	return 0;
}


// ROM 0x0021f990 GetWordSize__12TStdWordUnitFUl
long
TStdWordUnit::GetWordSize(ULong index)
{
	return 0;
}


// ROM 0x0021f9c4 ReinforceWordChoice__12TStdWordUnitFl
void
TStdWordUnit::ReinforceWordChoice(long index)
{
}


// ROM 0x0021fa38 GetTrainingData__12TStdWordUnitFl
Handle
TStdWordUnit::GetTrainingData(long index)
{
	return nil;
}


// ROM 0x0021fa40 DisposeTrainingData__12TStdWordUnitFPPc
void
TStdWordUnit::DisposeTrainingData(Handle data)
{
}


/*------------------------------------------------------------------------------
	T R e c U n i t   a n d   T W R e c U n i t
------------------------------------------------------------------------------*/

// ROM 0x0026e810 IRecUnit__8TRecUnitFP7TDomainUlP6TArray
// The engine has no working store for the unit yet.  Sixteen bytes is
// what an interpretation of this kind takes: a label, a score, an angle
// and the handle its word lives in.
long
TRecUnit::IRecUnit(TDomain* domain, ULong kind, TArray* areas)
{
	fUnitInfo = nil;
	return IStdWordUnit(domain, kind, areas, sizeof(UnitInterpretation));
}


// ROM 0x0026e8e4 Dump__8TRecUnitFP4TMsg
void
TRecUnit::Dump(TMsg* msg)
{
}


// ROM 0x0026e8b8 IDispose__8TRecUnitFv
// The engine's working store goes back through the domain, which is the
// only thing that knows which heap it came out of.
void
TRecUnit::IDispose(void)
{
	if (fUnitInfo != nil)
		((TWRecDomain*) fDomain)->UnitInfoFreePtr(fUnitInfo);
	TSIUnit::IDispose();
}


// ROM 0x0026e83c Make__9TWRecUnitSFP7TDomainUlP6TArray
TWRecUnit*
TWRecUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TWRecUnit* unit = new TWRecUnit;
	if (unit == nil)
		return nil;
	if (unit->IWRecUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		return nil;
	}
	return unit;
}


// ROM 0x0026e8b4 IWRecUnit__9TWRecUnitFP7TDomainUlP6TArray
// In the ROM one instruction, a branch straight to TRecUnit's.
long
TWRecUnit::IWRecUnit(TDomain* domain, ULong kind, TArray* areas)
{
	return IRecUnit(domain, kind, areas);
}


/*------------------------------------------------------------------------------
	A   w o r d   u n i t ' s   r e a d i n g s   s e t   a s i d e
------------------------------------------------------------------------------*/

// ROM 0x0021f6a8 GetInterpretationsCopy__FP12TStdWordUnit
// Each interpretation copied with a copy of its word's handle.  When a
// handle cannot be copied the array is cut back and answered as it is.
// ROM QUIRK, kept: it is cut at the entry *before* the failed one
// (CutToIndex(i - 1)), so the last copy that did succeed is dropped too,
// its handle never given back.
// DEVIATION: an entry is sizeof(UnitInterpretation), not the ROM's 0x10
// bytes - the parameter is a host pointer.
TDArray*
GetInterpretationsCopy(TStdWordUnit* unit)
{
	ULong count = (ULong) unit->InterpretationCount();
	TDArray* copy = TDArray::Make(sizeof(UnitInterpretation), count);
	if (copy != nil)
	{
		for (ULong i = 0; i < count; i++)
		{
			UnitInterpretation interp = *unit->GetInterpretation(i);
			Handle word = (Handle) interp.param;
			if (CopyHandle(&word) != 0)
			{
				copy->CutToIndex(i == 0 ? 0 : i - 1);
				return copy;
			}
			NameHandle(word, kWordStringHandleName);
			interp.param = (TRecObject*) word;
			memcpy(copy->GetEntry(i), &interp, sizeof(UnitInterpretation));
		}
	}
	return copy;
}


// ROM 0x0021f78c SetInterpretationsCopy__FP12TStdWordUnitP7TDArray
// The unit's interpretations deleted, last first, and each one in the
// array added in its place with a copy of its handle.
// ROM QUIRK, kept: when a handle cannot be copied it is interpretation i -
// the array's index - that is deleted, not the one just added; the unit
// was emptied first, so the two are the same unless an add failed.
long
SetInterpretationsCopy(TStdWordUnit* unit, TDArray* copy)
{
	for (long i = unit->InterpretationCount() - 1; i >= 0; i--)
		unit->DeleteInterpretation((ULong) i);
	long count = (copy != nil) ? copy->fCount : 0;
	for (long i = 0; i < count; i++)
	{
		UnitInterpretation* from = (UnitInterpretation*) copy->GetEntry((ULong) i);
		if (from == nil)
			continue;
		UnitInterpretation saved = *from;
		long index = unit->AddWordInterpretation();
		if (index == -1)
			continue;
		UnitInterpretation* interp = unit->GetInterpretation((ULong) index);
		DeleteHandle((Handle) interp->param);
		interp->param = nil;
		Handle word = (Handle) saved.param;
		if (CopyHandle(&word) != 0)
		{
			unit->DeleteInterpretation((ULong) i);
			return 0;
		}
		NameHandle(word, kWordStringHandleName);
		interp = unit->GetInterpretation((ULong) index);
		interp->label = saved.label;
		interp->score = saved.score;
		interp->angle = saved.angle;
		interp->param = (TRecObject*) word;
	}
	return 0;
}


// ROM 0x0021f8d8 DeleteInterpretationsCopy__FP7TDArray
void
DeleteInterpretationsCopy(TDArray* copy)
{
	if (copy == nil)
		return;
	long count = copy->fCount;
	for (long i = 0; i < count; i++)
	{
		UnitInterpretation* interp = (UnitInterpretation*) copy->GetEntry((ULong) i);
		if (interp != nil)
			DeleteHandle((Handle) interp->param);
	}
	copy->Dispose();
}
