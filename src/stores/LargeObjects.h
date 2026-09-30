/*
	File:		stores/LargeObjects.h

	Contains:	Large objects: data kept on a store as fixed 0x400-byte
				blocks behind a store compander (StoreCompander.h), and
				mapped into memory whole to be read and written - what a
				large binary (a VBO) and a package on a store are made of.

				On the store a large object is a LargeObjectRoot (0x20
				bytes, big-endian words): the chunk array's id (one store
				object id per block), the compander's name's id (a C string
				object - TSimpleStoreCompander when none is given), the
				compander's parameters' id, flags (the kind, 2, in the low
				half; 0x10000 when made uncompressed), 'paok' once the data
				is all there (PackageAllocationOk), and the size.
				LODefaultCreate makes one (InitializeChunkArray: an empty
				block object for every 0x400 bytes), filling it from a pipe
				when given one (FillChunkArray, the blocks written through
				the compander).

				Mapping, unmapping, resizing, flushing, committing and
				aborting are asked of the ROM domain manager - on the
				MessagePad a kernel monitor that maps a store object into
				virtual memory and pages its blocks in (and dirty ones out)
				on a fault (TROMDomainManager1K, 0x001adbe8-0x001b0e30).
				Its requests (RDMParams, selectors 1-18) are
				ROMDomainUserRequest's.

				DEVIATION: the host has no paging, so its domain manager
				(HostLargeObjects.cpp) keeps each mapped object whole in a
				host block read through the compander when it is mapped;
				the block's address is the object's address, a resize may
				move it (as the ROM's answers a new address too), and every
				writable object is taken to be dirty (the host cannot see a
				write), so a flush or commit writes it all back through the
				compander, adding block objects to the chunk array when the
				object has grown and updating the root's size.

				A large object whose compander is not a TStoreCompander
				but is claimed by a TLrgObjStore (the protocol below,
				found by the compander's name as a capability:
				GetLOAllocator) is made, deleted, sized, streamed and
				duplicated by that implementation instead - which is how
				a package is kept on a store (TLOPackageStore,
				packages/StorePackages.h: the root's kind is 1 and the
				data is the package's pages, each compressed on its own).

				A large object streamed compressed (LODefaultBackup's
				other form: the root's flags and the size, then each
				block as it lies on the store with its length before it)
				is made again by LODefCreateFromComp, each block put back
				unopened (FillChunkArrayCompressed).  A package being
				made from a pipe tells a progress callback how far it has
				got (TLOCallback).

				NOT YET RECONSTRUCTED: the XIP requests
				(TXIPPackageStore).

	Reconstructed from the MP2x00 US ROM (0x001014bc-0x00103ccc,
	0x00161f90); each function cites its origin.
*/

#ifndef __LARGEOBJECTS_H
#define __LARGEOBJECTS_H

#ifndef __STORE_H
#include "Store.h"
#endif
#ifndef __HOSTORDER_H
#include "HostOrder.h"
#endif

class CPipe;
class RefStruct;


// What a large object being made tells its progress callback (0x14 bytes):
// the package's size, how much of it has been read, its name (big-endian
// UniChars, as the package holds them), the part being read and how many
// parts there are.
struct TLOCallbackInfo
{
	ULong				fPackageSize;		// +0x00
	ULong				fAmountRead;		// +0x04
	const UniChar*		fPackageName;		// +0x08
	ULong				fCurrentPart;		// +0x0c
	ULong				fNumberOfParts;		// +0x10
};

// The progress callback: not a class with a vtable but a record whose
// first word is the function to call (the ROM's is always
// TLOCallback::Callback), then the script's callback function and the info
// frame it is given (both in RefHandles), then how many bytes are read
// between calls.  Whatever makes a large object from a pipe calls
// fProc(this, &info) each time that many bytes have been read
// (TPackageIterator::Store, FillChunkArrayCompressed).  The ROM's is 0x10
// bytes, made on the stack by AllocatePackage.
class TLOCallback
{
public:
	void				Callback(TLOCallbackInfo* info);		// ROM 0x00102ac8 Callback__11TLOCallbackFP15TLOCallbackInfo
	static void			CallbackProc(TLOCallback* callback, TLOCallbackInfo* info)	{ callback->Callback(info); }	// (the host's way of holding Callback in fProc)

	void				(*fProc)(TLOCallback*, TLOCallbackInfo*);	// +0x00
	RefStruct*			fFunction;			// +0x04  the script's callback, nil for none
	RefStruct*			fInfoFrame;			// +0x08  made from canonicalPackageCallbackInfo on the first call
	ULong				fFrequency;			// +0x0c  bytes between calls
};


// The root object, as it lies on the store (big-endian words).
const long kLargeObjectRootSize = 0x20;
enum
{
	kLORootChunkArray		= 0x00,		// the chunk array's id
	kLORootCompanderName	= 0x04,		// the compander's name's id
	kLORootCompanderParams	= 0x08,		// its parameters' id (an empty object when there are none)
	kLORootFlags			= 0x0c,		// low half: the kind (1 a package, 2 a large object); 0x10000 made uncompressed
	kLORootState			= 0x10,		// 'paok' once complete
	kLORootSize				= 0x14		// the object's size
};
const ULong kLOComplete = 'paok';


// What a request to the ROM domain manager carries (0x1c bytes).
struct RDMParams
{
					RDMParams();						// ROM 0x001adcb0 __ct__9RDMParamsFv

	TStore*			fStore;				// +0x00
	PSSId			fObjectId;			// +0x04
	ULong			fAddress;			// +0x08  where it is mapped
	ULong			fPackageId;			// +0x0c
	long			fSize;				// +0x10
	long			fOffset;			// +0x14  where a resize happens (-1: at the end)
	UChar			fReadOnly;			// +0x18
	UChar			fDirty;				// +0x19
};

// the domain manager's requests (UserRequest's selectors)
enum
{
	kRDMMap					= 1,		// store, id, read-only -> address
	kRDMUnmap				= 2,		// by package id, address, or store and id -> store, id
	kRDMSetPackageId		= 4,
	kRDMIdToStore			= 5,		// package id -> store, id
	kRDMStoreToId			= 6,		// store, id -> package id
	kRDMIdToVAddr			= 7,		// package id -> address
	kRDMFlush				= 9,		// by package id, store and id, or address
	kRDMResize				= 10,		// address, size, offset -> address
	kRDMAbort				= 11,		// address, or every object of a store
	kRDMCommit				= 12,		// address, or every object of a store
	kRDMInfo				= 13,		// address -> store, id, package id, size, read-only, dirty
	kRDMAddress				= 14,		// store, id -> address, size
	kRDMObjectAt			= 15,		// an address within an object -> its base, store, id, ...
	kRDMEndSession			= 16,		// address
	kRDMXIPObjectHasMoved	= 17		// store, id: an execute-in-place object was moved by a compaction
};

// The ROM domain manager's user monitor (TROMDomainManager1K::UserRequest,
// reached with MonitorDispatchSWI).  ==> an error.
NewtonErr	ROMDomainUserRequest(long selector, RDMParams* params);


// The protocol a large object's own store implements (TLOPackageStore for
// the packages); its implementations name the companders they take in
// their capability lists.
PROTOCOL TLrgObjStore : public TProtocol
{
public:
	static TLrgObjStore*	New(char* implementation);		// ROM 0x00387404 New__12TLrgObjStoreSFPc
	void			Delete();								// ROM 0x00387430 Delete__12TLrgObjStoreFv

	VIRTUAL NewtonErr	Init() ENDVIRTUAL;					// ROM 0x0038744c Init__12TLrgObjStoreFv
	VIRTUAL NewtonErr	Create(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters,
							   long parametersSize, TLOCallback* callback) ENDVIRTUAL;				// ROM 0x00387458 Create__12TLrgObjStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
	VIRTUAL NewtonErr	CreateFromCompressed(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters,
							   long parametersSize, TLOCallback* callback) ENDVIRTUAL;				// ROM 0x00387464 CreateFromCompressed__12TLrgObjStoreFPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
	VIRTUAL NewtonErr	DeleteObject(TStore* store, PSSId id) ENDVIRTUAL;						// ROM 0x00387470 DeleteObject__12TLrgObjStoreFP6TStoreUl
	VIRTUAL NewtonErr	Duplicate(PSSId* newId, TStore* store, PSSId id, TStore* toStore) ENDVIRTUAL;	// ROM 0x0038747c Duplicate__12TLrgObjStoreFPUlP6TStoreUlT2
	VIRTUAL NewtonErr	Resize(TStore* store, PSSId id, ULong size) ENDVIRTUAL;				// ROM 0x00387488 Resize__12TLrgObjStoreFP6TStoreUlT2
	VIRTUAL long		StorageSize(TStore* store, PSSId id) ENDVIRTUAL;						// ROM 0x00387494 StorageSize__12TLrgObjStoreFP6TStoreUl
	VIRTUAL long		SizeOfStream(TStore* store, PSSId id, UChar compressed) ENDVIRTUAL;	// ROM 0x003874a0 SizeOfStream__12TLrgObjStoreFP6TStoreUlUc
	VIRTUAL NewtonErr	Backup(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback) ENDVIRTUAL;	// ROM 0x003874ac Backup__12TLrgObjStoreFP5CPipeP6TStoreUlUcP11TLOCallback
};

// The TLrgObjStore that claims the object's compander (initialised), or nil
// when a TStoreCompander of that name does the work.  ==> kError_Bad_Parameters
// when neither knows the name.
NewtonErr	GetLOAllocator(TStore* store, PSSId id, TLrgObjStore** allocator);	// ROM 0x0010389c GetLOAllocator__FP6TStoreUlPP12TLrgObjStore


// Creating
NewtonErr	CreateLargeObject(ULong* id, TStore* store, long size, char* compander, void* parameters, long parametersSize);	// ROM 0x00102f70 CreateLargeObject__FPUlP6TStorelPcPvT3
NewtonErr	CreateLargeObject(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
							  TLOCallback* callback, UChar fromCompressed);	// ROM 0x00103300 CreateLargeObject__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallbackT5
NewtonErr	LODefaultCreate(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
							TLOCallback* callback);			// ROM 0x00101bd0 LODefaultCreate__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
NewtonErr	InitializeChunkArray(TStore* store, ULong* chunkArrayId, ULong size);	// ROM 0x001017a8 InitializeChunkArray__FP6TStorePUlUl
NewtonErr	FillChunkArray(TStore* store, ULong rootId, ULong chunkArrayId, CPipe* pipe, ULong size, char* compander, ULong parametersId,
						   TLOCallback* callback);			// ROM 0x001018f4 FillChunkArray__FP6TStoreUlT2P5CPipeT2PcT2P11TLOCallback
NewtonErr	LODefCreateFromComp(ULong* id, TStore* store, CPipe* pipe, long streamSize, UChar readOnly, char* compander, void* parameters,
								long parametersSize, TLOCallback* callback);	// ROM 0x00102080 LODefCreateFromComp__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback - made from the stream LODefaultBackup writes compressed
NewtonErr	FillChunkArrayCompressed(TStore* store, ULong chunkArrayId, CPipe* pipe, long streamSize, TLOCallback* callback);	// ROM 0x00101e14 FillChunkArrayCompressed__FP6TStoreUlP5CPipelP11TLOCallback - each block put back as it came
Boolean		PackageAllocationOk(TStore* store, PSSId rootId);	// ROM 0x00161f90 PackageAllocationOk__FP6TStoreUl - 'paok'

// Mapping
NewtonErr	MapLargeObject(ULong* address, TStore* store, PSSId id, UChar readOnly);	// ROM 0x00103bc0 MapLargeObject__FPUlP6TStoreUlUc
NewtonErr	UnmapLargeObject(TStore** store, ULong* id, ULong address);	// ROM 0x00102fb8 UnmapLargeObject__FPP6TStorePUlUl
NewtonErr	UnmapLargeObject(ULong address);								// ROM 0x00103ccc UnmapLargeObject__FUl
NewtonErr	ResizeLargeObject(ULong* newAddress, ULong address, long size, long offset);	// ROM 0x00103c58 ResizeLargeObject__FPUlUllT3
// DEVIATION (host): the byte order a mapped large binary is kept in
// (frames/HostOrder.h), its bytes turned into it - stores/host/HostLargeObjects.cpp
void		SetLargeObjectHostOrder(ULong address, EHostOrder order);
NewtonErr	FlushLargeObject(TStore* store, PSSId id);						// ROM 0x00103828 FlushLargeObject__FP6TStoreUl
NewtonErr	AbortObject(TStore* store, PSSId id);							// ROM 0x00103028 AbortObject__FP6TStoreUl
NewtonErr	AbortObjects(TStore* store);									// ROM 0x001030e4 AbortObjects__FP6TStore
NewtonErr	CommitObject(ULong address);									// ROM 0x0010313c CommitObject__FUl
NewtonErr	CommitObjects(TStore* store);									// ROM 0x00103194 CommitObjects__FP6TStore

// A flash store's compaction moved an execute-in-place object (a package
// run where it lies on a card): the domain manager must map it afresh.
// DEVIATION: the host's domain manager maps copies (host/HostLargeObjects.cpp),
// never an object where it lies, so it has nothing to do (its default case).
void		XIPObjectHasMoved(TStore* store, PSSId id);						// ROM 0x0027a318 XIPObjectHasMoved__FP6TStoreUl

// Finding out
NewtonErr	VAddrToStore(TStore** store, ULong* id, ULong address);		// ROM 0x001031ec VAddrToStore__FPP6TStorePUlUl
NewtonErr	StoreToVAddr(ULong* address, TStore* store, PSSId id);			// ROM 0x0010325c StoreToVAddr__FPUlP6TStoreUl
NewtonErr	VAddrToId(ULong* packageId, ULong address);					// ROM 0x00103a04 VAddrToId__FPUlUl
NewtonErr	VAddrToBase(ULong* base, ULong address);						// ROM 0x00103a70 VAddrToBase__FPUlUl
long		ObjectSize(ULong address);										// ROM 0x001034f0 ObjectSize__FUl
Boolean		IsOnStoreAsPackage(TStore* store, PSSId id);					// ROM 0x00103470 IsOnStoreAsPackage__FP6TStoreUl
Boolean		IsOnStoreAsPackage(ULong address);								// ROM 0x001032c8 IsOnStoreAsPackage__FUl - the large object mapped there
Boolean		LargeObjectAddressIsValid(ULong address);						// ROM 0x00103660 LargeObjectAddressIsValid__FUl
Boolean		LargeObjectIsDirty(ULong address);							// ROM 0x001036cc LargeObjectIsDirty__FUl
Boolean		LargeObjectIsReadOnly(ULong address);						// ROM 0x00103730 LargeObjectIsReadOnly__FUl
NewtonErr	GetLargeObjectInfo(RDMParams* params, ULong address);			// ROM 0x00103794 GetLargeObjectInfo__FP9RDMParamsUl
long		StorageSizeOfLargeObject(ULong address);						// ROM 0x001037ec StorageSizeOfLargeObject__FUl
long		StorageSizeOfLargeObject(TStore* store, PSSId id);				// ROM 0x0010395c StorageSizeOfLargeObject__FP6TStoreUl
NewtonErr	LOCompanderName(char** name, TStore* store, PSSId id);			// ROM 0x00102cb8 LOCompanderName__FPPcP6TStoreUl
NewtonErr	LOCompanderName(TStore* store, PSSId id, char* name);			// ROM 0x00102a2c LOCompanderName__FP6TStoreUlPc
NewtonErr	LOCompanderNameStrLen(TStore* store, PSSId id, long* length);	// ROM 0x00101894 LOCompanderNameStrLen__FP6TStoreUlPl
long		LODefaultStorageSize(TStore* store, PSSId id);					// ROM 0x00102420 LODefaultStorageSize__FP6TStoreUl
long		GetPagesSize(TStore* store, PSSId chunkArrayId);				// ROM 0x00102370 GetPagesSize__FP6TStoreUl

// Deleting
NewtonErr	DeleteLargeObject(TStore* store, PSSId id);					// ROM 0x00103b3c DeleteLargeObject__FP6TStoreUl
NewtonErr	LODeleteByProtocol(TStore* store, PSSId id);					// ROM 0x00103adc LODeleteByProtocol__FP6TStoreUl
NewtonErr	LODefaultDelete(TStore* store, PSSId id);						// ROM 0x0010231c LODefaultDelete__FP6TStoreUl - DeallocatePackage

// The compander's parameters
NewtonErr	LOCompanderParameterSize(TStore* store, PSSId id, long* size);	// ROM 0x001014bc LOCompanderParameterSize__FP6TStoreUlPl
NewtonErr	LOCompanderParameters(TStore* store, PSSId id, void* parameters);	// ROM 0x00101530 LOCompanderParameters__FP6TStoreUlPv

// Written to a pipe (the object streamer's large binaries)
long		LOSizeOfStream(TStore* store, PSSId id, UChar compressed);		// ROM 0x00102d38 LOSizeOfStream__FP6TStoreUlUc
NewtonErr	LOWrite(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback);	// ROM 0x00102e44 LOWrite__FP5CPipeP6TStoreUlUcP11TLOCallback
long		LODefaultStreamSize(TStore* store, PSSId id, UChar compressed);	// ROM 0x001024fc LODefaultStreamSize__FP6TStoreUlUc
NewtonErr	LODefaultBackup(CPipe* pipe, TStore* store, PSSId id, UChar compressed, TLOCallback* callback);	// ROM 0x00102608 LODefaultBackup__FP5CPipeP6TStoreUlUcP11TLOCallback

// Duplicating
NewtonErr	DuplicateLargeObject(PSSId* newId, TStore* store, PSSId id, TStore* toStore);	// ROM 0x001035d4 DuplicateLargeObject__FPUlP6TStoreUlT2
NewtonErr	LODefaultDuplicate(PSSId* newId, TStore* store, PSSId id, TStore* toStore);	// ROM 0x00102320 LODefaultDuplicate__FPUlP6TStoreUlT2 - flushed, then copied object by object
NewtonErr	DuplicatePackageData(TStore* store, PSSId id, TStore* toStore, PSSId* newId, Boolean separately);	// ROM 0x0016245c DuplicatePackageData__FP6TStoreUlT1PUlUc


// A package kept on a store (PackageObjects.cpp; the writing and reading of
// its pages are packages/StorePackages.h): its root is a PackageRoot of
// kind 1 whose chunk array is an *index table* of page objects.
NewtonErr	RemoveIndexTable(TStore* store, PSSId indexId);				// ROM 0x001617f4 RemoveIndexTable__FP6TStoreUl - every page object and the table deleted
NewtonErr	DeallocatePackage(TStore* store, PSSId rootId);				// ROM 0x001618ac DeallocatePackage__FP6TStoreUl - a package's (or a large object's) store objects deleted
NewtonErr	PackageAvailable(TStore* store, PSSId rootId, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled);	// ROM 0x00161ff4 PackageAvailable__FP6TStoreUlPUlPUcT4 - mapped and installed
NewtonErr	PackageAvailable(TStore* store, PSSId rootId, ULong* packageId);	// ROM 0x001620f0 PackageAvailable__FP6TStoreUlPUl
NewtonErr	PackageUnavailable(ULong packageId);							// ROM 0x00162160 PackageUnavailable__FUl - removed and unmapped
NewtonErr	DeletePackage(ULong packageId);									// ROM 0x00161d30 DeletePackage__FUl
NewtonErr	IdToStore(ULong packageId, TStore** store, PSSId* rootId);		// ROM 0x00161d88 IdToStore__FUlPP6TStorePUl
NewtonErr	IdToVAddr(ULong packageId, ULong* address);						// ROM 0x00161e0c IdToVAddr__FUlPUl
NewtonErr	StoreToId(TStore* store, PSSId rootId, ULong* packageId);		// ROM 0x00161e7c StoreToId__FP6TStoreUlPUl

#endif	/* __LARGEOBJECTS_H */
