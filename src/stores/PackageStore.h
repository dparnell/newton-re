/*
	File:		stores/PackageStore.h

	Contains:	TPackageStore, a read-only TStore over the data of a
				package's soup part: a directory of object offsets followed
				by the objects themselves.  Every modification answers
				kSError_WriteProtected; transactions are no-ops (the lock
				count is kept so that IsLocked/InTransaction answer).

	The ROM's layout (0x1c, after TProtocol's 0x10).
*/

#ifndef __PACKAGESTORE_H
#define __PACKAGESTORE_H

#ifndef __STORE_H
#include "Store.h"
#endif

// The data a package store is initialised over (big-endian words, as a
// package lies): the root object's id,
// the number of objects, then one offset (from the start of this
// structure) per object and one more for the end of the last, so that
// object i is the bytes [fOffsets[i], fOffsets[i + 1]).
struct SPackageStoreData
{
	StorePSSId	fRootId;			// +0x00
	ULong32		fNumObjects;		// +0x04
	ULong32		fOffsets[1];		// +0x08  fNumObjects + 1 of them
};


PROTOCOL TPackageStore : public TStore
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TPackageStore);

	TPackageStore*	New();
	void			Delete();

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

	SPackageStoreData*	fData;		// +0x10
	ULong		fSize;				// +0x14
	long		fLockCount;			// +0x18
};


/*------------------------------------------------------------------------------
	T P a c k a g e S t o r e P a r t H a n d l e r

	The 'soup part handler: a package's soup part mounted as a
	TPackageStore over the part where it lies, its store frame added to
	gPackageStores (which GetPackageStores answers and the soups look
	through); the store is the part's remove object, and removing the part
	takes the frame out of the list and kills it.  Only a part in memory
	can be mounted.  InitPackageSoups (the tail of InitQueries) makes the
	list and registers the handler in the world that runs it.
	(0x40 bytes: a TPartHandler.)
------------------------------------------------------------------------------*/

#ifndef __PARTHANDLER_H
#include "PartHandler.h"
#endif

class TPackageStorePartHandler : public TPartHandler
{
public:
					TPackageStorePartHandler();

	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);
};

NewtonErr	InitPackageSoups(void);

#endif	/* __PACKAGESTORE_H */
