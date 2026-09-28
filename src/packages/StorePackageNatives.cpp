/*
	File:		packages/StorePackageNatives.cpp

	Contains:	The NewtonScript side of packages kept on a store:
				store:SuckPackageFromBinary and store:RestorePackage (the
				package stored as a large object, wrapped as a 'package
				large binary - its pkgRef - and handed to the ROM's
				RegisterNewPackage, which records it in the store's
				"Packages" soup and activates it), ActivatePackage and
				DeActivatePackage (a pkgRef installed from where it is
				mapped, or taken out of use), the conversions between a
				pkgRef, a package id (pid) and a store object id (pssid),
				and GetPkgRefInfo / GetPkgInfoFromPssid / PidToPackageLite
				(what a package on a store is).

				A package is activated at boot the same way: the store's
				"Packages" soup is walked by the ROM's NewtonScript when
				the store is mounted (ActivateStorePackages), each entry's
				pkgRef handed to ActivatePackage.

	Reconstructed from the MP2x00 US ROM (0x001fb504-0x001fbd08,
	0x00321234-0x00322730); each function cites its origin.
*/

#include "StorePackages.h"
#include "PackageManager.h"
#include "PackagePipe.h"
#include "LargeBinaries.h"
#include "StoreWrapper.h"
#include "Soups.h"				// ToObject
#include "FramesPart.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "Pipes.h"
#include "BufferSegment.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"

#include <string.h>

extern const ExceptionName exPipeException;

// GetPkgRefInfo's complaint about something that is not a package
#define kNSErrNotAPackage	(ERRBASE_FRAMES - 421)

Ref		FSuckPackageFromBinary(RefArg rcvr, RefArg binary, RefArg parameters);


namespace
{
// the TStore under a store frame
TStore*
StoreOf(RefArg storeObject)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(storeObject, RSSYMstore);
	return wrapper->fStore;
}

// a string of the package's own UniChars, which are big-endian on every
// host (a package is a persistent format)
Ref
MakeBigEndianString(const UniChar* bigEndian)
{
	const UByte* bytes = (const UByte*) bigEndian;
	long length = 0;
	while (GetBigEndianHalf(bytes + 2 * length) != 0)
		length++;
	UniChar* text = new UniChar[length + 1];
	for (long i = 0; i <= length; i++)
		text[i] = GetBigEndianHalf(bytes + 2 * i);
	RefVar str(MakeString(text));
	delete[] text;
	return str;
}

void
ThrowFramesError(NewtonErr err)
{
	Throw(exFrames, (void*) (Long) err, nil);
}
}


/*------------------------------------------------------------------------------
	S t o r i n g
------------------------------------------------------------------------------*/

// ROM 0x003215f8 StorePackage__FP5CPipeP6TStoreP11TLOCallbackPUl
// The package in the pipe made a large object on the store (through a
// CPackagePipe, which reads the directory for the flags that choose the
// decompressor: uncompressed, Zippy or LZ, each relocating or not; XIP
// wins).  ==> the error; *id the large object.
NewtonErr
StorePackage(CPipe* pipe, TStore* store, TLOCallback* callback, ULong* id)
{
	const char* decompressor = "TLZStoreDecompressor";
	CPackagePipe packagePipe;
	volatile NewtonErr err = noErr;
	newton_try
	{
		packagePipe.Init(pipe);
		err = noErr;
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	if (err == noErr)
	{
		ULong flags = packagePipe.fIter->PackageFlags();
		if ((flags & 0x10000000) != 0)
			decompressor = (flags & 0x4000000) == 0 ? "TSimpleStoreDecompressor" : "TSimpleRelocStoreDecompressor";
		else if ((flags & 0x2000000) != 0)
			decompressor = (flags & 0x4000000) == 0 ? "TZippyStoreDecompressor" : "TZippyRelocStoreDecompressor";
		else if ((flags & 0x4000000) != 0)
			decompressor = "TLZRelocStoreDecompressor";
		if ((flags & 0x8000000) != 0)
			decompressor = "TXIPStoreCompander";
		err = CreateLargeObject(id, store, &packagePipe, 0, true, (char*) decompressor, nil, 0, callback, false);
	}
	return err;
}


// ROM 0x0032180c WrapPackage__FUlP6TStore
// The package mapped (read-only) and wrapped as a 'package large binary;
// aborted, and the error thrown, when that fails.
Ref
WrapPackage(ULong id, TStore* store)
{
	RefVar pkgRef;
	ULong address = 0;
	volatile NewtonErr err = MapLargeObject(&address, store, id, true);
	if (err == noErr)
	{
		newton_try
		{
			pkgRef = WrapLargeObject(store, RSSYMpackage, id, address);
			err = noErr;
		}
		newton_catch_all
		{
			err = (NewtonErr) (long) (Long) _info.exception.data;
		}
		end_try;
	}
	if (err != noErr)
	{
		if (ISNIL(pkgRef))
			AbortObject(store, id);
		ThrowFramesError(err);
	}
	return pkgRef;
}


// ROM 0x003218f4 AllocatePackage__FP5CPipeRC6RefVarT2Uli
// The package in the pipe stored on the store, wrapped, and handed to
// RegisterNewPackage(pkgRef, store, activate) - the store nil meaning the
// default store.  ==> RegisterNewPackage's answer.
// ROM BUG kept: only RegisterNewPackage is given the default store; the
// package itself is stored through the store argument's own `store` slot,
// which nil does not have.
// NOT YET RECONSTRUCTED: the progress callback (TLOCallback over the
// parameters' callback function, every callbackFreq bytes).
Ref
AllocatePackage(CPipe* pipe, RefArg storeObject, RefArg callback, ULong /*callbackFrequency*/, int activate)
{
	RefVar callbackFn(callback);
	RefVar store(storeObject);
	if (ISNIL(store))
		store = NSCallGlobalFn(RSSYMgetdefaultstore);
	TStore* theStore = StoreOf(storeObject);
	GC();
	ULong id = 0;
	NewtonErr err = StorePackage(pipe, theStore, nil, &id);
	if (err != noErr)
		ThrowFramesError(err);
	RefVar pkgRef(WrapPackage(id, theStore));
	RefVar activateIt(activate != 0 ? TRUEREF : NILREF);
	return NSCallGlobalFn(RSSYMregisternewpackage, pkgRef, store, activateIt);
}


// ROM 0x00321a6c AllocatePackage__FP5CPipeRC6RefVarT2
// ... with the parameters frame: callback, don'tActivate, callbackFreq
// (0x1000 bytes when there is none).
Ref
AllocatePackage(CPipe* pipe, RefArg storeObject, RefArg parameters)
{
	RefVar callback;
	ULong frequency = 0x1000;
	int activate = 1;
	if (NOTNIL(parameters))
	{
		callback = GetFrameSlotRef(parameters, RSSYMcallback);
		activate = ISNIL(GetFrameSlotRef(parameters, RSSYMdon_27tactivate));
		RefVar freq(GetFrameSlotRef(parameters, RSSYMcallbackfreq));
		if (NOTNIL(freq))
			frequency = (ULong) RINT(freq);
	}
	return AllocatePackage(pipe, storeObject, callback, frequency, activate);
}


// ROM 0x00321234 SuckPackageThruPipe__FP5CPipeRC6RefVarT2Uli
Ref
SuckPackageThruPipe(CPipe* pipe, RefArg storeObject, RefArg callback, ULong callbackFrequency, int activate)
{
	return AllocatePackage(pipe, storeObject, callback, callbackFrequency, activate);
}


// ROM 0x00321258 SuckPackageThruPipe__FP5CPipeRC6RefVarT2
Ref
SuckPackageThruPipe(CPipe* pipe, RefArg storeObject, RefArg parameters)
{
	return AllocatePackage(pipe, storeObject, parameters);
}


// ROM 0x0032125c NewPackage__FP5CPipeRC6RefVarT2Ul
// ... activated, with any exception answered as its error.
NewtonErr
NewPackage(CPipe* pipe, RefArg storeObject, RefArg callback, ULong callbackFrequency)
{
	volatile NewtonErr err = noErr;
	RefVar result;
	newton_try
	{
		result = AllocatePackage(pipe, storeObject, callback, callbackFrequency, 1);
	}
	newton_catch_all
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	return err;
}


// ROM 0x001fbd08 FSuckPackageFromBinary
// store:SuckPackageFromBinary(binary, parameters): the package in a binary
// stored (read through a memory pipe over the binary's bytes).
Ref
FSuckPackageFromBinary(RefArg rcvr, RefArg binary, RefArg parameters)
{
	long length = Length(binary);
	CBufferSegment segment;
	segment.Init(BinaryData(binary), length, false, 0, -1);
	MemoryPipe pipe;
	pipe.Init(&segment, nil, false);
	return SuckPackageThruPipe(&pipe, rcvr, parameters);
}


// ROM 0x001fb80c StorePackageRestore
// store:RestorePackage(binary): SuckPackageFromBinary with no parameters,
// any exception answered as its error (an integer, 0 for none).
static Ref
StorePackageRestore(RefArg rcvr, RefArg binary)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		RefVar none;
		FSuckPackageFromBinary(rcvr, binary, none);
	}
	newton_catch_all
	{
		err = (NewtonErr) (long) (Long) _info.exception.data;
	}
	end_try;
	return MAKEINT(err);
}


/*------------------------------------------------------------------------------
	A c t i v a t i n g
------------------------------------------------------------------------------*/

// ROM 0x00321c70 FInstallPackage
// ActivatePackage(pkgRef): the package installed from where its large
// object is mapped (removable memory on a version 2 store device) and,
// unless it was only dispatched, added to vars.activePackageList.  ==> its
// id; an error is thrown.
// DEVIATION: the ROM holds the ROM domain manager's monitor meanwhile;
// the host's domain manager is called directly.
static Ref
FInstallPackage(RefArg /*rcvr*/, RefArg pkgRef)
{
	char* address = BinaryData(pkgRef);
	RDMParams params;
	GetLargeObjectInfo(&params, (ULong) address);
	SourceType type;
	memset(&type, 0, sizeof(type));
	type.format = kRemovableMemory;
	type.deviceKind = kStoreDeviceV2;
	ULong packageId = 0;
	UChar forDispatchOnly = false;
	UChar patchInstalled = false;
	NewtonErr err = InstallPackage(address, type, &packageId, &forDispatchOnly, &patchInstalled, params.fStore, params.fObjectId);
	if (err != noErr)
		ThrowFramesError(err);
	if (!forDispatchOnly)
	{
		RefVar list(GetFrameSlotRef(gVarFrame, RSSYMactivepackagelist));
		AddArraySlot(list, pkgRef);
	}
	return MAKEINT(packageId);
}


// ROM 0x00321d8c FDeinstallPackage
// DeActivatePackage(pkgRef): the package flushed and taken out of use, the
// domain manager told it has no id any more, the pkgRef taken off
// vars.activePackageList and every ref into its bytes declawed.
// NOT YET RECONSTRUCTED: StopFrameSound first (the sound server).
static Ref
FDeinstallPackage(RefArg /*rcvr*/, RefArg pkgRef)
{
	char* address = BinaryData(pkgRef);
	Length(pkgRef);
	ULong packageId = 0;
	if (VAddrToId(&packageId, (ULong) address) == noErr && packageId != 0)
	{
		RDMParams params;
		params.fStore = nil;
		params.fObjectId = 0;
		params.fPackageId = packageId;
		ROMDomainUserRequest(kRDMFlush, &params);
		DeinstallPackage(packageId);
		GetLargeObjectInfo(&params, (ULong) address);
		params.fPackageId = 0;
		ROMDomainUserRequest(kRDMSetPackageId, &params);
	}
	RefVar list(GetFrameSlotRef(gVarFrame, RSSYMactivepackagelist));
	FSetRemove(RefVar(), list, pkgRef);
	LBData* data = LargeBinaryData(pkgRef);
	ULong start = data->fAddress;
	if (start != 0)
	{
		RegisterRangeForDeclawing(start, start + data->fLength);
		data->fAddress = 0;
		DeclawRefsInRegisteredRanges();
	}
	return NILREF;
}


// ROM 0x00321fb0 FIsProtocolPartInUse
// Whether a package's protocol parts have instances (so it cannot be
// deactivated safely).  A non-package is a bad type.
static Ref
FIsProtocolPartInUse(RefArg /*rcvr*/, RefArg pkgRef)
{
	UChar safe = true;
	if (!IsPackage(pkgRef))
		ThrowBadTypeWithFrameData(kNSErrNotAPackage, pkgRef);
	else
	{
		ULong packageId;
		if (VAddrToId(&packageId, (ULong) BinaryData(pkgRef)) == noErr && packageId != 0)
			SafeToDeactivatePackage(packageId, &safe);
	}
	return safe ? NILREF : TRUEREF;
}


/*------------------------------------------------------------------------------
	C o n v e r s i o n s
------------------------------------------------------------------------------*/

// ROM 0x00321300 FObjectPid
// ObjectPid(obj): the id of the package an object lies in; nil for an
// immediate or an object in no package on a store.
// DEVIATION: the host imports a package's frames into areas of their own,
// so only the package's own bytes (its pkgRef) lie in its mapping.
static Ref
FObjectPid(RefArg /*rcvr*/, RefArg obj)
{
	if (!ISPTR(obj))
		return NILREF;
	if (!IsLargeBinary(obj))
		ObjectPtr(obj);
	else
		BinaryData(obj);
	ULong packageId;
	if (VAddrToId(&packageId, (ULong) BinaryData(obj)) == noErr && packageId != 0)
		return MAKEINT(packageId);
	return NILREF;
}


// ROM 0x00321384 FObjectPkgRef
// ObjectPkgRef(obj): the pkgRef of the package an object lies in.
static Ref
FObjectPkgRef(RefArg /*rcvr*/, RefArg obj)
{
	if (ISPTR(obj))
	{
		ULong address = IsLargeBinary(obj) ? (ULong) BinaryData(obj) : (ULong) ObjectPtr(obj);
		if (VAddrToBase(&address, address) == noErr)
			return GetEntryFromLargeObjectVAddr(address);
	}
	return NILREF;
}


// ROM 0x00321400 FPidToPkgRef
static Ref
FPidToPkgRef(RefArg /*rcvr*/, RefArg packageId)
{
	ULong address;
	if (IdToVAddr((ULong) RINT(packageId), &address) == noErr)
		return GetEntryFromLargeObjectVAddr(address);
	return NILREF;
}


// ROM 0x0032144c FPssidToPkgRef
// PssidToPkgRef(pssid, store): the pkgRef of a package on a store, when it
// is mapped.
static Ref
FPssidToPkgRef(RefArg /*rcvr*/, RefArg pssid, RefArg storeObject)
{
	TStore* store = StoreOf(storeObject);
	ULong address;
	if (StoreToVAddr(&address, store, (PSSId) RINT(pssid)) == noErr)
		return GetEntryFromLargeObjectVAddr(address);
	return NILREF;
}


// ROM 0x003214c4 FPssidToPid
static Ref
FPssidToPid(RefArg /*rcvr*/, RefArg pssid, RefArg storeObject)
{
	TStore* store = StoreOf(storeObject);
	ULong packageId;
	if (StoreToId(store, (PSSId) RINT(pssid), &packageId) == noErr)
		return MAKEINT(packageId);
	return NILREF;
}


/*------------------------------------------------------------------------------
	W h a t   a   p a c k a g e   i s
------------------------------------------------------------------------------*/

// ROM 0x003220a4 GetPkgInfoFromVAddr__FUl
// A clone of canonicalPackageFrame filled in from the package mapped at
// address: its store and pssid, its id when installed, what its directory
// says, and for each part its type (the part type when it is to be
// notified, else 'protocol, 'frame or 'raw by kind) and what it is - a
// protocol part's implementation name, a frames part's top-level frame, a
// raw part's address as an integer.
// DEVIATION: the host runs no package code, so a protocol part's class
// info is not there to be asked its name (nil), and a frames part's
// top-level frame is its imported area's, when the part is installed.
Ref
GetPkgInfoFromVAddr(ULong address)
{
	RefVar info(Clone(Rcanonicalpackageframe));
	TStore* store = nil;
	ULong id = 0;
	if (VAddrToStore(&store, &id, address) == noErr)
	{
		SetFrameSlot(info, RSSYMstore, RefVar(ToObject(store)));
		SetFrameSlot(info, RSSYMpssid, RefVar(MAKEINT(id)));
	}
	ULong packageId;
	if (StoreToId(store, id, &packageId) == noErr)
		SetFrameSlot(info, RSSYMid, RefVar(MAKEINT(packageId)));
	TPackageIterator iter((void*) address);
	if (iter.Init() == noErr)
	{
		SetFrameSlot(info, RSSYMsize, RefVar(MAKEINT(iter.PackageSize())));
		SetFrameSlot(info, RSSYMtitle, RefVar(MakeBigEndianString(iter.PackageName())));
		SetFrameSlot(info, RSSYMversion, RefVar(MAKEINT(iter.GetVersion())));
		SetFrameSlot(info, RSSYMtimestamp, RefVar(MAKEINT(iter.ModifyDate())));
		SetFrameSlot(info, RSSYMcreationdate, RefVar(MAKEINT(iter.CreationDate() / 60)));
		SetFrameSlot(info, RSSYMdispatchonly, RefVar(MAKEBOOLEAN(iter.ForDispatchOnly())));
		SetFrameSlot(info, RSSYMcopyprotection, RefVar(MAKEBOOLEAN(iter.CopyProtected())));
		SetFrameSlot(info, RSSYMflags, RefVar(MAKEINT(iter.PackageFlags() >> 4)));
		SetFrameSlot(info, RSSYMcopyright, RefVar(MakeBigEndianString(iter.Copyright())));
		SetFrameSlot(info, RSSYMcompressed, RefVar(MAKEBOOLEAN((iter.PackageFlags() & 0x10000000) == 0)));
		SetFrameSlot(info, RSSYMcmprsdsz, RefVar(MAKEINT(StorageSizeOfLargeObject(address))));
		SetFrameSlot(info, RSSYMnumparts, RefVar(MAKEINT(iter.NumberOfParts())));
		RefVar parts(AllocateArray(RSSYMarray, 0));
		RefVar partTypes(AllocateArray(RSSYMarray, 0));
		SetFrameSlot(info, RSSYMparts, parts);
		SetFrameSlot(info, RSSYMparttypes, partTypes);
		ULong count = iter.NumberOfParts();
		for (ULong i = 0; i < count; i++)
		{
			PartInfo part;
			iter.GetPartInfo(i, &part);
			RefVar partType;
			RefVar what;
			if (part.notify)
			{
				char name[5];
				name[0] = (char) (part.type >> 24);
				name[1] = (char) (part.type >> 16);
				name[2] = (char) (part.type >> 8);
				name[3] = (char) part.type;
				name[4] = 0;
				partType = Intern(name);
			}
			if (part.kind == kProtocol)
			{
				if (ISNIL(partType))
					partType = RSSYMprotocol;
			}
			else if (part.kind == kFrames)
			{
				TImportedObjectArea* area = FindFramesPart((const void*) part.data);
				if (area != nil)
					what = FramePartToplevelFrame(area->fArea);
				if (ISNIL(partType))
					partType = RSSYMframe;
			}
			else if (part.kind == kRaw)
			{
				what = (Ref) (part.data & ~(ULong) 3);
				if (ISNIL(partType))
					partType = RSSYMraw;
			}
			AddArraySlot(partTypes, partType);
			AddArraySlot(parts, what);
		}
	}
	return info;
}


// ROM 0x00322658 FGetPkgRefInfo
// GetPkgRefInfo(pkgRef): what the package is (a bad type for anything
// whose bytes are not a package), the binary locked meanwhile.
static Ref
FGetPkgRefInfo(RefArg /*rcvr*/, RefArg pkgRef)
{
	if (!IsPackageHeader(BinaryData(pkgRef), Length(pkgRef)))
		ThrowBadTypeWithFrameData(kNSErrNotAPackage, pkgRef);
	RefVar info;
	LockRef(pkgRef);
	newton_try
	{
		info = GetPkgInfoFromVAddr((ULong) BinaryData(pkgRef));
	}
	cleanup
	{
		UnlockRef(pkgRef);
	}
	end_try;
	UnlockRef(pkgRef);
	return info;
}


// ROM 0x00322730 FGetPkgInfoFromPssid
// GetPkgInfoFromPssid(pssid, store): ... of a package on a store, mapped
// for the purpose when it is not (its parts then left out: nil).
static Ref
FGetPkgInfoFromPssid(RefArg /*rcvr*/, RefArg pssid, RefArg storeObject)
{
	TStore* store = StoreOf(storeObject);
	ULong address;
	Boolean wasMapped = true;
	if (StoreToVAddr(&address, store, (PSSId) RINT(pssid)) != noErr)
	{
		if (MapLargeObject(&address, store, (PSSId) RINT(pssid), true) == noErr)
			wasMapped = false;
		else
			ThrowExFramesWithBadValue(kNSErrBadArgs, pssid);
	}
	RefVar info(GetPkgInfoFromVAddr(address));
	if (!wasMapped)
	{
		SetFrameSlot(info, RSSYMparts, RefVar());
		UnmapLargeObject(address);
	}
	return info;
}


// ROM 0x001fb504 FPidToPackageLite
// PidToPackageLite(pid): a clone of canonicalPackageLiteFrame - the id,
// the size, the store and the pssid - of a package on a store; nil for
// any other.
static Ref
FPidToPackageLite(RefArg /*rcvr*/, RefArg packageId)
{
	ULong address;
	RDMParams params;
	if (IdToVAddr((ULong) RINT(packageId), &address) == noErr && GetLargeObjectInfo(&params, address) == noErr)
	{
		RefVar frame(Clone(Rcanonicalpackageliteframe));
		SetFrameSlot(frame, RSSYMid, packageId);
		SetFrameSlot(frame, RSSYMsize, RefVar(MAKEINT(params.fSize)));
		SetFrameSlot(frame, RSSYMstore, RefVar(ToObject(params.fStore)));
		SetFrameSlot(frame, RSSYMpssid, RefVar(MAKEINT(params.fObjectId)));
		return frame;
	}
	return NILREF;
}


void
RegisterStorePackageNatives(void)
{
	RegisterNativeFunction("FSuckPackageFromBinary", (void*) FSuckPackageFromBinary, 2);
	RegisterNativeFunction("StorePackageRestore", (void*) StorePackageRestore, 1);
	RegisterNativeFunction("FInstallPackage", (void*) FInstallPackage, 1);
	RegisterNativeFunction("FDeinstallPackage", (void*) FDeinstallPackage, 1);
	RegisterNativeFunction("FIsProtocolPartInUse", (void*) FIsProtocolPartInUse, 1);
	RegisterNativeFunction("FObjectPid", (void*) FObjectPid, 1);
	RegisterNativeFunction("FObjectPkgRef", (void*) FObjectPkgRef, 1);
	RegisterNativeFunction("FPidToPkgRef", (void*) FPidToPkgRef, 1);
	RegisterNativeFunction("FPssidToPkgRef", (void*) FPssidToPkgRef, 2);
	RegisterNativeFunction("FPssidToPid", (void*) FPssidToPid, 2);
	RegisterNativeFunction("FGetPkgRefInfo", (void*) FGetPkgRefInfo, 1);
	RegisterNativeFunction("FGetPkgInfoFromPssid", (void*) FGetPkgInfoFromPssid, 2);
	RegisterNativeFunction("FPidToPackageLite", (void*) FPidToPackageLite, 1);
}
