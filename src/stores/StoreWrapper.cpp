/*
	File:		stores/StoreWrapper.cpp

	Contains:	TStoreWrapper, TStoreHashTable, TStoreHashTableIterator and
				TCachedReadStore (StoreWrapper.h): the frames layer's symbol
				and map tables on a store.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	NOT YET RECONSTRUCTED: TEphemeralTracker (fEphemeralTracker stays nil,
	its calls are skipped as the ROM skips them when it is nil).
*/

#include "StoreWrapper.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"

#include <stdlib.h>
#include <string.h>

Boolean gAskedForFlush = false;			// 0x0c102a2c  a store is dirty: the flush task should run


// ROM 0x002b698c _OSErr__Fl
// A store error thrown as exStoreError.
void
ThrowOSErr(NewtonErr err)
{
	Throw(exStoreError, (void*) (Long) err, nil);
}


// ROM 0x00328a04 AskForFlush__FUc
void
AskForFlush(Boolean ask)
{
	gAskedForFlush = ask;
}


// a 2-byte big-endian length at an address of any alignment
static inline long
LengthAt(const void* p)
{
	const UByte* b = (const UByte*) p;
	return ((long) b[0] << 8) | b[1];
}

static inline void
SetLengthAt(void* p, long length)
{
	UByte* b = (UByte*) p;
	b[0] = (UByte) (length >> 8);
	b[1] = (UByte) length;
}


/* -------------------------------------------------------------------------------
	TStoreHashTable
------------------------------------------------------------------------------- */

// ROM 0x00328158 Create__15TStoreHashTableSFP6TStore
// A new table object, its 64 buckets empty; ==> its id.
PSSId
TStoreHashTable::Create(TStore* store)
{
	char buckets[kStoreHashTableSize];
	memset(buckets, 0, sizeof(buckets));
	PSSId id;
	OSErrIf(store->NewObject(buckets, sizeof(buckets), &id));
	return id;
}


// the 64 bucket ids read (they lie big-endian on the store)
static void
ReadBuckets(TStore* store, PSSId id, StorePSSId* buckets)
{
	char bytes[kStoreHashTableSize];
	OSErrIf(store->Read(id, 0, bytes, kStoreHashTableSize));
	for (long i = 0; i < kStoreHashTableBuckets; i++)
		buckets[i] = GetBigEndianWord(bytes + i * 4);
}


// ROM 0x003281a0 __ct__15TStoreHashTableFP6TStoreUl
TStoreHashTable::TStoreHashTable(TStore* store, PSSId id)
{
	fId = id;
	fStore = store;
	ReadBuckets(store, id, fBuckets);
}


// ROM 0x00328204 Abort__15TStoreHashTableFv
// The buckets as the store has them again (after its transaction aborted).
void
TStoreHashTable::Abort(void)
{
	ReadBuckets(fStore, fId, fBuckets);
}


// ROM 0x00328244 Insert__15TStoreHashTableFUlPcl
// The entry (size bytes of data) in the bucket its hash selects, added
// when not there yet; ==> its reference (bucket << 16 | offset).
long
TStoreHashTable::Insert(ULong hash, char* data, long size)
{
	long bucket = hash & (kStoreHashTableBuckets - 1);
	PSSId bucketId = fBuckets[bucket];
	long entrySize = size + 2;
	long offset = 0;
	Boolean found = false;
	if (bucketId == 0)
	{
		// the first entry: the bucket object made and recorded in the table
		OSErrIf(fStore->NewObject(entrySize, &bucketId));
		char stored[4];
		PutBigEndianWord(stored, (ULong32) bucketId);
		OSErrIf(fStore->Write(fId, bucket * sizeof(StorePSSId), stored, sizeof(StorePSSId)));
	}
	else
	{
		long bucketSize;
		OSErrIf(fStore->GetObjectSize(bucketId, &bucketSize));
		if (bucketSize > 0)
		{
			TCachedReadStore reader(fStore, bucketId, bucketSize);
			while (offset < bucketSize)
			{
				void* p;
				OSErrIf(reader.GetDataPtr(offset, 2, &p));
				long length = LengthAt(p);
				if (length == size)
				{
					OSErrIf(reader.GetDataPtr(offset + 2, size, &p));
					if (memcmp(p, data, size) == 0)
					{
						found = true;
						break;
					}
				}
				offset += length + 2;
			}
		}
		if (!found)
			OSErrIf(fStore->SetObjectSize(bucketId, offset + entrySize));
	}
	if (!found)
	{
		if (entrySize <= kStoreHashTableMaxEntry)
		{
			char entry[kStoreHashTableMaxEntry];
			SetLengthAt(entry, size);
			memcpy(entry + 2, data, size);
			OSErrIf(fStore->Write(bucketId, offset, entry, entrySize));
		}
		else
		{
			char length[2];
			SetLengthAt(length, size);
			OSErrIf(fStore->Write(bucketId, offset, length, 2));
			OSErrIf(fStore->Write(bucketId, offset + 2, data, size));
		}
	}
	fBuckets[bucket] = (StorePSSId) bucketId;
	return offset + (bucket << 16);
}


// ROM 0x003284e8 Get__15TStoreHashTableFlPcPl
// The entry a reference names, when it fits *size bytes; *size becomes
// its length either way.
Boolean
TStoreHashTable::Get(long reference, char* data, long* size)
{
	PSSId bucketId = fBuckets[reference >> 16];
	long offset = reference & 0xffff;
	char lengthBytes[2];
	OSErrIf(fStore->Read(bucketId, offset, lengthBytes, 2));
	long room = *size;
	long length = LengthAt(lengthBytes);
	*size = length;
	if (length <= room)
		OSErrIf(fStore->Read(bucketId, offset + 2, data, length));
	return length <= room;
}


// ROM 0x0032859c TotalSize__15TStoreHashTableFv
// The table object and its buckets.
long
TStoreHashTable::TotalSize(void)
{
	long total = kStoreHashTableSize;
	for (long i = 0; i < kStoreHashTableBuckets; i++)
		if (fBuckets[i] != 0)
		{
			long size;
			OSErrIf(fStore->GetObjectSize(fBuckets[i], &size));
			total += size;
		}
	return total;
}


// ROM 0x003285f8 __ct__23TStoreHashTableIteratorFP15TStoreHashTable
// Positioned on the first entry (or done).
TStoreHashTableIterator::TStoreHashTableIterator(TStoreHashTable* table)
{
	fBucket = -1;
	fBucketSize = 0;
	fTable = table;
	fOffset = 0;
	fEntryLength = 0;
	fDone = false;
	Next();
}


// ROM 0x00328654 Next__23TStoreHashTableIteratorFv
// On to the next entry: past the current one, then through the empty
// buckets to the next that has entries.
void
TStoreHashTableIterator::Next(void)
{
	if (fDone)
		return;
	fOffset += fEntryLength + 2;
	while (fOffset >= fBucketSize)
	{
		fOffset = 0;
		fBucket++;
		if (fBucket == kStoreHashTableBuckets)
		{
			fDone = true;
			return;
		}
		fBucketId = fTable->fBuckets[fBucket];
		if (fBucketId == 0)
			fBucketSize = 0;
		else
			OSErrIf(fTable->fStore->GetObjectSize(fBucketId, &fBucketSize));
	}
	char lengthBytes[2];
	OSErrIf(fTable->fStore->Read(fBucketId, fOffset, lengthBytes, 2));
	fEntryLength = LengthAt(lengthBytes);
}


// ROM 0x00328730 GetData__23TStoreHashTableIteratorFPcPl
// The current entry's bytes, up to *size of them; *size what was copied.
void
TStoreHashTableIterator::GetData(char* data, long* size)
{
	long length = fEntryLength;
	if (*size < length)
		length = *size;
	OSErrIf(fTable->fStore->Read(fBucketId, fOffset + 2, data, length));
	*size = length;
}


/* -------------------------------------------------------------------------------
	TCachedReadStore
------------------------------------------------------------------------------- */

// ROM 0x003299fc __ct__16TCachedReadStoreFv
TCachedReadStore::TCachedReadStore()
{
	fData = nil;
	fLoaded = false;
	fBigBuffer = nil;
}


// ROM 0x00329a38 __ct__16TCachedReadStoreFP6TStoreUll
TCachedReadStore::TCachedReadStore(TStore* store, PSSId id, long size)
{
	fData = nil;
	fLoaded = false;
	fBigBuffer = nil;
	Init(store, id, size);
}


// ROM 0x00329c04 __dt__16TCachedReadStoreFv
TCachedReadStore::~TCachedReadStore()
{
	if (fData != nil && fData != fBuffer)
		delete[] fData;
	if (fBigBuffer != nil)
		free(fBigBuffer);
}


// ROM 0x00329c48 Init__16TCachedReadStoreFP6TStoreUll
// Over an object of size bytes (-1: as the store says); nothing read yet.
void
TCachedReadStore::Init(TStore* store, PSSId id, long size)
{
	fStore = store;
	fId = id;
	if (size < 0 && store->GetObjectSize(id, &size) != noErr)
		size = 0;
	fSize = size;
	if (fData != nil && fData != fBuffer)
		delete[] fData;
	if (size <= kCachedReadStoreBufferSize)
		fData = fBuffer;
	else
		fData = new char[size];
	fLoaded = false;
}


// ROM 0x00329ce4 GetDataPtr__16TCachedReadStoreFlT1PPv
// A pointer to size bytes of the object at offset: into the cached copy
// (read on first use), or - for a range beyond it - read into a buffer
// of their own.
NewtonErr
TCachedReadStore::GetDataPtr(long offset, long size, void** data)
{
	if (fData == nil || fSize < offset + size)
	{
		if (size < kCachedReadStoreBufferSize)
			*data = fBuffer;
		else
		{
			if (fBigBuffer == nil || fBigBufferSize < size)
			{
				char* grown = (char*) realloc(fBigBuffer, size);
				if (grown == nil)
					return kError_No_Memory;
				fBigBuffer = grown;
				fBigBufferSize = size;
			}
			*data = fBigBuffer;
		}
		return fStore->Read(fId, offset, (char*) *data, size);
	}
	if (!fLoaded)
	{
		NewtonErr err = fStore->Read(fId, 0, fData, fSize);
		if (err != noErr && err != kSError_ObjectOverRun)
			return err;
		fLoaded = true;
	}
	*data = fData + offset;
	return noErr;
}


/* -------------------------------------------------------------------------------
	TStoreWrapper
------------------------------------------------------------------------------- */

// ROM 0x00328790 __ct__13TStoreWrapperFP6TStore
// Over a store; the tables are attached by whoever reads or makes the
// store's root (MakeStoreObject).
TStoreWrapper::TStoreWrapper(TStore* store)
{
	fMapCache = NILREF;
	fSymbolCache = NILREF;
	fIsDirty = false;
	fStore = store;
	fMapTable = nil;
	fSymbolTable = nil;
	fMapCache = AllocateArray(RSSYMarray, kMapCacheSize);
	fSymbolCache = AllocateArray(RSSYMarray, kSymbolCacheSize);
	for (long i = 0; i < kMapCacheSize; i++)
		fMapCacheRefs[i] = -1;
	fMapCacheNext = 0;
	for (long i = 0; i < kSymbolCacheSize; i++)
		fSymbolCacheRefs[i] = -1;
	fSymbolCacheNext = 0;
	fCopyInfo = nil;
	fEphemeralTracker = nil;
}


// ROM 0x00328948 __dt__13TStoreWrapperFv
TStoreWrapper::~TStoreWrapper()
{
	if (fMapTable != nil)
		delete fMapTable;
	if (fSymbolTable != nil)
		delete fSymbolTable;
	// NOT YET RECONSTRUCTED: delete fEphemeralTracker
}


// ROM 0x003289b4 Dirty__13TStoreWrapperFv
// A change begins: the store locked (committed by SparklingClean) and the
// flush asked for.
void
TStoreWrapper::Dirty(void)
{
	AskForFlush(true);
	fIsDirty = true;
	// NOT YET RECONSTRUCTED: fEphemeralTracker->LockEphemerals()
	fStore->LockStore();
}


// ROM 0x003289ec SparklingClean__13TStoreWrapperFv
// The change is complete: the store unlocked (committing when this was
// the last lock).
void
TStoreWrapper::SparklingClean(void)
{
	fIsDirty = false;
	// NOT YET RECONSTRUCTED: fEphemeralTracker->FlushEphemerals(), DeletePendingEphemerals()
	fStore->UnlockStore();
}


// ROM 0x00329930 LockStore__13TStoreWrapperFv
NewtonErr
TStoreWrapper::LockStore(void)
{
	// NOT YET RECONSTRUCTED: fEphemeralTracker->LockEphemerals()
	return fStore->LockStore();
}


// ROM 0x00329958 UnlockStore__13TStoreWrapperFv
NewtonErr
TStoreWrapper::UnlockStore(void)
{
	// NOT YET RECONSTRUCTED: fEphemeralTracker->FlushEphemerals(), DeletePendingEphemerals()
	return fStore->UnlockStore();
}


// ROM 0x003299b4 Abort__13TStoreWrapperFv
// The store's transaction undone and everything cached over it dropped.
NewtonErr
TStoreWrapper::Abort(void)
{
	// NOT YET RECONSTRUCTED: fEphemeralTracker->AbortEphemerals()
	fNodeCache.Clear();
	NewtonErr err = fStore->Abort();
	if (fMapTable != nil)
		fMapTable->Abort();
	if (fSymbolTable != nil)
		fSymbolTable->Abort();
	return err;
}


// ROM 0x00329900 GetStoreSizes__13TStoreWrapperFPlT1
// (after a collection, so that unreferenced entries are gone)
void
TStoreWrapper::GetStoreSizes(long* totalSize, long* usedSize)
{
	GC();
	fStore->GetStoreSizes(totalSize, usedSize);
}


// the hash of a map's names: each name's hash rotated into the running
// value by the count so far
static inline ULong
RotateInHash(ULong hash, ULong nameHash, long count)
{
	ULong x = hash ^ nameHash;
	ULong n = count & 0x1f;
	return n == 0 ? x : (x >> n) | (x << (32 - n));
}


// ROM 0x00328a14 AddMap__13TStoreWrapperFP12SortedMapTagUcPlT3
// The map of *count sorted tags entered in the map table: _proto is left
// out, and with skipHiddenSlots so are _uniqueID and _modTime; the names
// are packed after a 2-byte count into the buffer after indexes, and
// indexes[i] becomes the frame slot the i-th stored slot comes from.
// *count becomes the number kept; ==> the map reference.
long
TStoreWrapper::AddMap(SortedMapTag* tags, Boolean skipHiddenSlots, long* count, long* indexes)
{
	long numTags = *count;
	char* names = (char*) (indexes + numTags);		// the buffer follows the indexes
	ULong hash = 0;
	long size = 2;
	*count = 0;
	for (long i = 0; i < numTags; i++)
	{
		Ref sym = tags[i].fTag;
		ULong symHash = SymbolHash(sym);
		if (symHash == 0x6622439b && SymbolCompare(sym, RSSYM_proto) == 0)
			continue;
		if (skipHiddenSlots)
		{
			if (symHash == 0xf33529eb && SymbolCompare(sym, RSSYM_uniqueid) == 0)
				continue;
			if (symHash == 0x6ac9bf7e && SymbolCompare(sym, RSSYM_modtime) == 0)
				continue;
		}
		hash = RotateInHash(hash, symHash, *count);
		const char* name = SymbolName(sym);
		size_t length = strlen(name);
		memcpy(names + size, name, length + 1);
		size += length + 1;
		*indexes++ = tags[i].fIndex;
		(*count)++;
	}
	SetLengthAt(names, *count);
	return fMapTable->Insert(hash, names, size);
}


// ROM 0x00328bac FrameToMapReference__13TStoreWrapperFRC6RefVarUcPlPPl
// The frame's tags sorted (except a function's and an argFrame's, whose
// order matters) and entered in the map table; *indexes (the caller's to
// delete) says which slot each stored slot is, *count how many.
long
TStoreWrapper::FrameToMapReference(RefArg frame, Boolean skipHiddenSlots, long* count, long** indexes)
{
	long numTags = Length(frame);
	SortedMapTag stackTags[32];
	SortedMapTag* tags = stackTags;
	if (numTags > 32)
	{
		tags = new SortedMapTag[numTags];
		if (tags == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	Boolean sorted = !IsFunction(frame) && !FrameHasSlotRef(frame, RSSYM_implementor);
	GetFrameMapTags(frame, tags, sorted);
	long nameBytes = numTags;						// the terminators
	for (long i = 0; i < numTags; i++)
		nameBytes += strlen(SymbolName(tags[i].fTag));
	*indexes = new long[((nameBytes + 5) >> 2) + numTags];
	if (*indexes == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	*count = numTags;
	long reference = AddMap(tags, skipHiddenSlots, count, *indexes);
	if (tags != stackTags)
		delete[] tags;
	return reference;
}


// ROM 0x00328d18 ReferenceToMap__13TStoreWrapperFl
// The frame map a map reference names, from the cache of the last eight
// or built from the names in the map table.
Ref
TStoreWrapper::ReferenceToMap(long reference)
{
	RefVar map;
	long slot = -1;
	for (long i = 0; i < kMapCacheSize; i++)
		if (fMapCacheRefs[i] == reference)
		{
			map = GetArraySlotRef(fMapCache, i);
			slot = i;
		}
	if ((Ref) map == NILREF)
	{
		char stackBuffer[kStoreHashTableMaxEntry];
		char* buffer = stackBuffer;
		long size = kStoreHashTableMaxEntry;
		if (!fMapTable->Get(reference, buffer, &size))
		{
			buffer = new char[size];
			if (buffer == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			fMapTable->Get(reference, buffer, &size);
		}
		long count = LengthAt(buffer);
		char* name = buffer + 2;
		RefVar tags(AllocateArray(RSSYMarray, count));
		for (long i = 0; i < count; i++)
		{
			SetArraySlotRef(tags, i, Intern(name));
			name += strlen(name) + 1;
		}
		if (buffer != stackBuffer)
			delete[] buffer;
		map = AllocateMapWithTags(RefVar(NILREF), tags);
		if (slot == -1)
		{
			slot = fMapCacheNext++;
			if (fMapCacheNext == kMapCacheSize)
				fMapCacheNext = 0;
		}
		SetArraySlotRef(fMapCache, slot, map);
		fMapCacheRefs[slot] = reference;
	}
	return map;
}


// ROM 0x003297bc SymbolToReference__13TStoreWrapperFRC6RefVar
// The symbol entered in the symbol table (its name, in the bucket its
// hash selects); ==> the symbol reference.
long
TStoreWrapper::SymbolToReference(RefArg symbol)
{
	const char* name = SymbolName(symbol);
	return fSymbolTable->Insert(SymbolHash(symbol), (char*) name, strlen(name));
}


// ROM 0x0032980c ReferenceToSymbol__13TStoreWrapperFl
// The symbol a symbol reference names, from the cache of the last sixteen
// or interned from the symbol table.
Ref
TStoreWrapper::ReferenceToSymbol(long reference)
{
	RefVar symbol;
	long slot = -1;
	for (long i = 0; i < kSymbolCacheSize; i++)
		if (fSymbolCacheRefs[i] == reference)
		{
			symbol = GetArraySlotRef(fSymbolCache, i);
			slot = i;
		}
	if ((Ref) symbol == NILREF)
	{
		char name[256 + 1];
		long size = 256;
		fSymbolTable->Get(reference, name, &size);
		name[size] = 0;
		symbol = Intern(name);
		if (slot == -1)
		{
			slot = fSymbolCacheNext++;
			if (fSymbolCacheNext == kSymbolCacheSize)
				fSymbolCacheNext = 0;
		}
		SetArraySlotRef(fSymbolCache, slot, symbol);
		fSymbolCacheRefs[slot] = reference;
	}
	return symbol;
}


// ROM 0x00328f2c StartCopyMaps_Symbols__13TStoreWrapperFv
// The translation tables for copying another store's objects here.
void
TStoreWrapper::StartCopyMaps_Symbols(void)
{
	fCopyInfo = new StoreCopyInfo;
	if (fCopyInfo == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	for (long i = 0; i < 16; i++)
		fCopyInfo->fMaps[i].fFrom = -1;
	fCopyInfo->fNextSymbol = 0;
	for (long i = 0; i < 32; i++)
		fCopyInfo->fSymbols[i].fFrom = -1;
}


// ROM 0x00328fb0 EndCopyMaps_Symbols__13TStoreWrapperFv
void
TStoreWrapper::EndCopyMaps_Symbols(void)
{
	delete fCopyInfo;
	fCopyInfo = nil;
}


// ROM 0x00329594 CopyMap__13TStoreWrapperFlP13TStoreWrapperPl
// A map of the store from copied here (the names re-hashed and inserted);
// the last sixteen translations are remembered.  ==> the reference here;
// *count the number of names.
long
TStoreWrapper::CopyMap(long reference, TStoreWrapper* from, long* count)
{
	long freeSlot = -1;
	for (long i = 0; i < 16; i++)
	{
		if (fCopyInfo->fMaps[i].fFrom == reference)
		{
			*count = fCopyInfo->fMaps[i].fCount;
			return fCopyInfo->fMaps[i].fTo;
		}
		if (fCopyInfo->fMaps[i].fFrom == -1)
		{
			freeSlot = i;
			break;
		}
	}
	char stackBuffer[kStoreHashTableMaxEntry];
	char* buffer = stackBuffer;
	long size = kStoreHashTableMaxEntry;
	if (!from->fMapTable->Get(reference, buffer, &size))
	{
		buffer = new char[size];
		if (buffer == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		from->fMapTable->Get(reference, buffer, &size);
	}
	*count = LengthAt(buffer);
	const char* name = buffer + 2;
	ULong hash = 0;
	for (long i = 0; i < *count; i++)
	{
		hash = RotateInHash(hash, SymbolHashFunction(name), i);
		name += strlen(name) + 1;
	}
	long newReference = fMapTable->Insert(hash, buffer, size);
	if (buffer != stackBuffer)
		delete[] buffer;
	if (freeSlot >= 0)
	{
		fCopyInfo->fMaps[freeSlot].fFrom = reference;
		fCopyInfo->fMaps[freeSlot].fTo = newReference;
		fCopyInfo->fMaps[freeSlot].fCount = *count;
	}
	return newReference;
}


// ROM 0x00329704 CopySymbol__13TStoreWrapperFlP13TStoreWrapper
// A symbol of the store from entered here; the last 32 translations are
// remembered (round robin).
long
TStoreWrapper::CopySymbol(long reference, TStoreWrapper* from)
{
	for (long i = 0; i < 32; i++)
	{
		if (fCopyInfo->fSymbols[i].fFrom == reference)
			return fCopyInfo->fSymbols[i].fTo;
		if (fCopyInfo->fSymbols[i].fFrom == -1)
			break;
	}
	RefVar symbol(from->ReferenceToSymbol(reference));
	long newReference = SymbolToReference(symbol);
	long slot = fCopyInfo->fNextSymbol;
	fCopyInfo->fSymbols[slot].fFrom = reference;
	fCopyInfo->fSymbols[slot].fTo = newReference;
	if (++fCopyInfo->fNextSymbol == 32)
		fCopyInfo->fNextSymbol = 0;
	return newReference;
}


/* -------------------------------------------------------------------------------
	The root object
------------------------------------------------------------------------------- */

// ROM 0x00326dac ReadStoreRootData__FP6TStoreUlP13StoreRootDataPl
// The root object (rootId 0: the store's) read into data: *size becomes
// its size (-1 when it cannot be read); an old 0x14-byte root has fExtra 0.
void
ReadStoreRootData(TStore* store, PSSId rootId, StoreRootData* data, long* size)
{
	long localSize;
	if (size == nil)
		size = &localSize;
	*size = -1;
	if (rootId == 0 && store->GetRootId(&rootId) != noErr)
		return;
	if (store->GetObjectSize(rootId, size) != noErr)
		return;
	if (*size < kStoreRootDataSize)
		return;
	if (*size < (long) sizeof(StoreRootData))
		data->fExtra = 0;
	else
		*size = sizeof(StoreRootData);
	char bytes[sizeof(StoreRootData)];
	if (store->Read(rootId, 0, bytes, *size) != noErr)
		return;
	data->fSignature = GetBigEndianWord(bytes);
	data->fVersion = (Long32) GetBigEndianWord(bytes + 4);
	data->fMapTableId = GetBigEndianWord(bytes + 8);
	data->fSymbolTableId = GetBigEndianWord(bytes + 12);
	data->fRootFrameId = GetBigEndianWord(bytes + 16);
	if (*size >= (long) sizeof(StoreRootData))
		data->fExtra = GetBigEndianWord(bytes + 20);
}


// the root data as it lies on the store (the host's name for what
// MakeStoreObject does inline)
void
WriteStoreRootData(TStore* store, PSSId rootId, const StoreRootData* data)
{
	char bytes[kStoreRootDataSize];
	PutBigEndianWord(bytes, data->fSignature);
	PutBigEndianWord(bytes + 4, (ULong32) data->fVersion);
	PutBigEndianWord(bytes + 8, data->fMapTableId);
	PutBigEndianWord(bytes + 12, data->fSymbolTableId);
	PutBigEndianWord(bytes + 16, data->fRootFrameId);
	OSErrIf(store->ReplaceObject(rootId, bytes, kStoreRootDataSize));
}
