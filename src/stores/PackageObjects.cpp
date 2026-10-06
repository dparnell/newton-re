/*
	File:		stores/PackageObjects.cpp

	Contains:	A package kept on a store, as store objects: taken back
				(DeallocatePackage, RemoveIndexTable), made available and
				unavailable to the package manager (PackageAvailable,
				PackageUnavailable), and the ROM domain manager's package
				ids (IdToStore, IdToVAddr, StoreToId).  See LargeObjects.h;
				how the pages are written and read is
				packages/StorePackages.h.

				A package's root is a PackageRoot (0x14 bytes, big-endian
				words): the index table's id (one page object id per 0x400
				bytes of the package), the id of the decompressor's name,
				its parameters' id, the kind (1) in the low half of the
				flags, and 'paok' once it is all there.  A large object's
				root starts the same way (kind 2), which is why the one
				function takes both back.

	Reconstructed from the MP2x00 US ROM (0x001617f4-0x001618ac,
	0x00161d30-0x00161e7c, 0x00161ff4-0x001621f8); each function cites its
	origin.
*/

#include "LargeObjects.h"
#include "StoreWrapper.h"		// TCachedReadStore
#include "PackageManager.h"		// InstallPackage, DeinstallPackage
#include "PackageTypes.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include "UserTasks.h"		// Reboot
#include "SoundChannel.h"		// StopFrameSound

#include <string.h>


// what the root's fields come to (the root read as it lies on the store)
namespace
{
const long kPackageRootSize = 0x14;

struct RootWords
{
	PSSId	fIndex;			// +0x00
	PSSId	fName;			// +0x04
	PSSId	fParameters;	// +0x08
	ULong	fFlags;			// +0x0c  low half: the kind
	ULong	fState;			// +0x10  'paok'
};

NewtonErr
ReadRoot(TStore* store, PSSId rootId, RootWords* root)
{
	UByte bytes[kPackageRootSize];
	NewtonErr err = store->Read(rootId, 0, (char*) bytes, kPackageRootSize);
	if (err == noErr)
	{
		root->fIndex = GetBigEndianWord(bytes + 0x00);
		root->fName = GetBigEndianWord(bytes + 0x04);
		root->fParameters = GetBigEndianWord(bytes + 0x08);
		root->fFlags = GetBigEndianWord(bytes + 0x0c);
		root->fState = GetBigEndianWord(bytes + 0x10);
	}
	return err;
}
}


// ROM 0x001617f4 RemoveIndexTable__FP6TStoreUl
// Every page object the index table names deleted (the ones whose word
// is 0 skipped, and a page that cannot be read left), then the table.
// ==> kError_No_Memory when the table cannot be read into memory; the
// deletions' own errors are not looked at.
NewtonErr
RemoveIndexTable(TStore* store, PSSId indexId)
{
	TCachedReadStore* table = new TCachedReadStore(store, indexId, -1);
	if (table == nil)
		return kError_No_Memory;
	long count = table->fSize >> 2;
	for (long i = 0; i < count; i++)
	{
		void* word;
		if (table->GetDataPtr(i << 2, 4, &word) == noErr)
		{
			PSSId page = GetBigEndianWord((const UByte*) word);
			if (page != 0)
				store->DeleteObject(page);
		}
	}
	store->DeleteObject(indexId);
	delete table;
	return noErr;
}


// ROM 0x001618ac DeallocatePackage__FP6TStoreUl
// A package's (or a large object's: the kind is under 3) store objects
// deleted - the package taken out of use first when it is installed - the
// index table (and its pages), the name, the parameters and the root.
// ==> the first error; kError_Bad_Package for a root of another kind.
//
// ROM BUG (fixed): when there is no index table, the name's deletion
// answers into a register that was never set, which is what comes back
// when the name's own deletion went through (the host's 0).  The host's 0
// is also the fix - the first error, or noErr - so both paths are one (no
// RomBugFixed() test: there is no ROM behaviour the host could reproduce).
NewtonErr
DeallocatePackage(TStore* store, PSSId rootId)
{
	ULong packageId;
	if (StoreToId(store, rootId, &packageId) == noErr)
		PackageUnavailable(packageId);
	RootWords root;
	NewtonErr err = ReadRoot(store, rootId, &root);
	if (err != noErr)
		return err;
	if ((root.fFlags & 0xffff) >= 3)
		return kError_Bad_Package;
	NewtonErr other = noErr;		// (the ROM's r7, unset)
	if (root.fIndex != 0)
	{
		err = RemoveIndexTable(store, root.fIndex);
		other = err;
	}
	if (root.fName != 0)
	{
		other = store->DeleteObject(root.fName);
		if (err == noErr)
			err = other;
	}
	if (root.fParameters != 0)
		other = store->DeleteObject(root.fParameters);
	if (err == noErr)
		err = other;
	NewtonErr last = store->DeleteObject(rootId);
	if (err == noErr)
		err = last;
	return err;
}


// ROM 0x00161ff4 PackageAvailable__FP6TStoreUlPUlPUcT4
// A complete package on a store mapped (read-only) and handed to the
// package manager as removable memory on a store device; unmapped again
// when it will not install.  ==> kError_Bad_Package when it is not complete.
NewtonErr
PackageAvailable(TStore* store, PSSId rootId, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled)
{
	if (!PackageAllocationOk(store, rootId))
		return kError_Bad_Package;
	RDMParams params;
	params.fStore = store;
	params.fObjectId = rootId;
	params.fPackageId = 0;
	params.fReadOnly = 1;
	params.fDirty = 0;
	NewtonErr err = ROMDomainUserRequest(kRDMMap, &params);
	if (err == noErr)
	{
		SourceType type;
		type.format = kRemovableMemory;
		type.deviceKind = kStoreDevice;
		type.deviceNumber = 0;		// (the ROM's is whatever its stack held)
		type.deviceId = 0;
		err = InstallPackage((char*) params.fAddress, type, packageId, forDispatchOnly, patchInstalled, params.fStore, params.fObjectId);
		if (err != noErr)
			ROMDomainUserRequest(kRDMUnmap, &params);
	}
	return err;
}


// ROM 0x001620f0 PackageAvailable__FP6TStoreUlPUl
// ... no package id for one only dispatched; a patch that went in restarts
// the machine.
NewtonErr
PackageAvailable(TStore* store, PSSId rootId, ULong* packageId)
{
	UChar forDispatchOnly = false;
	UChar patchInstalled = false;
	NewtonErr err = PackageAvailable(store, rootId, packageId, &forDispatchOnly, &patchInstalled);
	if (err == noErr)
	{
		if (forDispatchOnly)
			*packageId = 0;
		if (patchInstalled)
			Reboot(-10077, 0, false);		// (the ROM's -0x275d)
	}
	return err;
}


// ROM 0x00162160 PackageUnavailable__FUl
// The package on a store taken out of use: flushed, removed from the
// package manager, and unmapped.  Any frame sound playing is stopped
// first (it may be the package's).
NewtonErr
PackageUnavailable(ULong packageId)
{
	StopFrameSound();
	RDMParams params;
	params.fStore = nil;
	params.fObjectId = 0;
	params.fPackageId = packageId;
	ROMDomainUserRequest(kRDMFlush, &params);
	NewtonErr err = DeinstallPackage(packageId);
	params.fStore = nil;
	params.fObjectId = 0;
	params.fPackageId = packageId;
	ROMDomainUserRequest(kRDMUnmap, &params);
	return err;
}


// ROM 0x00161d30 DeletePackage__FUl
// A package on a store deleted (taken out of use first, by
// DeallocatePackage); any other just removed.
NewtonErr
DeletePackage(ULong packageId)
{
	TStore* store;
	PSSId rootId;
	if (IdToStore(packageId, &store, &rootId) == noErr)
	{
		store->LockStore();
		DeallocatePackage(store, rootId);
		store->UnlockStore();
	}
	else
		DeinstallPackage(packageId);
	return noErr;
}


// ROM 0x00161d88 IdToStore__FUlPP6TStorePUl
// The store and root a package id is mapped from.
NewtonErr
IdToStore(ULong packageId, TStore** store, PSSId* rootId)
{
	RDMParams params;
	params.fStore = nil;
	params.fObjectId = 0;
	params.fPackageId = packageId;
	NewtonErr err = ROMDomainUserRequest(kRDMIdToStore, &params);
	if (err == noErr)
	{
		*store = params.fStore;
		*rootId = params.fObjectId;
	}
	return err;
}


// ROM 0x00161e0c IdToVAddr__FUlPUl
// Where a package id is mapped.
NewtonErr
IdToVAddr(ULong packageId, ULong* address)
{
	RDMParams params;
	params.fStore = nil;
	params.fObjectId = 0;
	params.fPackageId = packageId;
	NewtonErr err = ROMDomainUserRequest(kRDMIdToVAddr, &params);
	if (err == noErr)
		*address = params.fAddress;
	return err;
}


// ROM 0x00161e7c StoreToId__FP6TStoreUlPUl
// The package id a package on a store was installed as.
NewtonErr
StoreToId(TStore* store, PSSId rootId, ULong* packageId)
{
	RDMParams params;
	params.fPackageId = 0;
	params.fStore = store;
	params.fObjectId = rootId;
	NewtonErr err = ROMDomainUserRequest(kRDMStoreToId, &params);
	if (err == noErr)
		*packageId = params.fPackageId;
	return err;
}
