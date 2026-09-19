/*
	File:		stores/host/HostStore.h

	Contains:	THostStore, the host's internal store: a TStore kept in host
				memory with the ROM's transaction semantics.  It stands where
				the ROM has TFlashStore over the internal flash (or the
				battery-backed RAM): the objects, their ids, the lock count
				that commits when it falls to zero, Abort, the separate
				transactions, the read-only lock, NextObject, and the
				sizes; without the flash's log, blocks, erasure and
				compaction, which belong to the hardware.

	This is a host re-expression, not a reconstruction: no function here
	cites the ROM.  TStore's contract is the one TFlashStore keeps
	(Store.h; docs/stores/README.md).
*/

#ifndef __HOSTSTORE_H
#define __HOSTSTORE_H

#ifndef __STORE_H
#include "Store.h"
#endif

const PSSId kHostStoreRootId = 0x27;		// the root object's id (TFlashStore's, so ids look alike)

// an object of the store, with the state to go back to if its transaction aborts
struct SHostStoreObject
{
	char*		fData;
	long		fSize;
	Boolean		fExists;
	Boolean		fSeparate;			// in its own transaction, not the store's
	Boolean		fJournaled;			// fUndo... holds its state before the transaction
	Boolean		fUndoExists;
	char*		fUndoData;
	long		fUndoSize;
};


PROTOCOL THostStore : public TStore
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostStore);

	THostStore*	New();
	void		Delete();

	NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo);
	NewtonErr	NeedsFormat(Boolean* needsFormat);
	NewtonErr	Format();
	NewtonErr	GetRootId(PSSId* rootId);
	NewtonErr	NewObject(long size, PSSId* id);
	NewtonErr	EraseObject(PSSId id);
	NewtonErr	DeleteObject(PSSId id);
	NewtonErr	SetObjectSize(PSSId id, long size);
	NewtonErr	GetObjectSize(PSSId id, long* size);
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count);
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count);
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize);
	NewtonErr	IsReadOnly(Boolean* isReadOnly);
	NewtonErr	LockStore();
	NewtonErr	UnlockStore();
	NewtonErr	Abort();
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2);
	NewtonErr	NextObject(PSSId id, PSSId* nextId);
	NewtonErr	CheckIntegrity(ULong* arg);
	NewtonErr	SetBuddy(TStore* buddy);
	Boolean		OwnsObject(PSSId id);
	void*		Address(PSSId id);
	const char*	StoreKind();
	NewtonErr	SetStore(TStore* store, ULong arg);
	Boolean		IsSameStore(void* data, ULong size);
	Boolean		IsLocked();
	NewtonErr	VppOff();
	NewtonErr	Sleep();
	Boolean		IsROM();
	NewtonErr	NewWithinTransaction(long size, PSSId* id);
	NewtonErr	StartTransactionAgainst(PSSId id);
	NewtonErr	SeparatelyAbort(PSSId id);
	NewtonErr	AddToCurrentTransaction(PSSId id);
	Boolean		InSeparateTransaction(PSSId id);
	NewtonErr	LockReadOnly();
	NewtonErr	UnlockReadOnly(Boolean reset);
	Boolean		InTransaction();
	NewtonErr	NewObject(char* data, long size, PSSId* id);
	NewtonErr	ReplaceObject(PSSId id, char* data, long size);
	NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size);
	NewtonErr	NewXIPObject(long size, PSSId* id);
	NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3);

	// the host's own
	long		NumObjects();			// existing objects, the root included

	// The file the store is kept in between runs, which is what the
	// flash is on the machine.  Setting it reads the file in when there is
	// one (and answers whether there was); from then on every commit
	// writes it out again, so what is on the disk is what the last
	// finished transaction left.  Without a file the store is memory only,
	// as it was.
	Boolean		SetBackingFile(const char* path);	// ==> whether a store was read in
	NewtonErr	Save();					// written out now, whatever the transaction is doing

private:
	void		Clear();									// every object gone
	SHostStoreObject*	Object(PSSId id);					// nil when there is none
	NewtonErr	SetupForModify(PSSId id, SHostStoreObject** object);	// the checks every change makes; starts the transaction
	NewtonErr	Journal(SHostStoreObject* object);			// its state saved before its first change in a transaction
	NewtonErr	Grow(PSSId id);								// the table holds id
	NewtonErr	Create(long size, char* data, Boolean separate, PSSId* id);
	void		Commit(Boolean separateToo);				// the journal dropped
	void		Undo(Boolean separateToo);					// the journal applied
	void		Restore(SHostStoreObject* object);
	void		DropJournal(SHostStoreObject* object);
	void		Forget(SHostStoreObject* object);

	SHostStoreObject*	fObjects;		// indexed by id
	long		fCapacity;				// ids the table holds
	PSSId		fNextId;				// the next id to hand out
	ULong		fStoreSize;				// what Init was told; the sizes answered
	ULong		fUsed;					// the objects' bytes
	long		fLockCount;
	Boolean		fInTransaction;
	Boolean		fFormatted;
	Boolean		fReadOnly;				// Init's flags said so
	long		fReadOnlyLocks;			// LockReadOnly's count
	char*		fBackingFile;			// the file it is kept in, nil for memory only
	Boolean		fLoading;				// (no saving while the file is being read back)
};

#endif	/* __HOSTSTORE_H */
