/*
	File:		stores/host/HostStore.cpp

	Contains:	THostStore (HostStore.h), the host's in-memory internal
				store.  A host re-expression of what TFlashStore promises;
				no ROM citations.

	Transactions.  Every change (NewObject, Write, SetObjectSize,
	DeleteObject, ReplaceObject) locks the store around itself, so a
	change made with no outer lock commits at once; changes made between
	an outer LockStore and its UnlockStore commit together when the count
	falls to zero, or are undone together by Abort (which also drops the
	locks).  An object's state before its first change in a transaction is
	journaled; Commit drops the journal, Undo puts it back.  An object in
	a "separate transaction" (NewWithinTransaction, StartTransactionAgainst)
	keeps its own journal, which the store-wide commit and abort ignore:
	SeparatelyAbort undoes it, AddToCurrentTransaction hands it to the
	store's transaction (committing it at once when nothing is locked).
*/

#include "HostStore.h"
#include "OSErrors.h"

#include <stdlib.h>
#include <string.h>


PROTOCOL_CLASSINFO(THostStore, "TStore", "", 0, 0, nil)

const long kHostStoreInitialCapacity = 64;


size_t
THostStore::Sizeof()
{
	return sizeof(THostStore);
}


THostStore*
THostStore::New()
{
	fObjects = nil;
	fCapacity = 0;
	fNextId = kHostStoreRootId + 1;
	fStoreSize = 0;
	fUsed = 0;
	fLockCount = 0;
	fInTransaction = false;
	fFormatted = false;
	fReadOnly = false;
	fReadOnlyLocks = 0;
	return this;
}


void
THostStore::Delete()
{
	Clear();
}


// every object gone, the table empty
void
THostStore::Clear()
{
	if (fObjects != nil)
	{
		for (long i = 0; i < fCapacity; i++)
		{
			free(fObjects[i].fData);
			free(fObjects[i].fUndoData);
		}
		free(fObjects);
		fObjects = nil;
	}
	fCapacity = 0;
	fNextId = kHostStoreRootId + 1;
	fUsed = 0;
	fLockCount = 0;
	fInTransaction = false;
	fFormatted = false;
}


// storeSize is the size the store answers; a store with kStoreIsCard in
// its flags and pssInfo nil is read-only (a write-protected card).
NewtonErr
THostStore::Init(void* /*storeAddress*/, ULong storeSize, ULong /*arg3*/, int /*socketNumber*/, ULong flags, void* pssInfo)
{
	fStoreSize = storeSize;
	fReadOnly = (flags & kStoreIsCard) != 0 && pssInfo == nil;
	return noErr;
}


NewtonErr
THostStore::NeedsFormat(Boolean* needsFormat)
{
	*needsFormat = !fFormatted;
	return noErr;
}


// Everything dropped; the (empty) root object made.
NewtonErr
THostStore::Format()
{
	if (fReadOnly)
		return kSError_WriteProtected;
	Clear();
	fFormatted = true;
	NewtonErr err = Grow(kHostStoreRootId);
	if (err != noErr)
		return err;
	SHostStoreObject* root = &fObjects[kHostStoreRootId];
	root->fExists = true;
	root->fData = nil;
	root->fSize = 0;
	return noErr;
}


NewtonErr
THostStore::GetRootId(PSSId* rootId)
{
	*rootId = kHostStoreRootId;
	return noErr;
}


SHostStoreObject*
THostStore::Object(PSSId id)
{
	if (id == 0 || (long) id >= fCapacity || !fObjects[id].fExists)
		return nil;
	return &fObjects[id];
}


NewtonErr
THostStore::Grow(PSSId id)
{
	if ((long) id < fCapacity)
		return noErr;
	long capacity = fCapacity == 0 ? kHostStoreInitialCapacity : fCapacity;
	while (capacity <= (long) id)
		capacity *= 2;
	SHostStoreObject* objects = (SHostStoreObject*) realloc(fObjects, capacity * sizeof(SHostStoreObject));
	if (objects == nil)
		return kSError_StoreFull;
	memset(objects + fCapacity, 0, (capacity - fCapacity) * sizeof(SHostStoreObject));
	fObjects = objects;
	fCapacity = capacity;
	return noErr;
}


// The checks a change makes: writable, formatted, the object there;
// the transaction started.
NewtonErr
THostStore::SetupForModify(PSSId id, SHostStoreObject** object)
{
	if (fReadOnly || fReadOnlyLocks > 0)
		return kSError_WriteProtected;
	if (!fFormatted)
		return kSError_NeedsFormat;
	if (object != nil)
	{
		*object = Object(id);
		if (*object == nil)
			return id != 0 && (long) id < fNextId ? kSError_ObjectNotFound : kSError_BadPSSID;
	}
	fInTransaction = true;
	return noErr;
}


NewtonErr
THostStore::Journal(SHostStoreObject* object)
{
	if (object->fJournaled)
		return noErr;
	object->fUndoExists = object->fExists;
	object->fUndoSize = object->fSize;
	object->fUndoData = nil;
	if (object->fExists && object->fSize > 0)
	{
		object->fUndoData = (char*) malloc(object->fSize);
		if (object->fUndoData == nil)
			return kSError_StoreFull;
		memcpy(object->fUndoData, object->fData, object->fSize);
	}
	object->fJournaled = true;
	return noErr;
}


void
THostStore::DropJournal(SHostStoreObject* object)
{
	free(object->fUndoData);
	object->fUndoData = nil;
	object->fUndoSize = 0;
	object->fUndoExists = false;
	object->fJournaled = false;
}


void
THostStore::Restore(SHostStoreObject* object)
{
	if (object->fExists)
		fUsed -= object->fSize;
	free(object->fData);
	object->fData = object->fUndoData;
	object->fSize = object->fUndoSize;
	object->fExists = object->fUndoExists;
	if (object->fExists)
		fUsed += object->fSize;
	object->fUndoData = nil;
	object->fUndoSize = 0;
	object->fUndoExists = false;
	object->fJournaled = false;
	object->fSeparate = false;
}


// a deleted object's memory given back
void
THostStore::Forget(SHostStoreObject* object)
{
	if (!object->fExists)
	{
		free(object->fData);
		object->fData = nil;
		object->fSize = 0;
	}
}


void
THostStore::Commit(Boolean separateToo)
{
	for (long i = 0; i < fCapacity; i++)
	{
		SHostStoreObject* object = &fObjects[i];
		if (object->fJournaled && (separateToo || !object->fSeparate))
		{
			DropJournal(object);
			Forget(object);
		}
	}
}


void
THostStore::Undo(Boolean separateToo)
{
	for (long i = 0; i < fCapacity; i++)
	{
		SHostStoreObject* object = &fObjects[i];
		if (object->fJournaled && (separateToo || !object->fSeparate))
		{
			Restore(object);
			Forget(object);
		}
	}
}


NewtonErr
THostStore::Create(long size, char* data, Boolean separate, PSSId* id)
{
	Boolean noSlopCheck = (size & kStoreNoSlopCheck) != 0;
	size &= ~kStoreNoSlopCheck;
	if (id != nil)
		*id = 0;
	NewtonErr err = SetupForModify(0, nil);
	if (err != noErr)
		return err;
	if (size < 0 || (ULong) size > fStoreSize)
		return kSError_ObjectTooBig;
	if (!noSlopCheck && fUsed + (ULong) size > fStoreSize)
		return kSError_StoreFull;
	err = Grow(fNextId);
	if (err != noErr)
		return err;
	SHostStoreObject* object = &fObjects[fNextId];
	char* bytes = nil;
	if (size > 0)
	{
		bytes = (char*) malloc(size);
		if (bytes == nil)
			return kSError_StoreFull;
		if (data != nil)
			memcpy(bytes, data, size);
		else
			memset(bytes, 0, size);
	}
	object->fExists = false;
	object->fJournaled = false;
	object->fSeparate = separate;
	Journal(object);						// the undo: it did not exist
	object->fExists = true;
	object->fData = bytes;
	object->fSize = size;
	fUsed += size;
	if (id != nil)
		*id = fNextId;
	fNextId++;
	return noErr;
}


NewtonErr
THostStore::NewObject(long size, PSSId* id)
{
	LockStore();
	NewtonErr err = Create(size, nil, false, id);
	UnlockStore();
	return err;
}


NewtonErr
THostStore::NewObject(char* data, long size, PSSId* id)
{
	LockStore();
	NewtonErr err = Create(size, data, false, id);
	UnlockStore();
	return err;
}


// (the ROM's flash store has no EraseObject either: it answers noErr)
NewtonErr
THostStore::EraseObject(PSSId /*id*/)
{
	return noErr;
}


NewtonErr
THostStore::DeleteObject(PSSId id)
{
	LockStore();
	SHostStoreObject* object;
	NewtonErr err = SetupForModify(id, &object);
	if (err == noErr)
		err = Journal(object);
	if (err == noErr)
	{
		object->fExists = false;
		fUsed -= object->fSize;
	}
	UnlockStore();
	return err;
}


// The object grown (zero-filled) or cut.
NewtonErr
THostStore::SetObjectSize(PSSId id, long size)
{
	Boolean noSlopCheck = (id & kStoreNoSlopCheck) != 0;
	id &= ~(PSSId) kStoreNoSlopCheck;
	LockStore();
	SHostStoreObject* object;
	NewtonErr err = SetupForModify(id, &object);
	if (err == noErr && size != object->fSize)
	{
		if (size < 0 || (ULong) size > fStoreSize)
			err = kSError_ObjectTooBig;
		else if (!noSlopCheck && size > object->fSize && fUsed + (ULong) (size - object->fSize) > fStoreSize)
			err = kSError_StoreFull;
		else
			err = Journal(object);
		if (err == noErr)
		{
			char* bytes = nil;
			if (size > 0)
			{
				bytes = (char*) malloc(size);
				if (bytes == nil)
					err = kSError_StoreFull;
				else
				{
					long keep = size < object->fSize ? size : object->fSize;
					memcpy(bytes, object->fData, keep);
					if (size > keep)
						memset(bytes + keep, 0, size - keep);
				}
			}
			if (err == noErr)
			{
				// the old bytes may be the journal's: Journal copied them, so they can go
				free(object->fData);
				object->fData = bytes;
				fUsed += size - object->fSize;
				object->fSize = size;
			}
		}
	}
	UnlockStore();
	return err;
}


NewtonErr
THostStore::GetObjectSize(PSSId id, long* size)
{
	*size = 0;
	SHostStoreObject* object = Object(id);
	if (object == nil)
		return id != 0 && (long) id < fNextId ? kSError_ObjectNotFound : kSError_BadPSSID;
	*size = object->fSize;
	return noErr;
}


NewtonErr
THostStore::Write(PSSId id, long offset, char* buffer, long count)
{
	LockStore();
	SHostStoreObject* object;
	NewtonErr err = SetupForModify(id, &object);
	if (err == noErr && (offset < 0 || count < 0 || offset + count > object->fSize))
		err = kSError_ObjectOverRun;
	if (err == noErr)
		err = Journal(object);
	if (err == noErr && count > 0)
		memcpy(object->fData + offset, buffer, count);
	UnlockStore();
	return err;
}


// What there is of a range past the end is copied and
// kSError_ObjectOverRun answered (as TPackageStore reads).
NewtonErr
THostStore::Read(PSSId id, long offset, char* buffer, long count)
{
	SHostStoreObject* object = Object(id);
	if (object == nil)
		return id != 0 && (long) id < fNextId ? kSError_ObjectNotFound : kSError_BadPSSID;
	NewtonErr err = kSError_ObjectOverRun;
	if (offset >= 0 && offset < object->fSize)
	{
		if (offset + count <= object->fSize)
			err = noErr;
		else
			count = object->fSize - offset;
		if (count > 0)
			memcpy(buffer, object->fData + offset, count);
	}
	else if (offset == object->fSize && count == 0)
		err = noErr;
	return err;
}


NewtonErr
THostStore::GetStoreSizes(long* totalSize, long* usedSize)
{
	*totalSize = (long) fStoreSize;
	*usedSize = (long) fUsed;
	return noErr;
}


NewtonErr
THostStore::IsReadOnly(Boolean* isReadOnly)
{
	*isReadOnly = fReadOnly || fReadOnlyLocks > 0;
	return noErr;
}


NewtonErr
THostStore::LockStore()
{
	fLockCount++;
	return noErr;
}


// The last unlock commits.
NewtonErr
THostStore::UnlockStore()
{
	if (fLockCount == 1 && fInTransaction)
	{
		Commit(false);
		fInTransaction = false;
	}
	if (fLockCount > 0)
		fLockCount--;
	return noErr;
}


NewtonErr
THostStore::Abort()
{
	if (fInTransaction)
		Undo(false);
	fInTransaction = false;
	fLockCount = 0;
	return noErr;
}


NewtonErr
THostStore::Idle(Boolean* arg1, Boolean* arg2)
{
	*arg1 = false;
	*arg2 = false;
	return noErr;
}


// The next existing id after id (0 to start; 0 when there are no more).
NewtonErr
THostStore::NextObject(PSSId id, PSSId* nextId)
{
	*nextId = 0;
	for (long i = (long) id + 1; i < fCapacity; i++)
		if (fObjects[i].fExists)
		{
			*nextId = (PSSId) i;
			break;
		}
	return noErr;
}


NewtonErr
THostStore::CheckIntegrity(ULong* /*arg*/)
{
	return noErr;
}


NewtonErr
THostStore::SetBuddy(TStore* /*buddy*/)
{
	return noErr;
}


Boolean
THostStore::OwnsObject(PSSId id)
{
	return Object(id) != nil;
}


// The object's bytes, valid until it changes.
void*
THostStore::Address(PSSId id)
{
	SHostStoreObject* object = Object(id);
	return object != nil ? object->fData : nil;
}


const char*
THostStore::StoreKind()
{
	return "Internal";
}


NewtonErr
THostStore::SetStore(TStore* /*store*/, ULong /*arg*/)
{
	return noErr;
}


Boolean
THostStore::IsSameStore(void* /*data*/, ULong /*size*/)
{
	return false;
}


Boolean
THostStore::IsLocked()
{
	return fLockCount != 0;
}


NewtonErr
THostStore::VppOff()
{
	return noErr;
}


NewtonErr
THostStore::Sleep()
{
	return noErr;
}


Boolean
THostStore::IsROM()
{
	return false;
}


// A new object in its own transaction.
NewtonErr
THostStore::NewWithinTransaction(long size, PSSId* id)
{
	return Create(size, nil, true, id);
}


// The object put in its own transaction, from its state now.
NewtonErr
THostStore::StartTransactionAgainst(PSSId id)
{
	SHostStoreObject* object;
	NewtonErr err = SetupForModify(id, &object);
	if (err != noErr)
		return err;
	if (object->fSeparate)
		return noErr;
	if (object->fJournaled)
	{
		// its changes so far are the store's transaction's: they stay with it
		DropJournal(object);
	}
	err = Journal(object);
	if (err == noErr)
		object->fSeparate = true;
	return err;
}


// The object's own transaction undone.
NewtonErr
THostStore::SeparatelyAbort(PSSId id)
{
	if (id == 0 || (long) id >= fCapacity)
		return kSError_BadPSSID;
	SHostStoreObject* object = &fObjects[id];
	if (!object->fSeparate)
		return noErr;
	if (object->fJournaled)
		Restore(object);
	object->fSeparate = false;
	Forget(object);
	return noErr;
}


// The object's own transaction joins the store's (and commits at once
// when nothing holds the store locked).
NewtonErr
THostStore::AddToCurrentTransaction(PSSId id)
{
	LockStore();
	SHostStoreObject* object;
	NewtonErr err = SetupForModify(id, &object);
	if (err == noErr)
		object->fSeparate = false;
	UnlockStore();
	return err;
}


Boolean
THostStore::InSeparateTransaction(PSSId id)
{
	SHostStoreObject* object = Object(id);
	return object != nil && object->fSeparate;
}


NewtonErr
THostStore::LockReadOnly()
{
	fReadOnlyLocks++;
	return noErr;
}


NewtonErr
THostStore::UnlockReadOnly(Boolean reset)
{
	if (reset)
		fReadOnlyLocks = 0;
	else if (fReadOnlyLocks > 0)
		fReadOnlyLocks--;
	return noErr;
}


Boolean
THostStore::InTransaction()
{
	return fInTransaction;
}


// The object's contents replaced (its size becomes size).
NewtonErr
THostStore::ReplaceObject(PSSId id, char* data, long size)
{
	LockStore();
	NewtonErr err = SetObjectSize(id, size);
	if (err == noErr && size > 0)
		err = Write(id, 0, data, size);
	UnlockStore();
	return err;
}


NewtonErr
THostStore::CalcXIPObjectSize(long /*arg1*/, long /*arg2*/, long* /*size*/)
{
	return kError_XIP_Not_Possible;
}


NewtonErr
THostStore::NewXIPObject(long /*size*/, PSSId* /*id*/)
{
	return kError_XIP_Not_Possible;
}


NewtonErr
THostStore::GetXIPObjectInfo(PSSId /*id*/, ULong* /*arg1*/, ULong* /*arg2*/, ULong* /*arg3*/)
{
	return kError_XIP_Not_Possible;
}


long
THostStore::NumObjects()
{
	long count = 0;
	for (long i = 0; i < fCapacity; i++)
		if (fObjects[i].fExists)
			count++;
	return count;
}
