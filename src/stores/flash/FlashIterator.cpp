/*
	File:		stores/flash/FlashIterator.cpp

	Contains:	TFlashIterator (FlashStore.h): the walks over a flash store
				- its objects, one block's objects, a directory bucket and
				its continuations, the ids a tracker holds - and the
				directory lookup of an id.

	Reconstructed from the MP2x00 US ROM (0x000c1514-0x000c2120); each
	function cites its origin.
*/

#include "FlashStore.h"


// ROM 0x000c1514 __ct__14TFlashIteratorFP11TFlashStoreP7TObjRef14IterFilterType
TFlashIterator::TFlashIterator(TFlashStore* store, TObjRef* ref, IterFilterType filter)
{
	fRef = ref;
	fStore = store;
	Start(filter);
}


// ROM 0x000c155c __ct__14TFlashIteratorFP11TFlashStoreP7TObjRefUl14IterFilterType
TFlashIterator::TFlashIterator(TFlashStore* store, TObjRef* ref, ULong start, IterFilterType filter)
{
	fRef = ref;
	fStore = store;
	Start(start, filter);
}


// ROM 0x000c1890 __ct__14TFlashIteratorFP11TFlashStoreP7TObjRefP11TFlashBlock14IterFilterType
TFlashIterator::TFlashIterator(TFlashStore* store, TObjRef* ref, TFlashBlock* block, IterFilterType filter)
{
	fRef = ref;
	fStore = store;
	Start(block->fLogicalOffset, filter);
}


// ROM 0x000c18e0 __ct__14TFlashIteratorFP11TFlashStoreP7SDirEntUl
// The entries of the bucket at `bucket`, and of its continuations.
TFlashIterator::TFlashIterator(TFlashStore* store, SDirEnt* dirEnt, ULong bucket)
{
	fRef = nil;
	fStore = store;
	fDirEnt = dirEnt;
	fUnusedCount = store->fBucketSize;
	Start(bucket, kIterDirectory);
}


// ROM 0x000c15ac Start__14TFlashIteratorF14IterFilterType
void
TFlashIterator::Start(IterFilterType filter)
{
	fFilter = filter;
	fOffset = 4;
	fStart = 0;
	fDirEntOffset = 0xFFFFFFFF;
	fState = 0;
	fBucketCacheOffset = 0xFFFFFFFF;
}


// ROM 0x000c15d4 Start__14TFlashIteratorFUl14IterFilterType
void
TFlashIterator::Start(ULong start, IterFilterType filter)
{
	fStart = start;
	fState = 0;
	fFilter = filter;
	fBucketCacheOffset = 0xFFFFFFFF;
}


// ROM 0x000c15f0 Start__14TFlashIteratorFP13TFlashTracker
void
TFlashIterator::Start(TFlashTracker* tracker)
{
	fTracker = tracker;
	fFilter = kIterTracker;
	fState = 0;
}


// ROM 0x000c1608 Lookup__14TFlashIteratorFUliPl
// The id's object, in the state asked for (0: whatever a reader should
// see; -1: any), through the directory from fStart: entries whose bit 6
// is cleared are passed over (though one that says the object migrated to
// another block is noted), a link is followed, and anything else is an
// object to read the header of.
NewtonErr
TFlashIterator::Lookup(PSSId id, int state, long* migratedTo)
{
	ULong linkBase = 0;
	ULong pos = fStart;
	long objectNumber = 0;
	if (migratedTo != nil)
	{
		*migratedTo = -1;
		objectNumber = fStore->ObjectNumberFor(id);
	}
	for (;;)
	{
		SDirEnt entry;
		do
		{
			pos += 4;
			entry.fWord = GetDirEnt(pos);
			if (fStore->fVirginWord == entry.fWord)
				return kSError_ObjectNotFound;
		} while (!entry.IsValid(fStore));
		ULong zap = fStore->fZapWord & 1;
		if (zap == ((entry.fWord & 0x7F) >> 6))
		{
			if (migratedTo != nil && *migratedTo < 0 && zap == ((entry.fWord & 7) >> 2))
			{
				long number, block;
				entry.GetMigratedObjectInfo(&number, &block);
				if (number == objectNumber)
					*migratedTo = block;
			}
			continue;
		}
		ULong offset = (entry.fWord >> 8) * 4;
		if (fStore->fBlockCount * fStore->fBlockSize < offset)
			return kSError_NeedsFormat;
		if (zap == ((entry.fWord & 0xFF) >> 7))
		{
			pos = offset + linkBase;
			continue;
		}
		fRef->Set(offset, pos);
		if (fRef->Id() != id)
			continue;
		if (state == -1)
			return noErr;
		int found = fRef->State();
		if (state != kObjNoState)
		{
			if (found == state)
				return noErr;
			continue;
		}
		switch (found)
		{
		case kRAMObjNew:
		case kRAMObjCommitted:
		case kRAMObjSuperceder:
		case kFlashObjNew:
		case kFlashObjCommitted:
		case kFlashObjSuperceder:
			return noErr;
		}
	}
}


// ROM 0x000c1818 GetDirEnt__14TFlashIteratorFUl
// Out of the sixteen entries last read, or read afresh from offset.
ULong
TFlashIterator::GetDirEnt(ULong offset)
{
	if (fBucketCacheOffset != 0xFFFFFFFF && fBucketCacheOffset <= offset && offset < fBucketCacheOffset + 0x40)
		return fBucketCache[(offset - fBucketCacheOffset) >> 2];
	return ReadDirBucket(offset);
}


// ROM 0x000c20ac ReadDirBucket__14TFlashIteratorFUl
ULong
TFlashIterator::ReadDirBucket(ULong offset)
{
	fStore->ReadWords(fStore->Translate(offset), fBucketCache, 16);
	fBucketCacheOffset = offset;
	return fBucketCache[0];
}


// ROM 0x000c1854 CountUnusedDirEnt__14TFlashIteratorFv
long
TFlashIterator::CountUnusedDirEnt(void)
{
	while (!Done())
		Next();
	return fUnusedCount;
}


// ROM 0x000c1938 Done__14TFlashIteratorFv
// Probes for the next one unless Next has not taken the last yet; a
// compaction since the walk started makes it start again.
Boolean
TFlashIterator::Done(void)
{
	long state = fState;
	if (state == 0)
	{
		fCompactCount = fStore->fCompactCount;
		Probe();
		state = fState;
	}
	else
	{
		if (state != 2)
			return state == 3;
		if (fCompactCount != fStore->fCompactCount)
			fState = 0;
		Probe();
		state = fState;
	}
	if (state == 3)
		return true;
	fState = 1;
	return false;
}


// The walk of the store's objects, a block at a time, for Probe's first
// four filters: `wholeStore` goes on into the next block (which is where
// the log starts), `systemToo` returns the store's own objects too.
static Boolean
NextLiveObject(TFlashIterator* iter, Boolean wholeStore, Boolean systemToo)
{
	TFlashStore* store = iter->fStore;
	iter->fDirEntOffset = 0xFFFFFFFF;
	for (;;)
	{
		iter->fOffset = iter->fOffset + iter->fStep;
		if (store->fBlockSize - store->LogSize() <= (store->fBlockMask & iter->fOffset))
		{
			if (!wholeStore)
				return false;
			iter->fOffset = (store->fBlockSize + iter->fOffset - 1) & ~(store->fBlockSize - 1);
			if (store->StoreCapacity() <= iter->fOffset || store->BlockAt(iter->fOffset)->IsVirgin())
				return false;
		}
		iter->fRef->Set(iter->fOffset, 0xFFFFFFFF);
		TObjRef* ref = iter->fRef;
		if (!ref->IsValid(ref->fStore))
		{
			if (ref->fStore->fVirginWord == ref->fWord0)
			{
				if (!wholeStore)
					return false;
				iter->fOffset = (store->fBlockSize + iter->fOffset - 1) & ~(store->fBlockSize - 1);
				if (store->StoreCapacity() <= iter->fOffset || store->BlockAt(iter->fOffset)->IsVirgin())
					return false;
			}
			iter->fStep = 4;
		}
		else
		{
			if ((ref->fStore->fVirginWord & 1) == ((ref->fWord0 & 0x3FFFFFFF) >> 29))
				iter->fStep = ((ref->Size() + 3) & ~3) + 8;
			else
				iter->fStep = 4;
			if ((ref->fStore->fZapWord & 1) != ((ref->fWord0 & 0x7FFFFFFF) >> 30)
			 && (systemToo || ref->Id() > 0x20))
				return true;
		}
	}
}


// ROM 0x000c19d4 Probe__14TFlashIteratorFv
// Finds the next thing; fState 3 when there is none.
void
TFlashIterator::Probe(void)
{
	if (fState != 0)
	{
		if (fState == 1)
		{
			fState = 2;
			return;
		}
	}
	else
	{
		switch (fFilter)
		{
		case kIterAllObjects:
		case kIterAllObjectsAllStates:
			if (fCompactCount != fStore->fCompactCount)
			{
				// a compaction moved everything: from the start of the block it had got to
				fOffset = (~(fStore->fBlockSize - 1) & fOffset) + 4;
				fDirEntOffset = 0xFFFFFFFF;
				fStep = 4;
				break;
			}
			// fall through
		case kIterBlockObjects:
		case kIterBlockObjectsAllStates:
			fOffset = fStart;
			fDirEntOffset = 0xFFFFFFFF;
			fStep = 4;
			break;
		case kIterDirectory:
			fOffset = 0xFFFFFFFF;
			fBucketCacheOffset = 0xFFFFFFFF;
			fDirEntOffset = fStart;
			fStep = 0;
			break;
		case kIterTracker:
			fTrackerIndex = -1;
			break;
		}
		fState = 2;
		fCompactCount = fStore->fCompactCount;
	}

	switch (fFilter)
	{
	case kIterAllObjects:
		if (NextLiveObject(this, true, false))
			return;
		break;
	case kIterAllObjectsAllStates:
		if (NextLiveObject(this, true, true))
			return;
		break;
	case kIterBlockObjects:
		if (NextLiveObject(this, false, false))
			return;
		break;
	case kIterBlockObjectsAllStates:
		if (NextLiveObject(this, false, true))
			return;
		break;
	case kIterDirectory:
		for (;;)
		{
			fDirEntOffset += 4;
			fDirEnt->fWord = GetDirEnt(fDirEntOffset);
			if (fStore->fVirginWord == fDirEnt->fWord)
				break;
			if (fDirEnt->IsValid(fStore))
			{
				ULong word = fDirEnt->fWord;
				ULong zap = fStore->fZapWord & 1;
				if (zap != ((word & 0x7F) >> 6) || zap == ((word & 7) >> 2))
				{
					if (zap != ((word & 0xFF) >> 7))
					{
						fUnusedCount--;
						return;
					}
					fDirEntOffset = (word >> 8) << 2;
					fUnusedCount = fStore->fBucketSize + fUnusedCount;
				}
			}
		}
		break;
	case kIterTracker:
		for (;;)
		{
			ULong index = fTrackerIndex + 1;
			fTrackerIndex = index;
			if (fTracker->fCount <= index)
				break;
			PSSId id = fTracker->fIds[index];
			if (id != 0xFFFFFFFF && fStore->Lookup(id, -1, *fRef) == noErr)
				return;
		}
		break;
	default:
		return;
	}
	fState = 3;
}


// ROM 0x000c20f8 Next__14TFlashIteratorFv
TObjRef*
TFlashIterator::Next(void)
{
	Probe();
	return fRef;
}


// ROM 0x000c2114 Reset__14TFlashIteratorFv
void
TFlashIterator::Reset(void)
{
	fState = 0;
}
