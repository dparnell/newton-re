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
typedef ULong32 StorePSSId;				// a PSSId as it lies in store data (a 32-bit word)

// TStore::Init's flags
const ULong kStoreIsCard = 0x01;		// a card store (pssInfo is the card's TCardHandler info)
const ULong kStoreIsInternal = 0x08;	// the internal store
const ULong kStoreIsFlash = 0x10;		// pssInfo is the TFlash to use

// NewObject's size may carry this to ask for an object that must not fail
// for lack of the store's reserve (the ROM: flash "slop")
const long kStoreNoSlopCheck = (long) 0x80000000;

// The interface methods cite the ROM's protocol glue - three instructions
// each, through the implementation's dispatch table - which the host's
// virtual call stands in for (protocols/Protocols.h).
PROTOCOL TStore : public TProtocol
{
public:
	static TStore*	New(const char* implementation);
	void			Delete();

	VIRTUAL NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo) ENDVIRTUAL;	// ROM 0x00386a50 Init__6TStoreFPvUlT2iT2T1
	VIRTUAL NewtonErr	NeedsFormat(Boolean* needsFormat) ENDVIRTUAL;	// ROM 0x00386a5c NeedsFormat__6TStoreFPUc
	VIRTUAL NewtonErr	Format() ENDVIRTUAL;	// ROM 0x00386a68 Format__6TStoreFv
	VIRTUAL NewtonErr	GetRootId(PSSId* rootId) ENDVIRTUAL;	// ROM 0x00386a74 GetRootId__6TStoreFPUl
	VIRTUAL NewtonErr	NewObject(long size, PSSId* id) ENDVIRTUAL;	// ROM 0x00386a80 NewObject__6TStoreFlPUl
	VIRTUAL NewtonErr	EraseObject(PSSId id) ENDVIRTUAL;	// ROM 0x00386a8c EraseObject__6TStoreFUl
	VIRTUAL NewtonErr	DeleteObject(PSSId id) ENDVIRTUAL;	// ROM 0x00386a98 DeleteObject__6TStoreFUl
	VIRTUAL NewtonErr	SetObjectSize(PSSId id, long size) ENDVIRTUAL;	// ROM 0x00386aa4 SetObjectSize__6TStoreFUll
	VIRTUAL NewtonErr	GetObjectSize(PSSId id, long* size) ENDVIRTUAL;	// ROM 0x00386ab0 GetObjectSize__6TStoreFUlPl
	VIRTUAL NewtonErr	Write(PSSId id, long offset, char* buffer, long count) ENDVIRTUAL;	// ROM 0x00386abc Write__6TStoreFUllPcT2
	VIRTUAL NewtonErr	Read(PSSId id, long offset, char* buffer, long count) ENDVIRTUAL;	// ROM 0x00386ac8 Read__6TStoreFUllPcT2
	VIRTUAL NewtonErr	GetStoreSizes(long* totalSize, long* usedSize) ENDVIRTUAL;	// ROM 0x00386ad4 GetStoreSizes__6TStoreFPlT1
	VIRTUAL NewtonErr	IsReadOnly(Boolean* isReadOnly) ENDVIRTUAL;	// ROM 0x00386ae0 IsReadOnly__6TStoreFPUc
	VIRTUAL NewtonErr	LockStore() ENDVIRTUAL;	// ROM 0x00386aec LockStore__6TStoreFv
	VIRTUAL NewtonErr	UnlockStore() ENDVIRTUAL;	// ROM 0x00386af8 UnlockStore__6TStoreFv
	VIRTUAL NewtonErr	Abort() ENDVIRTUAL;	// ROM 0x00386b04 Abort__6TStoreFv
	VIRTUAL NewtonErr	Idle(Boolean* arg1, Boolean* arg2) ENDVIRTUAL;	// ROM 0x00386b10 Idle__6TStoreFPUcT1
	VIRTUAL NewtonErr	NextObject(PSSId id, PSSId* nextId) ENDVIRTUAL;	// ROM 0x00386b1c NextObject__6TStoreFUlPUl
	VIRTUAL NewtonErr	CheckIntegrity(ULong* arg) ENDVIRTUAL;	// ROM 0x00386b28 CheckIntegrity__6TStoreFPUl
	VIRTUAL NewtonErr	SetBuddy(TStore* buddy) ENDVIRTUAL;	// ROM 0x00386b34 SetBuddy__6TStoreFP6TStore
	VIRTUAL Boolean		OwnsObject(PSSId id) ENDVIRTUAL;	// ROM 0x00386b40 OwnsObject__6TStoreFUl
	VIRTUAL void*		Address(PSSId id) ENDVIRTUAL;	// ROM 0x00386b4c Address__6TStoreFUl - the object's data when it lives in memory (nil otherwise)
	VIRTUAL const char*	StoreKind() ENDVIRTUAL;	// ROM 0x00386b58 StoreKind__6TStoreFv
	VIRTUAL NewtonErr	SetStore(TStore* store, ULong arg) ENDVIRTUAL;	// ROM 0x00386b64 SetStore__6TStoreFP6TStoreUl
	VIRTUAL Boolean		IsSameStore(void* data, ULong size) ENDVIRTUAL;	// ROM 0x00386b70 IsSameStore__6TStoreFPvUl
	VIRTUAL Boolean		IsLocked() ENDVIRTUAL;	// ROM 0x00386b7c IsLocked__6TStoreFv
	VIRTUAL NewtonErr	VppOff() ENDVIRTUAL;	// ROM 0x00386b88 VppOff__6TStoreFv
	VIRTUAL NewtonErr	Sleep() ENDVIRTUAL;	// ROM 0x00386b94 Sleep__6TStoreFv
	VIRTUAL Boolean		IsROM() ENDVIRTUAL;	// ROM 0x00386ba0 IsROM__6TStoreFv
	VIRTUAL NewtonErr	NewWithinTransaction(long size, PSSId* id) ENDVIRTUAL;	// ROM 0x00386bac NewWithinTransaction__6TStoreFlPUl
	VIRTUAL NewtonErr	StartTransactionAgainst(PSSId id) ENDVIRTUAL;	// ROM 0x00386bb8 StartTransactionAgainst__6TStoreFUl
	VIRTUAL NewtonErr	SeparatelyAbort(PSSId id) ENDVIRTUAL;	// ROM 0x00386bc4 SeparatelyAbort__6TStoreFUl
	VIRTUAL NewtonErr	AddToCurrentTransaction(PSSId id) ENDVIRTUAL;	// ROM 0x00386bd0 AddToCurrentTransaction__6TStoreFUl
	VIRTUAL Boolean		InSeparateTransaction(PSSId id) ENDVIRTUAL;	// ROM 0x00386bdc InSeparateTransaction__6TStoreFUl
	VIRTUAL NewtonErr	LockReadOnly() ENDVIRTUAL;	// ROM 0x00386be8 LockReadOnly__6TStoreFv
	VIRTUAL NewtonErr	UnlockReadOnly(Boolean reset) ENDVIRTUAL;	// ROM 0x00386bf4 UnlockReadOnly__6TStoreFUc
	VIRTUAL Boolean		InTransaction() ENDVIRTUAL;	// ROM 0x00386c00 InTransaction__6TStoreFv
	VIRTUAL NewtonErr	NewObject(char* data, long size, PSSId* id) ENDVIRTUAL;	// ROM 0x00386c0c NewObject__6TStoreFPclPUl
	VIRTUAL NewtonErr	ReplaceObject(PSSId id, char* data, long size) ENDVIRTUAL;	// ROM 0x00386c18 ReplaceObject__6TStoreFUlPcl
	VIRTUAL NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size) ENDVIRTUAL;	// ROM 0x00386c24 CalcXIPObjectSize__6TStoreFlT1Pl
	VIRTUAL NewtonErr	NewXIPObject(long size, PSSId* id) ENDVIRTUAL;	// ROM 0x00386c30 NewXIPObject__6TStoreFlPUl
	VIRTUAL NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3) ENDVIRTUAL;	// ROM 0x00386c3c GetXIPObjectInfo__6TStoreFUlPUlN22
};

void	RegisterStoreImplementations(void);		// TPackageStore and THostStore with the protocol registry

#endif	/* __STORE_H */
