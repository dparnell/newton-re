/*
	File:		packages/ROMPackages.cpp

	Contains:	The ROM extension's packages found and loaded, and the
				package natives (ROMPackages.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NativeFunctions.h"
#include "ROMPackages.h"
#include "PackageManager.h"
#include "PackageIterator.h"
#include "ROMExtension.h"
#include "ROMImport.h"
#include "ObjectAreaImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Frames.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "ByteOrder.h"
#include "NewtonExceptions.h"
#include "NewtonDebug.h"
#include "OSErrors.h"
#include "Units.h"
#include "StorePackages.h"
#include "Soups.h"


// The extensions' headers, found in the ROM's own bytes.
//
// DEVIATION: the ROM keeps a table of the four extensions' addresses
// (0x0c1064ac, unnamed), filled at boot from what the loader found; the
// host does not boot from the ROM, so it looks for each RExBlock header in
// the ROM's own bytes, which the host keeps for exactly this
// (frames/ROMImport.h's ROMRegion: the image, or the extension an object
// file built from the ROM source tree carries) - on word boundaries, as
// the signature's comment in ddk/ROMExtension.h says it is meant to be.
// The header's words are big-endian, as everything in the image is.
static const unsigned char*
FindRExHeader(ULong rexId, ULong* romAddress, ULong* available)
{
	for (long region = 0; region < ROMRegionCount(); region++)
	{
		ULong base = 0, imageSize = 0;
		const unsigned char* rom = (const unsigned char*) ROMRegion(region, &base, &imageSize);
		if (rom == nil)
			continue;
		for (ULong at = (4 - (base & 3)) & 3; at + sizeof(RExHeader) <= imageSize; at += kARMWord)
		{
			if (GetBigEndianWord(rom + at) != kRExSignatureA || GetBigEndianWord(rom + at + 4) != kRExSignatureB)
				continue;
			if (GetBigEndianWord(rom + at + 0x1c) != rexId)		// the extension's id
				continue;
			if (romAddress != nil)
				*romAddress = base + at;
			if (available != nil)
				*available = imageSize - at;
			return rom + at;
		}
	}
	return nil;
}


// Where extension rexId is in the ROM's address space (the ROM's table at
// 0x0c1064ac), 0 when there is none.
ULong
RExAddress(ULong rexId)
{
	ULong address = 0;
	return FindRExHeader(rexId, &address, nil) != nil ? address : 0;
}


// ROM 0x0011ee60 PrimNextRExConfigEntry
// The next entry of the tag from *currentIndex on (an entry whose offset
// is -1 has been taken out), and the index after it.
// DEVIATION: the answer is where the host keeps the entry, in its copy of
// the image (see FindRExHeader); the entry's words are big-endian there.
VAddr
PrimNextRExConfigEntry(ULong rexId, ULong tag, ULong* length, ULong* currentIndex)
{
	*length = 0;
	if (rexId > 3)
		return 0;
	ULong available = 0;
	const unsigned char* rex = FindRExHeader(rexId, nil, &available);
	if (rex == nil)
		return 0;
	ULong count = GetBigEndianWord(rex + 0x24);
	for (ULong i = *currentIndex; i < count; i++)
	{
		ULong entry = 0x28 + i * 3 * kARMWord;
		if (entry + 3 * kARMWord > available)
			break;
		if (GetBigEndianWord(rex + entry) != tag || GetBigEndianWord(rex + entry + kARMWord) == 0xFFFFFFFF)
			continue;
		*length = GetBigEndianWord(rex + entry + 2 * kARMWord);
		*currentIndex = i + 1;
		// the offset is from the extension's start
		return (VAddr) (rex + GetBigEndianWord(rex + entry + kARMWord));
	}
	return 0;
}


// ROM 0x0011eddc PrimRExConfigEntry
// The first entry of the tag in extension rexId.
VAddr
PrimRExConfigEntry(ULong rexId, ULong tag, ULong* length)
{
	ULong index = 0;
	return PrimNextRExConfigEntry(rexId, tag, length, &index);
}


// ROM 0x0011eda0 PrimLastRExConfigEntry
// The entry of the tag in the highest-numbered extension that has one.
VAddr
PrimLastRExConfigEntry(ULong tag, ULong* length)
{
	for (long rexId = 3; rexId >= 0; rexId--)
	{
		VAddr entry = PrimRExConfigEntry((ULong) rexId, tag, length);
		if (entry != 0)
			return entry;
	}
	return 0;
}


// ROM 0x0011ef44 GetLastRExConfigEntry
// The ROM asks the kernel (generic system call 0x41) from user mode and
// goes straight to PrimLastRExConfigEntry in a privileged one; the host
// always goes straight there (the kernel has no table: see FindRExHeader).
VAddr
GetLastRExConfigEntry(ULong tag, ULong* length)
{
	return PrimLastRExConfigEntry(tag, length);
}


// ROM 0x0011ef10 GetRExConfigEntry
// The ROM does a generic system call (0x3b) and the kernel answers out of
// the extension headers the loader found at boot.
//
// DEVIATION: the host does not boot from the ROM, so the kernel has no
// such table (os600/kernel/GenericSWI.cpp answers
// kError_Call_Not_Implemented for that selector); the entry is found as
// PrimRExConfigEntry finds it, in the host's copy of the image - the
// address the package manager and the frames part handlers read it at -
// rather than at its ROM address.
VAddr
GetRExConfigEntry(ULong rexId, ULong tag, ULong* size)
{
	ULong length = 0;
	VAddr entry = PrimRExConfigEntry(rexId, tag, &length);
	if (size != nil)
		*size = length;
	return entry;
}


// ROM 0x0011eeec GetPackageList
VAddr
GetPackageList(ULong rexId)
{
	ULong size = 0;
	return GetRExConfigEntry(rexId, 'pkgl', &size);
}


// ROM 0x000e7040 LoadHighROMFramesPackages__Fv
// Every package in the package list of each of the four extensions sent
// to the package manager as a package in memory, where it lies - the
// world forked first, so that it can take the parts the manager sends
// back, and the load made under gPackageSemaphore.  An extension's
// packages follow one another, each the size its directory says; the
// walk stops at the first answer that is not "loaded", "already there"
// or "no handler for its part type" - which is how it ends, the bytes
// after the last package not being one.
void
LoadHighROMFramesPackages(void)
{
	for (ULong rexId = 0; rexId < kMaxROMExtensions; rexId++)
	{
		VAddr list = GetPackageList(rexId);
		if (list == 0)
			continue;
		ULong offset = 0;
		long err;
		do
		{
			TAppWorld* world = (TAppWorld*) GetGlobals();
			world->Fork(nil);
			PartSource source;
			source.stream.bufferId = (TObjectId) (list + offset);
			source.stream.messagePortId = 0;
			SourceType type;
			type.format = kFixedMemory;
			type.deviceKind = kNoDevice;
			type.deviceNumber = 0;
			type.deviceId = 0;		// (the ROM's is whatever was on its stack)
			TPkBeginLoadEvent event(type, source, *world->GetMyPort(), *world->GetMyPort(), true);
			TUPort port(PackageManagerPortId());
			world->ReleaseMutex();
			gPackageSemaphore->Acquire(kWaitOnBlock);
			ULong replySize;
			err = port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
			gPackageSemaphore->Release();
			world->AcquireMutex();
			if (err == noErr)
				err = event.fEventError;
			offset += GetBigEndianWord((const UByte*) (list + offset) + 0x1c);
		}
		while (err == kError_PartType_Not_Registered || err == kError_Package_Already_Exists || err == noErr);
	}
}


// ROM 0x001fb630 IteratorToPackageFrame__FP11TPMIterator
// One package as a script sees it: a clone of the ROM's
// canonicalTPMIteratorPackageFrame with what the package manager says
// about it.
// A package that lives on a store has its `store` and `pssid` too.
static Ref
IteratorToPackageFrame(TPMIterator* iter)
{
	RefVar frame(Clone(RefVar(Rcanonicaltpmiteratorpackageframe)));
	SetFrameSlot(frame, RSSYMid, RefVar(MAKEINT(iter->PackageId())));
	SetFrameSlot(frame, RSSYMsize, RefVar(MAKEINT(iter->PackageSize())));
	SetFrameSlot(frame, RSSYMtitle, RefVar(MakeString(iter->PackageName())));
	SetFrameSlot(frame, RSSYMversion, RefVar(MAKEINT(iter->fVersion)));
	SetFrameSlot(frame, RSSYMtimestamp, RefVar(MAKEINT(iter->fModifyDate)));
	SetFrameSlot(frame, RSSYMcopyprotection, RefVar(MAKEBOOLEAN(iter->IsCopyProtected())));
	TStore* store;
	PSSId id;
	if (IdToStore(iter->PackageId(), &store, &id) == noErr)
	{
		SetFrameSlot(frame, RSSYMstore, RefVar(ToObject(store)));
		SetFrameSlot(frame, RSSYMpssid, RefVar(MAKEINT(id)));
	}
	return frame;
}


// ROM 0x001fbaf8 FGetPackages__FRC6RefVar
// GetPackages(): an array with a frame for every package installed, in
// the package manager's order - what the setup assistant asks for when it
// has finished with the signature.  The world forks first ("couldn't fork
// it over" when it cannot): the walk waits on the package manager.
Ref
FGetPackages(RefArg /*rcvr*/)
{
	if (((TForkWorld*) GetGlobals())->Fork(nil) != noErr)
		ThrowMsg((char*) "couldn't fork it over");
	RefVar packages(AllocateArray(RSSYMarray, 0));
	TPMIterator iter;
	iter.Init();
	while (iter.More())
	{
		RefVar package(IteratorToPackageFrame(&iter));
		AddArraySlot(packages, package);
		iter.NextPackage();
	}
	iter.Done();
	return packages;
}


// ROM 0x001fba0c FPidToPackage
// PidToPackage(id): the frame of the installed package with this id
// (GetPackages' kind); nil when there is none.
static Ref
FPidToPackage(RefArg /*rcvr*/, RefArg packageId)
{
	if (((TForkWorld*) GetGlobals())->Fork(nil) != noErr)
		ThrowMsg((char*) "couldn't fork it over");
	Long id = RINT(packageId);
	RefVar package;
	TPMIterator iter;
	iter.Init();
	while (iter.More())
	{
		if (iter.PackageId() == (ULong) id)
		{
			package = IteratorToPackageFrame(&iter);
			break;
		}
		iter.NextPackage();
	}
	iter.Done();
	return package;
}


// ROM 0x001607f4 FGetPackageStores
// GetPackageStores(): the stores the installed packages' soup parts make.
static Ref
FGetPackageStores(RefArg /*rcvr*/)
{
	return gPackageStores;
}


// ROM 0x00321ef8 IsPackage__FRC6RefVar
// A package on a store, as a script holds it: a large binary of class
// 'package whose bytes are a package and are a package on its store.
Boolean
IsPackage(RefArg obj)
{
	if (!IsLargeBinary(obj))
		return false;
	if (!EQRef(ClassOf(obj), RSSYMpackage))
		return false;
	if (!IsPackageHeader(BinaryData(obj), Length(obj)))
		return false;
	return IsOnStoreAsPackage((ULong) BinaryData(obj));
}


// ROM 0x00321f8c FIsPackage
static Ref
FIsPackage(RefArg /*rcvr*/, RefArg obj)
{
	return IsPackage(obj) ? TRUEREF : NILREF;
}


// ROM 0x001fbbf0 FBackupPatchPackage
// No patch to back up in this ROM: nil (through a RefVar made of nil and
// disposed of again).
static Ref
FBackupPatchPackage(RefArg /*rcvr*/)
{
	RefVar nothing(NILREF);
	return nothing;
}


// ROM 0x001fbc14 FRestorePatchPackage
// No patch to restore in this ROM: the ref 0 (the integer 0).
static Ref
FRestorePatchPackage(RefArg /*rcvr*/, RefArg /*package*/)
{
	return (Ref) 0;
}


void
RegisterPackageNatives(void)
{
	RegisterNativeFunction("FBackupPatchPackage", (void*) FBackupPatchPackage, 0);
	RegisterNativeFunction("FRestorePatchPackage", (void*) FRestorePatchPackage, 1);
	RegisterNativeFunction("FGetPackages__FRC6RefVar", (void*) FGetPackages, 0);
	RegisterNativeFunction("FPidToPackage", (void*) FPidToPackage, 1);
	RegisterNativeFunction("FGetPackageStores", (void*) FGetPackageStores, 0);
	RegisterNativeFunction("FIsPackage", (void*) FIsPackage, 1);
	RegisterPackageUnitNatives();
	RegisterStorePackageNatives();
}
