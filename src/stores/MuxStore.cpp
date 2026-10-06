/*
	File:		stores/MuxStore.cpp

	Contains:	TMuxStore and its monitor (MuxStore.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "MuxStore.h"
#include "UserSemaphore.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "host/RomBugs.h"


/*------------------------------------------------------------------------------
	T S t o r e M o n i t o r   -   t h e   c a l l e r ' s   s i d e
------------------------------------------------------------------------------*/

// ROM 0x00386ca8 New__13TStoreMonitorSFPc
// (an instance, not yet New()ed: a monitor's New is its first call once it runs)
TStoreMonitor*
TStoreMonitor::New(const char* implementation)
{
	return (TStoreMonitor*) AllocInstanceByName("TStoreMonitor", implementation);
}


// ROM 0x00386ccc Delete__13TStoreMonitorFv
void
TStoreMonitor::Delete(void)
{
	ProtocolMonitorArgs args = { 0, { 0, 0, 0, 0 } };
	MonitorCall(kStoreMonitor_Delete, &args);
	DestroyMonitor();
}


static inline NewtonErr
StoreMonitorCall(TStoreMonitor* monitor, ULong selector, ULong a0 = 0, ULong a1 = 0, ULong a2 = 0, ULong a3 = 0)
{
	ProtocolMonitorArgs args = { 0, { a0, a1, a2, a3 } };
	return (NewtonErr) monitor->MonitorCall(selector, &args);
}

// ROM 0x00386ce4 Init__13TStoreMonitorFP6TStore
NewtonErr	TStoreMonitor::Init(TStore* store)						{ return StoreMonitorCall(this, kStoreMonitor_Init, (ULong) store); }
// ROM 0x00386cf0 NeedsFormat__13TStoreMonitorFPUc
NewtonErr	TStoreMonitor::NeedsFormat(Boolean* needsFormat)		{ return StoreMonitorCall(this, kStoreMonitor_NeedsFormat, (ULong) needsFormat); }
// ROM 0x00386cfc Format__13TStoreMonitorFv
NewtonErr	TStoreMonitor::Format(void)								{ return StoreMonitorCall(this, kStoreMonitor_Format); }
// ROM 0x00386d08 GetRootId__13TStoreMonitorFPUl
NewtonErr	TStoreMonitor::GetRootId(PSSId* rootId)					{ return StoreMonitorCall(this, kStoreMonitor_GetRootId, (ULong) rootId); }
// ROM 0x00386d14 NewObject__13TStoreMonitorFlPUl
NewtonErr	TStoreMonitor::NewObject(long size, PSSId* id)			{ return StoreMonitorCall(this, kStoreMonitor_NewObject, (ULong) size, (ULong) id); }
// ROM 0x00386d20 EraseObject__13TStoreMonitorFUl
NewtonErr	TStoreMonitor::EraseObject(PSSId id)					{ return StoreMonitorCall(this, kStoreMonitor_EraseObject, id); }
// ROM 0x00386d2c DeleteObject__13TStoreMonitorFUl
NewtonErr	TStoreMonitor::DeleteObject(PSSId id)					{ return StoreMonitorCall(this, kStoreMonitor_DeleteObject, id); }
// ROM 0x00386d38 SetObjectSize__13TStoreMonitorFUll
NewtonErr	TStoreMonitor::SetObjectSize(PSSId id, long size)		{ return StoreMonitorCall(this, kStoreMonitor_SetObjectSize, id, (ULong) size); }
// ROM 0x00386d44 GetObjectSize__13TStoreMonitorFUlPl
NewtonErr	TStoreMonitor::GetObjectSize(PSSId id, long* size)		{ return StoreMonitorCall(this, kStoreMonitor_GetObjectSize, id, (ULong) size); }
// ROM 0x00386d50 Write__13TStoreMonitorFUllPcT2
NewtonErr	TStoreMonitor::Write(PSSId id, long offset, char* buffer, long count)	{ return StoreMonitorCall(this, kStoreMonitor_Write, id, (ULong) offset, (ULong) buffer, (ULong) count); }
// ROM 0x00386d5c Read__13TStoreMonitorFUllPcT2
NewtonErr	TStoreMonitor::Read(PSSId id, long offset, char* buffer, long count)	{ return StoreMonitorCall(this, kStoreMonitor_Read, id, (ULong) offset, (ULong) buffer, (ULong) count); }
// ROM 0x00386d68 GetStoreSizes__13TStoreMonitorFPlT1
NewtonErr	TStoreMonitor::GetStoreSizes(long* totalSize, long* usedSize)	{ return StoreMonitorCall(this, kStoreMonitor_GetStoreSizes, (ULong) totalSize, (ULong) usedSize); }
// ROM 0x00386d74 IsReadOnly__13TStoreMonitorFPUc
NewtonErr	TStoreMonitor::IsReadOnly(Boolean* isReadOnly)			{ return StoreMonitorCall(this, kStoreMonitor_IsReadOnly, (ULong) isReadOnly); }
// ROM 0x00386d80 LockStore__13TStoreMonitorFv
NewtonErr	TStoreMonitor::LockStore(void)							{ return StoreMonitorCall(this, kStoreMonitor_LockStore); }
// ROM 0x00386d8c UnlockStore__13TStoreMonitorFv
NewtonErr	TStoreMonitor::UnlockStore(void)						{ return StoreMonitorCall(this, kStoreMonitor_UnlockStore); }
// ROM 0x00386d98 Abort__13TStoreMonitorFv
NewtonErr	TStoreMonitor::Abort(void)								{ return StoreMonitorCall(this, kStoreMonitor_Abort); }
// ROM 0x00386da4 Idle__13TStoreMonitorFPUcT1
NewtonErr	TStoreMonitor::Idle(Boolean* arg1, Boolean* arg2)		{ return StoreMonitorCall(this, kStoreMonitor_Idle, (ULong) arg1, (ULong) arg2); }
// ROM 0x00386db0 NextObject__13TStoreMonitorFUlPUl
NewtonErr	TStoreMonitor::NextObject(PSSId id, PSSId* nextId)		{ return StoreMonitorCall(this, kStoreMonitor_NextObject, id, (ULong) nextId); }
// ROM 0x00386dbc CheckIntegrity__13TStoreMonitorFPUl
NewtonErr	TStoreMonitor::CheckIntegrity(ULong* arg)				{ return StoreMonitorCall(this, kStoreMonitor_CheckIntegrity, (ULong) arg); }
// ROM 0x00386dc8 NewWithinTransaction__13TStoreMonitorFlPUl
NewtonErr	TStoreMonitor::NewWithinTransaction(long size, PSSId* id)	{ return StoreMonitorCall(this, kStoreMonitor_NewWithinTransaction, (ULong) size, (ULong) id); }
// ROM 0x00386dd4 StartTransactionAgainst__13TStoreMonitorFUl
NewtonErr	TStoreMonitor::StartTransactionAgainst(PSSId id)		{ return StoreMonitorCall(this, kStoreMonitor_StartTransactionAgainst, id); }
// ROM 0x00386de0 SeparatelyAbort__13TStoreMonitorFUl
NewtonErr	TStoreMonitor::SeparatelyAbort(PSSId id)				{ return StoreMonitorCall(this, kStoreMonitor_SeparatelyAbort, id); }
// ROM 0x00386dec AddToCurrentTransaction__13TStoreMonitorFUl
NewtonErr	TStoreMonitor::AddToCurrentTransaction(PSSId id)		{ return StoreMonitorCall(this, kStoreMonitor_AddToCurrentTransaction, id); }
// ROM 0x00386df8 LockReadOnly__13TStoreMonitorFv
NewtonErr	TStoreMonitor::LockReadOnly(void)						{ return StoreMonitorCall(this, kStoreMonitor_LockReadOnly); }
// ROM 0x00386e04 UnlockReadOnly__13TStoreMonitorFUc
NewtonErr	TStoreMonitor::UnlockReadOnly(Boolean reset)			{ return StoreMonitorCall(this, kStoreMonitor_UnlockReadOnly, reset); }
// ROM 0x00386e10 NewObject__13TStoreMonitorFPclPUl
NewtonErr	TStoreMonitor::NewObject(char* data, long size, PSSId* id)	{ return StoreMonitorCall(this, kStoreMonitor_NewObjectWithData, (ULong) data, (ULong) size, (ULong) id); }
// ROM 0x00386e1c ReplaceObject__13TStoreMonitorFUlPcl
NewtonErr	TStoreMonitor::ReplaceObject(PSSId id, char* data, long size)	{ return StoreMonitorCall(this, kStoreMonitor_ReplaceObject, id, (ULong) data, (ULong) size); }
// ROM 0x00386e28 NewXIPObject__13TStoreMonitorFlPUl
NewtonErr	TStoreMonitor::NewXIPObject(long size, PSSId* id)		{ return StoreMonitorCall(this, kStoreMonitor_NewXIPObject, (ULong) size, (ULong) id); }


/*------------------------------------------------------------------------------
	T M u x S t o r e M o n i t o r   -   t h e   m o n i t o r ' s   s i d e
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TMuxStoreMonitor)		// ROM 0x00123848 Sizeof__16TMuxStoreMonitorSFv
PROTOCOL_CLASSINFO(TMuxStoreMonitor, "TStoreMonitor", "", 0, 0, TMuxStoreMonitor::MonitorEntry)	// ROM 0x00386f88 ClassInfo__16TMuxStoreMonitorSFv


// ROM 0x0012423c New__16TMuxStoreMonitorFv
TMuxStoreMonitor*
TMuxStoreMonitor::New(void)
{
	fStore = nil;
	return this;
}


// ROM 0x00124248 Delete__16TMuxStoreMonitorFv
void
TMuxStoreMonitor::Delete(void)
{
}


// ROM 0x0012424c Init__16TMuxStoreMonitorFP6TStore
// DEVIATION: the ROM first locks the monitor's stack into memory
// (LockHeapRange over the kilobyte either side), which a host's needs not.
NewtonErr
TMuxStoreMonitor::Init(TStore* store)
{
	fStore = store;
	return noErr;
}

// ROM 0x0012428c NeedsFormat__16TMuxStoreMonitorFPUc
NewtonErr	TMuxStoreMonitor::NeedsFormat(Boolean* needsFormat)		{ return fStore->NeedsFormat(needsFormat); }
// ROM 0x00124294 Format__16TMuxStoreMonitorFv
NewtonErr	TMuxStoreMonitor::Format(void)								{ return fStore->Format(); }
// ROM 0x0012429c GetRootId__16TMuxStoreMonitorFPUl
NewtonErr	TMuxStoreMonitor::GetRootId(PSSId* rootId)					{ return fStore->GetRootId(rootId); }
// ROM 0x001242a4 NewObject__16TMuxStoreMonitorFlPUl
NewtonErr	TMuxStoreMonitor::NewObject(long size, PSSId* id)			{ return fStore->NewObject(size, id); }
// ROM 0x001242ac EraseObject__16TMuxStoreMonitorFUl
NewtonErr	TMuxStoreMonitor::EraseObject(PSSId id)					{ return fStore->EraseObject(id); }
// ROM 0x001242b4 DeleteObject__16TMuxStoreMonitorFUl
NewtonErr	TMuxStoreMonitor::DeleteObject(PSSId id)					{ return fStore->DeleteObject(id); }
// ROM 0x001242bc SetObjectSize__16TMuxStoreMonitorFUll
NewtonErr	TMuxStoreMonitor::SetObjectSize(PSSId id, long size)		{ return fStore->SetObjectSize(id, size); }
// ROM 0x00124348 GetObjectSize__16TMuxStoreMonitorFUlPl
NewtonErr	TMuxStoreMonitor::GetObjectSize(PSSId id, long* size)		{ return fStore->GetObjectSize(id, size); }
// ROM 0x00124350 Write__16TMuxStoreMonitorFUllPcT2
NewtonErr	TMuxStoreMonitor::Write(PSSId id, long offset, char* buffer, long count)	{ return fStore->Write(id, offset, buffer, count); }
// ROM 0x00124378 Read__16TMuxStoreMonitorFUllPcT2
NewtonErr	TMuxStoreMonitor::Read(PSSId id, long offset, char* buffer, long count)	{ return fStore->Read(id, offset, buffer, count); }
// ROM 0x001243a0 GetStoreSizes__16TMuxStoreMonitorFPlT1
NewtonErr	TMuxStoreMonitor::GetStoreSizes(long* totalSize, long* usedSize)	{ return fStore->GetStoreSizes(totalSize, usedSize); }
// ROM 0x001243a8 IsReadOnly__16TMuxStoreMonitorFPUc
NewtonErr	TMuxStoreMonitor::IsReadOnly(Boolean* isReadOnly)			{ return fStore->IsReadOnly(isReadOnly); }
// ROM 0x001243b0 LockStore__16TMuxStoreMonitorFv
NewtonErr	TMuxStoreMonitor::LockStore(void)							{ return fStore->LockStore(); }
// ROM 0x001243b8 UnlockStore__16TMuxStoreMonitorFv
NewtonErr	TMuxStoreMonitor::UnlockStore(void)						{ return fStore->UnlockStore(); }
// ROM 0x001243c0 Abort__16TMuxStoreMonitorFv
NewtonErr	TMuxStoreMonitor::Abort(void)								{ return fStore->Abort(); }
// ROM 0x001243c8 Idle__16TMuxStoreMonitorFPUcT1
NewtonErr	TMuxStoreMonitor::Idle(Boolean* arg1, Boolean* arg2)		{ return fStore->Idle(arg1, arg2); }
// ROM 0x001243d0 NextObject__16TMuxStoreMonitorFUlPUl
NewtonErr	TMuxStoreMonitor::NextObject(PSSId id, PSSId* nextId)		{ return fStore->NextObject(id, nextId); }
// ROM 0x00124454 CheckIntegrity__16TMuxStoreMonitorFPUl
NewtonErr	TMuxStoreMonitor::CheckIntegrity(ULong* arg)				{ return fStore->CheckIntegrity(arg); }
// ROM 0x0012445c NewWithinTransaction__16TMuxStoreMonitorFlPUl
NewtonErr	TMuxStoreMonitor::NewWithinTransaction(long size, PSSId* id)	{ return fStore->NewWithinTransaction(size, id); }
// ROM 0x00124464 StartTransactionAgainst__16TMuxStoreMonitorFUl
NewtonErr	TMuxStoreMonitor::StartTransactionAgainst(PSSId id)		{ return fStore->StartTransactionAgainst(id); }
// ROM 0x0012446c SeparatelyAbort__16TMuxStoreMonitorFUl
NewtonErr	TMuxStoreMonitor::SeparatelyAbort(PSSId id)				{ return fStore->SeparatelyAbort(id); }
// ROM 0x00124474 AddToCurrentTransaction__16TMuxStoreMonitorFUl
NewtonErr	TMuxStoreMonitor::AddToCurrentTransaction(PSSId id)		{ return fStore->AddToCurrentTransaction(id); }
// ROM 0x0012447c LockReadOnly__16TMuxStoreMonitorFv
NewtonErr	TMuxStoreMonitor::LockReadOnly(void)						{ return fStore->LockReadOnly(); }
// ROM 0x00124484 UnlockReadOnly__16TMuxStoreMonitorFUc
NewtonErr	TMuxStoreMonitor::UnlockReadOnly(Boolean reset)			{ return fStore->UnlockReadOnly(reset); }
// ROM 0x00124490 NewObject__16TMuxStoreMonitorFPclPUl
NewtonErr	TMuxStoreMonitor::NewObject(char* data, long size, PSSId* id)	{ return fStore->NewObject(data, size, id); }
// ROM 0x00124498 ReplaceObject__16TMuxStoreMonitorFUlPcl
NewtonErr	TMuxStoreMonitor::ReplaceObject(PSSId id, char* data, long size)	{ return fStore->ReplaceObject(id, data, size); }
// ROM 0x00124524 NewXIPObject__16TMuxStoreMonitorFlPUl
NewtonErr	TMuxStoreMonitor::NewXIPObject(long size, PSSId* id)		{ return fStore->NewXIPObject(size, id); }


// The monitor entry ProtocolGen generates: a selector to a method.
long
TMuxStoreMonitor::MonitorEntry(void* instance, ULong selector, void* argBlock)
{
	TMuxStoreMonitor* self = (TMuxStoreMonitor*) ((TProtocol*) instance)->fRealThis;
	ProtocolMonitorArgs* args = (ProtocolMonitorArgs*) argBlock;
	ULong* a = args->fArg;
	switch (selector)
	{
	case kStoreMonitor_New:				args->fResult = (Long) self->New(); break;
	case kStoreMonitor_Delete:			self->Delete(); args->fResult = 0; break;
	case kStoreMonitor_Init:			args->fResult = self->Init((TStore*) a[0]); break;
	case kStoreMonitor_NeedsFormat:		args->fResult = self->NeedsFormat((Boolean*) a[0]); break;
	case kStoreMonitor_Format:			args->fResult = self->Format(); break;
	case kStoreMonitor_GetRootId:		args->fResult = self->GetRootId((PSSId*) a[0]); break;
	case kStoreMonitor_NewObject:		args->fResult = self->NewObject((long) a[0], (PSSId*) a[1]); break;
	case kStoreMonitor_EraseObject:		args->fResult = self->EraseObject(a[0]); break;
	case kStoreMonitor_DeleteObject:	args->fResult = self->DeleteObject(a[0]); break;
	case kStoreMonitor_SetObjectSize:	args->fResult = self->SetObjectSize(a[0], (long) a[1]); break;
	case kStoreMonitor_GetObjectSize:	args->fResult = self->GetObjectSize(a[0], (long*) a[1]); break;
	case kStoreMonitor_Write:			args->fResult = self->Write(a[0], (long) a[1], (char*) a[2], (long) a[3]); break;
	case kStoreMonitor_Read:			args->fResult = self->Read(a[0], (long) a[1], (char*) a[2], (long) a[3]); break;
	case kStoreMonitor_GetStoreSizes:	args->fResult = self->GetStoreSizes((long*) a[0], (long*) a[1]); break;
	case kStoreMonitor_IsReadOnly:		args->fResult = self->IsReadOnly((Boolean*) a[0]); break;
	case kStoreMonitor_LockStore:		args->fResult = self->LockStore(); break;
	case kStoreMonitor_UnlockStore:		args->fResult = self->UnlockStore(); break;
	case kStoreMonitor_Abort:			args->fResult = self->Abort(); break;
	case kStoreMonitor_Idle:			args->fResult = self->Idle((Boolean*) a[0], (Boolean*) a[1]); break;
	case kStoreMonitor_NextObject:		args->fResult = self->NextObject(a[0], (PSSId*) a[1]); break;
	case kStoreMonitor_CheckIntegrity:	args->fResult = self->CheckIntegrity((ULong*) a[0]); break;
	case kStoreMonitor_NewWithinTransaction:	args->fResult = self->NewWithinTransaction((long) a[0], (PSSId*) a[1]); break;
	case kStoreMonitor_StartTransactionAgainst:	args->fResult = self->StartTransactionAgainst(a[0]); break;
	case kStoreMonitor_SeparatelyAbort:	args->fResult = self->SeparatelyAbort(a[0]); break;
	case kStoreMonitor_AddToCurrentTransaction:	args->fResult = self->AddToCurrentTransaction(a[0]); break;
	case kStoreMonitor_LockReadOnly:	args->fResult = self->LockReadOnly(); break;
	case kStoreMonitor_UnlockReadOnly:	args->fResult = self->UnlockReadOnly((Boolean) a[0]); break;
	case kStoreMonitor_NewObjectWithData:	args->fResult = self->NewObject((char*) a[0], (long) a[1], (PSSId*) a[2]); break;
	case kStoreMonitor_ReplaceObject:	args->fResult = self->ReplaceObject(a[0], (char*) a[1], (long) a[2]); break;
	case kStoreMonitor_NewXIPObject:	args->fResult = self->NewXIPObject((long) a[0], (PSSId*) a[1]); break;
	default:
		return -1;
	}
	return 0;
}


/*------------------------------------------------------------------------------
	T M u x S t o r e
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TMuxStore)		// ROM 0x001232d0 Sizeof__9TMuxStoreSFv
PROTOCOL_CLASSINFO(TMuxStore, "TStore", "", 0, 0, nil)	// ROM 0x00386e70 ClassInfo__9TMuxStoreSFv


// The pattern of every call: the lock taken, the call made, the lock given
// back even when the call throws (which then goes on up).
#define MUX_CALL(expression)							\
	NewtonErr result = noErr;							\
	fLock->Acquire(kWaitOnBlock);						\
	newton_try											\
	{													\
		result = (expression);							\
	}													\
	newton_catch_all									\
	{													\
		fLock->Release();								\
		rethrow;										\
	}													\
	end_try;											\
	fLock->Release();									\
	return result


// ROM 0x00123b98 New__9TMuxStoreFv
TMuxStore*
TMuxStore::New(void)
{
	fStore = nil;
	fMonitor = nil;
	fLock = new TULockingSemaphore;
	if (fLock != nil && fLock->Init() != noErr && fLock != nil)
	{
		// ROM BUG (fixed): destroyed but not freed, and left in fLock.  The
		// fix frees it and forgets it (as Delete does), so the store has no
		// lock either way but the block is not lost.
		if (RomBugFixed())
		{
			delete fLock;
			fLock = nil;
		}
		else
			fLock->~TULockingSemaphore();
	}
	return this;
}


// ROM 0x00123ec0 Delete__9TMuxStoreFv
void
TMuxStore::Delete(void)
{
	if (fMonitor != nil)
	{
		fMonitor->Delete();
		fMonitor = nil;
	}
	if (fStore != nil)
	{
		fStore->Delete();
		fStore = nil;
	}
	if (fLock != nil)
	{
		delete fLock;
		fLock = nil;
	}
}


// ROM 0x00124234 Init__9TMuxStoreFPvUlT2iT2T1
// Nothing: SetStore makes it.
NewtonErr
TMuxStore::Init(void*, ULong, ULong, int, ULong, void*)
{
	return noErr;
}


// ROM 0x00123b00 SetStore__9TMuxStoreFP6TStoreUl
// The store wrapped, and its monitor started (in the environment given)
// and told which store it is.
// ROM QUIRK: with no memory for the monitor and no memory error recorded,
// it goes on to start a monitor on nothing.
NewtonErr
TMuxStore::SetStore(TStore* store, ULong environment)
{
	fStore = store;
	fMonitor = TStoreMonitor::New("TMuxStoreMonitor");
	NewtonErr err;
	if (fMonitor == nil && (err = MemError()) != noErr)
		return err;
	err = fMonitor->StartMonitor(0x400, (TObjectId) environment, 'Tmux', false);
	if (err == noErr)
		return fMonitor->Init(fStore);
	return err;
}


// ROM 0x00124218 GetStore__9TMuxStoreFv
TStore*		TMuxStore::GetStore(void)				{ return fStore; }
// ROM 0x00124220 Acquire__9TMuxStoreFv
NewtonErr	TMuxStore::Acquire(void)				{ return fLock->Acquire(kWaitOnBlock); }
// ROM 0x0012422c Release__9TMuxStoreFv
NewtonErr	TMuxStore::Release(void)				{ return fLock->Release(); }

// ROM 0x001242c4 NeedsFormat__9TMuxStoreFPUc
NewtonErr	TMuxStore::NeedsFormat(Boolean* needsFormat)		{ MUX_CALL(fMonitor->NeedsFormat(needsFormat)); }
// ROM 0x001243d8 Format__9TMuxStoreFv
NewtonErr	TMuxStore::Format(void)								{ MUX_CALL(fMonitor->Format()); }
// ROM 0x001244a0 GetRootId__9TMuxStoreFPUl
NewtonErr	TMuxStore::GetRootId(PSSId* rootId)					{ MUX_CALL(fStore->GetRootId(rootId)); }
// ROM 0x0012452c NewObject__9TMuxStoreFlPUl
NewtonErr	TMuxStore::NewObject(long size, PSSId* id)			{ MUX_CALL(fMonitor->NewObject(size, id)); }
// ROM 0x001232d8 EraseObject__9TMuxStoreFUl
NewtonErr	TMuxStore::EraseObject(PSSId id)					{ MUX_CALL(fMonitor->EraseObject(id)); }
// ROM 0x0012335c DeleteObject__9TMuxStoreFUl
NewtonErr	TMuxStore::DeleteObject(PSSId id)					{ MUX_CALL(fMonitor->DeleteObject(id)); }
// ROM 0x001233e0 SetObjectSize__9TMuxStoreFUll
NewtonErr	TMuxStore::SetObjectSize(PSSId id, long size)		{ MUX_CALL(fMonitor->SetObjectSize(id, size)); }
// ROM 0x0012346c GetObjectSize__9TMuxStoreFUlPl
NewtonErr	TMuxStore::GetObjectSize(PSSId id, long* size)		{ MUX_CALL(fStore->GetObjectSize(id, size)); }
// ROM 0x001234f8 Write__9TMuxStoreFUllPcT2
NewtonErr	TMuxStore::Write(PSSId id, long offset, char* buffer, long count)	{ MUX_CALL(fMonitor->Write(id, offset, buffer, count)); }
// ROM 0x0012359c Read__9TMuxStoreFUllPcT2
NewtonErr	TMuxStore::Read(PSSId id, long offset, char* buffer, long count)	{ MUX_CALL(fStore->Read(id, offset, buffer, count)); }
// ROM 0x00123640 GetStoreSizes__9TMuxStoreFPlT1
NewtonErr	TMuxStore::GetStoreSizes(long* totalSize, long* usedSize)	{ MUX_CALL(fMonitor->GetStoreSizes(totalSize, usedSize)); }
// ROM 0x001236cc IsReadOnly__9TMuxStoreFPUc
NewtonErr	TMuxStore::IsReadOnly(Boolean* isReadOnly)			{ MUX_CALL(fStore->IsReadOnly(isReadOnly)); }
// ROM 0x00123750 LockStore__9TMuxStoreFv
NewtonErr	TMuxStore::LockStore(void)							{ MUX_CALL(fMonitor->LockStore()); }
// ROM 0x001237cc UnlockStore__9TMuxStoreFv
NewtonErr	TMuxStore::UnlockStore(void)						{ MUX_CALL(fMonitor->UnlockStore()); }
// ROM 0x00123850 Abort__9TMuxStoreFv
NewtonErr	TMuxStore::Abort(void)								{ MUX_CALL(fMonitor->Abort()); }
// ROM 0x001238cc Idle__9TMuxStoreFPUcT1
NewtonErr	TMuxStore::Idle(Boolean* arg1, Boolean* arg2)		{ MUX_CALL(fMonitor->Idle(arg1, arg2)); }
// ROM 0x00123958 NextObject__9TMuxStoreFUlPUl
NewtonErr	TMuxStore::NextObject(PSSId id, PSSId* nextId)		{ MUX_CALL(fStore->NextObject(id, nextId)); }
// ROM 0x001239e4 CheckIntegrity__9TMuxStoreFPUl
NewtonErr	TMuxStore::CheckIntegrity(ULong* arg)				{ MUX_CALL(fMonitor->CheckIntegrity(arg)); }
// ROM 0x00123c24 NewWithinTransaction__9TMuxStoreFlPUl
NewtonErr	TMuxStore::NewWithinTransaction(long size, PSSId* id)	{ MUX_CALL(fMonitor->NewWithinTransaction(size, id)); }
// ROM 0x00123cb0 StartTransactionAgainst__9TMuxStoreFUl
NewtonErr	TMuxStore::StartTransactionAgainst(PSSId id)		{ MUX_CALL(fMonitor->StartTransactionAgainst(id)); }
// ROM 0x00123d34 SeparatelyAbort__9TMuxStoreFUl
NewtonErr	TMuxStore::SeparatelyAbort(PSSId id)				{ MUX_CALL(fMonitor->SeparatelyAbort(id)); }
// ROM 0x00123db8 AddToCurrentTransaction__9TMuxStoreFUl
NewtonErr	TMuxStore::AddToCurrentTransaction(PSSId id)		{ MUX_CALL(fMonitor->AddToCurrentTransaction(id)); }
// ROM 0x00123e44 LockReadOnly__9TMuxStoreFv
NewtonErr	TMuxStore::LockReadOnly(void)						{ MUX_CALL(fMonitor->LockReadOnly()); }
// ROM 0x00123f2c NewObject__9TMuxStoreFPclPUl
NewtonErr	TMuxStore::NewObject(char* data, long size, PSSId* id)	{ MUX_CALL(fMonitor->NewObject(data, size, id)); }
// ROM 0x00123fc0 ReplaceObject__9TMuxStoreFUlPcl
NewtonErr	TMuxStore::ReplaceObject(PSSId id, char* data, long size)	{ MUX_CALL(fMonitor->ReplaceObject(id, data, size)); }
// ROM 0x00124054 CalcXIPObjectSize__9TMuxStoreFlT1Pl
NewtonErr	TMuxStore::CalcXIPObjectSize(long arg1, long arg2, long* size)	{ MUX_CALL(fStore->CalcXIPObjectSize(arg1, arg2, size)); }
// ROM 0x001240e8 NewXIPObject__9TMuxStoreFlPUl
NewtonErr	TMuxStore::NewXIPObject(long size, PSSId* id)		{ MUX_CALL(fMonitor->NewXIPObject(size, id)); }
// ROM 0x00124174 GetXIPObjectInfo__9TMuxStoreFUlPUlN22
NewtonErr	TMuxStore::GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3)	{ MUX_CALL(fStore->GetXIPObjectInfo(id, arg1, arg2, arg3)); }


// ROM 0x00123a70 OwnsObject__9TMuxStoreFUl
Boolean
TMuxStore::OwnsObject(PSSId id)
{
	Boolean owns = false;
	fLock->Acquire(kWaitOnBlock);
	newton_try
	{
		owns = fStore->OwnsObject(id);
	}
	newton_catch_all
	{
		fLock->Release();
		rethrow;
	}
	end_try;
	fLock->Release();
	return owns;
}


// The rest go straight to the store, unlocked.

// ROM 0x00123a68 SetBuddy__9TMuxStoreFP6TStore
NewtonErr	TMuxStore::SetBuddy(TStore* buddy)					{ return fStore->SetBuddy(buddy); }
// ROM 0x00123af8 StoreKind__9TMuxStoreFv
const char*	TMuxStore::StoreKind(void)							{ return fStore->StoreKind(); }
// ROM 0x00123b88 IsSameStore__9TMuxStoreFPvUl
Boolean		TMuxStore::IsSameStore(void* data, ULong size)		{ return fStore->IsSameStore(data, size); }
// ROM 0x00123b90 IsLocked__9TMuxStoreFv
Boolean		TMuxStore::IsLocked(void)							{ return fStore->IsLocked(); }
// ROM 0x00123c04 VppOff__9TMuxStoreFv
NewtonErr	TMuxStore::VppOff(void)								{ return fStore->VppOff(); }
// ROM 0x00123c0c Sleep__9TMuxStoreFv
NewtonErr	TMuxStore::Sleep(void)								{ return fStore->Sleep(); }
// ROM 0x00123c14 Address__9TMuxStoreFUl
void*		TMuxStore::Address(PSSId id)						{ return fStore->Address(id); }
// ROM 0x00123c1c IsROM__9TMuxStoreFv
Boolean		TMuxStore::IsROM(void)								{ return fStore->IsROM(); }
// ROM 0x00123e3c InSeparateTransaction__9TMuxStoreFUl
Boolean		TMuxStore::InSeparateTransaction(PSSId id)			{ return fStore->InSeparateTransaction(id); }
// ROM 0x00123f18 UnlockReadOnly__9TMuxStoreFUc
NewtonErr	TMuxStore::UnlockReadOnly(Boolean reset)			{ return fStore->UnlockReadOnly(reset); }
// ROM 0x00123f24 InTransaction__9TMuxStoreFv
Boolean		TMuxStore::InTransaction(void)						{ return fStore->InTransaction(); }
