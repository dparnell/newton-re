/*
	File:		stores/CopyEntries.cpp

	Contains:	A soup's entries copied to another soup (the CopyEntries
				and CopyEntriesWithCallback methods).  When the target soup
				is empty and has the same indexes, the store objects are
				copied as they lie (CopyPermObject: the map and symbol
				references translated, each old id mapped to its new one)
				and each index copied key by key with the ids translated
				through the mapping (CopySoupIndexes); otherwise, or when
				the mapping cannot be allocated, each entry is read, stored
				and indexed in turn (SlowCopyEntries).  A callback function
				is called every so many milliseconds of either.

	Reconstructed from the MP2x00 US ROM, 0x0034cd98-0x0034db08.
*/

#include "Soups.h"
#include "StoreObject.h"
#include "NSErrors.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "NewtonTime.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include <stdlib.h>
#include <new>

extern const ExceptionName exStoreError;

#define kNSErrCantCopyToUnionSoup	(ERRBASE_FRAMES - 15)		// CopyEntries to a union soup (0xffff4471)

// an entry's store object id in the source soup and in the target
struct PSSIDMapping
{
	PSSId		fFrom;
	PSSId		fTo;
};


// The callback called when the interval (in ticks) has passed since the
// last call; the time of that call kept.
static void
CallBackIfDue(RefArg callback, ULong interval, TTime* lastCall)
{
	TTime now = GetGlobalTime();
	TTime elapsed = now - *lastCall;
	if ((ULong) elapsed >= interval)
	{
		DoBlock(callback, RefVar(NILREF));
		*lastCall = GetGlobalTime();
	}
}


// ROM 0x0034cd98 SlowCopyEntries__FRC6RefVarN21Ul
// Each entry of fromSoup (walked through its _uniqueID index) read,
// stored on toSoup's store and put into toSoup's indexes, keeping its
// _uniqueID; toSoup's next _uniqueID moved past the largest copied (its
// persistent frame's lastUID cleared, so it is derived from the index
// when the soup is next opened).
Ref
SlowCopyEntries(RefArg fromSoup, RefArg toSoup, RefArg callback, ULong interval)
{
	TStoreWrapper* fromWrapper = (TStoreWrapper*) GetFrameSlotRef(fromSoup, RSSYMtstore);
	TStoreWrapper* toWrapper = (TStoreWrapper*) GetFrameSlotRef(toSoup, RSSYMtstore);
	SKey key;
	SKey data;
	key.Clear();
	data.Clear();
	TTime lastCall;
	if (interval != 0)
		lastCall = GetGlobalTime();
	TSoupIndex* uniqueIdIndex = GetSoupIndexObject(fromSoup, 0);
	toWrapper->LockStore();
	newton_try
	{
		long maxUID = 0;
		if (uniqueIdIndex->First(&key, &data) == kIndexOK)
		{
			RefVar entry;
			do {
				entry = LoadPermObject(fromWrapper, (PSSId) (long) data, nil);
				long uid = RINT(GetFrameSlotRef(entry, RSSYM_uniqueid));
				if (uid > maxUID)
					maxUID = uid;
				PSSId id = (PSSId) -1;
				StorePermObject(entry, toWrapper, id, nil, nil);
				AlterIndexes(true, toSoup, entry, id);
				if (interval != 0)
					CallBackIfDue(callback, interval, &lastCall);
			} while (uniqueIdIndex->Next(&key, &data, kIndexNextDupOrKey, &key, &data) == kIndexOK);
		}
		if (RINT(GetFrameSlotRef(toSoup, RSSYMindexnextuid)) <= maxUID)
		{
			SetFrameSlot(toSoup, RSSYMindexnextuid, RefVar(MAKEINT(maxUID + 1)));
			RefVar persistent(GetFrameSlotRef(toSoup, RSSYM_proto));
			SetFrameSlot(persistent, RSSYMlastuid, RefVar(NILREF));
			SoupChanged(persistent, false);
			WriteFaultBlock(persistent);
		}
	}
	newton_catch_all
	{
		toWrapper->Abort();
		AbortSoupIndexes(toSoup);
		rethrow;
	}
	end_try;
	toWrapper->UnlockStore();
	return NILREF;
}


// ROM 0x0034d0b0 CompareSoupIndexes__FRC6RefVarT1
// Whether the two soups' persistent frames have indexes on the same
// paths (as many, each of the first's found in the second).
Boolean
CompareSoupIndexes(RefArg soupPersistent1, RefArg soupPersistent2)
{
	RefVar indexes(GetFrameSlotRef(soupPersistent1, RSSYMindexes));
	long count = Length(indexes);
	if (Length(RefVar(GetFrameSlotRef(soupPersistent2, RSSYMindexes))) != count)
		return false;
	RefVar path;
	for (long i = count - 1; i >= 0; i--)
	{
		path = GetFrameSlotRef(RefVar(GetArraySlotRef(indexes, i)), RSSYMpath);
		if (IndexPathToIndexDesc(soupPersistent2, path, nil) == NILREF)
			return false;
	}
	return true;
}


// ROM 0x0034d1b4 ComparePSSIDMapping__FPCvT1
// The mappings ordered by source id, for qsort and bsearch.
static int
ComparePSSIDMapping(const void* a, const void* b)
{
	PSSId idA = ((const PSSIDMapping*) a)->fFrom;
	PSSId idB = ((const PSSIDMapping*) b)->fFrom;
	if (idA > idB)
		return 1;
	return idA < idB ? -1 : 0;
}


// ROM 0x0034d1d4 CopyIndexStopFn__FP4SKeyT1Pv
// Each key of the source index put into the target's, in one transaction,
// with the entry's id (the datum; the key for a tags index) translated
// through the mapping; the callback every hundredth key when due.
struct CopyIndexInfo
{
	TSoupIndex*		fToIndex;		// +0x00
	long			fCount;			// +0x04  mappings
	PSSIDMapping*	fMapping;		// +0x08  sorted by source id
	Boolean			fIsTags;		// +0x0c
	Ref				fCallback;		// +0x10
	ULong			fInterval;		// +0x14  ticks; 0: no callback
	TTime			fLastCall;		// +0x18
	long			fCountdown;		// +0x20
};

static int
CopyIndexStopFn(SKey* key, SKey* data, void* refCon)
{
	CopyIndexInfo* info = (CopyIndexInfo*) refCon;
	PSSIDMapping wanted;
	wanted.fFrom = (PSSId) (long) *(info->fIsTags ? key : data);
	PSSIDMapping* found = (PSSIDMapping*) bsearch(&wanted, info->fMapping, info->fCount, sizeof(PSSIDMapping), ComparePSSIDMapping);
	SKey newId;
	newId.Clear();
	newId = (long) found->fTo;
	if (info->fIsTags)
		info->fToIndex->AddInTransaction(&newId, data);
	else
		info->fToIndex->AddInTransaction(key, &newId);
	if (info->fInterval != 0 && --info->fCountdown < 1)
	{
		TTime now = GetGlobalTime();
		TTime elapsed = now - info->fLastCall;
		if ((ULong) elapsed >= info->fInterval)
		{
			DoBlock(RefVar(info->fCallback), RefVar(NILREF));
			info->fLastCall = GetGlobalTime();
			info->fCountdown = 100;
		}
	}
	return 0;
}


// ROM 0x0034d2fc CopySoupIndexes__FRC6RefVarT1P12PSSIDMappinglT1T4
// Every index of fromSoup copied into toSoup's index of the same
// position, the ids translated, each committed as one transaction.
void
CopySoupIndexes(RefArg fromSoup, RefArg toSoup, PSSIDMapping* mapping, long count, RefArg callback, ULong interval)
{
	RefVar fromPersistent(GetFrameSlotRef(fromSoup, RSSYM_proto));
	RefVar toPersistent(GetFrameSlotRef(toSoup, RSSYM_proto));
	RefVar fromIndexes(GetFrameSlotRef(fromPersistent, RSSYMindexes));
	RefVar toIndexes(GetFrameSlotRef(toPersistent, RSSYMindexes));
	long numIndexes = Length(fromIndexes);
	RefVar indexDesc;
	CopyIndexInfo info;
	info.fCount = count;
	info.fMapping = mapping;
	info.fCallback = NILREF;
	info.fInterval = interval;
	if (interval != 0)
	{
		info.fCallback = callback;
		info.fLastCall = GetGlobalTime();
		info.fCountdown = 100;
	}
	for (long i = 0; i < numIndexes; i++)
	{
		indexDesc = GetArraySlotRef(fromIndexes, i);
		TSoupIndex* fromIndex = GetSoupIndexObject(fromSoup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
		TSoupIndex* toIndex = GetSoupIndexObject(toSoup, (PSSId) RINT(GetFrameSlotRef(RefVar(GetArraySlotRef(toIndexes, i)), RSSYMindex)));
		info.fToIndex = toIndex;
		info.fIsTags = EQRef(GetFrameSlotRef(indexDesc, RSSYMtype), RSSYMtags);
		int result = fromIndex->Search(true, nil, nil, CopyIndexStopFn, &info, nil, nil);
		if (result < 0)
			Throw(exStoreError, (void*) (Long) result, nil);
		toIndex->fNodeCache->Commit(toIndex);
	}
}


// ROM 0x0034d564 CopyEntriesStopFn__FP4SKeyT1Pv
// Each entry's store object copied as it lies and its ids recorded; the
// callback when due.  Stops when the mapping is full.
struct CopyEntriesInfo
{
	TStoreWrapper*	fFrom;			// +0x00
	TStoreWrapper*	fTo;			// +0x04
	PSSIDMapping*	fMapping;		// +0x08
	long			fCapacity;		// +0x0c
	long			fCount;			// +0x10
	Ref				fCallback;		// +0x14
	ULong			fInterval;		// +0x18  ticks; 0: no callback
	TTime			fLastCall;		// +0x1c
};

static int
CopyEntriesStopFn(SKey* /*key*/, SKey* data, void* refCon)
{
	CopyEntriesInfo* info = (CopyEntriesInfo*) refCon;
	if (info->fCount >= info->fCapacity)
		return 1;
	PSSId id = (PSSId) (long) *data;
	info->fMapping[info->fCount].fFrom = id;
	info->fMapping[info->fCount].fTo = CopyPermObject(id, info->fFrom, info->fTo);
	info->fCount++;
	if (info->fInterval != 0)
		CallBackIfDue(RefVar(info->fCallback), info->fInterval, &info->fLastCall);
	return 0;
}


// ROM 0x0034d654 PlainSoupCopyEntriesWithCallBack
// The soup's entries copied to toSoup (a plain soup on a writable store);
// callback (a function of no arguments) is called every interval
// milliseconds.  An empty target with the same indexes takes the fast
// path: the store objects copied as they lie under the store's map and
// symbol translation, the ids mapped, the indexes copied and the
// _uniqueID state carried over; otherwise SlowCopyEntries.
Ref
PlainSoupCopyEntriesWithCallBack(RefArg rcvr, RefArg toSoup, RefArg callback, RefArg interval)
{
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	if ((Ref) persistent == NILREF)
		Throw(exStoreError, (void*) kNSErrSoupRemoved, nil);
	RefVar toPersistent(GetFrameSlotRef(toSoup, RSSYM_proto));
	if ((Ref) toPersistent == NILREF)
		Throw(exStoreError, (void*) kNSErrSoupRemoved, nil);
	if (FrameHasSlot(toSoup, RSSYMsouplist))
		Throw(exStoreError, (void*) kNSErrCantCopyToUnionSoup, nil);
	TStoreWrapper* toWrapper = (TStoreWrapper*) GetFrameSlotRef(toSoup, RSSYMtstore);
	CheckWriteProtect(toWrapper->Store());
	ULong ticks = 0;
	if ((Ref) callback != NILREF)
	{
		TTime period((ULong) RINT(interval), kMilliseconds);
		ticks = (ULong) period;
	}
	if (RINT(GetFrameSlotRef(toSoup, RSSYMindexnextuid)) != 0 || !CompareSoupIndexes(persistent, toPersistent))
		return SlowCopyEntries(rcvr, toSoup, callback, ticks);

	RefVar nextUID(GetFrameSlotRef(rcvr, RSSYMindexnextuid));
	long capacity = RINT(nextUID);
	PSSIDMapping* volatile mapping = new (std::nothrow) PSSIDMapping[capacity];
	if (mapping == nil)
		return SlowCopyEntries(rcvr, toSoup, callback, ticks);
	toWrapper->LockStore();
	newton_try
	{
		toWrapper->StartCopyMaps_Symbols();
		CopyEntriesInfo info;
		info.fFrom = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
		info.fTo = toWrapper;
		info.fMapping = mapping;
		info.fCapacity = capacity;
		info.fCount = 0;
		info.fCallback = callback;
		info.fInterval = ticks;
		if (ticks != 0)
			info.fLastCall = GetGlobalTime();
		int result = GetSoupIndexObject(rcvr, 0)->Search(true, nil, nil, CopyEntriesStopFn, &info, nil, nil);
		if (result < 0)
			Throw(exStoreError, (void*) (Long) result, nil);
		toWrapper->EndCopyMaps_Symbols();
		qsort(mapping, info.fCount, sizeof(PSSIDMapping), ComparePSSIDMapping);
		CopySoupIndexes(rcvr, toSoup, mapping, info.fCount, callback, ticks);
		delete[] mapping;
		mapping = nil;
		SetFrameSlot(toPersistent, RSSYMlastuid, RefVar(GetFrameSlotRef(persistent, RSSYMlastuid)));
		SoupChanged(toPersistent, false);
		WriteFaultBlock(toPersistent);
		SetFrameSlot(toSoup, RSSYMindexnextuid, nextUID);
	}
	newton_catch_all
	{
		toWrapper->EndCopyMaps_Symbols();
		delete[] mapping;
		toWrapper->Abort();
		AbortSoupIndexes(toSoup);
		rethrow;
	}
	end_try;
	toWrapper->UnlockStore();
	return NILREF;
}


// ROM 0x0034daa0 PlainSoupCopyEntries
Ref
PlainSoupCopyEntries(RefArg rcvr, RefArg toSoup)
{
	return PlainSoupCopyEntriesWithCallBack(rcvr, toSoup, RefVar(NILREF), RefVar(NILREF));
}
