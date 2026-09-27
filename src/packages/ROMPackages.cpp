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


// ROM 0x0011ef10 GetRExConfigEntry
// The ROM does a generic system call (0x3b) and the kernel answers out of
// the extension headers the loader found at boot.
//
// DEVIATION: the host does not boot from the ROM, so the kernel has no
// such table (os600/kernel/GenericSWI.cpp answers
// kError_Call_Not_Implemented for that selector).  The extensions are
// found in the ROM's own bytes instead, which the host keeps for exactly
// this (frames/ROMImport.h's ROMImageBase); a RExBlock header is looked
// for on word boundaries, as the signature's comment in
// ddk/ROMExtension.h says it is meant to be.  The answer is where the
// entry is in the host's copy of the image - the address the package
// manager and the frames part handlers read it at - rather than its ROM
// address.
VAddr
GetRExConfigEntry(ULong rexId, ULong tag, ULong* size)
{
	if (size != nil)
		*size = 0;
	ULong imageSize = 0;
	const unsigned char* rom = (const unsigned char*) ROMImageBase(&imageSize);
	if (rom == nil)
		return 0;
	for (ULong at = 0; at + sizeof(RExHeader) <= imageSize; at += kARMWord)
	{
		if (GetBigEndianWord(rom + at) != kRExSignatureA || GetBigEndianWord(rom + at + 4) != kRExSignatureB)
			continue;
		if (GetBigEndianWord(rom + at + 0x1c) != rexId)		// the extension's id
			continue;
		ULong count = GetBigEndianWord(rom + at + 0x24);
		ULong table = at + 0x28;
		for (ULong i = 0; i < count; i++)
		{
			ULong entry = table + i * 3 * kARMWord;
			if (entry + 3 * kARMWord > imageSize)
				break;
			if (GetBigEndianWord(rom + entry) != tag)
				continue;
			if (size != nil)
				*size = GetBigEndianWord(rom + entry + 2 * kARMWord);
			// the offset is from the extension's start
			return (VAddr) (rom + at + GetBigEndianWord(rom + entry + kARMWord));
		}
	}
	return 0;
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
// NOT YET RECONSTRUCTED: IdToStore (the ROM domain manager), which adds
// the `store` and `pssid` slots for a package that lives on a store.  The
// packages loaded so far are in memory, on no store, and the ROM leaves
// both slots out for those too.
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
	long id = RINT(packageId);
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
// (Large binaries are NOT YET RECONSTRUCTED - IsLargeBinary answers false
// on the host - so there are none: IsOnStoreAsPackage, which asks the ROM
// domain manager, is never reached.)
static Boolean
IsPackage(RefArg obj)
{
	if (!IsLargeBinary(obj))
		return false;
	if (!EQRef(ClassOf(obj), RSSYMpackage))
		return false;
	if (!IsPackageHeader(BinaryData(obj), Length(obj)))
		return false;
	return false;		// NOT YET RECONSTRUCTED: IsOnStoreAsPackage
}


// ROM 0x00321f8c FIsPackage
static Ref
FIsPackage(RefArg /*rcvr*/, RefArg obj)
{
	return IsPackage(obj) ? TRUEREF : NILREF;
}


void
RegisterPackageNatives(void)
{
	RegisterNativeFunction("FGetPackages__FRC6RefVar", (void*) FGetPackages, 0);
	RegisterNativeFunction("FPidToPackage", (void*) FPidToPackage, 1);
	RegisterNativeFunction("FGetPackageStores", (void*) FGetPackageStores, 0);
	RegisterNativeFunction("FIsPackage", (void*) FIsPackage, 1);
}
