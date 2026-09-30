/*
	File:		stores/Cursors.cpp

	Contains:	TUnionSoupIndex and TCursor (Cursors.h), the cursor natives
				and the soup query methods.

	Reconstructed from the MP2x00 US ROM (0x002cdc44-0x002d1c7c,
	0x002e875c-0x002e9460, 0x0034defc-0x0034e128); each function cites
	its origin.
*/

#include "Cursors.h"
#include "Tags.h"
#include "StoreObject.h"	// the entry's text and its hints
#include "SortTables.h"	// FindString, FindWord
#include "Frames.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include <string.h>
#include <new>

extern const ExceptionName exStoreError;

#define kNSErrCantQueryTagsIndex (ERRBASE_FRAMES - 32)		// the query's indexPath is the tags index (0xffff4460)

/*------------------------------------------------------------------------------
	T U n i o n S o u p I n d e x
------------------------------------------------------------------------------*/

// ROM 0x002e875c __ct__14UnionIndexDataFv
UnionIndexData::UnionIndexData()
{
	fIndex = nil;
	fState = kUnionStateInvalid;
	fKeyField = fKeyBuffer;
	fModCount = 0;
}


// ROM 0x002e87d8 __ct__15TUnionSoupIndexFlP14UnionIndexData
TUnionSoupIndex::TUnionSoupIndex(long numSoups, UnionIndexData* data)
{
	fNumSoups = numSoups;
	fData = data;
	fCurrentSoup = 0;
	fDirection = false;
}


// ROM 0x002e882c __dt__15TUnionSoupIndexFv
TUnionSoupIndex::~TUnionSoupIndex()
{
	delete[] fData;
}


// ROM 0x002e8874 Find__15TUnionSoupIndexFP4SKeyN21Uc
// The key looked for in every soup: the lowest of what they answer
// (an exact match beating a next key; a match in a soup with a sorting
// table is taken when not exact).  ==> Find's result for that soup.
int
TUnionSoupIndex::Find(SKey* key, SKey* outKey, SKey* outData, Boolean exact)
{
	if (fNumSoups == 1)
		return fData[0].fIndex->Find(key, outKey, outData, exact);
	SKey theKey = *key;
	SKey soupKey;
	SKey soupData;
	soupKey.Clear();
	soupData.Clear();
	int result = kIndexEnd;
	Boolean found = false;
	for (long i = 0; i < fNumSoups; i++)
	{
		TSoupIndex* index = fData[i].fIndex;
		int r = index->Find(&theKey, &soupKey, &soupData, exact);
		Boolean take;
		if (r == kIndexOK)
			take = (index->fSortingTable != nil && !exact) || result == kIndexNotFound || result == kIndexEnd;
		else
			take = r == kIndexNotFound;
		if (!take)
			continue;
		if (found && index->CompareKeys(soupKey, *outKey) >= 0)
			continue;
		result = r;
		memcpy((void*) outKey, &soupKey, index->kfSizeOfKey(&soupKey));
		if (outData != nil)
			memcpy((void*) outData, &soupData, index->kfSizeOfData(&soupData));
		SetCurrentSoup(i);
		found = true;
	}
	return result;
}


// ROM 0x002e8a24 First__15TUnionSoupIndexFP4SKeyT1
// The lowest first key of the soups.
int
TUnionSoupIndex::First(SKey* outKey, SKey* outData)
{
	if (fNumSoups == 1)
		return fData[0].fIndex->First(outKey, outData);
	SKey soupKey;
	SKey soupData;
	soupKey.Clear();
	soupData.Clear();
	int result = kIndexNotFound;
	Boolean found = false;
	for (long i = 0; i < fNumSoups; i++)
	{
		TSoupIndex* index = fData[i].fIndex;
		if (index->First(&soupKey, &soupData) != kIndexOK)
			continue;
		if (found && index->CompareKeys(soupKey, *outKey) >= 0)
			continue;
		result = kIndexOK;
		memcpy((void*) outKey, &soupKey, index->kfSizeOfKey(&soupKey));
		memcpy((void*) outData, &soupData, index->kfSizeOfData(&soupData));
		SetCurrentSoup(i);
		found = true;
	}
	return result;
}


// ROM 0x002e8b48 Last__15TUnionSoupIndexFP4SKeyT1
int
TUnionSoupIndex::Last(SKey* outKey, SKey* outData)
{
	if (fNumSoups == 1)
		return fData[0].fIndex->Last(outKey, outData);
	SKey soupKey;
	SKey soupData;
	soupKey.Clear();
	soupData.Clear();
	int result = kIndexNotFound;
	Boolean found = false;
	for (long i = 0; i < fNumSoups; i++)
	{
		TSoupIndex* index = fData[i].fIndex;
		if (index->Last(&soupKey, &soupData) != kIndexOK)
			continue;
		if (found && index->CompareKeys(soupKey, *outKey) <= 0)
			continue;
		result = kIndexOK;
		memcpy((void*) outKey, &soupKey, index->kfSizeOfKey(&soupKey));
		memcpy((void*) outData, &soupData, index->kfSizeOfData(&soupData));
		SetCurrentSoup(i);
		found = true;
	}
	return result;
}


// ROM 0x002e8c6c Next__15TUnionSoupIndexFP4SKeyT1iN21
int
TUnionSoupIndex::Next(SKey* key, SKey* data, int mode, SKey* outKey, SKey* outData)
{
	return Search(true, key, data, nil, nil, outKey, outData, mode);
}


// ROM 0x002e8cb0 Prior__15TUnionSoupIndexFP4SKeyT1UcN21
int
TUnionSoupIndex::Prior(SKey* key, SKey* data, Boolean skipDups, SKey* outKey, SKey* outData)
{
	return Search(false, key, data, nil, nil, outKey, outData, skipDups ? kIndexNextKey : kIndexNextDupOrKey);
}


// ROM 0x002e8cfc MoveToNextSoup__15TUnionSoupIndexFUciP4SKeyT1
// The soup with the next key after key (before it, backwards) becomes
// the current one: each soup not yet exhausted is positioned at the key
// after (its state kept), and the lowest (highest) wins - the current
// soup itself stays a candidate unless it is gone.  ==> kIndexOK, else
// kIndexEnd when every soup is exhausted.
int
TUnionSoupIndex::MoveToNextSoup(Boolean forward, int mode, SKey* key, Boolean currentGone)
{
	long i;
	long count;
	UnionIndexData* best;
	if (currentGone)
	{
		i = fCurrentSoup - 1;
		count = fNumSoups;
		best = nil;
	}
	else
	{
		i = fCurrentSoup;
		count = fNumSoups - 1;
		best = &fData[fCurrentSoup];
	}
	Boolean wrapped = false;
	for (long n = count - 1; n >= 0; n--)
	{
		i++;
		if (i == fNumSoups)
		{
			i = 0;
			wrapped = true;
		}
		UnionIndexData* d = &fData[i];
		if (d->fState == kUnionStateExhausted)
			continue;
		if (d->fState != kUnionStateValid)
		{
			d->fIndex->kfAssembleKeyField(d->fKeyField, key, nil);
			int r;
			if (forward)
			{
				r = d->fIndex->FindAndGetState(d->fKeyField, &d->fIndexState);
				if (r == kIndexOK)
				{
					if (wrapped || mode == kIndexNextKey)
						r = d->fIndex->MoveUsingState(forward, kIndexNextKey, d->fKeyField, &d->fIndexState);
					else
					{
						d->fState = kUnionStateValid;
						goto compare;
					}
				}
			}
			else
				r = d->fIndex->FindPriorAndGetState(d->fKeyField, !wrapped || mode == kIndexNextKey, &d->fIndexState);
			if (r == kIndexEnd)
			{
				d->fState = kUnionStateExhausted;
				continue;
			}
			if (r != kIndexOK && r != kIndexNotFound)
				return r;
			d->fState = kUnionStateValid;
		}
compare:
		if (best != nil && best->fState == kUnionStateValid)
		{
			int cmp = d->fIndex->CompareKeys(*KeyFieldKey(d->fKeyField), *KeyFieldKey(best->fKeyField));
			Boolean better;
			if (forward)
				better = cmp < 0 || (cmp == 0 && fCurrentSoup > i);
			else
				better = cmp > 0 || (cmp == 0 && fCurrentSoup < i);
			if (!better)
				continue;
		}
		best = d;
		fCurrentSoup = i;
	}
	return (best != nil && best->fState == kUnionStateValid) ? kIndexOK : kIndexEnd;
}


// ROM 0x002e8ef8 InvalidateState__15TUnionSoupIndexFv
void
TUnionSoupIndex::InvalidateState(void)
{
	for (long i = fNumSoups - 1; i >= 0; i--)
		fData[i].fState = kUnionStateInvalid;
}


// ROM 0x002e8f24 IsValidState__15TUnionSoupIndexFP4SKeyT1
// The states still stand: no soup's node cache has changed since, and
// the current soup is positioned on key/data.  The valid soups' nodes
// are marked in use again.
Boolean
TUnionSoupIndex::IsValidState(SKey* key, SKey* data)
{
	for (long i = fNumSoups - 1; i >= 0; i--)
	{
		UnionIndexData* d = &fData[i];
		TSoupIndex* index = d->fIndex;
		if (index->fNodeCache->fModCount != d->fModCount)
			return false;
		if (fCurrentSoup == i)
		{
			if (d->fState != kUnionStateValid)
				return false;
			void* fieldData = index->kfFirstDataAdr(d->fKeyField);
			long dataSize = index->kfSizeOfData(data);
			if (dataSize != index->kfSizeOfData(fieldData) || memcmp(fieldData, data, dataSize) != 0)
				return false;
			long keySize = index->kfSizeOfKey(key);
			if (keySize != index->kfSizeOfKey(KeyFieldKey(d->fKeyField)) || memcmp(KeyFieldKey(d->fKeyField), key, keySize) != 0)
				return false;
		}
		if (d->fState == kUnionStateValid)
			index->fNodeCache->Reuse(index);
	}
	return true;
}


// ROM 0x002e903c Search__15TUnionSoupIndexFUcP4SKeyT2PFP4SKeyT1Pv_iPvN22i
// From key/data in the current soup, each entry in turn (in order across
// the soups) to stop until it says so; the soups' states are kept
// between calls while they stay valid.  ==> kIndexOK when stopped,
// kIndexEnd when the soups run out, kIndexNotFound when key is not there.
int
TUnionSoupIndex::Search(Boolean forward, SKey* key, SKey* data, IndexStopProcPtr stop, void* refCon, SKey* outKey, SKey* outData, int mode)
{
	if (fDirection != forward || !IsValidState(key, data))
	{
		for (long i = fNumSoups - 1; i >= 0; i--)
		{
			fData[i].fState = kUnionStateInvalid;
			fData[i].fModCount = fData[i].fIndex->fNodeCache->fModCount;
		}
	}
	fDirection = forward;
	UnionIndexData* d = &fData[fCurrentSoup];
	TSoupIndex* index = d->fIndex;
	index->kfAssembleKeyField(d->fKeyField, key, data);
	volatile int result = kIndexOK;
	newton_try
	{
		for ( ; ; )
		{
			int r = d->fState == kUnionStateValid
				? index->MoveUsingState(forward, mode, d->fKeyField, &d->fIndexState)
				: index->MoveAndGetState(forward, mode, d->fKeyField, &d->fIndexState);
			Boolean moveOn;
			if (r == kIndexOK)
			{
				d->fState = kUnionStateValid;
				moveOn = fNumSoups != 1 && index->CompareKeys(*key, *KeyFieldKey(d->fKeyField)) != 0;
			}
			else if (r == kIndexEnd)
			{
				d->fState = kUnionStateExhausted;
				moveOn = true;
			}
			else
			{
				d->fState = kUnionStateInvalid;
				result = r;
				break;
			}
			if (moveOn)
			{
				r = MoveToNextSoup(forward, mode, key, false);
				if (r != kIndexOK)
				{
					result = r;
					break;
				}
				d = &fData[fCurrentSoup];
				index = d->fIndex;
			}
			result = kIndexOK;
			if (stop == nil)
				break;
			if (stop(KeyFieldKey(d->fKeyField), (SKey*) index->kfFirstDataAdr(d->fKeyField), refCon))
				break;
			if (index->fNodeCache->fNumEntries > 32)
			{
				index->fNodeCache->Commit(index);
				d->fState = kUnionStateInvalid;
			}
		}
	}
	newton_catch_all
	{
		Commit();
		rethrow;
	}
	end_try;
	Commit();
	if (result == kIndexOK)
		index->kfDisassembleKeyField(d->fKeyField, outKey, outData);
	return result;
}


// ROM 0x002e927c CurrentSoupGone__15TUnionSoupIndexFP4SKeyN21
// The current soup has gone: the soup with the next key after key takes
// its place (outKey/outData its entry); none: the first soup is current.
// ==> kIndexOK, kIndexEnd, or an exception's error.
int
TUnionSoupIndex::CurrentSoupGone(SKey* key, SKey* outKey, SKey* outData)
{
	volatile int result;
	newton_try
	{
		result = MoveToNextSoup(true, kIndexNextDupOrKey, key, true);
		if (result == kIndexOK)
		{
			UnionIndexData* d = &fData[fCurrentSoup];
			d->fIndex->kfDisassembleKeyField(d->fKeyField, outKey, outData);
		}
		else
			SetCurrentSoup(0);
	}
	newton_catch_all
	{
		result = (int) (Long) _info.exception.data;
	}
	end_try;
	Commit();
	return result;
}


// ROM 0x002e941c Commit__15TUnionSoupIndexFv
void
TUnionSoupIndex::Commit(void)
{
	for (long i = fNumSoups - 1; i >= 0; i--)
	{
		TSoupIndex* index = fData[i].fIndex;
		index->fNodeCache->Commit(index);
	}
}


// ROM 0x002e9458 SetCurrentSoup__15TUnionSoupIndexFl
void
TUnionSoupIndex::SetCurrentSoup(long index)
{
	fCurrentSoup = index;
	InvalidateState();
}


/*------------------------------------------------------------------------------
	T C u r s o r
------------------------------------------------------------------------------*/

// ROM 0x002cdc44 __ct__14CursorSoupInfoFv
CursorSoupInfo::CursorSoupInfo()
{
	fSoup = NILREF;
	fTagsBits = NILREF;
}


// ROM 0x002cffc0 __ct__7TCursorFv
TCursor::TCursor()
{
	fParkedAtEnd = false;
	fEntryRemoved = false;
	fIndex = nil;
	fTagsIndexes = nil;
	fSoupInfo = nil;
	fNumSoups = 0;
	fWordsHints = nil;
	fBeginKeyData = nil;
	fEndKeyData = nil;
	fMissingIndex = 0;
	fFlags = 0;
	memset(&fEntryData, 0, sizeof(fEntryData));
	fSoup = NILREF;
	fCursor = NILREF;
	fEntry = NILREF;
	fTagSpec = NILREF;
	fIndexPath = NILREF;
	fIndexType = NILREF;
	fWords = NILREF;
	fText = NILREF;
	fStartKey = NILREF;
	fBeginKey = NILREF;
	fEndKey = NILREF;
	fIndexValidTest = NILREF;
	fValidTest = NILREF;
	fEndTest = NILREF;
	fTestArgs = NILREF;
	fSecOrder = false;
	memset(&fKey, 0, sizeof(fKey));
}


// ROM 0x002d161c __dt__7TCursorFv
TCursor::~TCursor()
{
	Invalidate();
	if (fWordsHints != nil)
		delete[] (char*) fWordsHints;
	if (fBeginKeyData != nil)
		delete fBeginKeyData;
	if (fEndKeyData != nil)
		delete fEndKeyData;
}


// ROM 0x002cdc80 Invalidate__7TCursorFv
// The indexes and soup info let go; no soups, no entry.
void
TCursor::Invalidate(void)
{
	if (fIndex != nil)
	{
		delete fIndex;
		fIndex = nil;
	}
	if (fTagsIndexes != nil)
	{
		delete[] fTagsIndexes;
		fTagsIndexes = nil;
	}
	if (fSoupInfo != nil)
	{
		delete[] fSoupInfo;
		fSoupInfo = nil;
	}
	fEntry = NILREF;
	fParkedAtEnd = false;
	fNumSoups = 0;
}


// The C-object binary's hooks (ROM 0x002a8eec GCMarkCursor__FPv,
// 0x002a9e18 GCUpdateCursor__FPv, 0x002aa5c8 GCDeleteCursor__FPv).
static void
GCMarkCursor(void* cursor)
{
	((TCursor*) cursor)->GCMark();
}

static void
GCUpdateCursor(void* cursor)
{
	((TCursor*) cursor)->GCUpdate();
}

static void
GCDeleteCursor(void* cursor)
{
	((TCursor*) cursor)->~TCursor();
}


// ROM 0x002cf9d0 CreateNewCursor__7TCursorSFv
// A cursor frame: cursorPrototype with a TCursor in a C-object binary.
Ref
TCursor::CreateNewCursor(void)
{
	RefVar object(AllocateFramesCObject(sizeof(TCursor), GCDeleteCursor, GCMarkCursor, GCUpdateCursor));
	TCursor* cursor = (TCursor*) BinaryData(object);
	if (cursor != nil)
		new (cursor) TCursor;
	RefVar frame(::Clone(Rcursorprototype));
	SetFrameSlot(frame, RSSYMtcursor, object);
	return frame;
}


// ROM 0x002d02c4 CursorObj__FRC6RefVar
TCursor*
CursorObj(RefArg cursor)
{
	Ref object = GetFrameSlotRef(cursor, RSSYMtcursor);
	return (TCursor*) BinaryData(object);
}


// ROM 0x002d02f4 GCMark__7TCursorFv
void
TCursor::GCMark(void)
{
	DIYGCMark(fSoup);
	DIYGCMark(fCursor);
	DIYGCMark(fEntry);
	DIYGCMark(fTagSpec);
	DIYGCMark(fIndexPath);
	DIYGCMark(fIndexType);
	DIYGCMark(fWords);
	DIYGCMark(fText);
	DIYGCMark(fStartKey);
	if (fFlags & kQueryKeyBounds)
	{
		DIYGCMark(fBeginKey);
		DIYGCMark(fEndKey);
	}
	if (fFlags & kQueryEntryTests)
	{
		DIYGCMark(fIndexValidTest);
		DIYGCMark(fValidTest);
		DIYGCMark(fEndTest);
		DIYGCMark(fTestArgs);
	}
	if (fSoupInfo != nil)
		for (long i = 0; i < fNumSoups; i++)
		{
			DIYGCMark(fSoupInfo[i].fSoup);
			DIYGCMark(fSoupInfo[i].fTagsBits);
		}
}


// ROM 0x002d03d4 GCUpdate__7TCursorFv
void
TCursor::GCUpdate(void)
{
	fSoup = DIYGCUpdate(fSoup);
	fCursor = DIYGCUpdate(fCursor);
	fEntry = DIYGCUpdate(fEntry);
	fTagSpec = DIYGCUpdate(fTagSpec);
	fIndexPath = DIYGCUpdate(fIndexPath);
	fIndexType = DIYGCUpdate(fIndexType);
	fWords = DIYGCUpdate(fWords);
	fText = DIYGCUpdate(fText);
	fStartKey = DIYGCUpdate(fStartKey);
	if (fFlags & kQueryKeyBounds)
	{
		fBeginKey = DIYGCUpdate(fBeginKey);
		fEndKey = DIYGCUpdate(fEndKey);
	}
	if (fFlags & kQueryEntryTests)
	{
		fIndexValidTest = DIYGCUpdate(fIndexValidTest);
		fValidTest = DIYGCUpdate(fValidTest);
		fEndTest = DIYGCUpdate(fEndTest);
		fTestArgs = DIYGCUpdate(fTestArgs);
	}
	if (fSoupInfo != nil)
		for (long i = 0; i < fNumSoups; i++)
		{
			fSoupInfo[i].fSoup = DIYGCUpdate(fSoupInfo[i].fSoup);
			fSoupInfo[i].fTagsBits = DIYGCUpdate(fSoupInfo[i].fTagsBits);
		}
}


// ROM 0x002cdf70 CloneFrameSlot__7TCursorCFRC6RefVarT1
// A total clone of a slot's value; nil for none.
Ref
TCursor::CloneFrameSlot(RefArg frame, RefArg tag) const
{
	RefVar value(GetFrameSlotRef(frame, tag));
	if ((Ref) value == NILREF)
		return NILREF;
	return TotalClone(value);
}


// ROM 0x002d05a4 Init__7TCursorFRC6RefVarN21
// The cursor over soup for the query spec: the spec's parts taken (the
// keys total-cloned, the tags/words/text noted), the soups' info and
// indexes built, the cursor registered with the soup(s).
void
TCursor::Init(RefArg cursor, RefArg soup, RefArg querySpec)
{
	fFlags = 0;
	fEntry = NILREF;
	fParkedAtEnd = false;
	fEntryRemoved = false;
	fSoup = soup;
	fCursor = cursor;
	if ((Ref) querySpec == NILREF)
		fIndexPath = RSSYM_uniqueid;
	else
	{
		RefVar indexPath(GetFrameSlotRef(querySpec, RSSYMindexpath));
		fIndexPath = EnsureInternal(indexPath);
		if (fIndexPath == NILREF)
			fIndexPath = RSSYM_uniqueid;
		fWords = GetFrameSlotRef(querySpec, RSSYMwords);
		if (fWords != NILREF)
		{
			fFlags |= kQueryWords;
			if (GetFrameSlotRef(querySpec, RSSYMentirewords) != NILREF)
				fFlags |= kQueryEntireWords;
			if (!IsArray(RefVar(fWords)))
			{
				RefVar word(fWords);
				fWords = AllocateArray(RSSYMarray, 1);
				SetArraySlotRef(fWords, 0, word);
			}
			fWordsHints = GetWordsHints(RefVar(fWords));
		}
		fText = GetFrameSlotRef(querySpec, RSSYMtext);
		if (fText != NILREF)
			fFlags |= kQueryText;
		fTagSpec = CloneFrameSlot(querySpec, RSSYMtagspec);
		if (fTagSpec != NILREF)
			fFlags |= kQueryTags;
		fSecOrder = GetFrameSlotRef(querySpec, RSSYMsecorder) != NILREF;
		fBeginKey = CloneFrameSlot(querySpec, RSSYMbeginkey);
		if (fBeginKey != NILREF)
			fFlags |= kQueryBeginKey;
		else
		{
			fBeginKey = CloneFrameSlot(querySpec, RSSYMbeginexclkey);
			if (fBeginKey != NILREF)
				fFlags |= kQueryBeginExclKey;
		}
		fEndKey = CloneFrameSlot(querySpec, RSSYMendkey);
		if (fEndKey != NILREF)
			fFlags |= kQueryEndKey;
		else
		{
			fEndKey = CloneFrameSlot(querySpec, RSSYMendexclkey);
			if (fEndKey != NILREF)
				fFlags |= kQueryEndExclKey;
		}
		fStartKey = CloneFrameSlot(querySpec, RSSYMstartkey);
		fIndexValidTest = GetFrameSlotRef(querySpec, RSSYMindexvalidtest);
		if (fIndexValidTest != NILREF)
			fFlags |= kQueryIndexValidTest;
		fValidTest = GetFrameSlotRef(querySpec, RSSYMvalidtest);
		if (fValidTest != NILREF)
			fFlags |= kQueryValidTest;
		fEndTest = GetFrameSlotRef(querySpec, RSSYMendtest);
		if (fEndTest != NILREF)
			fFlags |= kQueryEndTest;
		if (fFlags & kQueryEntryTests)
			fTestArgs = AllocateArray(RSSYMarray, 1);
	}
	BuildSoupsInfo();
	CreateIndexes();
	RegisterInSoup(soup);
}


// ROM 0x002d0ba8 Init__7TCursorFRC6RefVarPC7TCursor
// A clone of another cursor: its query, position and state, over its
// own soup info and indexes.
void
TCursor::Init(RefArg cursor, const TCursor* other)
{
	newton_try
	{
		fSoup = other->fSoup;
		fCursor = other->fCursor;
		fFlags = other->fFlags;
		fNumSoups = other->fNumSoups;
		fSoupInfo = nil;
		fIndex = nil;
		fTagSpec = other->fTagSpec;
		fTagsIndexes = nil;
		fIndexPath = other->fIndexPath;
		fIndexType = other->fIndexType;
		fSecOrder = other->fSecOrder;
		fWords = other->fWords;
		fWordsHints = other->fWordsHints;
		fText = other->fText;
		fIndexValidTest = other->fIndexValidTest;
		fValidTest = other->fValidTest;
		fEndTest = other->fEndTest;
		fTestArgs = other->fTestArgs;
		fStartKey = other->fStartKey;
		fBeginKey = other->fBeginKey;
		fEndKey = other->fEndKey;
		fBeginKeyData = other->fBeginKeyData;
		fEndKeyData = other->fEndKeyData;
		fMissingIndex = other->fMissingIndex;
		fEntryData = other->fEntryData;
		fEntry = other->fEntry;
		fKey = other->fKey;
		fParkedAtEnd = other->fParkedAtEnd;
		fEntryRemoved = other->fEntryRemoved;
		fCursor = cursor;
		if (fWordsHints != nil)
			fWordsHints = GetWordsHints(RefVar(fWords));
		if (fBeginKeyData != nil)
		{
			SKey* copy = new SKey;
			*copy = *other->fBeginKeyData;
			fBeginKeyData = copy;
		}
		if (fEndKeyData != nil)
		{
			SKey* copy = new SKey;
			*copy = *other->fEndKeyData;
			fEndKeyData = copy;
		}
		BuildSoupsInfo();
		CreateIndexes();
		if (fIndex != nil)
			fIndex->SetCurrentSoup(other->fIndex != nil ? other->fIndex->fCurrentSoup : 0);
		RegisterInSoup(RefVar(fSoup));
	}
	newton_catch_all
	{
		Invalidate();
		rethrow;
	}
	end_try;
}


// ROM 0x002cdfc4 BuildSoupsInfo__7TCursorFv
// The soups queried (a union soup's soupList, else the soup itself) with
// what the query needs of each: the index on the path must be there (the
// first soup's index type is the cursor's; a tags index cannot be
// queried); the begin and end keys become SKeys (an exclusive begin key
// or inclusive end key of a multiSlot index sorts after shorter keys);
// the tags query is encoded against each soup's tags.
void
TCursor::BuildSoupsInfo(void)
{
	RefVar soupList(GetFrameSlotRef(fSoup, RSSYMsouplist));
	if ((Ref) soupList == NILREF)
		fNumSoups = 1;
	else
	{
		fNumSoups = Length(soupList);
		if (fNumSoups == 0)
			return;
	}
	newton_try
	{
		fSoupInfo = new CursorSoupInfo[fNumSoups];
		if (fSoupInfo == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		RefVar persistent;
		RefVar indexDesc;
		RefVar tagsDesc;
		for (long i = 0; i < fNumSoups; i++)
		{
			CursorSoupInfo* info = &fSoupInfo[i];
			info->fSoup = (Ref) soupList == NILREF ? fSoup : GetArraySlotRef(soupList, i);
			persistent = GetFrameSlotRef(info->fSoup, RSSYM_proto);
			if ((Ref) persistent == NILREF)
				Throw(exStoreError, (void*) kNSErrSoupRemoved, nil);
			indexDesc = IndexPathToIndexDesc(persistent, RefVar(fIndexPath), nil);
			if ((Ref) indexDesc == NILREF)
				Throw(exStoreError, (void*) kNSErrIndexNotFound, nil);
			if (i == 0)
			{
				fIndexType = GetFrameSlotRef(indexDesc, RSSYMtype);
				if (EQRef(RefVar(fIndexType), RSSYMtags))
					Throw(exStoreError, (void*) kNSErrCantQueryTagsIndex, nil);
				if (fFlags & kQueryKeyBounds)
				{
					Boolean isMulti = EQRef(GetFrameSlotRef(indexDesc, RSSYMstructure), RSSYMmultislot);
					SKey key;
					memset(&key, 0, sizeof(key));
					short size;
					if ((fFlags & (kQueryBeginKey | kQueryBeginExclKey)) && fBeginKeyData == nil)
					{
						KeyToSKey(RefVar(fBeginKey), RefVar(fIndexType), &key, &size, nil);
						if (isMulti && (fFlags & kQueryBeginExclKey))
							key.SetFlags((unsigned char) (key.Flags() | 0x80));
						fBeginKeyData = new SKey;
						if (fBeginKeyData == nil)
							Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
						*fBeginKeyData = key;
						fBeginKey = NILREF;
					}
					if ((fFlags & (kQueryEndKey | kQueryEndExclKey)) && fEndKeyData == nil)
					{
						KeyToSKey(RefVar(fEndKey), RefVar(fIndexType), &key, &size, nil);
						if (isMulti && (fFlags & kQueryEndKey))
							key.SetFlags((unsigned char) (key.Flags() | 0x80));
						fEndKeyData = new SKey;
						if (fEndKeyData == nil)
							Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
						*fEndKeyData = key;
						fEndKey = NILREF;
					}
				}
			}
			if (fTagSpec != NILREF)
			{
				tagsDesc = GetTagsIndexDesc(persistent);
				if ((Ref) tagsDesc == NILREF)
					Throw(exStoreError, (void*) kNSErrNoTagsIndex, nil);
				fSoupInfo[i].fTagsBits = EncodeQueryTags(tagsDesc, RefVar(fTagSpec));
			}
		}
	}
	newton_catch_all
	{
		Invalidate();
		rethrow;
	}
	end_try;
}


// ROM 0x002ce4c4 CreateIndexes__7TCursorFv
// The union index over each soup's index on the path (and the tags
// indexes when the query has tags).
void
TCursor::CreateIndexes(void)
{
	if (fNumSoups == 0)
		return;
	newton_try
	{
		UnionIndexData* data = new UnionIndexData[fNumSoups];
		if (data == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		if (fTagSpec == NILREF)
			fTagsIndexes = nil;
		else
		{
			fTagsIndexes = new TSoupIndex*[fNumSoups];
			if (fTagsIndexes == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		}
		RefVar persistent;
		RefVar indexDesc;
		RefVar soup;
		for (long i = 0; i < fNumSoups; i++)
		{
			soup = fSoupInfo[i].fSoup;
			persistent = GetFrameSlotRef(soup, RSSYM_proto);
			indexDesc = IndexPathToIndexDesc(persistent, RefVar(fIndexPath), nil);
			data[i].fIndex = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
			if (fTagsIndexes != nil)
			{
				indexDesc = GetTagsIndexDesc(persistent);
				fTagsIndexes[i] = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
			}
		}
		fIndex = new TUnionSoupIndex(fNumSoups, data);
		if (fIndex == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	newton_catch_all
	{
		Invalidate();
		rethrow;
	}
	end_try;
}


// ROM 0x002d0060 RegisterInSoup__7TCursorCFRC6RefVar
// The cursor frame put in the soup's cursors cache (and each of a union
// soup's soups').
void
TCursor::RegisterInSoup(RefArg soup) const
{
	RefVar cursor(fCursor);
	RefVar cursors(GetFrameSlotRef(soup, RSSYMcursors));
	PutEntryIntoCache(cursors, cursor);
	RefVar soupList(GetFrameSlotRef(soup, RSSYMsouplist));
	if ((Ref) soupList != NILREF)
		for (long i = Length(soupList) - 1; i >= 0; i--)
		{
			cursors = GetFrameSlotRef(GetArraySlotRef(soupList, i), RSSYMcursors);
			PutEntryIntoCache(cursors, cursor);
		}
}


// ROM 0x002d016c UnregisterFromSoup__7TCursorCFRC6RefVar
void
TCursor::UnregisterFromSoup(RefArg soup) const
{
	RefVar cursor(fCursor);
	RefVar soupList(GetFrameSlotRef(soup, RSSYMsouplist));
	RefVar cursors;
	if ((Ref) soupList == NILREF)
	{
		cursors = GetFrameSlotRef(soup, RSSYMcursors);
		DeleteEntryFromCache(cursors, cursor);
	}
	else
		for (long i = Length(soupList) - 1; i >= 0; i--)
		{
			cursors = GetFrameSlotRef(GetArraySlotRef(soupList, i), RSSYMcursors);
			DeleteEntryFromCache(cursors, cursor);
		}
}


// ROM 0x002ce73c ExitParking__7TCursorFUc
// From a parked cursor to the first entry in the direction: nothing
// when parked at that end; else the begin key (or the first key), or
// backwards the end key (or the last).  ==> the index result.
int
TCursor::ExitParking(Boolean forward)
{
	if (fParkedAtEnd == forward)
		return kIndexEnd;
	if (forward)
	{
		if ((fFlags & (kQueryBeginKey | kQueryBeginExclKey)) == 0)
			return fIndex->First(&fKey, &fEntryData);
		int r = fIndex->Find(fBeginKeyData, &fKey, &fEntryData, fSecOrder);
		if (r == kIndexNotFound)
			return kIndexOK;
		if (r != kIndexOK)
			return r;
		if (fFlags & kQueryBeginExclKey)
			return fIndex->Next(&fKey, &fEntryData, kIndexNextKey, &fKey, &fEntryData);
		return kIndexOK;
	}
	if ((fFlags & (kQueryEndKey | kQueryEndExclKey)) == 0)
		return fIndex->Last(&fKey, &fEntryData);
	int r = fIndex->FindPrior(fEndKeyData, &fKey, &fEntryData, fSecOrder, (fFlags & kQueryEndExclKey) != 0);
	if (r != kIndexNotFound)
		return r;
	return kIndexOK;
}


// ROM 0x002ce86c KeyBoundsValidTest__7TCursorFRC4SKeyUc
// Whether the key is within the begin key (atEnd false) or end key.
Boolean
TCursor::KeyBoundsValidTest(const SKey& key, Boolean atEnd)
{
	TSoupIndex* index = fIndex->fData[fIndex->fCurrentSoup].fIndex;
	if (!atEnd)
	{
		if ((fFlags & (kQueryBeginKey | kQueryBeginExclKey)) == 0)
			return true;
		int cmp = index->CompareKeys(key, *fBeginKeyData);
		if (cmp < 0)
			return false;
		if (cmp != 0)
			return true;
		return (fFlags & kQueryBeginExclKey) == 0;
	}
	if ((fFlags & (kQueryEndKey | kQueryEndExclKey)) == 0)
		return true;
	int cmp = index->CompareKeys(key, *fEndKeyData);
	if (cmp > 0)
		return false;
	if (cmp != 0)
		return true;
	return (fFlags & kQueryEndExclKey) == 0;
}


// ROM 0x0c105358 (unnamed) - gObjTextDecompressor
// The decompressor a search reads entries' text through (the text cache),
// made when the first entry is read, kept for the rest of the walk and
// let go by ReleasePermObjectTextCache when the walk is over (Move,
// CountEntries, Collect).
static TObjTextDecompressor*	gObjTextDecompressor = nil;


// What a words query hands its text callback: the array of words to look
// for, and whether a word may be matched at the start of a longer one.
struct WordsTestArgs
{
	RefStruct*	fWords;
	ULong		fEntireWords;
};


// ROM 0x002ce918 WordsValidTestTextProc__FPUslPv
// A words query answered over one run of an entry's text: every word of
// the query has to be somewhere in it.  The words are taken from the end
// backwards, and the walk stops at the first that is not there.
//
// A word of the query matches the start of a word of the text.  With
// kQueryEntireWords it has to match the whole of one, so the search
// carries on past each partial match to the next word start and gives up
// when there are no more.
static Boolean
WordsValidTestTextProc(UniChar* text, long length, void* refCon)
{
	WordsTestArgs* args = (WordsTestArgs*) refCon;
	RefVar words(*args->fWords);
	long at = Length(words) - 1;
	while (at >= 0)
	{
		RefVar one(GetArraySlotRef(words, at));
		const UniChar* needle = CString(one);
		const UniChar* found = FindWord(text, length, needle, true);
		if (found == nil)
			break;
		if (args->fEntireWords != 0)
		{
			const UniChar* after;
			for (;;)
			{
				// (the ROM reads the character after the match, which at
				//  the very end of the text is one past it)
				after = found + Ustrlen(needle);
				if (IsDelimiter(*after))
					break;					// the match is a whole word
				found = FindWord(after, length - (after - text), needle, false);
				if (found == nil)
					return false;
			}
			// (the ROM tests `after` for nil here, which it never is)
		}
		at--;
	}
	return at < 0;
}


// ROM 0x002ceaf0 TextValidTestTextProc__FPUslPv
// A text query: the string anywhere in the text at all.
static Boolean
TextValidTestTextProc(UniChar* text, long length, void* refCon)
{
	return FindString(text, length, (const UniChar*) refCon) != nil;
}


// ROM 0x002cea1c WordsValidTest__7TCursorFUl
// Whether the entry's text has all of the query's words in it.  The hint
// chunks it carries are tested first, so that most entries are refused
// without their text being read at all.
Boolean
TCursor::WordsValidTest(PSSId id)
{
	TStoreWrapper* wrapper = (TStoreWrapper*)
		GetFrameSlotRef(RefVar(fSoupInfo[fIndex->fCurrentSoup].fSoup), RSSYMtstore);
	long count = Length(RefVar(fWords));
	if (fWordsHints != nil && !TestObjHints((const char*) fWordsHints, count, wrapper, id))
		return false;

	RefStruct words(fWords);
	WordsTestArgs args;
	args.fWords = &words;
	args.fEntireWords = fFlags & kQueryEntireWords;
	return WithPermObjectTextDo(wrapper, id, WordsValidTestTextProc, &args, &gObjTextDecompressor);
}


// ROM 0x002ceb10 TextValidTest__7TCursorFUl
// Whether the entry's text has the query's string in it.
Boolean
TCursor::TextValidTest(PSSId id)
{
	TStoreWrapper* wrapper = (TStoreWrapper*)
		GetFrameSlotRef(RefVar(fSoupInfo[fIndex->fCurrentSoup].fSoup), RSSYMtstore);
	RefVar text(fText);
	return WithPermObjectTextDo(wrapper, id, TextValidTestTextProc,
								(void*) CString(text), &gObjTextDecompressor);
}


// ROM 0x002cebac ValidTest__7TCursorFRC4SKeyUlUcPUcT4
// Whether the entry at key/id passes the query: within the key bound in
// the direction (else outOfBounds: no further entry will), the tags,
// words and text, indexValidTest of the key, then - the entry
// made current (entryMade) - endTest (failing it: outOfBounds) and
// validTest of the entry.
Boolean
TCursor::ValidTest(const SKey& key, PSSId id, Boolean atEnd, Boolean* entryMade, Boolean* outOfBounds)
{
	*entryMade = false;
	*outOfBounds = false;
	if ((fFlags & 0x3ff) == 0)
		return true;
	if ((fFlags & kQueryKeyBounds) && !KeyBoundsValidTest(key, atEnd))
	{
		*outOfBounds = true;
		return false;
	}
	if (fTagsIndexes != nil)
	{
		long current = fIndex->fCurrentSoup;
		if (!TagsValidTest(*fTagsIndexes[current], RefVar(fSoupInfo[current].fTagsBits), id))
			return false;
	}
	if ((fFlags & kQueryWords) && !WordsValidTest(id))
		return false;
	if ((fFlags & kQueryText) && !TextValidTest(id))
		return false;
	if (fFlags & kQueryEntryTests)
	{
		RefVar args(fTestArgs);
		if (fFlags & kQueryIndexValidTest)
		{
			SetArraySlotRef(args, 0, SKeyToKey(key, RefVar(fIndexType), nil));
			if (DoBlock(RefVar(fIndexValidTest), args) == NILREF)
				return false;
		}
		if ((fFlags & (kQueryValidTest | kQueryEndTest)) == 0)
			return true;
		MakeEntryFaultBlock(id);
		*entryMade = true;
		SetArraySlotRef(args, 0, fEntry);
		if (fFlags & kQueryEndTest)
		{
			if (DoBlock(RefVar(fEndTest), args) != NILREF)
			{
				*outOfBounds = true;
				return false;
			}
		}
		if (fFlags & kQueryValidTest)
		{
			if (DoBlock(RefVar(fValidTest), args) == NILREF)
				return false;
		}
	}
	return true;
}


// ROM 0x002cee84 CursorStopFn__FP4SKeyT1Pv
// The stop function of Move's search: each valid entry counts one step;
// stop at the step wanted, or when the bounds are passed.
struct CursorMoveInfo
{
	TCursor*	fCursor;		// +0x00
	Boolean		fForward;		// +0x04
	Boolean		fEntryMade;		// +0x05
	long		fCount;			// +0x08
	long		fStep;			// +0x0c
};

static int
CursorStopFn(SKey* key, SKey* data, void* refCon)
{
	CursorMoveInfo* info = (CursorMoveInfo*) refCon;
	Boolean outOfBounds;
	if (info->fCursor->ValidTest(*key, (PSSId) (long) *data, info->fForward, &info->fEntryMade, &outOfBounds))
	{
		info->fCount += info->fStep;
		return info->fCount == 0;
	}
	return outOfBounds;
}


// ROM 0x002ceef0 Move__7TCursorFl
// The cursor moved count entries (back for a negative count; 0 re-tests
// the current one): out of parking first, then entry by entry counting
// the valid ones; parked at the end when the entries run out.  ==> the
// entry.
Ref
TCursor::Move(long count)
{
	if (fSoupInfo == nil)
		return NILREF;
	Boolean forward = count >= 0;
	Boolean wasParked = fEntry == NILREF;
	Boolean wasRemoved = fEntryRemoved;
	if (count != 0)
		fEntryRemoved = false;
	int r;
	if (wasParked)
		r = ExitParking(forward);
	else
	{
		if (count > 0 && wasRemoved)
			count--;						// the entry after the removed one is where we are
		if (count == 0)
			r = kIndexOK;
		else if (forward)
			r = fIndex->Next(&fKey, &fEntryData, kIndexNextDupOrKey, &fKey, &fEntryData);
		else
			r = fIndex->Prior(&fKey, &fEntryData, false, &fKey, &fEntryData);
	}
	Boolean arrived = false;
	CursorMoveInfo info;
	info.fEntryMade = false;
	if (r == kIndexOK)
	{
		long step = forward ? -1 : 1;
		if (count == 0)
			count = -step;
		Boolean outOfBounds;
		if (ValidTest(fKey, EntryId(), forward, &info.fEntryMade, &outOfBounds))
		{
			count += step;
			if (count == 0)
				arrived = true;
		}
		if (!arrived && !outOfBounds)
		{
			info.fCursor = this;
			info.fForward = forward;
			info.fCount = count;
			info.fStep = step;
			r = fIndex->Search(forward, &fKey, &fEntryData, CursorStopFn, &info, &fKey, &fEntryData, kIndexNextDupOrKey);
			arrived = r == kIndexOK && info.fCount == 0;
		}
	}
	if (arrived)
	{
		if (!info.fEntryMade)
			MakeEntryFaultBlock(EntryId());
	}
	else
		Park(forward);
	if (gObjTextDecompressor != nil)
	{
		ReleasePermObjectTextCache(gObjTextDecompressor);
		gObjTextDecompressor = nil;
	}
	return Entry();
}


// ROM 0x002cf0f0 Entry__7TCursorFv
// The current entry; 'deleted when it has been removed from its soup.
Ref
TCursor::Entry(void)
{
	return fEntryRemoved ? RSSYMdeleted : fEntry;
}


// ROM 0x002cf110 EntryKey__7TCursorFv
Ref
TCursor::EntryKey(void)
{
	if (fEntry == NILREF || fEntryRemoved)
		return NILREF;
	return SKeyToKey(fKey, RefVar(fIndexType), nil);
}


// ROM 0x002cf174 GetState__7TCursorFP11CursorState
void
TCursor::GetState(CursorState* state)
{
	state->fSoupIndex = fIndex->fCurrentSoup;
	state->fEntry = fEntry;
	if (fEntry != NILREF)
	{
		TSoupIndex* index = fIndex->fData[fIndex->fCurrentSoup].fIndex;
		memcpy(&state->fKey, &fKey, index->kfSizeOfKey(&fKey));
		state->fEntryData = fEntryData;
	}
	state->fParkedAtEnd = fParkedAtEnd;
	state->fEntryRemoved = fEntryRemoved;
}


// ROM 0x002cf200 SetState__7TCursorFR11CursorState
void
TCursor::SetState(CursorState& state)
{
	fIndex->SetCurrentSoup(state.fSoupIndex);
	fEntry = state.fEntry;
	if (state.fEntry != NILREF)
	{
		TSoupIndex* index = fIndex->fData[fIndex->fCurrentSoup].fIndex;
		memcpy(&fKey, &state.fKey, index->kfSizeOfKey(&state.fKey));
		fEntryData = state.fEntryData;
	}
	fParkedAtEnd = state.fParkedAtEnd;
	fEntryRemoved = state.fEntryRemoved;
}


// ROM 0x002cf28c MakeEntryFaultBlock__7TCursorFUl
// The current soup's entry for store object id becomes the entry.
void
TCursor::MakeEntryFaultBlock(PSSId id)
{
	RefVar soup(fSoupInfo[fIndex->fCurrentSoup].fSoup);
	fEntry = GetEntry(soup, id);
}


// ROM 0x002cf2d8 Park__7TCursorFUc
// No entry: parked before the first or after the last.
void
TCursor::Park(Boolean atEnd)
{
	fEntry = NILREF;
	fParkedAtEnd = atEnd;
	if (fIndex != nil)
		fIndex->SetCurrentSoup(0);
}


// ROM 0x002cf2f8 CountEntriesStopFn__FP4SKeyT1Pv
struct CursorCountInfo
{
	TCursor*	fCursor;
	long		fCount;
};

static int
CountEntriesStopFn(SKey* key, SKey* data, void* refCon)
{
	CursorCountInfo* info = (CursorCountInfo*) refCon;
	Boolean entryMade;
	Boolean outOfBounds;
	if (info->fCursor->ValidTest(*key, (PSSId) (long) *data, true, &entryMade, &outOfBounds))
		info->fCount++;
	return outOfBounds;
}


// ROM 0x002cf360 CountEntries__7TCursorFv
// The valid entries from the start, the cursor's position kept.
long
TCursor::CountEntries(void)
{
	if (fSoupInfo == nil)
		return 0;
	CursorState state;
	memset(&state, 0, sizeof(state));
	GetState(&state);
	volatile long count = 0;
	newton_try
	{
		if (Reset() != NILREF)
		{
			CursorCountInfo info;
			info.fCursor = this;
			info.fCount = 1;
			fIndex->Search(true, &fKey, &fEntryData, CountEntriesStopFn, &info, nil, nil, kIndexNextDupOrKey);
			count = info.fCount;
			if (gObjTextDecompressor != nil)
			{
				ReleasePermObjectTextCache(gObjTextDecompressor);
				gObjTextDecompressor = nil;
			}
		}
	}
	newton_catch_all
	{
		SetState(state);
		rethrow;
	}
	end_try;
	SetState(state);
	return count;
}


// ROM 0x002cf484 RebuildInfo__7TCursorFUcl
// The soup info (unless kept) and the indexes rebuilt after the soups or
// their indexes changed; removedSoup (-1: none) is the union soup index
// that went - the position follows the current soup, and when that is
// the one gone the next soup's entry becomes current.
void
TCursor::RebuildInfo(Boolean keepSoupInfo, long removedSoup)
{
	if (!keepSoupInfo)
	{
		if (fSoupInfo != nil)
		{
			delete[] fSoupInfo;
			fSoupInfo = nil;
		}
		BuildSoupsInfo();
	}
	long current = 0;
	if (fIndex != nil)
	{
		current = fIndex->fCurrentSoup;
		delete fIndex;
		fIndex = nil;
	}
	Boolean currentGone = false;
	if (removedSoup >= 0)
	{
		if (fEntry == NILREF)
			current = 0;
		else if (removedSoup == current)
			currentGone = true;
		else if (removedSoup < current)
			current--;
	}
	if (fTagsIndexes != nil)
	{
		delete[] fTagsIndexes;
		fTagsIndexes = nil;
	}
	CreateIndexes();
	if (fIndex != nil)
		fIndex->SetCurrentSoup(current);
	if (!currentGone)
		return;
	if (fIndex->CurrentSoupGone(&fKey, &fKey, &fEntryData) == kIndexOK)
		Move(0);
	else
		Park(false);
}


// ROM 0x002cf578 GetSoupInfoIndex__7TCursorFRC6RefVar
long
TCursor::GetSoupInfoIndex(RefArg soup)
{
	for (long i = fNumSoups - 1; i >= 0; i--)
		if (EQRef(fSoupInfo[i].fSoup, soup))
			return i;
	return -1;
}


// ROM 0x002cf5c8 SoupRemoved__7TCursorFRC6RefVar
// A soup went: the cursor's own (or a union soup's only one) invalidates
// it; one of a union's soups has the info rebuilt; another soup that
// cached this cursor drops it.
void
TCursor::SoupRemoved(RefArg soup)
{
	if (!EQRef(fSoup, soup))
	{
		long i = GetSoupInfoIndex(soup);
		if (i < 0)
		{
			UnregisterFromSoup(soup);
			return;
		}
		if (fNumSoups != 1)
		{
			RebuildInfo(false, i);
			return;
		}
	}
	Invalidate();
}


// ROM 0x002cf64c SoupAdded__7TCursorFRC6RefVar
// A soup joined the union: the info rebuilt when it has the index (and
// tags index) the query needs; otherwise the cursor is invalid, missing
// an index.
void
TCursor::SoupAdded(RefArg soup)
{
	if (fMissingIndex != 0)
		return;
	RefVar persistent(GetFrameSlotRef(soup, RSSYM_proto));
	if (IndexPathToIndexDesc(persistent, RefVar(fIndexPath), nil) != NILREF
	&& (fTagsIndexes == nil || GetTagsIndexDesc(persistent) != NILREF))
	{
		RebuildInfo(false, -1);
		return;
	}
	fMissingIndex = 1;
	Invalidate();
}


// ROM 0x002cf734 Status__7TCursorFv
Ref
TCursor::Status(void)
{
	return fMissingIndex == 0 ? RSSYMvalid : RSSYMmissingindex;
}


// ROM 0x002cf758 SetSoup__7TCursorFRC6RefVar
void
TCursor::SetSoup(RefArg soup)
{
	UnregisterFromSoup(RefVar(fSoup));
	fSoup = soup;
	RebuildInfo(false, -1);
}


// ROM 0x002cf81c IndexRemoved__7TCursorFRC6RefVarT1
// The index the query uses (or its tags index) went: invalid.
void
TCursor::IndexRemoved(RefArg /*soup*/, RefArg indexDesc)
{
	if (EQRef(GetFrameSlotRef(indexDesc, RSSYMtype), RSSYMtags))
	{
		if (fTagsIndexes == nil)
			return;
	}
	else
	{
		if (fSoupInfo == nil)
			return;
		RefVar path(GetFrameSlotRef(indexDesc, RSSYMpath));
		if (!IndexPathsEqual(RefVar(fIndexPath), path))
			return;
	}
	Invalidate();
}


// ROM 0x002cf908 IndexObjectsChanged__7TCursorFv
// The soup's TSoupIndex objects were remade: the indexes rebuilt.
void
TCursor::IndexObjectsChanged(void)
{
	if (fSoupInfo == nil)
		return;
	RebuildInfo(true, -1);
}


// ROM 0x002cf924 SoupTagsChanged__7TCursorFRC6RefVar
// The soup's tags changed: the query's tags re-encoded for it.
void
TCursor::SoupTagsChanged(RefArg soup)
{
	if (fTagSpec == NILREF)
		return;
	long i = GetSoupInfoIndex(soup);
	if (i < 0)
		return;
	RefVar tagsDesc(GetTagsIndexDesc(RefVar(GetFrameSlotRef(soup, RSSYM_proto))));
	fSoupInfo[i].fTagsBits = EncodeQueryTags(tagsDesc, RefVar(fTagSpec));
}


// ROM 0x002cfa60 PinCurrentKey__7TCursorFv
// The current key kept within the query's bounds: before the begin key
// the cursor resets, after the end key it resets to the end.  ==> whether
// it moved.
Boolean
TCursor::PinCurrentKey(void)
{
	if (!KeyBoundsValidTest(fKey, false))
	{
		Reset();
		return true;
	}
	if (!KeyBoundsValidTest(fKey, true))
	{
		ResetToEnd();
		return true;
	}
	return false;
}


// ROM 0x002cfac4 GotoKey__7TCursorFRC6RefVar
// The cursor to the entry with the key, or the one after it; parked at
// the end when there is none.  ==> the entry.
Ref
TCursor::GotoKey(RefArg key)
{
	if (fSoupInfo == nil)
		return NILREF;
	fEntryRemoved = false;
	KeyToSKey(key, RefVar(fIndexType), &fKey, nil, nil);
	int r = fIndex->Find(&fKey, &fKey, &fEntryData, fSecOrder);
	if (r == kIndexOK || r == kIndexNotFound)
	{
		if (!PinCurrentKey())
		{
			fEntry = TRUEREF;					// not parked: Move(0) tests the entry found
			Move(0);
		}
	}
	else
		Park(true);
	return fEntry;
}


// ROM 0x002cfb9c GotoEntry__7TCursorFRC6RefVar
// The cursor to the entry: an entry of one of the query's soups is
// looked up in that soup's index (and pinned within the bounds), another
// soup's found by its key.  ==> whether the cursor is now on it.
Ref
TCursor::GotoEntry(RefArg entry)
{
	if (!IsFaultBlock(entry))
		Throw(exStoreError, (void*) kNSErrNotASoupEntry, nil);
	if (fSoupInfo == nil)
		return NILREF;
	RefVar key(GetEntryKey(entry, RefVar(fIndexPath)));
	if ((Ref) key == NILREF)
		return NILREF;
	fEntryRemoved = false;
	RefVar soup(EntrySoup(entry));
	long i = GetSoupInfoIndex(soup);
	Boolean done = false;
	if (i >= 0)
	{
		KeyToSKey(key, RefVar(fIndexType), &fKey, nil, nil);
		fEntryData = (long) FaultBlockId(entry);
		int r = fIndex->fData[i].fIndex->Next(&fKey, &fEntryData, kIndexNextDupOrKey, nil, nil);
		if (r == kIndexOK || r == kIndexEnd)
		{
			fEntry = entry;
			fIndex->SetCurrentSoup(i);
			if (PinCurrentKey())
				return NILREF;
			Move(0);
			done = true;
		}
	}
	if (!done)
		GotoKey(key);
	return MAKEBOOLEAN(EQRef(entry, fEntry));
}


// ROM 0x002cfdb4 Reset__7TCursorFv
// To the start key when the query has one, else parked before the first
// and moved to it.
Ref
TCursor::Reset(void)
{
	if (fStartKey != NILREF)
		return GotoKey(RefVar(fStartKey));
	Park(false);
	return Move(1);
}


// ROM 0x002cfe24 ResetToEnd__7TCursorFv
Ref
TCursor::ResetToEnd(void)
{
	Park(true);
	return Move(-1);
}


// ROM 0x002cfe50 IsParked__7TCursorFv
// 'begin or 'end when parked; nil when on an entry.
Ref
TCursor::IsParked(void)
{
	if (fEntry != NILREF)
		return NILREF;
	return fParkedAtEnd ? RSSYMend : RSSYMbegin;
}


// ROM 0x002cfe84 EntryChanged__7TCursorFRC6RefVarUcT2
// The current entry was changed: its keys changed, the cursor finds it
// again; its tags changed, the entry is re-tested.
void
TCursor::EntryChanged(RefArg entry, Boolean keysChanged, Boolean tagsChanged)
{
	if (!EQRef(fEntry, entry))
		return;
	if (keysChanged)
	{
		GotoEntry(entry);
		return;
	}
	if (tagsChanged && fTagsIndexes != nil)
		Move(0);
}


// ROM 0x002cfef4 EntryReadded__7TCursorFRC6RefVarT1
// The current entry was added to another soup: this soup's fault block
// for it is the entry now.
void
TCursor::EntryReadded(RefArg entry, RefArg faultBlock)
{
	if (EQRef(fEntry, entry))
		fEntry = faultBlock;
}


// ROM 0x002cff2c EntryRemoved__7TCursorFRC6RefVar
// The current entry was removed: on to the next one, remembered as
// standing in for the removed one (Entry answers 'deleted).
void
TCursor::EntryRemoved(RefArg entry)
{
	if (!EQRef(fEntry, entry))
		return;
	fEntryRemoved = false;
	Move(1);
	fEntryRemoved = fEntry != NILREF;
}


// ROM 0x002cff80 EntrySoupChanged__7TCursorFRC6RefVarT1
void
TCursor::EntrySoupChanged(RefArg entry, RefArg newEntry)
{
	if (!EQRef(fEntry, entry))
		return;
	GotoEntry(newEntry);
}


// ROM 0x002d027c Clone__7TCursorFv
Ref
TCursor::Clone(void)
{
	RefVar cursor(CreateNewCursor());
	CursorObj(cursor)->Init(cursor, this);
	return cursor;
}


/*------------------------------------------------------------------------------
	T C o l l e c t C u r s o r
	The matching entries collected up front as [id, soup index] pairs.
------------------------------------------------------------------------------*/

// ROM 0x002d158c __ct__14TCollectCursorFv
TCollectCursor::TCollectCursor()
{
	fCurrent = 0;
	fEntries = NILREF;
}


// ROM 0x002d15dc __dt__14TCollectCursorFv
TCollectCursor::~TCollectCursor()
{ }


// ROM 0x002d14fc CreateNewCollectCursor__14TCollectCursorSFv
Ref
TCollectCursor::CreateNewCollectCursor(void)
{
	RefVar frame(::Clone(Rcursorprototype));
	RefVar object(AllocateFramesCObject(sizeof(TCollectCursor), GCDeleteCursor, GCMarkCursor, GCUpdateCursor));
	TCollectCursor* cursor = (TCollectCursor*) BinaryData(object);
	if (cursor != nil)
		new (cursor) TCollectCursor;
	SetFrameSlot(frame, RSSYMtcursor, object);
	return frame;
}


// ROM 0x002cdd0c Invalidate__14TCollectCursorFv
void
TCollectCursor::Invalidate(void)
{
	TCursor::Invalidate();
	fEntries = AllocateArray(RSSYMarray, 0);
}


// ROM 0x002cdd38 GCMark__14TCollectCursorFv
void
TCollectCursor::GCMark(void)
{
	TCursor::GCMark();
	DIYGCMark(fEntries);
}


// ROM 0x002cdd58 GCUpdate__14TCollectCursorFv
void
TCollectCursor::GCUpdate(void)
{
	TCursor::GCUpdate();
	fEntries = DIYGCUpdate(fEntries);
}


// ROM 0x002d142c CollectStopFn__FP4SKeyT1Pv
struct CollectInfo
{
	TCursor*	fCursor;
	RefVar		fEntries;
	long		fCount;
};

static int
CollectStopFn(SKey* key, SKey* data, void* refCon)
{
	CollectInfo* info = (CollectInfo*) refCon;
	Boolean entryMade;
	Boolean outOfBounds;
	if (info->fCursor->ValidTest(*key, (PSSId) (long) *data, true, &entryMade, &outOfBounds))
	{
		long slot = info->fCount * 2;
		if (Length(info->fEntries) == slot)
			SetLength(info->fEntries, slot + 0x40);
		SetArraySlotRef(info->fEntries, slot, MAKEINT((long) *data));
		SetArraySlotRef(info->fEntries, slot + 1, MAKEINT(info->fCursor->fIndex->fCurrentSoup));
		info->fCount++;
	}
	return outOfBounds;
}


// ROM 0x002d167c Collect__14TCollectCursorFv
// Every valid entry from the start collected; the cursor left on the
// first.
void
TCollectCursor::Collect(void)
{
	Park(false);
	TCursor::Move(1);
	if (fEntry == NILREF)
		fEntries = AllocateArray(RSSYMarray, 0);
	else
	{
		long current = fIndex->fCurrentSoup;
		fEntries = AllocateArray(RSSYMarray, 0x40);
		SetArraySlotRef(fEntries, 0, MAKEINT(EntryId()));
		SetArraySlotRef(fEntries, 1, MAKEINT(current));
		CollectInfo info;
		info.fCursor = this;
		info.fEntries = fEntries;
		info.fCount = 1;
		fIndex->Search(true, &fKey, &fEntryData, CollectStopFn, &info, nil, nil, kIndexNextDupOrKey);
		if (gObjTextDecompressor != nil)
		{
			ReleasePermObjectTextCache(gObjTextDecompressor);
			gObjTextDecompressor = nil;
		}
		SetLength(RefVar(fEntries), info.fCount * 2);
		fIndex->SetCurrentSoup(current);
	}
	fCurrent = 0;
}


// ROM 0x002cdce8 RebuildInfo__14TCollectCursorFUcl
void
TCollectCursor::RebuildInfo(Boolean keepSoupInfo, long removedSoup)
{
	TCursor::RebuildInfo(keepSoupInfo, removedSoup);
	Collect();
}


// ROM 0x002d17cc Move__14TCollectCursorFl
// count entries along the collected list; parked past either end.
Ref
TCollectCursor::Move(long count)
{
	long numEntries = CountEntries();
	if (fEntry == NILREF)
		fCurrent = fParkedAtEnd ? numEntries : -1;
	fCurrent += count;
	if (fCurrent < 0)
		Park(false);
	else if (fCurrent >= numEntries)
		Park(true);
	else
		DefineCurrentEntry();
	return fEntry;
}


// ROM 0x002d1848 DefineCurrentEntry__14TCollectCursorFv
void
TCollectCursor::DefineCurrentEntry(void)
{
	long slot = fCurrent * 2;
	PSSId id = (PSSId) RINT(GetArraySlotRef(fEntries, slot));
	long soupIndex = RINT(GetArraySlotRef(fEntries, slot + 1));
	RefVar soup(fSoupInfo[soupIndex].fSoup);
	fEntry = GetEntry(soup, id);
	fIndex->SetCurrentSoup(soupIndex);
}


// ROM 0x002d18dc FindEntry__14TCollectCursorFRC6RefVar
// The entry's place in the collected list; -1 for none.
long
TCollectCursor::FindEntry(RefArg entry)
{
	RefVar soup(FaultBlockHandler(entry));
	PSSId id = FaultBlockId(entry);
	long count = Length(fEntries);
	for (long slot = 0; slot < count; slot += 2)
	{
		if ((PSSId) RINT(GetArraySlotRef(fEntries, slot)) != id)
			continue;
		long soupIndex = RINT(GetArraySlotRef(fEntries, slot + 1));
		if (EQRef(fSoupInfo[soupIndex].fSoup, soup))
			return slot / 2;
	}
	return -1;
}


// ROM 0x002d19c8 GotoEntry__14TCollectCursorFRC6RefVar
Ref
TCollectCursor::GotoEntry(RefArg entry)
{
	if (!IsFaultBlock(entry))
		Throw(exStoreError, (void*) kNSErrNotASoupEntry, nil);
	long i = FindEntry(entry);
	if (i < 0)
		return NILREF;
	fCurrent = i;
	DefineCurrentEntry();
	return TRUEREF;
}


// ROM 0x002d1a38 GotoKey__14TCollectCursorFRC6RefVar
Ref
TCollectCursor::GotoKey(RefArg key)
{
	RefVar entry(TCursor::GotoKey(key));
	if ((Ref) entry == NILREF)
		return NILREF;
	return GotoEntry(entry);
}


// ROM 0x002d1a8c CountEntries__14TCollectCursorFv
long
TCollectCursor::CountEntries(void)
{
	return Length(fEntries) / 2;
}


// ROM 0x002d1aac Clone__14TCollectCursorFv
Ref
TCollectCursor::Clone(void)
{
	RefVar cursor(CreateNewCollectCursor());
	CursorObj(cursor)->Init(cursor, this);
	return cursor;
}


// ROM 0x002d1b08 EntryRemoved__14TCollectCursorFRC6RefVar
// The entry out of the list; the current one moves up when it was the
// one removed.
void
TCollectCursor::EntryRemoved(RefArg entry)
{
	long i = FindEntry(entry);
	if (i < 0)
		return;
	RefVar entries(fEntries);
	RefVar none;
	ArrayMunger(entries, i * 2, 2, none, 0, 0);
	if (fCurrent == i)
	{
		long count = CountEntries();
		if (count == 0)
			Park(false);
		else
		{
			if (fCurrent == count)
				fCurrent = count - 1;
			DefineCurrentEntry();
		}
	}
	else if (fCurrent > i)
		fCurrent--;
}


// ROM 0x002d1bd0 EntrySoupChanged__14TCollectCursorFRC6RefVarT1
// The entry moved to another soup: its list entry follows it when that
// soup is one of the query's, else it leaves the list.
void
TCollectCursor::EntrySoupChanged(RefArg entry, RefArg newEntry)
{
	long i = FindEntry(entry);
	if (i >= 0)
	{
		RefVar newSoup(FaultBlockHandler(newEntry));
		long soupIndex = GetSoupInfoIndex(newSoup);
		if (soupIndex < 0)
			EntryRemoved(entry);
		else
		{
			SetArraySlotRef(fEntries, i * 2, MAKEINT(FaultBlockId(newEntry)));
			SetArraySlotRef(fEntries, i * 2 + 1, MAKEINT(soupIndex));
		}
	}
	if (EQRef(fEntry, entry))
		GotoEntry(newEntry);
}


/*------------------------------------------------------------------------------
	T h e   s o u p ' s   c u r s o r s
------------------------------------------------------------------------------*/

// ROM 0x002cdd7c EachSoupCursorDo__FRC6RefVarlN21
// Every cursor in the soup's cursors cache told what happened.
void
EachSoupCursorDo(RefArg soup, int op, RefArg arg1, RefArg arg2)
{
	RefVar cursors(GetFrameSlotRef(soup, RSSYMcursors));
	RefVar cursor;
	for (long i = Length(cursors) - 1; i >= 0; i--)
	{
		cursor = GetArraySlotRef(cursors, i);
		if ((Ref) cursor == NILREF)
			continue;
		TCursor* c = CursorObj(cursor);
		switch (op)
		{
		case kSoupCursorSoupAdded:
			c->SoupAdded(arg1);
			break;
		case kSoupCursorSoupRemoved:
			c->SoupRemoved(arg1);
			break;
		case kSoupCursorEntryRemoved:
			c->EntryRemoved(arg1);
			break;
		case kSoupCursorSetSoup:
			c->SetSoup(arg1);
			break;
		case kSoupCursorTagsChanged:
			c->SoupTagsChanged(soup);
			break;
		case kSoupCursorIndexesChanged:
			c->IndexObjectsChanged();
			break;
		case kSoupCursorEntryMoved:
			c->EntrySoupChanged(arg1, arg2);
			break;
		case kSoupCursorEntryReadded:
			c->EntryReadded(arg1, arg2);
			break;
		case kSoupCursorIndexRemoved:
			c->IndexRemoved(soup, arg1);
			break;
		}
	}
}


// ROM 0x002cdec8 EachSoupCursorDo__FRC6RefVarl
void
EachSoupCursorDo(RefArg soup, int op)
{
	RefVar none1;
	RefVar none2;
	EachSoupCursorDo(soup, op, none1, none2);
}


// ROM 0x002cdf28 EachSoupCursorDo__FRC6RefVarlT1
void
EachSoupCursorDo(RefArg soup, int op, RefArg arg)
{
	RefVar none;
	EachSoupCursorDo(soup, op, arg, none);
}


// Every cursor of the soup told the entry changed (TCursor::EntryChanged,
// as EntryChangeCommon does).
void
EachSoupCursorEntryChanged(RefArg soup, RefArg entry, Boolean keysChanged, Boolean tagsChanged)
{
	RefVar cursors(GetFrameSlotRef(soup, RSSYMcursors));
	RefVar cursor;
	for (long i = Length(cursors) - 1; i >= 0; i--)
	{
		cursor = GetArraySlotRef(cursors, i);
		if ((Ref) cursor != NILREF)
			CursorObj(cursor)->EntryChanged(entry, keysChanged, tagsChanged);
	}
}


/*------------------------------------------------------------------------------
	Q u e r i e s
------------------------------------------------------------------------------*/

// ROM 0x0034defc DefineCursor__FRC6RefVarN21
// The cursor set up over the soup for the query (a union soup with an
// errorCode: over its last soup, and cached in the union soup's cursors).
void
DefineCursor(RefArg soup, RefArg querySpec, RefArg cursor)
{
	RefVar theSoup(soup);
	Boolean errored = GetFrameSlotRef(soup, RSSYMerrorcode) != NILREF;
	if (errored)
	{
		RefVar soupList(GetFrameSlotRef(soup, RSSYMsouplist));
		theSoup = GetArraySlotRef(soupList, Length(soupList) - 1);
	}
	CursorObj(cursor)->Init(cursor, theSoup, querySpec);
	if (errored)
	{
		RefVar cursors(GetFrameSlotRef(soup, RSSYMcursors));
		PutEntryIntoCache(cursors, cursor);
	}
}


// ROM 0x0034e008 CommonSoupQuery
// A soup's Query method: a new cursor, reset to its first entry.
Ref
CommonSoupQuery(RefArg rcvr, RefArg querySpec)
{
	RefVar cursor(TCursor::CreateNewCursor());
	DefineCursor(rcvr, querySpec, cursor);
	CursorObj(cursor)->Reset();
	return cursor;
}


// ROM 0x0034e05c SoupCollect
// A soup's collect method: a collect cursor (a plain query when memory
// runs out).
Ref
SoupCollect(RefArg rcvr, RefArg querySpec)
{
	RefVar cursor(TCollectCursor::CreateNewCollectCursor());
	DefineCursor(rcvr, querySpec, cursor);
	volatile Boolean outOfMemory = false;
	newton_try
	{
		((TCollectCursor*) CursorObj(cursor))->Collect();
	}
	newton_catch(exOutOfMemory)
	{
		outOfMemory = true;
	}
	end_try;
	if (outOfMemory)
		return CommonSoupQuery(rcvr, querySpec);
	return cursor;
}


/*------------------------------------------------------------------------------
	T h e   c u r s o r   n a t i v e s
	The cursor prototype's methods (the receiver is the cursor frame).
------------------------------------------------------------------------------*/

// ROM 0x002d04f8 CursorMove__FRC6RefVarl
Ref
CursorMove(RefArg cursor, long count)
{
	return CursorObj(cursor)->Move(count);
}


// ROM 0x002d0af0 CursorNext__FRC6RefVar
Ref
CursorNext(RefArg cursor)
{
	return CursorObj(cursor)->Move(1);
}


// ROM 0x002d0af4 CursorPrev__FRC6RefVar
Ref
CursorPrev(RefArg cursor)
{
	return CursorObj(cursor)->Move(-1);
}


// ROM 0x002d0af8 CursorReset__FRC6RefVar
Ref
CursorReset(RefArg cursor)
{
	return CursorObj(cursor)->Reset();
}


// ROM 0x002d0afc CursorEntry__FRC6RefVar
Ref
CursorEntry(RefArg cursor)
{
	return CursorObj(cursor)->Entry();
}


// ROM 0x002d0b00 CursorClone__FRC6RefVar
Ref
CursorClone(RefArg cursor)
{
	return CursorObj(cursor)->Clone();
}


// ROM 0x002d0ae8 CursorGoto__FRC6RefVarT1
Ref
CursorGoto(RefArg cursor, RefArg entry)
{
	return CursorObj(cursor)->GotoEntry(entry);
}


// ROM 0x002d0aec CursorGotoKey__FRC6RefVarT1
Ref
CursorGotoKey(RefArg cursor, RefArg key)
{
	return CursorObj(cursor)->GotoKey(key);
}


// ROM 0x002d0518 FCursorMove
static Ref
FCursorMove(RefArg rcvr, RefArg count)
{
	return CursorObj(rcvr)->Move(RINT(count));
}


// ROM 0x002d055c FCursorGoto
static Ref
FCursorGoto(RefArg rcvr, RefArg entry)
{
	return CursorObj(rcvr)->GotoEntry(entry);
}


// ROM 0x002d0580 FCursorGotoKey
static Ref
FCursorGotoKey(RefArg rcvr, RefArg key)
{
	return CursorObj(rcvr)->GotoKey(key);
}


// ROM 0x002d0958 FCursorNext
static Ref
FCursorNext(RefArg rcvr)
{
	return CursorObj(rcvr)->Move(1);
}


// ROM 0x002d0974 FCursorPrev
static Ref
FCursorPrev(RefArg rcvr)
{
	return CursorObj(rcvr)->Move(-1);
}


// ROM 0x002d0990 FCursorEntry
static Ref
FCursorEntry(RefArg rcvr)
{
	return CursorObj(rcvr)->Entry();
}


// ROM 0x002d09a8 FCursorReset
static Ref
FCursorReset(RefArg rcvr)
{
	return CursorObj(rcvr)->Reset();
}


// ROM 0x002d09c0 CursorResetToEnd
Ref
CursorResetToEnd(RefArg rcvr)
{
	return CursorObj(rcvr)->ResetToEnd();
}


// ROM 0x002d09d8 FCursorClone
static Ref
FCursorClone(RefArg rcvr)
{
	return CursorObj(rcvr)->Clone();
}


// ROM 0x002d09f4 CursorCountEntries
Ref
CursorCountEntries(RefArg rcvr)
{
	return MAKEINT(CursorObj(rcvr)->CountEntries());
}


// ROM 0x002d0a18 CursorWhichEnd
Ref
CursorWhichEnd(RefArg rcvr)
{
	return CursorObj(rcvr)->IsParked();
}


// ROM 0x002d0a30 CursorSoup
static Ref
CursorSoup(RefArg rcvr)
{
	return CursorObj(rcvr)->fSoup;
}


// ROM 0x002d0a48 CursorIndexPath
static Ref
CursorIndexPath(RefArg rcvr)
{
	return CursorObj(rcvr)->fIndexPath;
}


// ROM 0x002d0ab8 CursorEntryKey
static Ref
CursorEntryKey(RefArg rcvr)
{
	return CursorObj(rcvr)->EntryKey();
}


// ROM 0x002d0ad0 CursorStatus
static Ref
CursorStatus(RefArg rcvr)
{
	return CursorObj(rcvr)->Status();
}


// Host: the cursor prototype frame (the ROM's 0x005d2a59 cursorPrototype)
// when there are no ROM objects.
void
InitCursorPrototype(void)
{
	if (Rcursorprototype != NILREF)
		return;
	AddGCRoot(Rcursorprototype);
	static const struct { const char* fName; void* fFunction; long fNumArgs; } methods[] = {
		{ "move", (void*) FCursorMove, 1 },
		{ "goTo", (void*) FCursorGoto, 1 },
		{ "GotoKey", (void*) FCursorGotoKey, 1 },
		{ "next", (void*) FCursorNext, 0 },
		{ "prev", (void*) FCursorPrev, 0 },
		{ "reset", (void*) FCursorReset, 0 },
		{ "ResetToEnd", (void*) CursorResetToEnd, 0 },
		{ "entry", (void*) FCursorEntry, 0 },
		{ "clone", (void*) FCursorClone, 0 },
		{ "CountEntries", (void*) CursorCountEntries, 0 },
		{ "WhichEnd", (void*) CursorWhichEnd, 0 },
		{ "soup", (void*) CursorSoup, 0 },
		{ "indexPath", (void*) CursorIndexPath, 0 },
		{ "EntryKey", (void*) CursorEntryKey, 0 },
		{ "status", (void*) CursorStatus, 0 },
	};
	RefVar parent(AllocateFrame());
	RefVar fn;
	for (unsigned long i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
	{
		fn = MakeCFunction(methods[i].fFunction, methods[i].fNumArgs, nil);
		SetFrameSlot(parent, RefVar(Intern((char*) methods[i].fName)), fn);
	}
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYM_parent, parent);
	SetFrameSlot(frame, RSSYMtcursor, RefVar(NILREF));
	Rcursorprototype = frame;
}


void
RegisterCursorNatives(void)
{
	RegisterNativeFunction("FCursorMove", (void*) FCursorMove, 1);
	RegisterNativeFunction("FCursorGoto", (void*) FCursorGoto, 1);
	RegisterNativeFunction("FCursorGotoKey", (void*) FCursorGotoKey, 1);
	RegisterNativeFunction("FCursorNext", (void*) FCursorNext, 0);
	RegisterNativeFunction("FCursorPrev", (void*) FCursorPrev, 0);
	RegisterNativeFunction("FCursorEntry", (void*) FCursorEntry, 0);
	RegisterNativeFunction("FCursorReset", (void*) FCursorReset, 0);
	RegisterNativeFunction("CursorResetToEnd", (void*) CursorResetToEnd, 0);
	RegisterNativeFunction("FCursorClone", (void*) FCursorClone, 0);
	RegisterNativeFunction("CursorCountEntries", (void*) CursorCountEntries, 0);
	RegisterNativeFunction("CursorWhichEnd", (void*) CursorWhichEnd, 0);
	RegisterNativeFunction("CursorSoup", (void*) CursorSoup, 0);
	RegisterNativeFunction("CursorIndexPath", (void*) CursorIndexPath, 0);
	RegisterNativeFunction("CursorEntryKey", (void*) CursorEntryKey, 0);
	RegisterNativeFunction("CursorStatus", (void*) CursorStatus, 0);
	RegisterNativeFunction("CommonSoupQuery", (void*) CommonSoupQuery, 1);
	RegisterNativeFunction("SoupCollect", (void*) SoupCollect, 1);
}
