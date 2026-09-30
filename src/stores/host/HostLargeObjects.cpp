/*
	File:		stores/host/HostLargeObjects.cpp

	Contains:	The host's ROM domain manager: what TROMDomainManager1K's
				user requests (0x001af498) come to on a machine with no
				paging.  See LargeObjects.h.

				DEVIATION (the whole file): on the MessagePad a mapped large
				object is a range of virtual memory whose pages are read
				from the store through the object's compander when first
				touched and written back when dirty.  Here a mapped object
				is a host block holding all of it, read through the
				compander when it is mapped:

				  - map: the root read, the compander made by its name over
				    the root and parameters, every block read; the block's
				    address is the object's (mapping it again answers the
				    same one);
				  - resize: the block grown or shrunk at the offset (or the
				    end) - its address may change;
				  - flush / commit: every writable object is taken to be
				    dirty (the host cannot see a write), so its data is
				    written back through the compander, the chunk array
				    given a block object for every 0x400 bytes it has grown
				    by, and the root's size brought up to date; a commit
				    also hands the object's store objects to the store's
				    transaction (they were made in separate ones) and tells
				    the compander;
				  - abort: the object's changes thrown away and the object
				    unmapped, as the ROM's abort ends the session;
				  - unmap: the block let go (nothing written);
				  - the block is a whole number of 0x400-byte pages long,
				    as a mapping is on the MessagePad: a decompressor may
				    write a few bytes past the count it is asked for into
				    the rest of the page (the LZ one does, reading the last
				    page of a large binary 646 bytes long);
				  - the byte order: a large binary of a class the host keeps
				    in its own order (a string - frames/HostOrder.h) is
				    turned into it once mapped, when the large binaries
				    layer says what it is (SetLargeObjectHostOrder), and
				    each page written back is turned back, so the store
				    holds a MessagePad's big-endian UniChars.

				A package kept on a store (a root of kind 1) is mapped the
				way TROMDomainManager1K::AddPackage (0x001add00) and
				DecompressAndMap (0x001af024) do it, through the
				TStoreDecompressor its root names (given the shared LZ
				buffer when it is one of the LZ ones, else the root's
				parameters): every 0x400-byte page of the index table read
				in turn, as TStoreCompanderWrapper::Read does, at
				kHostPageNotRelocated (StoreCompander.h) - the package's
				bytes as they were written.  A package is read-only.  The
				package manager's id for it (kRDMSetPackageId) answers the
				package-id requests (5, 6, 7).

				A different domain manager - a real one over an MMU - takes
				the place of this file alone: LargeObjects.cpp talks to it
				only through ROMDomainUserRequest.
*/

#include "LargeObjects.h"
#include "StoreCompander.h"
#include "ByteOrder.h"
#include "HostOrder.h"
#include "OSErrors.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>


namespace
{

struct MappedObject
{
	TStore*				fStore;
	PSSId				fId;
	char*				fData;			// the object, whole
	long				fSize;
	TStoreCompander*	fCompander;		// a large object's
	TStoreDecompressor*	fDecompressor;	// a package's
	ULong				fPackageId;
	Boolean				fReadOnly;
	EHostOrder			fOrder;			// the order fData is in (kROMOrder: as the store holds it)
};

MappedObject*	gMapped = nil;
long			gMappedCount = 0;
long			gMappedCapacity = 0;


// how much a block of size bytes is given: whole pages (above)
size_t
PagesFor(long size)
{
	return (size_t) ((size > 0 ? size : 1) + kCompanderBlockSize - 1) & ~(size_t) (kCompanderBlockSize - 1);
}


// the entry for the store's object, the one whose data holds the address,
// or the one mapped at the package id; -1 for none
long
FindObject(TStore* store, PSSId id)
{
	for (long i = 0; i < gMappedCount; i++)
		if (gMapped[i].fStore == store && gMapped[i].fId == id)
			return i;
	return -1;
}

long
FindAddress(ULong address)
{
	for (long i = 0; i < gMappedCount; i++)
	{
		ULong base = (ULong) gMapped[i].fData;
		if (address >= base && address < base + (gMapped[i].fSize > 0 ? gMapped[i].fSize : 1))
			return i;
	}
	return -1;
}

long
FindPackage(ULong packageId)
{
	for (long i = 0; i < gMappedCount; i++)
		if (gMapped[i].fPackageId == packageId)
			return i;
	return -1;
}


void
Forget(long index)
{
	MappedObject* entry = &gMapped[index];
	free(entry->fData);
	if (entry->fCompander != nil)
		entry->fCompander->Delete();
	if (entry->fDecompressor != nil)
		entry->fDecompressor->Delete();
	gMapped[index] = gMapped[--gMappedCount];
}


// a package read whole, page by page, through the decompressor its root
// names (AddPackage, then DecompressAndMap for every page)
NewtonErr
LoadPackage(MappedObject* entry, const UByte* root, char* name)
{
	TStore* store = entry->fStore;
	ULong parameter = GetBigEndianWord(root + kLORootCompanderParams);
	if (strcmp(name, "TLZStoreDecompressor") == 0 || strcmp(name, "TLZRelocStoreDecompressor") == 0)
	{
		char* buffer = nil;
		GetSharedLZObjects(nil, nil, &buffer, nil);
		parameter = (ULong) buffer;
	}
	entry->fDecompressor = TStoreDecompressor::New(name);
	free(name);
	if (entry->fDecompressor == nil)
		return kError_Bad_Parameters;
	NewtonErr err = entry->fDecompressor->Init(store, parameter);
	if (err != noErr)
		return err;
	PSSId indexId = GetBigEndianWord(root + kLORootChunkArray);
	long indexSize = 0;
	if ((err = store->GetObjectSize(indexId, &indexSize)) != noErr)
		return err;
	entry->fReadOnly = true;
	entry->fSize = (indexSize >> 2) << 10;
	entry->fData = (char*) calloc(entry->fSize > 0 ? entry->fSize : 1, 1);
	if (entry->fData == nil)
		return kError_No_Memory;
	for (long offset = 0; offset < entry->fSize && err == noErr; offset += kCompanderBlockSize)
	{
		UByte word[4];
		if ((err = store->Read(indexId, (offset >> 10) << 2, (char*) word, 4)) == noErr)
			err = entry->fDecompressor->Read(GetBigEndianWord(word), entry->fData + offset, kCompanderBlockSize, kHostPageNotRelocated);
	}
	return err;
}


// the object read whole through its compander
NewtonErr
Load(MappedObject* entry)
{
	UByte root[kLargeObjectRootSize];
	memset(root, 0, sizeof(root));
	// (a package's root is only a PackageRoot, 0x14 bytes)
	long rootSize = 0;
	NewtonErr err = entry->fStore->GetObjectSize(entry->fId, &rootSize);
	if (err == noErr)
		err = entry->fStore->Read(entry->fId, 0, (char*) root, rootSize < kLargeObjectRootSize ? rootSize : kLargeObjectRootSize);
	if (err != noErr)
		return err;
	char* name = nil;
	if ((err = LOCompanderName(&name, entry->fStore, entry->fId)) != noErr)
		return err;
	if ((GetBigEndianWord(root + kLORootFlags) & 0xffff) == 1)
		return LoadPackage(entry, root, name);
	entry->fCompander = TStoreCompander::New(name);
	free(name);
	if (entry->fCompander == nil)
		return kError_Bad_Parameters;
	err = entry->fCompander->Init(entry->fStore, entry->fId, GetBigEndianWord(root + kLORootCompanderParams), entry->fReadOnly, false);
	if (err != noErr)
		return err;
	entry->fSize = (long) GetBigEndianWord(root + kLORootSize);
	entry->fData = (char*) calloc(PagesFor(entry->fSize), 1);
	if (entry->fData == nil)
		return kError_No_Memory;
	for (long offset = 0; offset < entry->fSize && err == noErr; offset += kCompanderBlockSize)
	{
		long n = entry->fSize - offset < kCompanderBlockSize ? entry->fSize - offset : kCompanderBlockSize;
		err = entry->fCompander->Read(offset, entry->fData + offset, n, (ULong) (uintptr_t) entry->fData);	// (the object's base, as TROMDomainManager1K::DecompressAndMap passes it)
	}
	return err;
}


// the object written back: the chunk array grown to the size, every block
// written through the compander, the root's size brought up to date
NewtonErr
WriteBack(MappedObject* entry)
{
	if (entry->fReadOnly)
		return noErr;
	TStore* store = entry->fStore;
	UByte root[kLargeObjectRootSize];
	NewtonErr err = store->Read(entry->fId, 0, (char*) root, kLargeObjectRootSize);
	if (err != noErr)
		return err;
	PSSId chunkArrayId = GetBigEndianWord(root + kLORootChunkArray);
	long arraySize = 0;
	if ((err = store->GetObjectSize(chunkArrayId, &arraySize)) != noErr)
		return err;
	long have = arraySize >> 2;
	long need = (entry->fSize + kCompanderBlockSize - 1) / kCompanderBlockSize;
	if (need > have)
	{
		if ((err = store->SetObjectSize(chunkArrayId, need << 2)) != noErr)
			return err;
		for (long i = have; i < need; i++)
		{
			PSSId blockId = 0;
			UByte word[4];
			if ((err = store->NewObject((long) 0, &blockId)) != noErr)
				return err;
			PutBigEndianWord(word, (ULong32) blockId);
			if ((err = store->Write(chunkArrayId, i << 2, (char*) word, 4)) != noErr)
				return err;
		}
	}
	// DEVIATION: the ROM writes a page out as it lets the page go
	// (TROMDomainManager1K::WriteOutPage), so a compander may change the
	// page it is given - TPixelMapCompander filters it in place; the host
	// keeps the whole object mapped, so each page is written from a copy.
	// The last argument is the object's base, as WriteOutPage passes it.
	uint32_t copy[kCompanderBlockSize / 4];
	for (long offset = 0; offset < entry->fSize && err == noErr; offset += kCompanderBlockSize)
	{
		long n = entry->fSize - offset < kCompanderBlockSize ? entry->fSize - offset : kCompanderBlockSize;
		memcpy(copy, entry->fData + offset, n);
		if (!HostWriteOldByteOrder())					// (tests: as an older host wrote it)
			SwapHostOrder(entry->fOrder, copy, n);		// (a page is whole UniChars and whole reals)
		err = entry->fCompander->Write(offset, (char*) copy, n, (ULong) (uintptr_t) entry->fData);
	}
	if (err == noErr && (long) GetBigEndianWord(root + kLORootSize) != entry->fSize)
	{
		PutBigEndianWord(root + kLORootSize, entry->fSize);
		err = store->Write(entry->fId, 0, (char*) root, kLargeObjectRootSize);
	}
	return err;
}


// the object's store objects (made in separate transactions) handed to
// the store's own transaction
void
JoinStoreTransaction(MappedObject* entry)
{
	TStore* store = entry->fStore;
	UByte root[kLargeObjectRootSize];
	memset(root, 0, sizeof(root));
	// (a package's root is only a PackageRoot, 0x14 bytes: read as much as
	// there is, or its objects are never joined and a store that keeps
	// separate transactions apart - the flash store - throws them away at
	// the next mount)
	long rootSize = 0;
	if (store->GetObjectSize(entry->fId, &rootSize) != noErr
	 || store->Read(entry->fId, 0, (char*) root, rootSize < kLargeObjectRootSize ? rootSize : kLargeObjectRootSize) != noErr)
		return;
	PSSId chunkArrayId = GetBigEndianWord(root + kLORootChunkArray);
	PSSId ids[] = { entry->fId, GetBigEndianWord(root + kLORootCompanderName), GetBigEndianWord(root + kLORootCompanderParams), chunkArrayId };
	for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
		if (ids[i] != 0 && store->InSeparateTransaction(ids[i]))
			store->AddToCurrentTransaction(ids[i]);
	long arraySize = 0;
	if (store->GetObjectSize(chunkArrayId, &arraySize) != noErr)
		return;
	for (long i = 0; i < arraySize >> 2; i++)
	{
		UByte word[4];
		if (store->Read(chunkArrayId, i << 2, (char*) word, 4) != noErr)
			return;
		PSSId blockId = GetBigEndianWord(word);
		if (store->InSeparateTransaction(blockId))
			store->AddToCurrentTransaction(blockId);
	}
}


void
Describe(RDMParams* params, const MappedObject* entry)
{
	params->fStore = entry->fStore;
	params->fObjectId = entry->fId;
	params->fPackageId = entry->fPackageId;
	params->fSize = entry->fSize;
	params->fReadOnly = entry->fReadOnly;
	params->fDirty = !entry->fReadOnly;		// (every writable object may have been written)
}


NewtonErr
Commit(long index)
{
	MappedObject* entry = &gMapped[index];
	NewtonErr err = WriteBack(entry);
	if (err == noErr)
	{
		JoinStoreTransaction(entry);
		if (entry->fCompander != nil)
			entry->fCompander->DoTransactionAgainst(2, 0);
	}
	else
	{
		if (entry->fCompander != nil)
			entry->fCompander->DoTransactionAgainst(1, 0);
		Forget(index);
	}
	return err;
}


void
Abort(long index)
{
	if (gMapped[index].fCompander != nil)
		gMapped[index].fCompander->DoTransactionAgainst(1, 0);
	Forget(index);
}

}	// namespace


// The object mapped at address (a large binary's) is of a kind the host
// keeps in its own order - or no longer is: its bytes turned from the
// order they are in to that one.
void
SetLargeObjectHostOrder(ULong address, EHostOrder order)
{
	long index = FindAddress(address);
	if (index < 0 || gMapped[index].fOrder == order)
		return;
	MappedObject* entry = &gMapped[index];
	SwapHostOrder(entry->fOrder, entry->fData, entry->fSize);
	SwapHostOrder(order, entry->fData, entry->fSize);
	entry->fOrder = order;
}


NewtonErr
ROMDomainUserRequest(long selector, RDMParams* params)
{
	long index;
	switch (selector)
	{
	case kRDMMap:
		index = FindObject(params->fStore, params->fObjectId);
		if (index < 0)
		{
			if (gMappedCount == gMappedCapacity)
			{
				long capacity = gMappedCapacity == 0 ? 8 : gMappedCapacity * 2;
				MappedObject* grown = (MappedObject*) realloc(gMapped, capacity * sizeof(MappedObject));
				if (grown == nil)
					return kError_No_Memory;
				gMapped = grown;
				gMappedCapacity = capacity;
			}
			MappedObject* entry = &gMapped[gMappedCount];
			memset(entry, 0, sizeof(MappedObject));
			entry->fStore = params->fStore;
			entry->fId = params->fObjectId;
			entry->fReadOnly = params->fReadOnly;
			gMappedCount++;
			NewtonErr err = Load(entry);
			if (err != noErr)
			{
				Forget(gMappedCount - 1);
				return err;
			}
			index = gMappedCount - 1;
		}
		params->fAddress = (ULong) gMapped[index].fData;
		return noErr;

	case kRDMUnmap:
	case kRDMEndSession:
		if (selector == kRDMUnmap && params->fPackageId != 0)
			index = FindPackage(params->fPackageId);
		else if (params->fAddress != 0)
			index = FindAddress(params->fAddress);
		else
			index = FindObject(params->fStore, params->fObjectId);
		if (index < 0)
			return kError_No_Such_Package;
		params->fStore = gMapped[index].fStore;
		params->fObjectId = gMapped[index].fId;
		Forget(index);
		return noErr;

	case kRDMSetPackageId:
		index = FindObject(params->fStore, params->fObjectId);
		if (index < 0)
			return kError_No_Such_Package;
		gMapped[index].fPackageId = params->fPackageId;
		return noErr;

	case kRDMIdToStore:
	case kRDMIdToVAddr:
		index = params->fPackageId != 0 ? FindPackage(params->fPackageId) : -1;
		if (index < 0)
			return kError_No_Such_Package;
		params->fStore = gMapped[index].fStore;
		params->fObjectId = gMapped[index].fId;
		params->fAddress = (ULong) gMapped[index].fData;
		return noErr;

	case kRDMStoreToId:
		index = FindObject(params->fStore, params->fObjectId);
		if (index < 0 || gMapped[index].fPackageId == 0)
			return kError_No_Such_Package;
		params->fPackageId = gMapped[index].fPackageId;
		return noErr;

	case kRDMFlush:
		if (params->fPackageId != 0)
			index = FindPackage(params->fPackageId);
		else if (params->fStore != nil)
			index = FindObject(params->fStore, params->fObjectId);
		else if (params->fAddress != 0)
			index = FindAddress(params->fAddress);
		else
			return kError_Bad_Parameters;
		if (index < 0)
			return kError_No_Such_Package;
		return WriteBack(&gMapped[index]);

	case kRDMResize:
	{
		index = FindAddress(params->fAddress);
		if (index < 0)
			return kError_No_Such_Package;
		MappedObject* entry = &gMapped[index];
		long oldSize = entry->fSize;
		long newSize = params->fSize;
		long delta = newSize - oldSize;
		char* data = entry->fData;
		if (delta > 0)
		{
			data = (char*) realloc(data, PagesFor(newSize));
			if (data == nil)
				return kError_No_Memory;
			long at = params->fOffset < 0 ? oldSize : params->fOffset;
			memmove(data + at + delta, data + at, oldSize - at);
			memset(data + at, 0, delta);
		}
		else if (delta < 0)
		{
			long at = params->fOffset < 0 ? newSize : params->fOffset;
			memmove(data + at, data + at - delta, oldSize - (at - delta));
			char* smaller = (char*) realloc(data, PagesFor(newSize));
			if (smaller != nil)
				data = smaller;
		}
		entry->fData = data;
		entry->fSize = newSize;
		params->fAddress = (ULong) data;
		return noErr;
	}

	case kRDMAbort:
		if (params->fAddress == 0)
		{
			if (params->fStore == nil)
				return noErr;
			for (long i = gMappedCount - 1; i >= 0; i--)
				if (gMapped[i].fStore == params->fStore)
					Abort(i);
			return noErr;
		}
		index = FindAddress(params->fAddress);
		if (index < 0)
			return kError_No_Such_Package;
		Abort(index);
		return noErr;

	case kRDMCommit:
		if (params->fAddress == 0)
		{
			if (params->fStore == nil)
				return noErr;
			for (long i = gMappedCount - 1; i >= 0; i--)
				if (gMapped[i].fStore == params->fStore)
					Commit(i);
			return noErr;
		}
		index = FindAddress(params->fAddress);
		if (index < 0)
			return kError_No_Such_Package;
		return Commit(index);

	case kRDMInfo:
		index = FindAddress(params->fAddress);
		if (index < 0)
			return kError_No_Such_Package;
		Describe(params, &gMapped[index]);
		return noErr;

	case kRDMAddress:
		index = FindObject(params->fStore, params->fObjectId);
		if (index < 0)
			return kError_No_Such_Package;
		params->fAddress = (ULong) gMapped[index].fData;
		params->fSize = gMapped[index].fSize;
		return noErr;

	case kRDMObjectAt:
		index = FindAddress(params->fAddress);
		if (index < 0)
			return kError_Item_Not_Found;		// (the ROM's -0x2718)
		params->fAddress = (ULong) gMapped[index].fData;
		Describe(params, &gMapped[index]);
		return noErr;

	default:
		return noErr;
	}
}
