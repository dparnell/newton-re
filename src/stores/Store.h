/*
	File:		stores/Store.h

	Contains:	TStore, the persistent object store protocol - the PSS
				(persistent store system) layer beneath the frames stores and
				soups: objects of bytes addressed by a store-wide id (a
				PSSId), a root object, transactions (the changes made while
				the store is locked commit when the last lock is released, or
				are undone by Abort), and the "separate transaction" an
				object can be put in so that the store-wide commit and abort
				leave it alone.

	The ROM implements it as TFlashStore (the internal RAM/flash store and
	the flash cards; a log-structured store on top of the flash driver),
	TPackageStore (a read-only store over a package's frames part) and
	TMuxStore (a wrapper serialising access to another store through a
	semaphore and a store monitor).  Their interface is the dispatch table
	tools/newton-rom/analysis/classinfo.py decodes (docs/protocols/); the
	DDK has no header for it, so this one is written from that table and
	from the implementations.  On the host the internal store is a
	THostStore (host/HostStore.h), an in-memory store with the same
	transaction semantics.

	Errors are OSErrors.h's kSError_... (kStoreError_Base -10600).
*/

#ifndef __STORE_H
#define __STORE_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

typedef ULong PSSId;					// an object's id within its store; 0 is never one

// TStore::Init's flags
const ULong kStoreIsCard = 0x01;		// a card store (pssInfo is the card's TCardHandler info)
const ULong kStoreIsInternal = 0x08;	// the internal store
const ULong kStoreIsFlash = 0x10;		// pssInfo is the TFlash to use

// NewObject's size may carry this to ask for an object that must not fail
// for lack of the store's reserve (the ROM: flash "slop")
const long kStoreNoSlopCheck = (long) 0x80000000;

PROTOCOL TStore : public TProtocol
{
public:
	static TStore*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo) ENDVIRTUAL;
	VIRTUAL NewtonErr	NeedsFormat(Boolean* needsFormat) ENDVIRTUAL;
	VIRTUAL NewtonErr	Format() ENDVIRTUAL;
	VIRTUAL NewtonErr	GetRootId(PSSId* rootId) ENDVIRTUAL;
	VIRTUAL NewtonErr	NewObject(long size, PSSId* id) ENDVIRTUAL;
	VIRTUAL NewtonErr	EraseObject(PSSId id) ENDVIRTUAL;
	VIRTUAL NewtonErr	DeleteObject(PSSId id) ENDVIRTUAL;
	VIRTUAL NewtonErr	SetObjectSize(PSSId id, long size) ENDVIRTUAL;
	VIRTUAL NewtonErr	GetObjectSize(PSSId id, long* size) ENDVIRTUAL;
	VIRTUAL NewtonErr	Write(PSSId id, long offset, char* buffer, long count) ENDVIRTUAL;
	VIRTUAL NewtonErr	Read(PSSId id, long offset, char* buffer, long count) ENDVIRTUAL;
	VIRTUAL NewtonErr	GetStoreSizes(long* totalSize, long* usedSize) ENDVIRTUAL;
	VIRTUAL NewtonErr	IsReadOnly(Boolean* isReadOnly) ENDVIRTUAL;
	VIRTUAL NewtonErr	LockStore() ENDVIRTUAL;
	VIRTUAL NewtonErr	UnlockStore() ENDVIRTUAL;
	VIRTUAL NewtonErr	Abort() ENDVIRTUAL;
	VIRTUAL NewtonErr	Idle(Boolean* arg1, Boolean* arg2) ENDVIRTUAL;
	VIRTUAL NewtonErr	NextObject(PSSId id, PSSId* nextId) ENDVIRTUAL;
	VIRTUAL NewtonErr	CheckIntegrity(ULong* arg) ENDVIRTUAL;
	VIRTUAL NewtonErr	SetBuddy(TStore* buddy) ENDVIRTUAL;
	VIRTUAL Boolean		OwnsObject(PSSId id) ENDVIRTUAL;
	VIRTUAL void*		Address(PSSId id) ENDVIRTUAL;		// the object's data when it lives in memory (nil otherwise)
	VIRTUAL const char*	StoreKind() ENDVIRTUAL;
	VIRTUAL NewtonErr	SetStore(TStore* store, ULong arg) ENDVIRTUAL;
	VIRTUAL Boolean		IsSameStore(void* data, ULong size) ENDVIRTUAL;
	VIRTUAL Boolean		IsLocked() ENDVIRTUAL;
	VIRTUAL NewtonErr	VppOff() ENDVIRTUAL;
	VIRTUAL NewtonErr	Sleep() ENDVIRTUAL;
	VIRTUAL Boolean		IsROM() ENDVIRTUAL;
	VIRTUAL NewtonErr	NewWithinTransaction(long size, PSSId* id) ENDVIRTUAL;
	VIRTUAL NewtonErr	StartTransactionAgainst(PSSId id) ENDVIRTUAL;
	VIRTUAL NewtonErr	SeparatelyAbort(PSSId id) ENDVIRTUAL;
	VIRTUAL NewtonErr	AddToCurrentTransaction(PSSId id) ENDVIRTUAL;
	VIRTUAL Boolean		InSeparateTransaction(PSSId id) ENDVIRTUAL;
	VIRTUAL NewtonErr	LockReadOnly() ENDVIRTUAL;
	VIRTUAL NewtonErr	UnlockReadOnly(Boolean reset) ENDVIRTUAL;
	VIRTUAL Boolean		InTransaction() ENDVIRTUAL;
	VIRTUAL NewtonErr	NewObject(char* data, long size, PSSId* id) ENDVIRTUAL;
	VIRTUAL NewtonErr	ReplaceObject(PSSId id, char* data, long size) ENDVIRTUAL;
	VIRTUAL NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size) ENDVIRTUAL;
	VIRTUAL NewtonErr	NewXIPObject(long size, PSSId* id) ENDVIRTUAL;
	VIRTUAL NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3) ENDVIRTUAL;
};

void	RegisterStoreImplementations(void);		// TPackageStore and THostStore with the protocol registry

#endif	/* __STORE_H */
