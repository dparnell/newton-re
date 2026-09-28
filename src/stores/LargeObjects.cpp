/*
	File:		stores/LargeObjects.cpp

	Contains:	Large objects, the user side - see LargeObjects.h.  The
				domain manager's side is HostLargeObjects.cpp.

	DEVIATION: the ROM reaches the domain manager with MonitorDispatchSWI
	on the monitor GetROMDomainUserMonitor (or, for a flush,
	GetROMDomainManagerId) answers; the host calls ROMDomainUserRequest.

	Reconstructed from the MP2x00 US ROM (0x001014bc-0x00103ccc,
	0x00161f90); each function cites its origin.
*/

#include "LargeObjects.h"
#include "StoreCompander.h"
#include "StoreWrapper.h"
#include "Soups.h"				// IsValidStore
#include "Pipes.h"
#include "Protocols.h"
#include "ByteOrder.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "AppWorld.h"			// TForkWorld (a pipe to read from forks)
#include "UserTasks.h"			// GetGlobals

#include <string.h>
#include <stdlib.h>

extern const ExceptionName exPipeException;


// count bytes from the pipe; its exception's error, or noErr
static NewtonErr
ReadBlock(CPipe* pipe, char* block, long count)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		long got = count;
		Boolean eof = false;
		pipe->ReadChunk(block, got, eof);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x001adcb0 __ct__9RDMParamsFv
RDMParams::RDMParams()
{
	fStore = nil;
	fObjectId = 0;
	fAddress = 0;
	fPackageId = 0;
	fReadOnly = true;
	fOffset = -1;
	fDirty = false;
	fSize = 0;				// (the ROM leaves +0x10 alone)
}


/*------------------------------------------------------------------------------
	C r e a t i n g
------------------------------------------------------------------------------*/

// ROM 0x00161f90 PackageAllocationOk__FP6TStoreUl
// Whether the object's root says it is complete ('paok').
Boolean
PackageAllocationOk(TStore* store, PSSId rootId)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	NewtonErr err = store->Read(rootId, 0, (char*) root, 0x14);
	return err == noErr && GetBigEndianWord(root + kLORootState) == kLOComplete;
}


// ROM 0x001017a8 InitializeChunkArray__FP6TStorePUlUl
// The chunk array: an empty block object (in a separate transaction) for
// every 0x400 bytes of the size, their ids written into a new object.
// ROM BUG kept: when a block object cannot be made, the ones already made
// are to be given back, but every pass of the loop aborts the same entry -
// the first one that was *not* made (whatever the array held there) - and
// the loop stops at its first error, so nothing made is given back; nor is
// the chunk array object itself.
NewtonErr
InitializeChunkArray(TStore* store, ULong* chunkArrayId, ULong size)
{
	ULong count = (size + 0x3ff) >> 10;
	long bytes = count << 2;
	// DEVIATION: a word to spare, which the ROM BUG's abort reads when every
	// block was made and the write failed (the ROM reads past its array);
	// cleared, where the ROM's operator new leaves the heap's bytes
	UByte* ids = (UByte*) calloc(bytes + 4, 1);
	ULong made = 0;
	NewtonErr err = MemError();
	PSSId arrayId = 0;
	if (err == noErr && (err = store->NewWithinTransaction(bytes, &arrayId)) == noErr)
	{
		*chunkArrayId = arrayId;
		for (made = 0; made < count; made++)
		{
			PSSId blockId = 0;
			if ((err = store->NewWithinTransaction(0, &blockId)) != noErr)
				break;
			PutBigEndianWord(ids + made * 4, (ULong32) blockId);
		}
		if (err == noErr)
			err = store->Write(*chunkArrayId, 0, (char*) ids, bytes);
	}
	if (err != noErr)
	{
		for (long i = 0; i < (long) made; i++)
			if (store->SeparatelyAbort(GetBigEndianWord(ids + made * 4)) != noErr)
				break;
	}
	free(ids);
	return err;
}


// ROM 0x001018f4 FillChunkArray__FP6TStoreUlT2P5CPipeT2PcT2P11TLOCallback
// The object's data read from the pipe a block at a time and written
// through the compander.  On a failure the blocks written so far are given
// back (SeparatelyAbort).
// NOT YET RECONSTRUCTED: the callback (TLOCallback, told how far the
// filling has got every so many bytes) - the host's callers give none.
NewtonErr
FillChunkArray(TStore* store, ULong rootId, ULong chunkArrayId, CPipe* pipe, ULong size, char* compander, ULong parametersId,
			   TLOCallback* /*callback*/)
{
	ULong count = 0;
	TStoreCompander* theCompander = nil;
	char* block = (char*) malloc(0x400);
	NewtonErr err = MemError();
	if (err == noErr)
	{
		theCompander = TStoreCompander::New(compander);
		if (theCompander == nil)
			err = kError_No_Memory;
		else if ((err = theCompander->Init(store, rootId, parametersId, false, false)) == noErr
				 && (err = store->GetObjectSize(chunkArrayId, (long*) &count)) == noErr)
		{
			count >>= 2;
			long left = size;
			for (long i = 0; i < (long) count; i++)
			{
				long n = left < 0x400 ? left : 0x400;
				err = ReadBlock(pipe, block, n);
				StorePSSId blockId = 0;
				if (err != noErr
					|| (err = store->Read(chunkArrayId, i << 2, (char*) &blockId, 4)) != noErr
					|| (err = theCompander->Write(i << 10, block, n, 0)) != noErr)
					break;
				left -= n;
			}
		}
	}
	if (err != noErr)
	{
		for (long i = 0; i < (long) count; i++)
		{
			StorePSSId blockId = 0;
			if (store->Read(chunkArrayId, i << 2, (char*) &blockId, 4) != noErr
				|| store->SeparatelyAbort(GetBigEndianWord((const UByte*) &blockId)) != noErr)
				break;
		}
	}
	if (theCompander != nil)
		theCompander->Delete();
	free(block);
	return err;
}


// ROM 0x00101bd0 LODefaultCreate__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
// The root, the compander's name and parameters and the chunk array made
// (each in a separate transaction), then the root written - its flags the
// kind, 2, with 0x10000 when not made read-only - then filled from the
// pipe if there is one and marked complete ('paok').  On a failure what
// was made is given back.
NewtonErr
LODefaultCreate(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
				TLOCallback* callback)
{
	UByte root[kLargeObjectRootSize];
	memset(root, 0, sizeof(root));
	PutBigEndianWord(root + kLORootFlags, 2);		// (LargeObjectRoot's constructor)
	PSSId rootId = 0, nameId = 0, paramsId = 0;
	ULong chunkArrayId = 0;
	NewtonErr err = store->NewWithinTransaction(kLargeObjectRootSize, &rootId);
	if (err == noErr)
	{
		*id = rootId;
		if (compander == nil)
			compander = (char*) "TSimpleStoreCompander";
		if ((err = store->NewWithinTransaction(strlen(compander), &nameId)) == noErr
			&& (err = store->Write(nameId, 0, compander, strlen(compander))) == noErr
			&& (err = store->NewWithinTransaction(parametersSize, &paramsId)) == noErr
			&& (parametersSize == 0 || (err = store->Write(paramsId, 0, (char*) parameters, parametersSize)) == noErr)
			&& (err = InitializeChunkArray(store, &chunkArrayId, size)) == noErr)
		{
			PutBigEndianWord(root + kLORootChunkArray, (ULong32) chunkArrayId);
			PutBigEndianWord(root + kLORootCompanderName, (ULong32) nameId);
			PutBigEndianWord(root + kLORootCompanderParams, (ULong32) paramsId);
			PutBigEndianWord(root + kLORootFlags, (readOnly == 0 ? 0x10000 : 0) | 2);
			PutBigEndianWord(root + kLORootSize, size);
			if ((err = store->Write(rootId, 0, (char*) root, kLargeObjectRootSize)) == noErr
				&& (pipe == nil
					|| (err = FillChunkArray(store, rootId, chunkArrayId, pipe, size, compander, paramsId, callback)) == noErr))
			{
				PutBigEndianWord(root + kLORootState, kLOComplete);
				err = store->Write(rootId, 0, (char*) root, kLargeObjectRootSize);
			}
		}
	}
	if (err != noErr)
	{
		if (paramsId != 0)
			store->SeparatelyAbort(paramsId);
		if (nameId != 0)
			store->SeparatelyAbort(nameId);
		if (chunkArrayId != 0)
			store->SeparatelyAbort(chunkArrayId);
		if (rootId != 0)
			store->SeparatelyAbort(rootId);
	}
	return err;
}


// ROM 0x00102f70 CreateLargeObject__FPUlP6TStorelPcPvT3
// An empty large object of the size.
NewtonErr
CreateLargeObject(ULong* id, TStore* store, long size, char* compander, void* parameters, long parametersSize)
{
	return CreateLargeObject(id, store, nil, size, false, compander, parameters, parametersSize, nil, false);
}


// ROM 0x00103300 CreateLargeObject__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallbackT5
// A large object made by the compander's own large-object store if it has
// one (TLrgObjStore), else the default way - as long as the compander is
// known at all (kError_Bad_Parameters when it is not).  With a pipe to
// read from, the world forks first.
// NOT YET RECONSTRUCTED: TLrgObjStore, and made from compressed blocks
// (LODefCreateFromComp): the host answers kError_Call_Not_Implemented.
NewtonErr
CreateLargeObject(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
				  TLOCallback* callback, UChar fromCompressed)
{
	if (pipe != nil)
	{
		NewtonErr err = ((TForkWorld*) GetGlobals())->Fork(nil);
		if (err != noErr)
			return err;
	}
	TProtocol* allocator = NewByName("TLrgObjStore", nil, compander);
	if (allocator != nil)
		return kError_Call_Not_Implemented;		// (never on the host: none is registered)
	if (ClassInfoByName("TStoreCompander", compander) == nil)
		return kError_Bad_Parameters;
	if (fromCompressed)
		return kError_Call_Not_Implemented;
	return LODefaultCreate(id, store, pipe, size, readOnly, compander, parameters, parametersSize, callback);
}


/*------------------------------------------------------------------------------
	M a p p i n g
------------------------------------------------------------------------------*/

// ROM 0x00103bc0 MapLargeObject__FPUlP6TStoreUlUc
// The object mapped (read-only or not): its address.  An object that is
// not complete ('paok') is kError_Bad_Object.
NewtonErr
MapLargeObject(ULong* address, TStore* store, PSSId id, UChar readOnly)
{
	NewtonErr err = kError_Bad_Object;
	if (PackageAllocationOk(store, id))
	{
		RDMParams params;
		params.fStore = store;
		params.fObjectId = id;
		params.fReadOnly = readOnly;
		err = ROMDomainUserRequest(kRDMMap, &params);
		if (err == noErr)
			*address = params.fAddress;
	}
	return err;
}


// ROM 0x00102fb8 UnmapLargeObject__FPP6TStorePUlUl
NewtonErr
UnmapLargeObject(TStore** store, ULong* id, ULong address)
{
	RDMParams params;
	params.fAddress = address;
	NewtonErr err = ROMDomainUserRequest(kRDMUnmap, &params);
	*store = params.fStore;
	*id = params.fObjectId;
	return err;
}


// ROM 0x00103ccc UnmapLargeObject__FUl
NewtonErr
UnmapLargeObject(ULong address)
{
	TStore* store;
	ULong id;
	return UnmapLargeObject(&store, &id, address);
}


// ROM 0x00103c58 ResizeLargeObject__FPUlUllT3
// The mapped object made size bytes long, the difference made or taken at
// the offset (-1: at the end).  ==> its address, which may have moved.
NewtonErr
ResizeLargeObject(ULong* newAddress, ULong address, long size, long offset)
{
	RDMParams params;
	params.fAddress = address;
	params.fSize = size;
	params.fOffset = offset;
	NewtonErr err = ROMDomainUserRequest(kRDMResize, &params);
	*newAddress = params.fAddress;
	return err;
}


// ROM 0x00103828 FlushLargeObject__FP6TStoreUl
// What has been written to the mapped object put back on the store.  An
// object that is not mapped (kError_No_Such_Package, in the domain
// manager's words) has nothing to flush.
NewtonErr
FlushLargeObject(TStore* store, PSSId id)
{
	RDMParams params;
	params.fPackageId = 0;
	params.fStore = store;
	params.fObjectId = id;
	NewtonErr err = ROMDomainUserRequest(kRDMFlush, &params);
	if (err == kError_No_Such_Package)
		err = noErr;
	return err;
}


// ROM 0x00103028 AbortObject__FP6TStoreUl
// The object's changes thrown away: through the domain manager when it is
// mapped, else its separate transaction's (LODefaultDoTransaction).
NewtonErr
AbortObject(TStore* store, PSSId id)
{
	ULong address;
	NewtonErr err = StoreToVAddr(&address, store, id);
	if (err == noErr)
	{
		RDMParams params;
		params.fAddress = address;
		err = ROMDomainUserRequest(kRDMAbort, &params);
	}
	else if (store->InSeparateTransaction(id))
		err = LODefaultDoTransaction(store, id, 0, 1, true);
	return err;
}


// ROM 0x001030e4 AbortObjects__FP6TStore
NewtonErr
AbortObjects(TStore* store)
{
	RDMParams params;
	params.fStore = store;
	return ROMDomainUserRequest(kRDMAbort, &params);
}


// ROM 0x0010313c CommitObject__FUl
NewtonErr
CommitObject(ULong address)
{
	RDMParams params;
	params.fAddress = address;
	return ROMDomainUserRequest(kRDMCommit, &params);
}


// ROM 0x00103194 CommitObjects__FP6TStore
NewtonErr
CommitObjects(TStore* store)
{
	RDMParams params;
	params.fStore = store;
	return ROMDomainUserRequest(kRDMCommit, &params);
}


/*------------------------------------------------------------------------------
	F i n d i n g   o u t
------------------------------------------------------------------------------*/

// ROM 0x001031ec VAddrToStore__FPP6TStorePUlUl
NewtonErr
VAddrToStore(TStore** store, ULong* id, ULong address)
{
	RDMParams params;
	params.fAddress = address;
	NewtonErr err = ROMDomainUserRequest(kRDMInfo, &params);
	*store = params.fStore;
	*id = params.fObjectId;
	return err;
}


// ROM 0x0010325c StoreToVAddr__FPUlP6TStoreUl
NewtonErr
StoreToVAddr(ULong* address, TStore* store, PSSId id)
{
	RDMParams params;
	params.fStore = store;
	params.fObjectId = id;
	NewtonErr err = ROMDomainUserRequest(kRDMAddress, &params);
	*address = params.fAddress;
	return err;
}


// ROM 0x00103a04 VAddrToId__FPUlUl
NewtonErr
VAddrToId(ULong* packageId, ULong address)
{
	RDMParams params;
	params.fAddress = address;
	NewtonErr err = ROMDomainUserRequest(kRDMObjectAt, &params);
	if (err != noErr)
		params.fPackageId = 0;
	*packageId = params.fPackageId;
	return err;
}


// ROM 0x00103a70 VAddrToBase__FPUlUl
NewtonErr
VAddrToBase(ULong* base, ULong address)
{
	RDMParams params;
	params.fAddress = address;
	NewtonErr err = ROMDomainUserRequest(kRDMObjectAt, &params);
	*base = err == noErr ? params.fAddress : 0;
	return err;
}


// ROM 0x001034f0 ObjectSize__FUl
// The mapped object's size.
// NOT YET RECONSTRUCTED: a package kept as a large object answers its
// package's size (IsPackageHeader, TPackageIterator) - packages sit above
// the stores here.
long
ObjectSize(ULong address)
{
	RDMParams params;
	params.fSize = 0;
	params.fAddress = address;
	ROMDomainUserRequest(kRDMInfo, &params);
	return params.fSize;
}


// ROM 0x00103470 IsOnStoreAsPackage__FP6TStoreUl
// Whether the root is a package's (kind 1) or a large object's (kind 2)
// flagged as one (0x20000).
Boolean
IsOnStoreAsPackage(TStore* store, PSSId id)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	ULong flags = GetBigEndianWord(root + kLORootFlags);
	return err == noErr && (flags & 0xffff) < 3 && ((flags & 0xffff) == 1 || (flags & 0x20000) != 0);
}


// ROM 0x00103660 LargeObjectAddressIsValid__FUl
Boolean
LargeObjectAddressIsValid(ULong address)
{
	RDMParams params;
	params.fAddress = address;
	if (ROMDomainUserRequest(kRDMObjectAt, &params) != noErr)
		return false;
	return IsValidStore(params.fStore);
}


// ROM 0x001036cc LargeObjectIsDirty__FUl
Boolean
LargeObjectIsDirty(ULong address)
{
	RDMParams params;
	params.fAddress = address;
	if (ROMDomainUserRequest(kRDMInfo, &params) != noErr)
		params.fDirty = false;
	return params.fDirty;
}


// ROM 0x00103730 LargeObjectIsReadOnly__FUl
Boolean
LargeObjectIsReadOnly(ULong address)
{
	RDMParams params;
	params.fAddress = address;
	if (ROMDomainUserRequest(kRDMInfo, &params) != noErr)
		params.fReadOnly = true;
	return params.fReadOnly;
}


// ROM 0x00103794 GetLargeObjectInfo__FP9RDMParamsUl
NewtonErr
GetLargeObjectInfo(RDMParams* params, ULong address)
{
	params->fAddress = address;
	return ROMDomainUserRequest(kRDMInfo, params);
}


// ROM 0x001037ec StorageSizeOfLargeObject__FUl
long
StorageSizeOfLargeObject(ULong address)
{
	TStore* store;
	ULong id;
	if (VAddrToStore(&store, &id, address) != noErr)
		return 0;
	return StorageSizeOfLargeObject(store, id);
}


// ROM 0x0010395c StorageSizeOfLargeObject__FP6TStoreUl
// What the object takes up on the store (flushed first); a failure is
// thrown as evt.ex.abt.
long
StorageSizeOfLargeObject(TStore* store, PSSId id)
{
	long size = 0;
	NewtonErr err = FlushLargeObject(store, id);
	if (err == noErr)
	{
		// (GetLOAllocator: no TLrgObjStore on the host, so only whether the
		// compander is known matters)
		char* name = nil;
		err = LOCompanderName(&name, store, id);
		if (err == noErr)
		{
			if (ClassInfoByName("TStoreCompander", name) == nil)
				err = kError_Bad_Parameters;
			free(name);
		}
		if (err == noErr)
			size = LODefaultStorageSize(store, id);
	}
	if (err != noErr)
		Throw("evt.ex.abt", (void*) (Long) err, nil);
	return size;
}


// ROM 0x00101894 LOCompanderNameStrLen__FP6TStoreUlPl
NewtonErr
LOCompanderNameStrLen(TStore* store, PSSId id, long* length)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	if (err == noErr)
		err = store->GetObjectSize(GetBigEndianWord(root + kLORootCompanderName), length);
	return err;
}


// ROM 0x00102a2c LOCompanderName__FP6TStoreUlPc
// The compander's name, terminated, into name.
NewtonErr
LOCompanderName(TStore* store, PSSId id, char* name)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	long length = 0;
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	PSSId nameId = GetBigEndianWord(root + kLORootCompanderName);
	if (err == noErr && (err = store->GetObjectSize(nameId, &length)) == noErr)
	{
		name[length] = 0;
		err = store->Read(nameId, 0, name, length);
	}
	return err;
}


// ROM 0x00102cb8 LOCompanderName__FPPcP6TStoreUl
// The compander's name in a block of its own (the caller's to free).
NewtonErr
LOCompanderName(char** name, TStore* store, PSSId id)
{
	long length = 0;
	NewtonErr err = LOCompanderNameStrLen(store, id, &length);
	if (err == noErr)
	{
		*name = (char*) malloc(length + 1);
		err = *name == nil ? kError_No_Memory : noErr;
		if (err == noErr && (err = LOCompanderName(store, id, *name)) != noErr)
		{
			free(*name);
			*name = nil;
		}
	}
	return err;
}


// ROM 0x00102370 GetPagesSize__FP6TStoreUl
// The block objects' sizes, together.
long
GetPagesSize(TStore* store, PSSId chunkArrayId)
{
	long total = 0;
	TCachedReadStore* cache = new TCachedReadStore(store, chunkArrayId, -1);
	if (cache != nil)
	{
		long count = (ULong) cache->fSize >> 2;
		for (long i = 0; i < count; i++)
		{
			void* word;
			long size;
			if (cache->GetDataPtr(i << 2, 4, &word) == noErr
				&& store->GetObjectSize(GetBigEndianWord((const UByte*) word), &size) == noErr)
				total += size;
		}
		delete cache;
	}
	return total;
}


// ROM 0x00102420 LODefaultStorageSize__FP6TStoreUl
// The root, the compander's parameters, the chunk array and the blocks.
long
LODefaultStorageSize(TStore* store, PSSId id)
{
	long total = 0;
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	if (err == noErr && (err = store->GetObjectSize(id, &total)) == noErr)
	{
		PSSId paramsId = GetBigEndianWord(root + kLORootCompanderParams);
		if (paramsId != 0)
		{
			long paramsSize;
			if (store->GetObjectSize(paramsId, &paramsSize) != noErr)
				return total;
			total += paramsSize;
		}
		PSSId chunkArrayId = GetBigEndianWord(root + kLORootChunkArray);
		long arraySize;
		if (store->GetObjectSize(chunkArrayId, &arraySize) == noErr)
			total = GetPagesSize(store, chunkArrayId) + arraySize + total;
	}
	return total;
}


/*------------------------------------------------------------------------------
	D e l e t i n g
------------------------------------------------------------------------------*/

// ROM 0x00103b3c DeleteLargeObject__FP6TStoreUl
// Unmapped if it is mapped, then deleted by whoever made it.
NewtonErr
DeleteLargeObject(TStore* store, PSSId id)
{
	ULong address;
	NewtonErr err = StoreToVAddr(&address, store, id);
	if (err != noErr || (err = UnmapLargeObject(address)) == noErr)
		err = LODeleteByProtocol(store, id);
	return err;
}


// ROM 0x00103adc LODeleteByProtocol__FP6TStoreUl
// (no TLrgObjStore on the host: GetLOAllocator never finds one)
NewtonErr
LODeleteByProtocol(TStore* store, PSSId id)
{
	return LODefaultDelete(store, id);
}


// ROM 0x0010231c LODefaultDelete__FP6TStoreUl
// Nothing: the ROM's is a return.  The store objects stay (a store's
// objects are taken back when the entry that refers to them goes).
NewtonErr
LODefaultDelete(TStore* /*store*/, PSSId /*id*/)
{
	return noErr;
}
