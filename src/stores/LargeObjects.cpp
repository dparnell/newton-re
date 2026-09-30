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
#include "PackageIterator.h"	// ObjectSize: a package's own size
#include "UserTasks.h"			// GetGlobals
#include "Frames.h"				// TLOCallback::Callback
#include "ObjectHeap.h"
#include "Interpreter.h"		// DoBlock
#include "ROMConstants.h"		// Rcanonicalpackagecallbackinfo
#include "RSSymbols.h"

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
// back (SeparatelyAbort).  A callback is told {size, bytes so far} each
// time its frequency's worth has been written.
NewtonErr
FillChunkArray(TStore* store, ULong rootId, ULong chunkArrayId, CPipe* pipe, ULong size, char* compander, ULong parametersId,
			   TLOCallback* callback)
{
	TLOCallbackInfo info;
	info.fPackageSize = size;
	info.fAmountRead = 0;
	info.fPackageName = nil;
	// (the ROM leaves the part and the number of parts as the stack had
	// them; nought here)
	info.fCurrentPart = 0;
	info.fNumberOfParts = 0;
	ULong sinceCall = 0;
	ULong done = 0;
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
				done += n;
				sinceCall += n;
				if (callback != nil && callback->fFrequency <= sinceCall)
				{
					info.fAmountRead = done;
					callback->fProc(callback, &info);
					sinceCall = 0;
				}
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


// ROM 0x00101e14 FillChunkArrayCompressed__FP6TStoreUlP5CPipelP11TLOCallback
// The chunk array's blocks filled from a compressed stream: for each, its
// length (a big-endian word) and that many bytes, put into the block
// object unopened (ReplaceObject) - they are as the compander left them.
// The callback is told the bytes read so far (counting the stream's two
// leading words) every so many bytes.  On a failure every block object is
// given back.
// ROM BUGS kept: a block's length is never checked against the 0x520-byte
// buffer it is read into; the callback's part count is left unset
// (DEVIATION: the host's is nought, where the ROM's is stack rubbish).
NewtonErr
FillChunkArrayCompressed(TStore* store, ULong chunkArrayId, CPipe* pipe, long streamSize, TLOCallback* callback)
{
	char* block = (char*) malloc(0x520);
	NewtonErr err = block == nil ? kError_No_Memory : noErr;		// (the ROM: MemError after operator new)
	long count = 0;
	if (err == noErr && (err = store->GetObjectSize(chunkArrayId, &count)) == noErr)
	{
		count = (long) ((ULong) count >> 2);
		TLOCallbackInfo progress;
		progress.fPackageSize = streamSize;
		progress.fAmountRead = 0;
		progress.fPackageName = nil;
		progress.fCurrentPart = 0;
		progress.fNumberOfParts = 0;
		ULong amountRead = 8;
		ULong sinceTold = 8;
		for (long i = 0; i < count; i++)
		{
			volatile long length = 0;
			volatile NewtonErr readErr = noErr;
			newton_try
			{
				UByte word[4];
				long n = 4;
				Boolean eof;
				pipe->ReadChunk(word, n, eof);
				length = (long) GetBigEndianWord(word);
				n = length;
				pipe->ReadChunk(block, n, eof);
			}
			newton_catch(exPipeException)
			{
				readErr = (NewtonErr) (long) (Long) _info.exception.data;
			}
			end_try;
			UByte idWord[4];
			if ((err = readErr) != noErr
			|| (err = store->Read(chunkArrayId, i << 2, (char*) idWord, 4)) != noErr
			|| (err = store->ReplaceObject(GetBigEndianWord(idWord), block, length)) != noErr)
				break;
			amountRead += length + 4;
			sinceTold += length + 4;
			if (callback != nil && callback->fFrequency <= sinceTold)
			{
				progress.fAmountRead = amountRead;
				callback->fProc(callback, &progress);
				sinceTold = 0;
			}
		}
	}
	if (err != noErr)
	{
		for (long i = 0; i < count; i++)
		{
			UByte idWord[4];
			if (store->Read(chunkArrayId, i << 2, (char*) idWord, 4) != noErr
			|| store->SeparatelyAbort(GetBigEndianWord(idWord)) != noErr)
				break;
		}
	}
	free(block);
	return err;
}


// ROM 0x00102080 LODefCreateFromComp__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
// A large object made from a compressed stream (LODefaultBackup's form):
// the root's flags and the object's size read first, then the root, the
// compander's name and parameters and the chunk array made (each in a
// separate transaction), the blocks filled from the stream as they are,
// and the root written - the flags as they came (without 0x10000 when made
// read-only), the size, 'paok'.  On a failure what was made is given back
// (though not the blocks the chunk array names).
NewtonErr
LODefCreateFromComp(ULong* id, TStore* store, CPipe* pipe, long streamSize, UChar readOnly, char* compander, void* parameters,
					long parametersSize, TLOCallback* callback)
{
	UByte root[kLargeObjectRootSize];
	memset(root, 0, sizeof(root));
	PutBigEndianWord(root + kLORootFlags, 2);		// (LargeObjectRoot's constructor)
	PSSId rootId = 0, nameId = 0, paramsId = 0;
	ULong chunkArrayId = 0;
	volatile NewtonErr err = noErr;
	volatile ULong flags = 0, size = 0;
	newton_try
	{
		UByte word[4];
		long n = 4;
		Boolean eof;
		pipe->ReadChunk(word, n, eof);
		flags = GetBigEndianWord(word);
		n = 4;
		pipe->ReadChunk(word, n, eof);
		size = GetBigEndianWord(word);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	if (err == noErr && (err = store->NewWithinTransaction(kLargeObjectRootSize, &rootId)) == noErr)
	{
		*id = rootId;
		if ((err = store->NewWithinTransaction(strlen(compander), &nameId)) == noErr
		&&  (err = store->Write(nameId, 0, compander, strlen(compander))) == noErr
		&&  (err = store->NewWithinTransaction(parametersSize, &paramsId)) == noErr
		&&  (parametersSize == 0 || (err = store->Write(paramsId, 0, (char*) parameters, parametersSize)) == noErr)
		&&  (err = InitializeChunkArray(store, &chunkArrayId, size)) == noErr
		&&  (err = FillChunkArrayCompressed(store, chunkArrayId, pipe, streamSize, callback)) == noErr)
		{
			PutBigEndianWord(root + kLORootChunkArray, (ULong32) chunkArrayId);
			PutBigEndianWord(root + kLORootCompanderName, (ULong32) nameId);
			PutBigEndianWord(root + kLORootCompanderParams, (ULong32) paramsId);
			PutBigEndianWord(root + kLORootState, kLOComplete);
			PutBigEndianWord(root + kLORootFlags, readOnly != 0 ? (flags & ~0x10000) : flags);
			PutBigEndianWord(root + kLORootSize, size);
			err = store->Write(rootId, 0, (char*) root, kLargeObjectRootSize);
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
// read from, the world forks first.  fromCompressed: the stream is
// LODefaultBackup's compressed form (LODefCreateFromComp).
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
	TLrgObjStore* allocator = (TLrgObjStore*) NewByName("TLrgObjStore", nil, compander);
	if (allocator == nil)
	{
		if (ClassInfoByName("TStoreCompander", compander) == nil)
			return kError_Bad_Parameters;
		if (fromCompressed)
			return LODefCreateFromComp(id, store, pipe, size, readOnly, compander, parameters, parametersSize, callback);
		return LODefaultCreate(id, store, pipe, size, readOnly, compander, parameters, parametersSize, callback);
	}
	NewtonErr err = allocator->Init();
	if (err == noErr)
	{
		if (!fromCompressed)
			err = allocator->Create(id, store, pipe, size, readOnly, compander, parameters, parametersSize, callback);
		else
			err = allocator->CreateFromCompressed(id, store, pipe, size, readOnly, compander, parameters, parametersSize, callback);
	}
	allocator->Delete();
	return err;
}


// ROM 0x00387404 New__12TLrgObjStoreSFPc
TLrgObjStore*
TLrgObjStore::New(char* implementation)
{
	TLrgObjStore* p = (TLrgObjStore*) AllocInstanceByName("TLrgObjStore", implementation);
	return p != nil ? (TLrgObjStore*) p->GlueNew() : nil;
}


// ROM 0x00387430 Delete__12TLrgObjStoreFv
void
TLrgObjStore::Delete()
{
	GlueDelete();
}


// ROM 0x0010389c GetLOAllocator__FP6TStoreUlPP12TLrgObjStore
// The large-object store that claims the object's compander by name, made
// and initialised (one that fails to initialise is given back, though
// *allocator is left pointing at it - ROM BUG kept: the callers only look
// at it when the answer is noErr); nil when the compander is a plain
// TStoreCompander.  ROM BUG kept: when neither knows the name, the name's
// block is not given back.
NewtonErr
GetLOAllocator(TStore* store, PSSId id, TLrgObjStore** allocator)
{
	*allocator = nil;
	char* name = nil;
	NewtonErr err = LOCompanderName(&name, store, id);
	if (err != noErr)
		return err;
	*allocator = (TLrgObjStore*) NewByName("TLrgObjStore", nil, name);
	if (*allocator == nil)
	{
		if (ClassInfoByName("TStoreCompander", name) == nil)
			return kError_Bad_Parameters;
		free(name);
		return noErr;
	}
	err = (*allocator)->Init();
	if (err != noErr)
		(*allocator)->Delete();
	free(name);
	return err;
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


// ROM 0x0027a318 XIPObjectHasMoved__FP6TStoreUl
void
XIPObjectHasMoved(TStore* store, PSSId id)
{
	RDMParams params;
	params.fStore = store;
	params.fObjectId = id;
	ROMDomainUserRequest(kRDMXIPObjectHasMoved, &params);
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
// The mapped object's size - a package's own size when the object is a
// package (its pages come to a whole number of 0x400 bytes).
long
ObjectSize(ULong address)
{
	RDMParams params;
	params.fSize = 0;
	params.fAddress = address;
	NewtonErr err = ROMDomainUserRequest(kRDMInfo, &params);
	if (err == noErr && params.fSize != 0 && IsPackageHeader((const void*) address, 0x34))
	{
		TPackageIterator iter((void*) address);
		if (iter.Init() == noErr)
			return (long) iter.PackageSize();
	}
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


// ROM 0x001032c8 IsOnStoreAsPackage__FUl
Boolean
IsOnStoreAsPackage(ULong address)
{
	TStore* store;
	ULong id;
	if (VAddrToStore(&store, &id, address) != noErr)
		return false;
	return IsOnStoreAsPackage(store, id);
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
	TLrgObjStore* allocator = nil;
	NewtonErr err = FlushLargeObject(store, id);
	if (err == noErr && (err = GetLOAllocator(store, id, &allocator)) == noErr)
	{
		if (allocator == nil)
			size = LODefaultStorageSize(store, id);
		else
			size = allocator->StorageSize(store, id);
	}
	if (allocator != nil)
		allocator->Delete();
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


// ROM 0x00102ac8 Callback__11TLOCallbackFP15TLOCallbackInfo
// The progress callback.  The first call only makes the info frame - a
// clone of canonicalPackageCallbackInfo with the package's size, how many
// parts it has and its name; every later one (when there is a function)
// sets the part being read and the amount read in it and calls the script
// with the frame, any exception the script throws dropped.
// DEVIATION: the name is the package's own big-endian UniChars, which the
// host puts in its own order before making the string.
void
TLOCallback::Callback(TLOCallbackInfo* info)
{
	if (ISNIL(*fInfoFrame))
	{
		*fInfoFrame = Clone(RefVar(Rcanonicalpackagecallbackinfo));
		SetFrameSlot(RefVar(*fInfoFrame), RefVar(RSSYMpackagesize), RefVar(MAKEINT(info->fPackageSize)));
		SetFrameSlot(RefVar(*fInfoFrame), RefVar(RSSYMnumberofparts), RefVar(MAKEINT(info->fNumberOfParts)));
		if (info->fPackageName != nil)
		{
			long length = 0;
			while (GetBigEndianHalf((const UByte*) (info->fPackageName + length)) != 0)
				length++;
			UniChar* name = (UniChar*) malloc((length + 1) * sizeof(UniChar));
			if (name != nil)
			{
				for (long i = 0; i <= length; i++)
					name[i] = GetBigEndianHalf((const UByte*) (info->fPackageName + i));
				SetFrameSlot(RefVar(*fInfoFrame), RefVar(RSSYMpackagename), RefVar(MakeString(name)));
				free(name);
			}
		}
		return;
	}
	if (ISNIL(*fFunction))
		return;
	RefVar args(AllocateArray(RefVar(RSSYMarray), 1));
	SetFrameSlot(RefVar(*fInfoFrame), RefVar(RSSYMcurrentpartnumber), RefVar(MAKEINT(info->fCurrentPart)));
	SetFrameSlot(RefVar(*fInfoFrame), RefVar(RSSYMamountread), RefVar(MAKEINT(info->fAmountRead)));
	SetArraySlot(args, 0, RefVar(*fInfoFrame));
	newton_try
	{
		DoBlock(RefVar(*fFunction), args);
	}
	newton_catch((ExceptionName) "")
	{}
	end_try;
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


// ROM 0x001014bc LOCompanderParameterSize__FP6TStoreUlPl
// The size of the compander's parameters (0 when it was made with none).
NewtonErr
LOCompanderParameterSize(TStore* store, PSSId id, long* size)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	*size = 0;
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	PSSId paramsId = GetBigEndianWord(root + kLORootCompanderParams);
	if (err == noErr && paramsId != 0)
		err = store->GetObjectSize(paramsId, size);
	return err;
}


// ROM 0x00101530 LOCompanderParameters__FP6TStoreUlPv
// The compander's parameters, into parameters (LOCompanderParameterSize
// bytes of room).
NewtonErr
LOCompanderParameters(TStore* store, PSSId id, void* parameters)
{
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	long size;
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	PSSId paramsId = GetBigEndianWord(root + kLORootCompanderParams);
	if (err == noErr && paramsId != 0 && (err = store->GetObjectSize(paramsId, &size)) == noErr)
		err = store->Read(paramsId, 0, (char*) parameters, size);
	return err;
}


/*------------------------------------------------------------------------------
	W r i t i n g   o n e   t o   a   p i p e
	(the object streamer's large binaries: the NSOF writer asks how many
	bytes one will take and then has it written)
------------------------------------------------------------------------------*/

// ROM 0x00102d38 LOSizeOfStream__FP6TStoreUlUc
// How many bytes LOWrite would write (compressed: as the blocks lie on the
// store) - the object's own store's answer when it has one.
//
// ROM BUG kept: a compander that is not registered answers an error with
// the name's block not given back.
long
LOSizeOfStream(TStore* store, PSSId id, UChar compressed)
{
	char* name = nil;
	long size = 0;
	NewtonErr err = LOCompanderName(&name, store, id);
	if (err == noErr)
	{
		TLrgObjStore* allocator = (TLrgObjStore*) NewByName("TLrgObjStore", nil, name);
		if (allocator == nil)
		{
			if (ClassInfoByName("TStoreCompander", name) == nil)
				return kError_Bad_Parameters;
			free(name);
			return LODefaultStreamSize(store, id, compressed);
		}
		if ((err = allocator->Init()) == noErr)
			size = allocator->SizeOfStream(store, id, compressed);
		allocator->Delete();
	}
	if (err != noErr)
		size = 0;
	free(name);
	return size;
}


// ROM 0x00102e44 LOWrite__FP5CPipeP6TStoreUlUcP11TLOCallback
// The large object written to pipe (compressed: its blocks as they lie) -
// by its own store when it has one.  ROM BUG kept: as LOSizeOfStream, an
// unregistered compander leaves the name's block behind.
NewtonErr
LOWrite(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback)
{
	char* name = nil;
	NewtonErr err = LOCompanderName(&name, store, id);
	if (err == noErr)
	{
		TLrgObjStore* allocator = (TLrgObjStore*) NewByName("TLrgObjStore", nil, name);
		if (allocator == nil)
		{
			if (ClassInfoByName("TStoreCompander", name) == nil)
				return kError_Bad_Parameters;
			free(name);
			return LODefaultBackup(pipe, store, id, compressed, callback);
		}
		if ((err = allocator->Init()) == noErr)
			err = allocator->Backup(pipe, store, id, compressed, callback);
		allocator->Delete();
	}
	free(name);
	return err;
}


// ROM 0x001024fc LODefaultStreamSize__FP6TStoreUlUc
// Uncompressed: the object's size (mapped read-only for the asking when it
// was not mapped).  Compressed: the blocks as they lie on the store, a
// size word before each, and two words in front (0 when the store cannot
// say).
long
LODefaultStreamSize(TStore* store, PSSId id, UChar compressed)
{
	if (!compressed)
	{
		ULong address;
		Boolean mapped = false;
		if (StoreToVAddr(&address, store, id) != noErr)
		{
			if (MapLargeObject(&address, store, id, true) != noErr)
				return 0;
			mapped = true;
		}
		long size = ObjectSize(address);
		if (mapped)
			UnmapLargeObject(address);
		return size;
	}
	UByte root[0x14];
	memset(root, 0, sizeof(root));
	long arraySize;
	if (store->Read(id, 0, (char*) root, 0x14) != noErr
	|| store->GetObjectSize(GetBigEndianWord(root + kLORootChunkArray), &arraySize) != noErr)
		return 0;
	return GetPagesSize(store, GetBigEndianWord(root + kLORootChunkArray)) + (long) (((ULong) arraySize >> 2) << 2) + 8;
}


// ROM 0x00102608 LODefaultBackup__FP5CPipeP6TStoreUlUcP11TLOCallback
// The object written to pipe.  Uncompressed: its bytes (mapped read-only
// for the writing when they were not).  Compressed: the root's flags word
// and the object's size, then every block as it lies on the store with its
// size before it (words big-endian), a callback told how far it has got.
// A pipe exception becomes the answer.
NewtonErr
LODefaultBackup(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback)
{
	char* block = NewPtr(0x520);
	NewtonErr err = MemError();
	if (err != noErr)
		return err;
	ULong address;
	Boolean mapped = false;
	if ((err = StoreToVAddr(&address, store, id)) != noErr)
	{
		if ((err = MapLargeObject(&address, store, id, true)) != noErr)
		{
			DisposePtr(block);
			return err;
		}
		mapped = true;
	}
	long size = ObjectSize(address);
	TLOCallbackInfo info;
	info.fPackageSize = size;
	info.fAmountRead = 0;
	info.fPackageName = nil;
	info.fCurrentPart = 0;
	info.fNumberOfParts = 0;
	ULong written = 0;
	ULong sinceCall = 0;
	if (!compressed)
	{
		newton_try
		{
			pipe->WriteChunk((const void*) address, size, false);
		}
		newton_catch(exPipeException)
		{
			err = (NewtonErr) (Long) CurrentException()->data;
		}
		end_try;
	}
	else
	{
		UByte root[0x14];
		memset(root, 0, sizeof(root));
		long arraySize;
		if ((err = store->Read(id, 0, (char*) root, 0x14)) != noErr
		|| (err = store->GetObjectSize(GetBigEndianWord(root + kLORootChunkArray), &arraySize)) != noErr)
		{
			DisposePtr(block);
			return err;
		}
		long count = (long) ((ULong) arraySize >> 2);
		newton_try
		{
			UByte word[4];
			pipe->WriteChunk(root + kLORootFlags, 4, false);
			PutBigEndianWord(word, (ULong32) size);
			pipe->WriteChunk(word, 4, false);
		}
		newton_catch(exPipeException)
		{
			err = (NewtonErr) (Long) CurrentException()->data;
		}
		end_try;
		if (err != noErr)
		{
			DisposePtr(block);
			return err;
		}
		for (long i = 0; i < count; i++)
		{
			UByte idWord[4];
			long blockSize;
			if ((err = store->Read(GetBigEndianWord(root + kLORootChunkArray), i * 4, (char*) idWord, 4)) != noErr
			|| (err = store->GetObjectSize(GetBigEndianWord(idWord), &blockSize)) != noErr
			|| (err = store->Read(GetBigEndianWord(idWord), 0, block, blockSize)) != noErr)
			{
				DisposePtr(block);
				return err;
			}
			newton_try
			{
				UByte word[4];
				PutBigEndianWord(word, (ULong32) blockSize);
				pipe->WriteChunk(word, 4, false);
				pipe->WriteChunk(block, blockSize, false);
			}
			newton_catch(exPipeException)
			{
				err = (NewtonErr) (Long) CurrentException()->data;
			}
			end_try;
			if (err != noErr)
			{
				DisposePtr(block);
				return err;
			}
			// the progress callback, told {size, bytes so far - each block
			// with its length word} each time its frequency's worth has gone
			written += blockSize + 4;
			sinceCall += blockSize + 4;
			if (callback != nil && callback->fFrequency <= sinceCall)
			{
				info.fAmountRead = written;
				callback->fProc(callback, &info);
				sinceCall = 0;
			}
		}
	}
	if (mapped)
		UnmapLargeObject(address);
	DisposePtr(block);
	return err;
}


/*------------------------------------------------------------------------------
	D u p l i c a t i n g
------------------------------------------------------------------------------*/

// ROM 0x001035d4 DuplicateLargeObject__FPUlP6TStoreUlT2
// A copy of large object id of store, made on toStore; *newId its root -
// by the object's own store when it has one.
NewtonErr
DuplicateLargeObject(PSSId* newId, TStore* store, PSSId id, TStore* toStore)
{
	TLrgObjStore* allocator;
	NewtonErr err = GetLOAllocator(store, id, &allocator);
	if (err == noErr)
	{
		if (allocator == nil)
			err = LODefaultDuplicate(newId, store, id, toStore);
		else
		{
			err = allocator->Duplicate(newId, store, id, toStore);
			allocator->Delete();
		}
	}
	return err;
}


// ROM 0x00102320 LODefaultDuplicate__FPUlP6TStoreUlT2
// The object flushed, then its store objects copied one by one, each made
// within the store's current transaction.
NewtonErr
LODefaultDuplicate(PSSId* newId, TStore* store, PSSId id, TStore* toStore)
{
	NewtonErr err = FlushLargeObject(store, id);
	if (err != noErr)
		return err;
	return DuplicatePackageData(store, id, toStore, newId, true);
}


// ROM 0x001621ec (unnamed)
// A new object on store, made within its current transaction when
// separately (so SeparatelyAbort can take it back on its own).
static NewtonErr
NewPackageObject(TStore* store, long size, PSSId* id, Boolean separately)
{
	if (separately)
		return store->NewWithinTransaction(size, id);
	return store->NewObject(size, id);
}


// ROM 0x001621f8 CopyPackageData__FP11PackageRootP6TStoreT2Uc
// The chunk array of the root and every block it names copied from store
// to toStore (a block is at most 0x524 bytes: 0x400 compressed, and what a
// compander adds); the root's chunk array id changed to the copy's.  On
// failure, with separately, the objects made are taken back.  The chunk
// array's ids are big-endian words, as on the MessagePad.
static NewtonErr
CopyPackageData(UByte* root, TStore* store, TStore* toStore, Boolean separately)
{
	ULong made = 0;
	char* block = nil;
	UByte* newIds = nil;
	UByte* ids = nil;
	PSSId newArrayId = 0;
	long arraySize;
	ULong count = 0;
	NewtonErr err = store->GetObjectSize(GetBigEndianWord(root + kLORootChunkArray), &arraySize);
	if (err == noErr && (err = NewPackageObject(toStore, arraySize, &newArrayId, separately)) == noErr)
	{
		count = (ULong) arraySize >> 2;
		ids = (UByte*) NewPtr(count << 2);
		err = MemError();
		if (err == noErr)
		{
			newIds = (UByte*) NewPtr(count << 2);
			err = MemError();
			if (err == noErr
			&& (err = store->Read(GetBigEndianWord(root + kLORootChunkArray), 0, (char*) ids, count << 2)) == noErr)
			{
				block = NewPtr(0x524);
				for ( ; made < count; made++)
				{
					long size;
					PSSId blockId = GetBigEndianWord(ids + made * 4);
					if ((err = store->GetObjectSize(blockId, &size)) != noErr)
						break;
					if (size > 0x524)
					{
						err = kError_No_Memory;
						break;
					}
					PSSId newBlockId;
					if ((err = NewPackageObject(toStore, size, &newBlockId, separately)) != noErr
					|| (err = store->Read(blockId, 0, block, size)) != noErr
					|| (err = toStore->Write(newBlockId, 0, block, size)) != noErr)
						break;
					PutBigEndianWord(newIds + made * 4, (ULong32) newBlockId);
				}
				if (err == noErr && (err = toStore->Write(newArrayId, 0, (char*) newIds, count << 2)) == noErr)
					PutBigEndianWord(root + kLORootChunkArray, (ULong32) newArrayId);
			}
		}
	}
	if (err != noErr && separately)
	{
		if (newArrayId != 0)
			toStore->SeparatelyAbort(newArrayId);
		for (ULong i = 0; i < made; i++)
			if (toStore->SeparatelyAbort(GetBigEndianWord(newIds + i * 4)) != noErr)
				break;
	}
	if (block != nil)
		DisposePtr(block);
	if (newIds != nil)
		DisposePtr((Ptr) newIds);
	if (ids != nil)
		DisposePtr((Ptr) ids);
	return err;
}


// ROM 0x0016245c DuplicatePackageData__FP6TStoreUlT1PUlUc
// A copy of the large object (or package: kind 1) at id: its root, the
// compander's name (written without its terminator, as the ROM does),
// the compander's parameters and the blocks.  Not separately, the copy is
// made under a lock of toStore of its own and an error aborts toStore;
// separately, the objects made are taken back one by one.  ==> the new
// root in *newId.
NewtonErr
DuplicatePackageData(TStore* store, PSSId id, TStore* toStore, PSSId* newId, Boolean separately)
{
	PSSId newRootId = 0, newParamsId = 0, newNameId = 0;
	char* name = nil;
	char* params = nil;
	UByte root[kLargeObjectRootSize];
	memset(root, 0, sizeof(root));
	NewtonErr err = store->Read(id, 0, (char*) root, 0x14);
	if (err == noErr)
	{
		ULong kind = GetBigEndianWord(root + kLORootFlags) & 0xffff;
		Boolean largeObject = (kind == 2);
		long rootSize = largeObject ? 0x20 : 0x14;
		if (kind > 2)
			err = kError_Bad_Package;
		else if ((!largeObject || (err = store->Read(id, 0, (char*) root, 0x20)) == noErr))
		{
			long nameSize;
			PSSId nameId = GetBigEndianWord(root + kLORootCompanderName);
			if ((err = store->GetObjectSize(nameId, &nameSize)) == noErr)
			{
				name = NewPtr(nameSize + 1);
				err = MemError();
				if (err == noErr && (err = store->Read(nameId, 0, name, nameSize)) == noErr)
				{
					name[nameSize] = 0;
					if (!separately)
						toStore->LockStore();
					if ((err = NewPackageObject(toStore, rootSize, &newRootId, separately)) == noErr
					&& (err = NewPackageObject(toStore, strlen(name), &newNameId, separately)) == noErr
					&& (err = toStore->Write(newNameId, 0, name, strlen(name))) == noErr)
					{
						PutBigEndianWord(root + kLORootCompanderName, (ULong32) newNameId);
						PSSId paramsId = GetBigEndianWord(root + kLORootCompanderParams);
						if (paramsId != 0)
						{
							long paramsSize;
							if ((err = store->GetObjectSize(paramsId, &paramsSize)) == noErr)
							{
								params = NewPtr(paramsSize);
								err = MemError();
								if (err == noErr
								&& (err = NewPackageObject(toStore, paramsSize, &newParamsId, separately)) == noErr
								&& (err = store->Read(paramsId, 0, params, paramsSize)) == noErr
								&& (err = toStore->Write(newParamsId, 0, params, paramsSize)) == noErr)
									PutBigEndianWord(root + kLORootCompanderParams, (ULong32) newParamsId);
							}
						}
						if (err == noErr
						&& (err = CopyPackageData(root, store, toStore, separately)) == noErr
						&& (err = toStore->Write(newRootId, 0, (char*) root, rootSize)) == noErr
						&& !separately)
							err = toStore->UnlockStore();
					}
				}
			}
		}
	}
	if (err != noErr)
	{
		if (separately)
		{
			if (newRootId != 0)
				toStore->SeparatelyAbort(newRootId);
			if (newParamsId != 0)
				toStore->SeparatelyAbort(newParamsId);
			if (newNameId != 0)
				toStore->SeparatelyAbort(newNameId);
		}
		else
			toStore->Abort();
	}
	if (name != nil)
		DisposePtr(name);
	if (params != nil)
		DisposePtr(params);
	*newId = newRootId;
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
// The object deleted by its own store, or the default way.  (GetLOAllocator's
// answer is not looked at: an unknown compander deletes the default way.)
NewtonErr
LODeleteByProtocol(TStore* store, PSSId id)
{
	TLrgObjStore* allocator;
	GetLOAllocator(store, id, &allocator);
	if (allocator == nil)
		return LODefaultDelete(store, id);
	NewtonErr err = allocator->DeleteObject(store, id);
	allocator->Delete();
	return err;
}


// ROM 0x0010231c LODefaultDelete__FP6TStoreUl
// The object's store objects deleted: the ROM's is a branch to
// DeallocatePackage, which takes a package's pages and a large object's
// blocks back alike (an earlier reading of this function as an empty
// return was wrong - the branch goes through the jump table).
NewtonErr
LODefaultDelete(TStore* store, PSSId id)
{
	return DeallocatePackage(store, id);
}
