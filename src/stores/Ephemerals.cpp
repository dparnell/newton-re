/*
	File:		stores/Ephemerals.cpp

	Contains:	TEphemeralTracker, SetupEphemeralTracker and
				EnsureStoreHasEphemeralTracker (Ephemerals.h).

	The list of ephemerals is kept on the store as the ROM keeps it - an
	array of 4-byte object ids, which on the MessagePad are big-endian
	because that is how the processor lays them out; the host swaps them on
	the way in and out.

	Reconstructed from the MP2x00 US ROM (0x002db388-0x002dbd78); each
	function cites its origin.
*/

#include "Ephemerals.h"
#include "Entries.h"			// StoreWritable, CanCreateLargeObjectsOnStore
#include "LargeObjects.h"		// DeleteLargeObject
#include "LargeBinaries.h"		// BreakLargeObjectToEntryLink
#include "StoreObject.h"		// StorePermObject
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "DynamicArray.h"
#include "ByteOrder.h"

#include <string.h>


// ROM 0x002db4c0 __ct__17TEphemeralTrackerFv
TEphemeralTracker::TEphemeralTracker()
{
	fNew = nil;
	fList = nil;
	fPending = nil;
	fCommitted = nil;
}


// ROM 0x002dba3c __dt__17TEphemeralTrackerFv
TEphemeralTracker::~TEphemeralTracker()
{
	if (fNew != nil)
		delete fNew;
	if (fList != nil)
		delete fList;
	if (fPending != nil)
		delete fPending;
	if (fCommitted != nil)
		delete fCommitted;
}


// ROM 0x002dbae4 Init__17TEphemeralTrackerFP13TStoreWrapperUl
// The tracker of wrapper's store, its list kept in store object listId
// (and read from it now).  The lists hold 4-byte ids (the ROM's element
// size), which a PSSId always fits.
NewtonErr
TEphemeralTracker::Init(TStoreWrapper* wrapper, PSSId listId)
{
	fWrapper = wrapper;
	fStore = wrapper->fStore;
	fListId = listId;
	fNew = new CDynamicArray(sizeof(StorePSSId), 4);
	fList = new CDynamicArray(sizeof(StorePSSId), 4);
	fPending = new CDynamicArray(sizeof(StorePSSId), 4);
	fCommitted = new CDynamicArray(sizeof(StorePSSId), 4);
	ReadEphemeralList();
	return noErr;
}


ULong
TEphemeralTracker::PendingCount(void) const
{
	return fPending->GetArraySize();
}


// ROM 0x002db754 Find__17TEphemeralTrackerSFUlP13CDynamicArray
// id's index in list, -1 when it is not there.
long
TEphemeralTracker::Find(PSSId id, CDynamicArray* list)
{
	for (ArrayIndex i = 0; i < list->GetArraySize(); i++)
		if (*(StorePSSId*) list->SafeElementPtrAt(i) == (StorePSSId) id)
			return (long) i;
	return -1;
}


// ROM 0x002db7ac FindAndRemove__17TEphemeralTrackerSFUlP13CDynamicArray
// ==> whether id was in list (and is not now).
Boolean
TEphemeralTracker::FindAndRemove(PSSId id, CDynamicArray* list)
{
	long index = Find(id, list);
	if (index != -1)
	{
		list->RemoveElementsAt(index, 1);
		return true;
	}
	return false;
}


// ROM 0x002db6d4 ReadEphemeralList__17TEphemeralTrackerFv
// fList read from the store (its words big-endian there).
NewtonErr
TEphemeralTracker::ReadEphemeralList(void)
{
	long size = 0;
	fStore->GetObjectSize(fListId, &size);
	ULong count = (ULong) size >> 2;
	fList->SetElementCount(count);
	NewtonErr err = noErr;
	if (count != 0)
	{
		StorePSSId* ids = (StorePSSId*) fList->ElementPtrAt(0);
		err = fStore->Read(fListId, 0, (char*) ids, (long) (count << 2));
		for (ULong i = 0; i < count; i++)
			ids[i] = GetBigEndianWord((const UByte*) &ids[i]);
	}
	return err;
}


// ROM 0x002db730 WriteEphemeralList__17TEphemeralTrackerFv
// fList written to the store object in place of what was there.  (The
// words go big-endian, the MessagePad's order.)
NewtonErr
TEphemeralTracker::WriteEphemeralList(void)
{
	ULong count = fList->GetArraySize();
	UByte* words = count != 0 ? new UByte[count << 2] : nil;
	for (ULong i = 0; i < count; i++)
		PutBigEndianWord(words + (i << 2), *(StorePSSId*) fList->ElementPtrAt(i));
	NewtonErr err = fStore->ReplaceObject(fListId, (char*) words, (long) (count << 2));
	delete[] words;
	return err;
}


// ROM 0x002dbb40 LockEphemerals__17TEphemeralTrackerFv
// A change to the store begins: what is waiting to be deleted is (unless
// the store is locked already - ==> true then), and the lists of what was
// made and taken since the last lock are started afresh.
NewtonErr
TEphemeralTracker::LockEphemerals(void)
{
	Boolean locked = fWrapper->fStore->IsLocked();
	if (locked)
		return (NewtonErr) locked;
	if (fPending->GetArraySize() != 0)
		DeletePendingEphemerals();
	fNew->RemoveAll();
	return fCommitted->RemoveAll();
}


// ROM 0x002dbb9c FlushEphemerals__17TEphemeralTrackerFv
// The change is over: what was made is added to the list on the store
// (when anything was made or taken since the lock), and the list of what
// was taken started afresh.  A failure to write aborts and is thrown.
NewtonErr
TEphemeralTracker::FlushEphemerals(void)
{
	long count = (long) fNew->GetArraySize();
	if (count < 1)
		count = (long) fCommitted->GetArraySize();
	if (count < 1)
		return (NewtonErr) count;
	fList->Merge(fNew);
	NewtonErr err = WriteEphemeralList();
	fNew->RemoveAll();
	if (err != noErr)
	{
		AbortEphemerals();
		ThrowOSErr(err);
	}
	return fCommitted->RemoveAll();
}


// ROM 0x002dbc1c AbortEphemerals__17TEphemeralTrackerFv
// The change is undone: what was made is forgotten, the list read back as
// the store has it, and each large binary taken since the lock loses its
// entry again.
NewtonErr
TEphemeralTracker::AbortEphemerals(void)
{
	fNew->RemoveAll();
	ReadEphemeralList();
	for (ArrayIndex i = 0; i < fCommitted->GetArraySize(); i++)
		BreakLargeObjectToEntryLink(*(StorePSSId*) fCommitted->SafeElementPtrAt(i), fWrapper);
	return fCommitted->RemoveAll();
}


// ROM 0x002dbc98 AddEphemeral__17TEphemeralTrackerFUl
// id made, and so ephemeral until an entry takes it.
void
TEphemeralTracker::AddEphemeral(PSSId id)
{
	if (Find(id, fNew) != -1)
		return;
	StorePSSId word = (StorePSSId) id;
	fNew->InsertElementsBefore(fNew->GetArraySize(), &word, 1);
}


// ROM 0x002dbcd8 IsEphemeral__17TEphemeralTrackerFUl
Boolean
TEphemeralTracker::IsEphemeral(PSSId id)
{
	return Find(id, fNew) != -1 || Find(id, fList) != -1 || Find(id, fPending) != -1;
}


// ROM 0x002dbd34 RemoveEphemeral__17TEphemeralTrackerFUl
// id taken by an entry: off the lists, and noted as taken so that an abort
// can undo it.
void
TEphemeralTracker::RemoveEphemeral(PSSId id)
{
	Boolean found = FindAndRemove(id, fNew);
	if (!found)
		found = FindAndRemove(id, fList);
	if (!found)
		return;
	StorePSSId word = (StorePSSId) id;
	fCommitted->InsertElementsBefore(fCommitted->GetArraySize(), &word, 1);
}


// ROM 0x002db500 DeleteEphemeral1__17TEphemeralTrackerFUl
// The large object deleted from the store and off the lists - at once
// when the store is writable and out of a transaction (the store aborted
// if that fails for any reason but the object's being gone already),
// otherwise put off: added to the pending list.
NewtonErr
TEphemeralTracker::DeleteEphemeral1(PSSId id)
{
	NewtonErr err = noErr;
	if (!fStore->InTransaction() && StoreWritable(fStore))
	{
		fStore->LockStore();
		err = DeleteLargeObject(fStore, id);
		if (err != noErr && err != kSError_ObjectNotFound)
			fStore->Abort();
		else
		{
			Boolean found = FindAndRemove(id, fNew);
			if (!found)
				found = FindAndRemove(id, fList);
			if (!found)
				FindAndRemove(id, fPending);
			err = WriteEphemeralList();
			fStore->UnlockStore();
		}
	}
	else
	{
		StorePSSId word = (StorePSSId) id;
		fPending->InsertElementsBefore(fPending->GetArraySize(), &word, 1);
		if (!FindAndRemove(id, fNew))
			FindAndRemove(id, fList);
	}
	return err;
}


// ROM 0x002db614 DeleteAllEphemerals__17TEphemeralTrackerFv
// Every large object on the list deleted - what was left ephemeral when
// the store was last used (the store is being mounted).
NewtonErr
TEphemeralTracker::DeleteAllEphemerals(void)
{
	while (fList->GetArraySize() > 0)
	{
		NewtonErr err = DeleteEphemeral1(*(StorePSSId*) fList->SafeElementPtrAt(0));
		if (err != noErr)
			return err;
	}
	return noErr;
}


// ROM 0x002db66c DeletePendingEphemerals__17TEphemeralTrackerFv
// What was put off deleted now (when the store is writable).
//
// ROM QUIRK kept: were the store in a transaction still, DeleteEphemeral1
// would put the id back on the pending list and this would never end;
// its callers only ask once the store is unlocked.
NewtonErr
TEphemeralTracker::DeletePendingEphemerals(void)
{
	NewtonErr err = noErr;
	if (StoreWritable(fStore))
	{
		while (fPending->GetArraySize() > 0)
			err = DeleteEphemeral1(*(StorePSSId*) fPending->SafeElementPtrAt(0));
	}
	return err;
}


/*------------------------------------------------------------------------------
	S e t t i n g   u p
------------------------------------------------------------------------------*/

// ROM 0x002db388 SetupEphemeralTracker__FRC6RefVarUl
// The store's tracker made as the store is mounted: its list is the store
// object the persistent frame's `ephemerals` slot names (made now, and the
// frame written, when there is none), and whatever it still lists -
// large objects no entry ever took - is deleted.  A store that cannot
// hold large objects has none (kNSErrNoLargeObjectsOnStore).
NewtonErr
SetupEphemeralTracker(RefArg storeObject, PSSId rootFrameId)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	if (!CanCreateLargeObjectsOnStore(wrapper->fStore))
		return kNSErrNoLargeObjectsOnStore;
	RefVar listId(GetFrameSlotRef(persistent, RSSYMephemerals));
	if (ISNIL(listId))
	{
		NewtonErr err = EnsureStoreHasEphemeralTracker(storeObject, rootFrameId);
		if (err != noErr)
			return err;
		listId = GetFrameSlotRef(persistent, RSSYMephemerals);
	}
	TEphemeralTracker* tracker = new TEphemeralTracker;
	tracker->Init(wrapper, (PSSId) RINT(listId));
	wrapper->fEphemeralTracker = tracker;
	tracker->DeleteAllEphemerals();
	return noErr;
}


// ROM 0x002db7f0 EnsureStoreHasEphemeralTracker__FRC6RefVarUl
// The store's persistent frame given an `ephemerals` slot - an empty store
// object for the list - and written back (as object rootFrameId), unless
// it has one already.  ==> kSError_WriteProtected on a store that cannot
// be written, kNSErrNoLargeObjectsOnStore on one that cannot hold large
// objects.
NewtonErr
EnsureStoreHasEphemeralTracker(RefArg storeObject, PSSId rootFrameId)
{
	NewtonErr err = noErr;
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	if (NOTNIL(GetFrameSlotRef(persistent, RSSYMephemerals)))
		return noErr;
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	TStore* store = wrapper->fStore;
	if (!StoreWritable(store))
		return kSError_WriteProtected;
	if (!CanCreateLargeObjectsOnStore(store))
		return kNSErrNoLargeObjectsOnStore;
	err = store->LockStore();
	if (err == noErr)
	{
		newton_try
		{
			PSSId listId;
			err = store->NewObject(0, &listId);
			if (err == noErr)
			{
				SetFrameSlot(persistent, RSSYMephemerals, RefVar(MAKEINT((long) listId)));
				StorePermObject(persistent, wrapper, rootFrameId, nil, nil);
			}
		}
		newton_catch_all
		{
			SetFrameSlot(persistent, RSSYMephemerals, RefVar(NILREF));
			store->Abort();
			rethrow;
		}
		end_try;
		store->UnlockStore();
	}
	return err;
}
