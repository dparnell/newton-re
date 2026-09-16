/*
	File:		stores/PackageStore.cpp

	Contains:	TPackageStore (PackageStore.h): the read-only store over a
				package's soup part.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "PackageStore.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"


PROTOCOL_CLASSINFO(TPackageStore, "TStore", "", 0, 0, nil)	// ROM 0x0037aa20 ClassInfo__13TPackageStoreSFv


// ROM 0x00162ae8 Sizeof__13TPackageStoreSFv
size_t
TPackageStore::Sizeof()
{
	return sizeof(TPackageStore);		// the ROM: 0x1c
}


// ROM 0x00162afc New__13TPackageStoreFv
TPackageStore*
TPackageStore::New()
{
	fData = nil;
	fSize = 0;
	fLockCount = 0;
	return this;
}


// ROM 0x00162b10 Delete__13TPackageStoreFv
void
TPackageStore::Delete()
{ }


// ROM 0x00162b14 Init__13TPackageStoreFPvUlT2iT2T1
// The store is the package data given.
NewtonErr
TPackageStore::Init(void* storeAddress, ULong storeSize, ULong /*arg3*/, int /*socketNumber*/, ULong /*flags*/, void* /*pssInfo*/)
{
	fData = (SPackageStoreData*) storeAddress;
	fSize = storeSize;
	return noErr;
}


// ROM 0x00162b24 NeedsFormat__13TPackageStoreFPUc
NewtonErr
TPackageStore::NeedsFormat(Boolean* needsFormat)
{
	*needsFormat = false;
	return noErr;
}


// ROM 0x001625ec Format__13TPackageStoreFv
NewtonErr
TPackageStore::Format()
{
	return kSError_WriteProtected;
}


// ROM 0x00162924 GetRootId__13TPackageStoreFPUl
NewtonErr
TPackageStore::GetRootId(PSSId* rootId)
{
	*rootId = GetBigEndianWord(&fData->fRootId);
	return noErr;
}


// ROM 0x001625f8 NewObject__13TPackageStoreFlPUl
NewtonErr
TPackageStore::NewObject(long /*size*/, PSSId* /*id*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162604 EraseObject__13TPackageStoreFUl
NewtonErr
TPackageStore::EraseObject(PSSId /*id*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162610 DeleteObject__13TPackageStoreFUl
NewtonErr
TPackageStore::DeleteObject(PSSId /*id*/)
{
	return kSError_WriteProtected;
}


// ROM 0x0016261c SetObjectSize__13TPackageStoreFUll
NewtonErr
TPackageStore::SetObjectSize(PSSId /*id*/, long /*size*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162938 GetObjectSize__13TPackageStoreFUlPl
// The distance between an object's offset and the next.
NewtonErr
TPackageStore::GetObjectSize(PSSId id, long* size)
{
	if (id >= GetBigEndianWord(&fData->fNumObjects))
		return kSError_BadPSSID;
	*size = (long) (GetBigEndianWord(&fData->fOffsets[id + 1]) - GetBigEndianWord(&fData->fOffsets[id]));
	return noErr;
}


// ROM 0x00162628 Write__13TPackageStoreFUllPcT2
NewtonErr
TPackageStore::Write(PSSId /*id*/, long /*offset*/, char* /*buffer*/, long /*count*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162978 Read__13TPackageStoreFUllPcT2
// count bytes of the object from offset; what there is of a range past
// the end is copied and kSError_ObjectOverRun answered.
NewtonErr
TPackageStore::Read(PSSId id, long offset, char* buffer, long count)
{
	if (id >= GetBigEndianWord(&fData->fNumObjects))
		return kSError_BadPSSID;
	long start = (long) GetBigEndianWord(&fData->fOffsets[id]);
	long size = (long) GetBigEndianWord(&fData->fOffsets[id + 1]) - start;
	NewtonErr err = kSError_ObjectOverRun;
	if (offset >= 0 && offset < size)
	{
		if (offset + count <= size)
			err = noErr;
		else
			count = size - offset;
		BlockMove((char*) fData + start + offset, buffer, count);
	}
	return err;
}


// ROM 0x00162a04 GetStoreSizes__13TPackageStoreFPlT1
// The package data is all used.
NewtonErr
TPackageStore::GetStoreSizes(long* totalSize, long* usedSize)
{
	*totalSize = (long) fSize;
	*usedSize = (long) fSize;
	return noErr;
}


// ROM 0x00162634 IsReadOnly__13TPackageStoreFPUc
NewtonErr
TPackageStore::IsReadOnly(Boolean* isReadOnly)
{
	*isReadOnly = true;
	return noErr;
}


// ROM 0x001627b0 LockStore__13TPackageStoreFv
NewtonErr
TPackageStore::LockStore()
{
	fLockCount++;
	return noErr;
}


// ROM 0x001627c4 UnlockStore__13TPackageStoreFv
NewtonErr
TPackageStore::UnlockStore()
{
	fLockCount--;
	return noErr;
}


// ROM 0x001627d8 Abort__13TPackageStoreFv
NewtonErr
TPackageStore::Abort()
{
	fLockCount = 0;
	return noErr;
}


// ROM 0x00162644 Idle__13TPackageStoreFPUcT1
NewtonErr
TPackageStore::Idle(Boolean* arg1, Boolean* arg2)
{
	*arg1 = false;
	*arg2 = false;
	return noErr;
}


// ROM 0x00162654 NextObject__13TPackageStoreFUlPUl
// Not iterable: no next object.
NewtonErr
TPackageStore::NextObject(PSSId /*id*/, PSSId* nextId)
{
	*nextId = 0;
	return noErr;
}


// ROM 0x00162660 CheckIntegrity__13TPackageStoreFPUl
NewtonErr
TPackageStore::CheckIntegrity(ULong* /*arg*/)
{
	return noErr;
}


// ROM 0x001627a0 SetBuddy__13TPackageStoreFP6TStore
NewtonErr
TPackageStore::SetBuddy(TStore* /*buddy*/)
{
	return noErr;
}


// ROM 0x001627a8 OwnsObject__13TPackageStoreFUl
Boolean
TPackageStore::OwnsObject(PSSId /*id*/)
{
	return true;
}


// ROM 0x0016280c Address__13TPackageStoreFUl
void*
TPackageStore::Address(PSSId /*id*/)
{
	return nil;
}


// ROM 0x00162814 StoreKind__13TPackageStoreFv
const char*
TPackageStore::StoreKind()
{
	return "Package";
}


// ROM 0x00162804 SetStore__13TPackageStoreFP6TStoreUl
NewtonErr
TPackageStore::SetStore(TStore* /*store*/, ULong /*arg*/)
{
	return noErr;
}


// ROM 0x001627fc IsSameStore__13TPackageStoreFPvUl
Boolean
TPackageStore::IsSameStore(void* /*data*/, ULong /*size*/)
{
	return false;
}


// ROM 0x001627e8 IsLocked__13TPackageStoreFv
Boolean
TPackageStore::IsLocked()
{
	return fLockCount != 0;
}


// ROM 0x0016290c VppOff__13TPackageStoreFv
NewtonErr
TPackageStore::VppOff()
{
	return noErr;
}


// ROM 0x00162914 Sleep__13TPackageStoreFv
NewtonErr
TPackageStore::Sleep()
{
	return noErr;
}


// ROM 0x0016291c IsROM__13TPackageStoreFv
Boolean
TPackageStore::IsROM()
{
	return true;
}


// ROM 0x00162a70 NewWithinTransaction__13TPackageStoreFlPUl
NewtonErr
TPackageStore::NewWithinTransaction(long size, PSSId* id)
{
	return NewObject(size, id);
}


// ROM 0x00162a74 StartTransactionAgainst__13TPackageStoreFUl
NewtonErr
TPackageStore::StartTransactionAgainst(PSSId /*id*/)
{
	return noErr;
}


// ROM 0x00162a8c SeparatelyAbort__13TPackageStoreFUl
NewtonErr
TPackageStore::SeparatelyAbort(PSSId /*id*/)
{
	return noErr;
}


// ROM 0x00162a94 AddToCurrentTransaction__13TPackageStoreFUl
NewtonErr
TPackageStore::AddToCurrentTransaction(PSSId /*id*/)
{
	return noErr;
}


// ROM 0x00162a9c InSeparateTransaction__13TPackageStoreFUl
Boolean
TPackageStore::InSeparateTransaction(PSSId /*id*/)
{
	return false;
}


// ROM 0x00162aa4 LockReadOnly__13TPackageStoreFv
NewtonErr
TPackageStore::LockReadOnly()
{
	return noErr;
}


// ROM 0x00162aac UnlockReadOnly__13TPackageStoreFUc
NewtonErr
TPackageStore::UnlockReadOnly(Boolean /*reset*/)
{
	return noErr;
}


// ROM 0x00162ab4 InTransaction__13TPackageStoreFv
Boolean
TPackageStore::InTransaction()
{
	return IsLocked();
}


// ROM 0x00162ad0 NewObject__13TPackageStoreFPclPUl
NewtonErr
TPackageStore::NewObject(char* /*data*/, long /*size*/, PSSId* /*id*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162adc ReplaceObject__13TPackageStoreFUlPcl
NewtonErr
TPackageStore::ReplaceObject(PSSId /*id*/, char* /*data*/, long /*size*/)
{
	return kSError_WriteProtected;
}


// ROM 0x00162ab8 CalcXIPObjectSize__13TPackageStoreFlT1Pl
NewtonErr
TPackageStore::CalcXIPObjectSize(long /*arg1*/, long /*arg2*/, long* /*size*/)
{
	return kError_XIP_Not_Possible;
}


// ROM 0x00162ac4 NewXIPObject__13TPackageStoreFlPUl
NewtonErr
TPackageStore::NewXIPObject(long /*size*/, PSSId* /*id*/)
{
	return kError_XIP_Not_Possible;
}


// ROM 0x00162af0 GetXIPObjectInfo__13TPackageStoreFUlPUlN22
NewtonErr
TPackageStore::GetXIPObjectInfo(PSSId /*id*/, ULong* /*arg1*/, ULong* /*arg2*/, ULong* /*arg3*/)
{
	return kError_XIP_Not_Possible;
}
