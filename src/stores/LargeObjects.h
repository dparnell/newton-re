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

				NOT YET RECONSTRUCTED: TLrgObjStore (the protocol a
				compander's own large-object store implements; none is
				registered on the host, so the defaults are used),
				LODefCreateFromComp/FillChunkArrayCompressed (an object
				made from already-compressed blocks), duplicating
				(DuplicateLargeObject over DuplicatePackageData), backups
				(LODefaultBackup, TLOCallback), a package kept as a large
				object (ObjectSize's package case, DeallocatePackage), the
				XIP requests.

	Reconstructed from the MP2x00 US ROM (0x001014bc-0x00103ccc,
	0x00161f90); each function cites its origin.
*/

#ifndef __LARGEOBJECTS_H
#define __LARGEOBJECTS_H

#ifndef __STORE_H
#include "Store.h"
#endif

class CPipe;
class TLOCallback;


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
	kRDMFlush				= 9,		// by package id, store and id, or address
	kRDMResize				= 10,		// address, size, offset -> address
	kRDMAbort				= 11,		// address, or every object of a store
	kRDMCommit				= 12,		// address, or every object of a store
	kRDMInfo				= 13,		// address -> store, id, package id, size, read-only, dirty
	kRDMAddress				= 14,		// store, id -> address, size
	kRDMObjectAt			= 15,		// an address within an object -> its base, store, id, ...
	kRDMEndSession			= 16		// address
};

// The ROM domain manager's user monitor (TROMDomainManager1K::UserRequest,
// reached with MonitorDispatchSWI).  ==> an error.
NewtonErr	ROMDomainUserRequest(long selector, RDMParams* params);


// Creating
NewtonErr	CreateLargeObject(ULong* id, TStore* store, long size, char* compander, void* parameters, long parametersSize);	// ROM 0x00102f70 CreateLargeObject__FPUlP6TStorelPcPvT3
NewtonErr	CreateLargeObject(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
							  TLOCallback* callback, UChar fromCompressed);	// ROM 0x00103300 CreateLargeObject__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallbackT5
NewtonErr	LODefaultCreate(ULong* id, TStore* store, CPipe* pipe, long size, UChar readOnly, char* compander, void* parameters, long parametersSize,
							TLOCallback* callback);			// ROM 0x00101bd0 LODefaultCreate__FPUlP6TStoreP5CPipelUcPcPvT4P11TLOCallback
NewtonErr	InitializeChunkArray(TStore* store, ULong* chunkArrayId, ULong size);	// ROM 0x001017a8 InitializeChunkArray__FP6TStorePUlUl
NewtonErr	FillChunkArray(TStore* store, ULong rootId, ULong chunkArrayId, CPipe* pipe, ULong size, char* compander, ULong parametersId,
						   TLOCallback* callback);			// ROM 0x001018f4 FillChunkArray__FP6TStoreUlT2P5CPipeT2PcT2P11TLOCallback
Boolean		PackageAllocationOk(TStore* store, PSSId rootId);	// ROM 0x00161f90 PackageAllocationOk__FP6TStoreUl - 'paok'

// Mapping
NewtonErr	MapLargeObject(ULong* address, TStore* store, PSSId id, UChar readOnly);	// ROM 0x00103bc0 MapLargeObject__FPUlP6TStoreUlUc
NewtonErr	UnmapLargeObject(TStore** store, ULong* id, ULong address);	// ROM 0x00102fb8 UnmapLargeObject__FPP6TStorePUlUl
NewtonErr	UnmapLargeObject(ULong address);								// ROM 0x00103ccc UnmapLargeObject__FUl
NewtonErr	ResizeLargeObject(ULong* newAddress, ULong address, long size, long offset);	// ROM 0x00103c58 ResizeLargeObject__FPUlUllT3
NewtonErr	FlushLargeObject(TStore* store, PSSId id);						// ROM 0x00103828 FlushLargeObject__FP6TStoreUl
NewtonErr	AbortObject(TStore* store, PSSId id);							// ROM 0x00103028 AbortObject__FP6TStoreUl
NewtonErr	AbortObjects(TStore* store);									// ROM 0x001030e4 AbortObjects__FP6TStore
NewtonErr	CommitObject(ULong address);									// ROM 0x0010313c CommitObject__FUl
NewtonErr	CommitObjects(TStore* store);									// ROM 0x00103194 CommitObjects__FP6TStore

// Finding out
NewtonErr	VAddrToStore(TStore** store, ULong* id, ULong address);		// ROM 0x001031ec VAddrToStore__FPP6TStorePUlUl
NewtonErr	StoreToVAddr(ULong* address, TStore* store, PSSId id);			// ROM 0x0010325c StoreToVAddr__FPUlP6TStoreUl
NewtonErr	VAddrToId(ULong* packageId, ULong address);					// ROM 0x00103a04 VAddrToId__FPUlUl
NewtonErr	VAddrToBase(ULong* base, ULong address);						// ROM 0x00103a70 VAddrToBase__FPUlUl
long		ObjectSize(ULong address);										// ROM 0x001034f0 ObjectSize__FUl
Boolean		IsOnStoreAsPackage(TStore* store, PSSId id);					// ROM 0x00103470 IsOnStoreAsPackage__FP6TStoreUl
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
NewtonErr	LODefaultDelete(TStore* store, PSSId id);						// ROM 0x0010231c LODefaultDelete__FP6TStoreUl - nothing

#endif	/* __LARGEOBJECTS_H */
