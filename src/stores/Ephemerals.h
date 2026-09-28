/*
	File:		stores/Ephemerals.h

	Contains:	TEphemeralTracker: the large objects of a store that no
				entry has taken yet.

				A large binary is made on a store (LBAllocCompressed,
				WrapLargeObject, a duplicate) before any soup entry refers
				to it.  If the entry is never written - the binary dropped,
				the change undone, the machine reset - the object would be
				left on the store with nothing pointing at it.  So every one
				made is an *ephemeral* until an entry that holds it is
				written (CommitLargeBinary takes it off), and the ids of the
				ones still ephemeral are kept in a store object of their own
				(the store frame's `ephemerals` slot names it: a list of
				big-endian object ids), which is read back when the store is
				mounted and whatever is on it deleted (SetupEphemeralTracker,
				DeleteAllEphemerals).

				Four lists: fNew, made since the store was last locked;
				fList, the list as it is on the store (FlushEphemerals
				merges fNew into it and writes it when the store is
				unlocked); fCommitted, taken off since the last lock (an
				abort puts their entries' links back to nil,
				BreakLargeObjectToEntryLink); fPending, ones to delete once
				the store is out of its transaction.  The store wrapper
				drives it: LockStore/Dirty lock, UnlockStore/SparklingClean
				flush and delete what is pending, Abort aborts.

				The ROM's object is 0x1c bytes.

	Reconstructed from the MP2x00 US ROM (0x002db388-0x002dbd78); each
	function cites its origin.
*/

#ifndef __EPHEMERALS_H
#define __EPHEMERALS_H

#ifndef __STOREWRAPPER_H
#include "StoreWrapper.h"
#endif

class CDynamicArray;


class TEphemeralTracker
{
public:
				TEphemeralTracker();
				~TEphemeralTracker();

	NewtonErr	Init(TStoreWrapper* wrapper, PSSId listId);
	NewtonErr	LockEphemerals(void);
	NewtonErr	FlushEphemerals(void);
	NewtonErr	AbortEphemerals(void);
	void		AddEphemeral(PSSId id);
	Boolean		IsEphemeral(PSSId id);
	void		RemoveEphemeral(PSSId id);
	NewtonErr	DeleteEphemeral1(PSSId id);
	NewtonErr	DeleteAllEphemerals(void);
	NewtonErr	DeletePendingEphemerals(void);
	NewtonErr	ReadEphemeralList(void);
	NewtonErr	WriteEphemeralList(void);

	static long		Find(PSSId id, CDynamicArray* list);
	static Boolean	FindAndRemove(PSSId id, CDynamicArray* list);

	ULong		PendingCount(void) const;

	TStoreWrapper*	fWrapper;			// +0x00
	TStore*			fStore;				// +0x04
	PSSId			fListId;			// +0x08  the store object the list is kept in
	CDynamicArray*	fNew;				// +0x0c  made since the last lock
	CDynamicArray*	fCommitted;			// +0x10  taken off since the last lock
	CDynamicArray*	fList;				// +0x14  as on the store
	CDynamicArray*	fPending;			// +0x18  to delete when the store is free
};

NewtonErr	EnsureStoreHasEphemeralTracker(RefArg storeObject, PSSId rootFrameId);	// ROM 0x002db7f0 EnsureStoreHasEphemeralTracker__FRC6RefVarUl

#endif	/* __EPHEMERALS_H */
