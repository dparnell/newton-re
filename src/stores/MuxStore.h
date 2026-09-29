/*
	File:		stores/MuxStore.h

	Contains:	TMuxStore, the store every task uses: a TStore wrapped round
				the real one (the internal flash's TFlashStore, a card's) so
				that one task at a time goes into it.  Each call takes the
				wrapper's lock; a change goes into the store through the
				TStoreMonitor, a monitor started for the purpose (so it runs
				on the monitor's own stack, reboot-protected, whichever task
				asked), and a read goes straight to the store.

	Reconstructed from the MP2x00 US ROM (0x001232d0-0x00124540, the
	monitor's glue 0x00386ca8-0x00386e34); each function cites its origin.
	The monitor's selectors are its dispatch table's order
	(classinfo.py --name TMuxStoreMonitor).
*/

#ifndef __MUXSTORE_H
#define __MUXSTORE_H

#ifndef __STORE_H
#include "Store.h"
#endif

class TULockingSemaphore;


/*------------------------------------------------------------------------------
	T S t o r e M o n i t o r
------------------------------------------------------------------------------*/

enum
{
	kStoreMonitor_New,
	kStoreMonitor_Delete,
	kStoreMonitor_Init,
	kStoreMonitor_NeedsFormat,
	kStoreMonitor_Format,
	kStoreMonitor_GetRootId,
	kStoreMonitor_NewObject,
	kStoreMonitor_EraseObject,
	kStoreMonitor_DeleteObject,
	kStoreMonitor_SetObjectSize,
	kStoreMonitor_GetObjectSize,
	kStoreMonitor_Write,
	kStoreMonitor_Read,
	kStoreMonitor_GetStoreSizes,
	kStoreMonitor_IsReadOnly,
	kStoreMonitor_LockStore,
	kStoreMonitor_UnlockStore,
	kStoreMonitor_Abort,
	kStoreMonitor_Idle,
	kStoreMonitor_NextObject,
	kStoreMonitor_CheckIntegrity,
	kStoreMonitor_NewWithinTransaction,
	kStoreMonitor_StartTransactionAgainst,
	kStoreMonitor_SeparatelyAbort,
	kStoreMonitor_AddToCurrentTransaction,
	kStoreMonitor_LockReadOnly,
	kStoreMonitor_UnlockReadOnly,
	kStoreMonitor_NewObjectWithData,
	kStoreMonitor_ReplaceObject,
	kStoreMonitor_NewXIPObject
};

MONITOR TStoreMonitor : public TProtocol
{
public:
	static TStoreMonitor*	New(const char* implementation);			// ROM 0x00386ca8 New__13TStoreMonitorSFPc
	void		Delete(void);												// ROM 0x00386ccc Delete__13TStoreMonitorFv

	NewtonErr	Init(TStore* store);										// ROM 0x00386ce4 Init__13TStoreMonitorFP6TStore
	NewtonErr	NeedsFormat(Boolean* needsFormat);						// ROM 0x00386cf0 NeedsFormat__13TStoreMonitorFPUc
	NewtonErr	Format(void);												// ROM 0x00386cfc Format__13TStoreMonitorFv
	NewtonErr	GetRootId(PSSId* rootId);									// ROM 0x00386d08 GetRootId__13TStoreMonitorFPUl
	NewtonErr	NewObject(long size, PSSId* id);							// ROM 0x00386d14 NewObject__13TStoreMonitorFlPUl
	NewtonErr	EraseObject(PSSId id);										// ROM 0x00386d20 EraseObject__13TStoreMonitorFUl
	NewtonErr	DeleteObject(PSSId id);									// ROM 0x00386d2c DeleteObject__13TStoreMonitorFUl
	NewtonErr	SetObjectSize(PSSId id, long size);						// ROM 0x00386d38 SetObjectSize__13TStoreMonitorFUll
	NewtonErr	GetObjectSize(PSSId id, long* size);						// ROM 0x00386d44 GetObjectSize__13TStoreMonitorFUlPl
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count);	// ROM 0x00386d50 Write__13TStoreMonitorFUllPcT2
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count);		// ROM 0x00386d5c Read__13TStoreMonitorFUllPcT2
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize);			// ROM 0x00386d68 GetStoreSizes__13TStoreMonitorFPlT1
	NewtonErr	IsReadOnly(Boolean* isReadOnly);							// ROM 0x00386d74 IsReadOnly__13TStoreMonitorFPUc
	NewtonErr	LockStore(void);											// ROM 0x00386d80 LockStore__13TStoreMonitorFv
	NewtonErr	UnlockStore(void);											// ROM 0x00386d8c UnlockStore__13TStoreMonitorFv
	NewtonErr	Abort(void);												// ROM 0x00386d98 Abort__13TStoreMonitorFv
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2);						// ROM 0x00386da4 Idle__13TStoreMonitorFPUcT1
	NewtonErr	NextObject(PSSId id, PSSId* nextId);						// ROM 0x00386db0 NextObject__13TStoreMonitorFUlPUl
	NewtonErr	CheckIntegrity(ULong* arg);								// ROM 0x00386dbc CheckIntegrity__13TStoreMonitorFPUl
	NewtonErr	NewWithinTransaction(long size, PSSId* id);				// ROM 0x00386dc8 NewWithinTransaction__13TStoreMonitorFlPUl
	NewtonErr	StartTransactionAgainst(PSSId id);							// ROM 0x00386dd4 StartTransactionAgainst__13TStoreMonitorFUl
	NewtonErr	SeparatelyAbort(PSSId id);									// ROM 0x00386de0 SeparatelyAbort__13TStoreMonitorFUl
	NewtonErr	AddToCurrentTransaction(PSSId id);							// ROM 0x00386dec AddToCurrentTransaction__13TStoreMonitorFUl
	NewtonErr	LockReadOnly(void);										// ROM 0x00386df8 LockReadOnly__13TStoreMonitorFv
	NewtonErr	UnlockReadOnly(Boolean reset);								// ROM 0x00386e04 UnlockReadOnly__13TStoreMonitorFUc
	NewtonErr	NewObject(char* data, long size, PSSId* id);				// ROM 0x00386e10 NewObject__13TStoreMonitorFPclPUl
	NewtonErr	ReplaceObject(PSSId id, char* data, long size);			// ROM 0x00386e1c ReplaceObject__13TStoreMonitorFUlPcl
	NewtonErr	NewXIPObject(long size, PSSId* id);						// ROM 0x00386e28 NewXIPObject__13TStoreMonitorFlPUl
};


// The monitor's side: every call handed to the store it was given.
PROTOCOL TMuxStoreMonitor : public TStoreMonitor
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMuxStoreMonitor);

	TMuxStoreMonitor*	New(void);							// ROM 0x0012423c New__16TMuxStoreMonitorFv
	void		Delete(void);								// ROM 0x00124248 Delete__16TMuxStoreMonitorFv

	NewtonErr	Init(TStore* store);										// ROM 0x0012424c Init__16TMuxStoreMonitorFP6TStore
	NewtonErr	NeedsFormat(Boolean* needsFormat);						// ROM 0x0012428c NeedsFormat__16TMuxStoreMonitorFPUc
	NewtonErr	Format(void);												// ROM 0x00124294 Format__16TMuxStoreMonitorFv
	NewtonErr	GetRootId(PSSId* rootId);									// ROM 0x0012429c GetRootId__16TMuxStoreMonitorFPUl
	NewtonErr	NewObject(long size, PSSId* id);							// ROM 0x001242a4 NewObject__16TMuxStoreMonitorFlPUl
	NewtonErr	EraseObject(PSSId id);										// ROM 0x001242ac EraseObject__16TMuxStoreMonitorFUl
	NewtonErr	DeleteObject(PSSId id);									// ROM 0x001242b4 DeleteObject__16TMuxStoreMonitorFUl
	NewtonErr	SetObjectSize(PSSId id, long size);						// ROM 0x001242bc SetObjectSize__16TMuxStoreMonitorFUll
	NewtonErr	GetObjectSize(PSSId id, long* size);						// ROM 0x00124348 GetObjectSize__16TMuxStoreMonitorFUlPl
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count);	// ROM 0x00124350 Write__16TMuxStoreMonitorFUllPcT2
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count);		// ROM 0x00124378 Read__16TMuxStoreMonitorFUllPcT2
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize);			// ROM 0x001243a0 GetStoreSizes__16TMuxStoreMonitorFPlT1
	NewtonErr	IsReadOnly(Boolean* isReadOnly);							// ROM 0x001243a8 IsReadOnly__16TMuxStoreMonitorFPUc
	NewtonErr	LockStore(void);											// ROM 0x001243b0 LockStore__16TMuxStoreMonitorFv
	NewtonErr	UnlockStore(void);											// ROM 0x001243b8 UnlockStore__16TMuxStoreMonitorFv
	NewtonErr	Abort(void);												// ROM 0x001243c0 Abort__16TMuxStoreMonitorFv
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2);						// ROM 0x001243c8 Idle__16TMuxStoreMonitorFPUcT1
	NewtonErr	NextObject(PSSId id, PSSId* nextId);						// ROM 0x001243d0 NextObject__16TMuxStoreMonitorFUlPUl
	NewtonErr	CheckIntegrity(ULong* arg);								// ROM 0x00124454 CheckIntegrity__16TMuxStoreMonitorFPUl
	NewtonErr	NewWithinTransaction(long size, PSSId* id);				// ROM 0x0012445c NewWithinTransaction__16TMuxStoreMonitorFlPUl
	NewtonErr	StartTransactionAgainst(PSSId id);							// ROM 0x00124464 StartTransactionAgainst__16TMuxStoreMonitorFUl
	NewtonErr	SeparatelyAbort(PSSId id);									// ROM 0x0012446c SeparatelyAbort__16TMuxStoreMonitorFUl
	NewtonErr	AddToCurrentTransaction(PSSId id);							// ROM 0x00124474 AddToCurrentTransaction__16TMuxStoreMonitorFUl
	NewtonErr	LockReadOnly(void);										// ROM 0x0012447c LockReadOnly__16TMuxStoreMonitorFv
	NewtonErr	UnlockReadOnly(Boolean reset);								// ROM 0x00124484 UnlockReadOnly__16TMuxStoreMonitorFUc
	NewtonErr	NewObject(char* data, long size, PSSId* id);				// ROM 0x00124490 NewObject__16TMuxStoreMonitorFPclPUl
	NewtonErr	ReplaceObject(PSSId id, char* data, long size);			// ROM 0x00124498 ReplaceObject__16TMuxStoreMonitorFUlPcl
	NewtonErr	NewXIPObject(long size, PSSId* id);						// ROM 0x00124524 NewXIPObject__16TMuxStoreMonitorFlPUl

	static long	MonitorEntry(void* instance, ULong selector, void* args);

	TStore*		fStore;			// +0x10
};


/*------------------------------------------------------------------------------
	T M u x S t o r e
	0x1C bytes.
------------------------------------------------------------------------------*/

PROTOCOL TMuxStore : public TStore
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMuxStore);

	TMuxStore*	New(void);																	// ROM 0x00123b98 New__9TMuxStoreFv
	void		Delete(void);																// ROM 0x00123ec0 Delete__9TMuxStoreFv

	NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo);	// ROM 0x00124234 Init__9TMuxStoreFPvUlT2iT2T1
	NewtonErr	NeedsFormat(Boolean* needsFormat);										// ROM 0x001242c4 NeedsFormat__9TMuxStoreFPUc
	NewtonErr	Format(void);																// ROM 0x001243d8 Format__9TMuxStoreFv
	NewtonErr	GetRootId(PSSId* rootId);													// ROM 0x001244a0 GetRootId__9TMuxStoreFPUl
	NewtonErr	NewObject(long size, PSSId* id);											// ROM 0x0012452c NewObject__9TMuxStoreFlPUl
	NewtonErr	EraseObject(PSSId id);														// ROM 0x001232d8 EraseObject__9TMuxStoreFUl
	NewtonErr	DeleteObject(PSSId id);														// ROM 0x0012335c DeleteObject__9TMuxStoreFUl
	NewtonErr	SetObjectSize(PSSId id, long size);											// ROM 0x001233e0 SetObjectSize__9TMuxStoreFUll
	NewtonErr	GetObjectSize(PSSId id, long* size);										// ROM 0x0012346c GetObjectSize__9TMuxStoreFUlPl
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count);						// ROM 0x001234f8 Write__9TMuxStoreFUllPcT2
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count);						// ROM 0x0012359c Read__9TMuxStoreFUllPcT2
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize);								// ROM 0x00123640 GetStoreSizes__9TMuxStoreFPlT1
	NewtonErr	IsReadOnly(Boolean* isReadOnly);											// ROM 0x001236cc IsReadOnly__9TMuxStoreFPUc
	NewtonErr	LockStore(void);															// ROM 0x00123750 LockStore__9TMuxStoreFv
	NewtonErr	UnlockStore(void);															// ROM 0x001237cc UnlockStore__9TMuxStoreFv
	NewtonErr	Abort(void);																// ROM 0x00123850 Abort__9TMuxStoreFv
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2);											// ROM 0x001238cc Idle__9TMuxStoreFPUcT1
	NewtonErr	NextObject(PSSId id, PSSId* nextId);										// ROM 0x00123958 NextObject__9TMuxStoreFUlPUl
	NewtonErr	CheckIntegrity(ULong* arg);													// ROM 0x001239e4 CheckIntegrity__9TMuxStoreFPUl
	NewtonErr	SetBuddy(TStore* buddy);													// ROM 0x00123a68 SetBuddy__9TMuxStoreFP6TStore
	Boolean		OwnsObject(PSSId id);														// ROM 0x00123a70 OwnsObject__9TMuxStoreFUl
	void*		Address(PSSId id);															// ROM 0x00123c14 Address__9TMuxStoreFUl
	const char*	StoreKind(void);															// ROM 0x00123af8 StoreKind__9TMuxStoreFv
	NewtonErr	SetStore(TStore* store, ULong environment);									// ROM 0x00123b00 SetStore__9TMuxStoreFP6TStoreUl
	Boolean		IsSameStore(void* data, ULong size);										// ROM 0x00123b88 IsSameStore__9TMuxStoreFPvUl
	Boolean		IsLocked(void);																// ROM 0x00123b90 IsLocked__9TMuxStoreFv
	NewtonErr	VppOff(void);																// ROM 0x00123c04 VppOff__9TMuxStoreFv
	NewtonErr	Sleep(void);																// ROM 0x00123c0c Sleep__9TMuxStoreFv
	Boolean		IsROM(void);																// ROM 0x00123c1c IsROM__9TMuxStoreFv
	NewtonErr	NewWithinTransaction(long size, PSSId* id);									// ROM 0x00123c24 NewWithinTransaction__9TMuxStoreFlPUl
	NewtonErr	StartTransactionAgainst(PSSId id);											// ROM 0x00123cb0 StartTransactionAgainst__9TMuxStoreFUl
	NewtonErr	SeparatelyAbort(PSSId id);													// ROM 0x00123d34 SeparatelyAbort__9TMuxStoreFUl
	NewtonErr	AddToCurrentTransaction(PSSId id);											// ROM 0x00123db8 AddToCurrentTransaction__9TMuxStoreFUl
	Boolean		InSeparateTransaction(PSSId id);											// ROM 0x00123e3c InSeparateTransaction__9TMuxStoreFUl
	NewtonErr	LockReadOnly(void);															// ROM 0x00123e44 LockReadOnly__9TMuxStoreFv
	NewtonErr	UnlockReadOnly(Boolean reset);												// ROM 0x00123f18 UnlockReadOnly__9TMuxStoreFUc
	Boolean		InTransaction(void);														// ROM 0x00123f24 InTransaction__9TMuxStoreFv
	NewtonErr	NewObject(char* data, long size, PSSId* id);								// ROM 0x00123f2c NewObject__9TMuxStoreFPclPUl
	NewtonErr	ReplaceObject(PSSId id, char* data, long size);								// ROM 0x00123fc0 ReplaceObject__9TMuxStoreFUlPcl
	NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size);						// ROM 0x00124054 CalcXIPObjectSize__9TMuxStoreFlT1Pl
	NewtonErr	NewXIPObject(long size, PSSId* id);											// ROM 0x001240e8 NewXIPObject__9TMuxStoreFlPUl
	NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3);			// ROM 0x00124174 GetXIPObjectInfo__9TMuxStoreFUlPUlN22

	TStore*		GetStore(void);																// ROM 0x00124218 GetStore__9TMuxStoreFv
	NewtonErr	Acquire(void);																// ROM 0x00124220 Acquire__9TMuxStoreFv
	NewtonErr	Release(void);																// ROM 0x0012422c Release__9TMuxStoreFv

	TStore*			fStore;			// +0x10
	TStoreMonitor*	fMonitor;		// +0x14
	TULockingSemaphore*	fLock;		// +0x18
};

#endif	/* __MUXSTORE_H */
