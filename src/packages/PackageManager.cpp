/*
	File:		packages/PackageManager.cpp

	Contains:	The package manager task and the calls on it
				(PackageManager.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PackageManager.h"
#include "PackageIterator.h"
#include "Protocols.h"
#include "UserPorts.h"
#include "UserSemaphore.h"
#include "KernelGlobals.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "Unicode.h"
#include "Random.h"
#include "ByteOrder.h"
#include "OSErrors.h"
#include "PartPipe.h"
#include "RingBuffer.h"

extern const ExceptionName exPipeException;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

TULockingSemaphore*	gPackageSemaphore = nil;		// ROM 0x0c1016dc gPackageSemaphore

// the three a package is still loaded after (LoadHighROMFramesPackages)
static void	RegisterPackageWithDebugger(void* address, ULong packageId);
static void	RegisterLoadedCodeWithDebugger(void* address, const char* name, ULong packageId);
static void	DeregisterLoadedCodeWithDebugger(ULong packageId);


/*------------------------------------------------------------------------------
	T h e   p a c k a g e ' s   n a m e

	DEVIATION: a package's name and copyright lie in its directory as
	big-endian UniChars, which is what the ROM's UniChars are; a host's are
	its own byte order.  The manager copies them into the package block, the
	events and the part infos, and the part handlers make NewtonScript
	strings of them, so the host turns them round once, as the manager takes
	them from the package: every name inside the manager is in the host's
	order.
------------------------------------------------------------------------------*/

static UniChar*
HostOrderCopy(const UniChar* bigEndian)
{
	if (bigEndian == nil)
		return nil;
	const UByte* bytes = (const UByte*) bigEndian;
	long length = 0;
	while (GetBigEndianHalf(bytes + 2 * length) != 0)
		length++;
	UniChar* copy = new UniChar[length + 1];
	for (long i = 0; i <= length; i++)
		copy[i] = GetBigEndianHalf(bytes + 2 * i);
	return copy;
}

static UniChar*	gHostPackageName = nil;		// the package being loaded's
static UniChar*	gHostCopyright = nil;

static void
TakeNames(TPackageIterator* iter)
{
	delete[] gHostPackageName;
	delete[] gHostCopyright;
	gHostPackageName = HostOrderCopy(iter->PackageName());
	gHostCopyright = HostOrderCopy(iter->Copyright());
}


/*------------------------------------------------------------------------------
	T h e   l i s t s
------------------------------------------------------------------------------*/

// ROM 0x0015dcac __ct__13TRegistryInfoFUlT1
TRegistryInfo::TRegistryInfo(ULong type, ULong portId)
{
	fPortId = portId;
	fPartType = type;
}


// ROM 0x0015dce4 Init__13TPackageBlockFUlN2110SourceTypeT1PUsT6N21
// The block filled in, with copies of the name and the copyright and an
// empty list of parts.  ==> kError_No_Memory when any of the three could
// not be made (they are all let go again), kError_Bad_Package when copying
// the strings threw.
// ROM BUG: on either failure the name and the copyright are let go but
// fName and fCopyright still point at them; the caller throws the block
// away, so nothing reads them.
NewtonErr
TPackageBlock::Init(ULong packageId, ULong version, ULong size, SourceType source, ULong flags,
					const UniChar* name, const UniChar* copyright, ULong numParts, ULong modifyDate)
{
	NewtonErr err = noErr;
	fState = 0;
	fPackageId = packageId;
	fVersion = version;
	fSize = size;
	fSourceType = source;
	fCopyright = nil;
	fFlags = flags;
	fName = nil;
	fParts = nil;
	fModifyDate = modifyDate;
	newton_try
	{
		fName = new UniChar[Ustrlen(name) + 1];
		if (fName != nil)
			Ustrcpy(fName, name);
		if (copyright != nil)
		{
			fCopyright = new UniChar[Ustrlen(copyright) + 1];
			if (fCopyright != nil)
				Ustrcpy(fCopyright, copyright);
		}
	}
	newton_catch_all
	{
		err = kError_Bad_Package;
	}
	end_try;
	fParts = new CDynamicArray(sizeof(TInstalledPart), numParts);
	if (err == noErr)
	{
		if (fParts != nil && fName != nil && (fCopyright != nil || copyright == nil))
			return noErr;
		err = kError_No_Memory;
	}
	if (fParts != nil)
		delete fParts;
	delete[] fName;
	delete[] fCopyright;
	return err;
}


// ROM 0x0015de50 __ct__14TInstalledPartFUllT2UcN34T1
TInstalledPart::TInstalledPart(ULong type, PartKind kind, RemoveObjPtr removeObj,
							   Boolean autoLoad, Boolean ownsCode, Boolean notify, Boolean accepted, ULong classInfo)
{
	fKind = kind;
	fType = type;
	fRemoveObjPtr = removeObj;
	fAutoLoad = autoLoad;
	fNotify = notify & 1;
	fAccepted = accepted & 1;
	fOwnsCode = ownsCode & 1;
	fClassInfo = classInfo;
}


/*------------------------------------------------------------------------------
	T P a c k a g e M a n a g e r
------------------------------------------------------------------------------*/

// ROM 0x0015e074 GetSizeOf__15TPackageManagerFv
ULong
TPackageManager::GetSizeOf()
{
	return sizeof(TPackageManager);
}


// ROM 0x0015def0 MainConstructor__15TPackageManagerFv
// In the new task: the app world's own construction, the two heaps the
// handler moves between, the event handler (for 'newt/'pckm events) and
// gPackageSemaphore.
// ROM BUG: the app world's MainConstructor answer is not looked at, and
// neither is the semaphore's Init's: a manager that could not get its
// port starts all the same.
// DEVIATION: the ROM finds the 'prot' domain's heap, zaps it and destroys
// it, and takes the persistent heap from the 'kstk' entry of the memory
// object database; the host has one heap (and no such entries), which
// serves as both.
long
TPackageManager::MainConstructor()
{
	fDefaultHeap = nil;
	fUnused7C = 0;
	fPersistentHeap = nil;
	TAppWorld::MainConstructor();
	fDefaultHeap = GetHeap();
	fPersistentHeap = GetHeap();
	fHandler = new TPackageEventHandler;
	if (fHandler == nil)
		return MemError();
	fHandler->Init(kPackageEventId, kNewtEventClass);
	gPackageSemaphore = new TULockingSemaphore;
	if (gPackageSemaphore == nil)
		return MemError();
	gPackageSemaphore->Init();
	return noErr;
}


// ROM 0x0015e018 MainDestructor__15TPackageManagerFv
void
TPackageManager::MainDestructor()
{
	if (fHandler != nil)
		delete fHandler;
	if (gPackageSemaphore != nil)
		delete gPackageSemaphore;
	gPackageSemaphore = nil;
	TAppWorld::MainDestructor();
}


// ROM 0x0015fdb8 InitializePackageManager__FUl
// The manager's task, 'pckm' in the name server, started in the given
// environment (the kernel services task gives it 'prot').
void
InitializePackageManager(TObjectId environment)
{
	TPackageManager manager;
	manager.Init(kPackageEventId, true, 6000, 10, environment);
}


/*------------------------------------------------------------------------------
	T P a c k a g e E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x0015e07c __ct__20TPackageEventHandlerFv
// The package list is kept in the persistent heap, the registry in the
// ordinary one.
TPackageEventHandler::TPackageEventHandler()
{
	fForwarding = false;
	fIter = nil;
	fPackage = nil;
	fPipe = nil;
	fBuffer = nil;
	fPatchInstalled = false;
	SetPersistentHeap();
	fPackages = new CDynamicArray(sizeof(TPackageBlock), 6);
	SetDefaultHeap();
	fRegistry = new CDynamicArray(sizeof(TRegistryInfo), 8);
	fUnused60 = 0;
	InitValidatePackageDriver();
}


// ROM 0x0015e124 __dt__20TPackageEventHandlerFv
// (the package list is not let go)
TPackageEventHandler::~TPackageEventHandler()
{
	if (fRegistry != nil)
		delete fRegistry;
}


// ROM 0x0015e180 AEHandlerProc__20TPackageEventHandlerFP10TUMsgTokenPUlP7TAEvent
// An event by its code; anything shorter than an event's header, and
// anything unknown, is left alone.
void
TPackageEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* size, TAEvent* event)
{
	if (*size < sizeof(TPkBaseEvent))
		return;
	switch (((TPkBaseEvent*) event)->fEventCode)
	{
	case kPkSafeToDeactivateEvent:
		SafeToDeactivatePackage((TPkSafeToDeactivate*) event);
		break;
	case kPkBeginLoadEvent:
		BeginLoadPackage((TPkBeginLoadEvent*) event);
		break;
	case kPkBackupEvent:
		GetBackupInfo((TPkBackupEvent*) event);
		break;
	case kPkRemoveEvent:
		RemovePackage((TPkRemoveEvent*) event, true, true);
		break;
	case kPkRegisterEvent:
		Register((TPkRegisterEvent*) event);
		break;
	case kPkUnregisterEvent:
		Unregister((TPkUnregisterEvent*) event);
		break;
	}
}


// ROM 0x0015e208 AECompletionProc__20TPackageEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TPackageEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x0015e20c InitValidatePackageDriver__20TPackageEventHandlerFv
// The driver that vets a package before it goes in (a security add-on;
// none is ever registered in a MessagePad as it ships, so this is nil).
void
TPackageEventHandler::InitValidatePackageDriver(void)
{
	fValidator = (TValidatePackageDriver*) NewByName("TValidatePackageDriver", "TValidatePackage");
}


// ROM 0x0015e25c ValidatePackage__20TPackageEventHandlerFP17TPkBeginLoadEventP16TPackageIterator
// NOT YET RECONSTRUCTED: the validation driver's ValidateBegin and, for a
// package on a store, the package backed up through a
// CValidateBackupPipe to ValidateEnd.  There is never a driver on the
// host (InitValidatePackageDriver answers nil), so nothing calls this.
NewtonErr
TPackageEventHandler::ValidatePackage(TPkBeginLoadEvent* /*event*/, TPackageIterator* /*iter*/)
{
	return noErr;
}


// ROM 0x0015e374 GetUniquePackageId__20TPackageEventHandlerFv
// A package id is 24 random bits (never 0) that no installed package has.
ULong
TPackageEventHandler::GetUniquePackageId(void)
{
	ULong id;
	long index;
	do
	{
		id = NewtonRand() & 0xffffff;
		if (id == 0)
			id = 1;
	}
	while (SearchPackageList(&index, id) == noErr);
	return id;
}


// ROM 0x0015e3bc LoadProtocolCode__20TPackageEventHandlerFPPvR8PartInfo10SourceTypeRC10PartSource
// A protocol part's code, where it can run from: a part in memory is used
// where it lies unless it asks to be copied (autoCopy); a streamed part is
// read into memory of the persistent heap, through a CPartPipe of its own
// over the sender's ring buffer (a pipe exception is kError_Bad_Package).
NewtonErr
TPackageEventHandler::LoadProtocolCode(void** classInfo, PartInfo& info, SourceType type, const PartSource& source)
{
	NewtonErr err = noErr;
	if (!IsMemory(type))
	{
		CPartPipe pipe;
		UChar eof = false;
		CShadowRingBuffer* buffer = new CShadowRingBuffer;
		if (buffer == nil)
			err = MemError();
		else
		{
			buffer->Init(source.stream.bufferId, 0, 0);
			pipe.Init(source.stream.messagePortId, buffer, true);
			SetPersistentHeap();
			*classInfo = malloc(info.size);
			err = MemError();
			SetDefaultHeap();
			if (*classInfo != nil)
			{
				newton_try
				{
					long count = info.size;
					pipe.ReadChunk(*classInfo, count, eof);
				}
				newton_catch(exPipeException)
				{
					err = kError_Bad_Package;
				}
				end_try;
			}
		}
		return err;
	}
	if (!info.autoCopy)
		*classInfo = (void*) source.stream.bufferId;
	else
	{
		SetPersistentHeap();
		*classInfo = malloc(info.size);
		err = MemError();
		SetDefaultHeap();
		if (*classInfo != nil)
			BlockMove((void*) source.stream.bufferId, *classInfo, info.size);
	}
	return err;
}


// ROM 0x0015e578 CheckAndInstallPatch__20TPackageEventHandlerFR8PartInfo10SourceTypeRC10PartSource
// A system patch ('ptch part): when the patch was built for this ROM and
// is newer than what is installed, it is copied into a heap of the 'prot
// domain and its pages registered with the patch manager, and the machine
// must reboot (fPatchInstalled).
// DEVIATION: a patch is ARM code for the ROM's own pages, which the host
// cannot run; it is taken as one that does not apply to this ROM, which
// the ROM itself answers noErr for.
NewtonErr
TPackageEventHandler::CheckAndInstallPatch(PartInfo& /*info*/, SourceType /*type*/, const PartSource& /*source*/)
{
	return noErr;
}


// ROM 0x0015e804 InstallPart__20TPackageEventHandlerFPUlPlPUcRC6PartIdR16ExtendedPartInfo10SourceTypeRC10PartSource
// One part installed.  A part for another processor is passed over (not
// accepted, no error).  An autoLoad part is the manager's own business: a
// protocol part's code is registered with the protocol registry, a
// 'ptch part applied.  A notify part then goes to the handler registered
// for its type - kError_PartType_Not_Registered when there is none - as a
// TPkPartInstallEvent, and its reply says what to remove it with and
// whether it was taken.  A protocol part whose install fails is
// deregistered (and its copy let go).
long
TPackageEventHandler::InstallPart(ULong* classInfo, RemoveObjPtr* removeObj, UChar* accepted, const PartId& partId,
								  ExtendedPartInfo& info, SourceType type, const PartSource& source)
{
	long err = noErr;
	*removeObj = 0;
	*classInfo = 0;
	ULong processor = fIter->ProcessorTypeOfPart(fPartIndex);
	if (processor != 0 && processor != 0x1000)
	{
		*accepted = false;
		return noErr;
	}
	if (info.autoLoad)
	{
		if (info.kind == kRaw)
		{
			if (info.type != kPatchPartType)
				goto notify;
			err = CheckAndInstallPatch(info, type, source);
		}
		if (info.kind == kProtocol)
		{
			void* code = nil;
			err = LoadProtocolCode(&code, info, type, source);
			// DEVIATION: the part's class info is the ROM's - ARM code and a
			// table the host's protocol registry cannot use - so it is not
			// registered (TClassInfo::Register): the part goes in with no
			// class info, and a copy is let go at once.  The ROM's own
			// protocol parts (the screen drivers) have host stand-ins.
			if (err == noErr)
			{
				if (code != nil && (!IsMemory(type) || info.autoCopy))
					free(code);
				*classInfo = 0;
				goto notify;
			}
			if (code != nil && (!IsMemory(type) || info.autoCopy))
				free(code);
		}
		if (err != noErr)
			goto done;
	}
notify:
	if (info.notify && info.type != kPatchPartType)
	{
		long index;
		ULong portId;
		err = SearchRegistry(&index, &portId, info.type);
		if (err == noErr)
		{
			if (fForwarding && fSenderPortId == portId)
				portId = fForwardPortId;
			TUPort port(portId);
			if (*classInfo != 0)
				info.data = *classInfo;
			TPkPartInstallEvent event(partId, info, type, source);
			TPkPartInstallEventReply reply;
			ULong replySize;
			err = port.SendRPC(&replySize, &event, sizeof(event), &reply, sizeof(reply));
			if (err == noErr)
				err = reply.fEventError;
			*removeObj = reply.fRemoveObjPtr;
			*accepted = reply.fAccepted;
		}
	}
done:
	if (*classInfo != 0 && err != noErr)
	{
		((TClassInfo*) *classInfo)->DeRegister();
		if (!IsMemory(type) || info.autoCopy)
			free((void*) *classInfo);
		*classInfo = 0;
	}
	return err;
}


// ROM 0x0015ead0 BeginLoadPackage__20TPackageEventHandlerFP17TPkBeginLoadEvent
// A 'pkbl event: the package at the event's source read (its directory,
// through a TPackageIterator), given an id and entered in the package
// list, then its parts installed one by one (LoadNextPart); the answer
// goes back with the id, the package's size, parts, version and name.
// A package whose name is installed already is refused, the answer
// saying whether the installed one is older, newer or the same version
// (and giving its id) - unless either version is 0, when the package
// goes in beside it.  Should the package just installed be the
// validation driver's own ("VPD..."), the driver is looked for again.
// A streamed source is read through a CPartPipe over a shadow of the
// sender's ring buffer (PartPipe.h), which is kept (fPipe, fBuffer) until
// LoadNextPart has read the last part and closes it.
// ROM BUG kept: when the stream's directory cannot be read the pipe is not
// closed - the sender's 'pipe' world is left waiting, and the pipe and its
// buffer stay in fPipe/fBuffer until the next streamed load takes their
// places.
void
TPackageEventHandler::BeginLoadPackage(TPkBeginLoadEvent* event)
{
	ULong startsVPD = 0;
	SourceType type = event->fSourceType;
	ULong deviceId = event->fSourceType.deviceId;
	fSource = event->fSource;
	fPartIndex = 0;
	fPatchInstalled = false;
	UChar format = type.format;
	long result;
	if (!IsMemory(type))
	{
		fBuffer = new CShadowRingBuffer;
		fBuffer->Init(event->fSource.stream.bufferId, 0, 0);
		fPipe = new CPartPipe;
		fPipe->Init(event->fSource.stream.messagePortId, fBuffer, true);
		fIter = new TPackageIterator(fPipe);
	}
	else
		fIter = new TPackageIterator((void*) fSource.stream.bufferId);
	if (fIter == nil)
		result = MemError();
	else
		result = fIter->Init();
	if (result != noErr)
	{
		if (fIter != nil)
			delete fIter;
		fIter = nil;
		goto parts;
	}
	newton_try
	{
		TakeNames(fIter);
		event->fPackageId = GetUniquePackageId();
		event->fPackageSize = fIter->PackageSize();
		event->fNumParts = fIter->NumberOfParts();
		event->fVersion = fIter->GetVersion();
		event->fSizeInMemory = 0;
		for (ULong i = 0; i < fIter->NumberOfParts(); i++)
		{
			PartInfo info;
			fIter->GetPartInfo(i, &info);
			event->fSizeInMemory += info.sizeInMemory;
		}
		Ustrncpy(event->fPackageName, gHostPackageName, kMaxPackageNameSize);
		// the first three characters, as the ROM's three loads of the
		// words at +0x40, +0x42 and +0x44 put them together
		startsVPD = ((ULong) event->fPackageName[0] << 16) | ((ULong) event->fPackageName[1] << 8) | event->fPackageName[2];
		long index;
		if (SearchPackageList(&index, gHostPackageName, 0) == noErr)
		{
			TPackageBlock* installed = PackageAt(index);
			if (installed->fVersion != 0 && fIter->GetVersion() != 0)
			{
				if (installed->fVersion < fIter->GetVersion())
					result = kError_Older_Package_Already_Exists;
				else if (fIter->GetVersion() < installed->fVersion)
					result = kError_Newer_Package_Already_Exists;
				else
					result = kError_Package_Already_Exists;
				event->fPackageId = installed->fPackageId;
			}
		}
		if (result == noErr && fValidator != nil)
			result = ValidatePackage(event, fIter);
		if (result == noErr)
		{
			SetPersistentHeap();
			TPackageBlock block;
			SourceType source = type;
			source.deviceId = deviceId;
			result = block.Init(event->fPackageId, fIter->GetVersion(), fIter->PackageSize(), source,
								fIter->PackageFlags(), gHostPackageName, gHostCopyright,
								fIter->NumberOfParts(), fIter->ModifyDate());
			if (result == noErr)
				result = fPackages->InsertElementsBefore(fPackages->GetArraySize(), &block, 1);
			if (result == noErr)
			{
				fPackage = (TPackageBlock*) fPackages->SafeElementPtrAt(fPackages->GetArraySize() - 1);
				fPackage->fState = kPackageLoading;
				if (IsMemory(type))
					RegisterPackageWithDebugger((void*) fSource.stream.bufferId, fPackage->fPackageId);
			}
			SetDefaultHeap();
			fForwarding = true;
			fSenderPortId = event->fSenderPortId;
			fForwardPortId = event->fForwardPortId;
		}
	}
	newton_catch_all
	{
		SetDefaultHeap();
		result = kError_Bad_Package;
		if (fIter != nil)
			delete fIter;
		fIter = nil;
	}
	end_try;
parts:
	if (result == noErr)
		while (LoadNextPart(&result, &event->fForDispatchOnly, &event->fPatchInstalled))
			;
	if (result == noErr && fValidator == nil && format == 1 && startsVPD == 0x565044)
		InitValidatePackageDriver();
	event->fEventError = result;
	SetReply(sizeof(TPkBeginLoadEvent), event);
	ReplyImmed();
}


// ROM 0x0015ef94 LoadNextPart__20TPackageEventHandlerFPlPUcT2
// The next part of the package being loaded installed and entered in its
// block.  ==> true while there are more to do (result: noErr).  After the
// last part - or the first that fails, whose error is the result - the
// package is finished: a failure takes out what went in (RemovePackage,
// its handlers told), success marks the package valid; a package for
// dispatch only is removed again at once (its handlers not told: the
// parts have done their work), and the iterator goes.
// A streamed source's pipe is told each part's size before the part is
// installed, reads past what its handler left of it afterwards, and is
// closed (the sender's 'pipe' world finished) once the package is done.
Boolean
TPackageEventHandler::LoadNextPart(long* result, UChar* forDispatchOnly, UChar* patchInstalled)
{
	long err = noErr;
	Boolean more = true;
	if (fPartIndex < fIter->NumberOfParts())
	{
		newton_try
		{
			ULong packageId = fPackage->fPackageId;
			PartId partId;
			partId.partIndex = fPartIndex;
			partId.packageId = packageId;
			ExtendedPartInfo info;
			fIter->GetPartInfo(fPartIndex, &info);
			Ustrncpy(info.packageName, gHostPackageName, kMaxPackageNameSize);
			info.packageName[kMaxPackageNameSize] = 0;
			long partSize = GetPartSize();
			if (fPipe == nil)
				fSource.stream.bufferId = info.data;
			else
				fPipe->SetStreamSize(partSize);
			if (info.kind == kFrames && info.infoSize != 0 && !info.compressed && IsMemory(fPackage->fSourceType))
			{
				// ROM BUG: the info is copied to a buffer on the stack for as
				// long as it is; the host stops at the buffer's end
				char name[kMaxInfoSize + 1];
				ULong length = info.infoSize < kMaxInfoSize ? info.infoSize : kMaxInfoSize;
				BlockMove(info.info, name, length);
				name[length] = 0;
				RegisterLoadedCodeWithDebugger((void*) info.data, name, packageId);
			}
			ULong classInfo = 0;
			RemoveObjPtr removeObj = 0;
			UChar accepted = false;		// (the ROM's is whatever was on the stack when a part is not sent to a handler)
			err = InstallPart(&classInfo, &removeObj, &accepted, partId, info, fPackage->fSourceType, fSource);
			// DEVIATION: the ROM tells the debugger the implementation name
			// of a protocol part's class info - the one it registered, or the
			// one the part holds - which the host never has (InstallPart)
			if (err == noErr && info.kind == kProtocol && !info.compressed && classInfo != 0)
			{
				char name[256];
				strcpy(name, ((TClassInfo*) classInfo)->ImplementationName());
				RegisterLoadedCodeWithDebugger((void*) classInfo, name, packageId);
			}
			if (fPipe != nil)
				fPipe->SeekEOF();		// whatever of the part its handler did not read
			if (err == noErr)
			{
				SetPersistentHeap();
				Boolean ownsCode = !(!info.autoCopy && (!info.autoLoad || IsMemory(fPackage->fSourceType)));
				TInstalledPart part(info.type, info.kind, removeObj, info.autoLoad, ownsCode, info.notify, accepted, classInfo);
				err = fPackage->fParts->InsertElementsBefore(fPackage->fParts->GetArraySize(), &part, 1);
				SetDefaultHeap();
				if (err == noErr)
					fPartIndex++;
			}
		}
		newton_catch_all
		{
			err = kError_Bad_Package;
		}
		end_try;
		if (err == noErr && fIter->NumberOfParts() != fPartIndex)
		{
			*result = err;
			return more;
		}
	}
	else if (fIter->NumberOfParts() != fPartIndex)
	{
		*result = err;
		return more;
	}
	if (fPipe != nil)
	{
		// the package read: the sender's 'pipe' world closed (the pipe owns
		// the shadow ring buffer, which goes with it)
		fPipe->Close();
		delete fPipe;
		fPipe = nil;
		fBuffer = nil;
	}
	newton_try
	{
		if (fIter != nil && fPackage != nil)
		{
			if (fPartIndex < fIter->NumberOfParts())
			{
				TPkRemoveEvent remove(fPackage->fPackageId, fSenderPortId, fForwardPortId);
				RemovePackage(&remove, false, true);
			}
			else
				fPackage->fState = kPackageValid;
		}
	}
	newton_catch_all
	{ }
	end_try;
	*forDispatchOnly = false;
	*patchInstalled = false;
	if (fIter != nil)
	{
		newton_try
		{
			if (fIter->ForDispatchOnly())
			{
				*forDispatchOnly = true;
				TPkRemoveEvent remove(fPackage->fPackageId, fSenderPortId, fForwardPortId);
				RemovePackage(&remove, false, false);
			}
			*patchInstalled = fPatchInstalled;
			fPatchInstalled = false;
		}
		newton_catch_all
		{ }
		end_try;
		delete fIter;
	}
	fPackage = nil;
	fIter = nil;
	fForwarding = false;
	more = false;
	fUnused60 = 0;
	*result = err;
	return more;
}


// ROM 0x0015f47c GetPartSize__20TPackageEventHandlerFv
// What the current part takes up in the package: to the next part's
// data, or for the last to the end of the package.
long
TPackageEventHandler::GetPartSize(void)
{
	if (fIter == nil)
		return 0;
	ULong end;
	if (fPartIndex < fIter->NumberOfParts() - 1)
		end = fIter->GetPartDataOffset(fPartIndex + 1);
	else
		end = fIter->PackageSize();
	return end - fIter->GetPartDataOffset(fPartIndex);
}


// ROM 0x0015f4e0 RemovePart__20TPackageEventHandlerFRC6PartIdRC14TInstalledPartUc
// A part taken out: its handler sent a TPkPartRemoveEvent (when asked to
// and the part went to one - the manager waits up to 20 seconds), then a
// protocol part's class info deregistered, and let go if it was a copy.
void
TPackageEventHandler::RemovePart(const PartId& partId, const TInstalledPart& part, UChar notify)
{
	if (notify && part.fNotify)
	{
		TPkPartRemoveEvent event(partId, part.fKind, part.fType, part.fRemoveObjPtr);
		long index;
		ULong portId;
		if (SearchRegistry(&index, &portId, part.fType) == noErr)
		{
			if (fForwarding && fSenderPortId == portId)
				portId = fForwardPortId;
			TUPort port(portId);
			ULong replySize;
			port.SendRPC(&replySize, &event, sizeof(event), nil, 0, 20 * kSeconds);
		}
	}
	if (!part.fAutoLoad || part.fClassInfo == 0 || part.fKind != kProtocol)
		return;
	gProtocolRegistry->DeRegister((TClassInfo*) part.fClassInfo, true);
	if (part.fOwnsCode)
		free((void*) part.fClassInfo);
}


// ROM 0x0015f634 SearchPackageList__20TPackageEventHandlerFPlUl
// The package with this id.  ==> kError_No_Such_Package when there is none.
NewtonErr
TPackageEventHandler::SearchPackageList(long* index, ULong packageId)
{
	CArrayIterator iter(fPackages);
	NewtonErr err = kError_No_Such_Package;
	for (ArrayIndex i = iter.FirstIndex(); iter.More(); i = iter.NextIndex())
	{
		if (PackageAt(i)->fPackageId == packageId)
		{
			err = noErr;
			*index = i;
			break;
		}
	}
	return err;
}


// ROM 0x0015f6d4 SearchPackageList__20TPackageEventHandlerFPlPUsUl
// The package with this name (the last argument is not used).
NewtonErr
TPackageEventHandler::SearchPackageList(long* index, const UniChar* name, ULong /*unused*/)
{
	CArrayIterator iter(fPackages);
	NewtonErr err = kError_No_Such_Package;
	for (ArrayIndex i = iter.FirstIndex(); iter.More(); i = iter.NextIndex())
	{
		if (Ustrcmp(PackageAt(i)->fName, name) == 0)
		{
			err = noErr;
			*index = i;
			break;
		}
	}
	return err;
}


// ROM 0x0015f77c RemovePackage__20TPackageEventHandlerFP14TPkRemoveEventUcT2
// A 'pkrm event (or the manager's own removal of a package it could not
// finish): each part taken out, last first - a removal that was cut short
// carries on from the part it had got to - and the package's block let
// go.  ==> in the event, kError_No_Such_Package when the id is unknown;
// the reply is set only when asked for.
// NOT YET RECONSTRUCTED: SetCardReinsertReason (0x0004b010), which keeps
// the package's name for the message asking for its card back while the
// parts come out.
void
TPackageEventHandler::RemovePackage(TPkRemoveEvent* event, UChar reply, UChar notify)
{
	ULong packageId = event->fPackageId;
	fForwarding = true;
	fSenderPortId = event->fSenderPortId;
	fForwardPortId = event->fForwardPortId;
	long index;
	NewtonErr err = SearchPackageList(&index, packageId);
	if (err == noErr)
	{
		TPackageBlock* block = PackageAt(index);
		ArrayIndex from = 0;
		if (block->fState == kPackageRemoving)
			from = block->fRemoveIndex;
		CArrayIterator iter(block->fParts, from, block->fParts->GetArraySize() - 1, false);
		block->fState = kPackageRemoving;
		PartId partId;
		partId.packageId = packageId;
		for (ArrayIndex i = iter.FirstIndex(); iter.More(); i = iter.NextIndex())
		{
			partId.partIndex = i;
			block->fRemoveIndex = i + 1;
			RemovePart(partId, *(TInstalledPart*) block->fParts->ElementPtrAt(i), notify);
		}
		SetPersistentHeap();
		if (block->fParts != nil)
			delete block->fParts;
		delete[] block->fName;
		delete[] block->fCopyright;
		fPackages->RemoveElementsAt(index, 1);
		SetDefaultHeap();
	}
	fForwarding = false;
	DeregisterLoadedCodeWithDebugger(packageId);
	event->fEventError = err;
	if (reply)
		SetReply(sizeof(TPkRemoveEvent), event);
}


// ROM 0x0015f944 SearchRegistry__20TPackageEventHandlerFPlPUll
// The handler registered for a part type.  ==> its port;
// kError_PartType_Not_Registered when there is none.
NewtonErr
TPackageEventHandler::SearchRegistry(long* index, ULong* portId, long type)
{
	CArrayIterator iter(fRegistry);
	NewtonErr err = kError_PartType_Not_Registered;
	for (ArrayIndex i = iter.FirstIndex(); iter.More(); i = iter.NextIndex())
	{
		TRegistryInfo* info = (TRegistryInfo*) fRegistry->SafeElementPtrAt(i);
		if (info != nil && info->fPartType == (ULong) type)
		{
			*portId = info->fPortId;
			err = noErr;
			*index = i;
			break;
		}
	}
	return err;
}


// ROM 0x0015f9f8 Register__20TPackageEventHandlerFP16TPkRegisterEvent
// A 'rgtr event: the part type entered in the registry with the port to
// send it to.  ==> kError_PartType_Already_Registered when a handler has
// it already.
void
TPackageEventHandler::Register(TPkRegisterEvent* event)
{
	long index;
	ULong portId;
	NewtonErr err;
	if (SearchRegistry(&index, &portId, event->fPartType) == kError_PartType_Not_Registered)
	{
		TRegistryInfo info(event->fPartType, event->fPortId);
		fRegistry->InsertElementsBefore(fRegistry->GetArraySize(), &info, 1);
		err = noErr;
	}
	else
		err = kError_PartType_Already_Registered;
	event->fEventError = err;
	SetReply(sizeof(TPkRegisterEvent), event);
	ReplyImmed();
}


// ROM 0x0015fa90 Unregister__20TPackageEventHandlerFP18TPkUnregisterEvent
// A 'urgr event: the part type out of the registry (not an error when it
// was not there).
void
TPackageEventHandler::Unregister(TPkUnregisterEvent* event)
{
	long index;
	ULong portId;
	if (SearchRegistry(&index, &portId, event->fPartType) == noErr)
		fRegistry->RemoveElementsAt(index, 1);
	event->fEventError = noErr;
	SetReply(sizeof(TPkUnregisterEvent), event);
	ReplyImmed();
}


// ROM 0x0015faf4 SafeToDeactivatePackage__20TPackageEventHandlerFP19TPkSafeToDeactivate
// A 'pksc event: whether the package can go - true unless one of its
// protocol parts' implementations still has instances.
// ROM BUG: a part's kind is checked but not whether it has a class info,
// so a protocol part the manager registered none for asks the registry
// about nil.
void
TPackageEventHandler::SafeToDeactivatePackage(TPkSafeToDeactivate* event)
{
	event->fSafe = true;
	long index;
	NewtonErr err = SearchPackageList(&index, event->fPackageId);
	if (err == noErr)
	{
		TPackageBlock* block = PackageAt(index);
		CArrayIterator iter(block->fParts, 0, block->fParts->GetArraySize() - 1, false);
		for (ArrayIndex i = iter.FirstIndex(); iter.More(); i = iter.NextIndex())
		{
			TInstalledPart* part = (TInstalledPart*) block->fParts->ElementPtrAt(i);
			if (part->fKind == kProtocol)
			{
				long instances = gProtocolRegistry->GetInstanceCount((TClassInfo*) part->fClassInfo);
				event->fSafe = instances == 0;
				if (instances != 0)
					break;
			}
		}
	}
	event->fEventError = err;
	SetReply(sizeof(TPkSafeToDeactivate), event);
}


// ROM 0x0015fc10 SetPersistentHeap__20TPackageEventHandlerFv
// (the task's current heap set without SetHeap's bookkeeping, once the
// OS is running)
void
TPackageEventHandler::SetPersistentHeap(void)
{
	TPackageManager* manager = (TPackageManager*) GetGlobals();
	if (gOSIsRunning || gCurrentTaskId != 0)
		SetHeap(manager->fPersistentHeap);
}


// ROM 0x0015fc2c SetDefaultHeap__20TPackageEventHandlerFv
void
TPackageEventHandler::SetDefaultHeap(void)
{
	TPackageManager* manager = (TPackageManager*) GetGlobals();
	if (gOSIsRunning || gCurrentTaskId != 0)
		SetHeap(manager->fDefaultHeap);
}


// ROM 0x0015fc48 GetBackupInfo__20TPackageEventHandlerFP14TPkBackupEvent
// A 'pkbu event: the next package of the list (the first when the event's
// index is 0), its block's facts in the event and its index (-1 when the
// list is done).  A package flagged 0x20000000 is passed over.
// ROM QUIRK: a package is answered only when the last backup date asked
// about is -1 (every package): with any other date every package is
// passed over and the walk answers -1 at once.
void
TPackageEventHandler::GetBackupInfo(TPkBackupEvent* event)
{
	fForwarding = true;
	fSenderPortId = event->fSenderPortId;
	fForwardPortId = event->fForwardPortId;
	if (event->fIndex == 0)
		fBackupIter.Init(fPackages, 0, fPackages->GetArraySize() - 1, true);
	else
		fBackupIter.NextIndex();
	long index;
	TPackageBlock* block = nil;
	if (!fBackupIter.More())
		index = -1;
	else
	{
		index = fBackupIter.CurrentIndex();
		while (fBackupIter.More())
		{
			block = PackageAt(index);
			if ((block->fFlags & 0x20000000) == 0 && event->fLastBackupDate == 0xffffffff)
				break;
			index = fBackupIter.NextIndex();
		}
	}
	if (index == -1)
	{
		event->fPackageSize = 0;
		event->fPackageId = 0;
		event->fVersion = 0;
		event->fPackageFlags = 0;
	}
	else
	{
		event->fPackageSize = block->fSize;
		event->fPackageId = block->fPackageId;
		event->fVersion = block->fVersion;
		event->fSourceType = block->fSourceType;
		event->fPackageFlags = block->fFlags;
		event->fModifyDate = block->fModifyDate;
		Ustrncpy(event->fPackageName, block->fName, kMaxPackageNameSize);
	}
	event->fIndex = index;
	fForwarding = false;
	event->fEventError = noErr;
	SetReply(sizeof(TPkBackupEvent), event);
}


/*------------------------------------------------------------------------------
	T h e   d e b u g g e r

	The manager tells a serial debugger (on a development unit) where a
	package and its code went, so that it can name them.  Each call is a
	system call that does nothing unless gWantSerialDebugging is set.
	DEVIATION: there is no serial debugger on the host.
------------------------------------------------------------------------------*/

// ROM 0x000e6650 RegisterPackageWithDebugger__FPvUl
static void
RegisterPackageWithDebugger(void* /*address*/, ULong /*packageId*/)
{ }


// ROM 0x000e6690 RegisterLoadedCodeWithDebugger__FPvPCcUl
static void
RegisterLoadedCodeWithDebugger(void* /*address*/, const char* /*name*/, ULong /*packageId*/)
{ }


// ROM 0x000e66e0 DeregisterLoadedCodeWithDebugger__FUl
static void
DeregisterLoadedCodeWithDebugger(ULong /*packageId*/)
{ }


/*------------------------------------------------------------------------------
	T h e   c a l l s   o n   t h e   m a n a g e r
------------------------------------------------------------------------------*/

// ROM 0x00161b68 InstallPackage__FPc10SourceTypePUlPUcT4P6TStoreUl
// The package at buffer loaded: the world forked (so that it goes on
// taking the manager's part events), the begin-load event sent under
// gPackageSemaphore with the world's mutex let go meanwhile.  ==> the
// manager's answer; the package's id (0 when it was only dispatched),
// whether it was only dispatched, whether a patch went in.
// NOT YET RECONSTRUCTED: a package on a store (store not nil) - the ROM
// domain manager told the package is in use (its monitor's call 4).
NewtonErr
InstallPackage(char* buffer, SourceType type, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled,
			   TStore* /*store*/, ULong /*storeId*/)
{
	long err = ((TForkWorld*) GetGlobals())->Fork(nil);
	if (err != noErr)
		return err;
	PartSource source;
	source.stream.bufferId = (TObjectId) buffer;
	source.stream.messagePortId = 0;
	TAppWorld* world = (TAppWorld*) GetGlobals();
	TPkBeginLoadEvent event(type, source, *world->GetMyPort(), *world->GetMyPort(), true);
	TUPort port(PackageManagerPortId());
	world->ReleaseMutex();
	gPackageSemaphore->Acquire(kWaitOnBlock);
	ULong replySize;
	err = port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	if (err == noErr)
		err = event.fEventError;
	gPackageSemaphore->Release();
	world->AcquireMutex();
	if (!event.fForDispatchOnly)
		*packageId = event.fPackageId;
	else
		*packageId = 0;
	if (forDispatchOnly != nil)
		*forDispatchOnly = event.fForDispatchOnly;
	if (patchInstalled != nil)
		*patchInstalled = event.fPatchInstalled;
	return err;
}


// ROM 0x001619b0 InstallPackage__FPc10SourceTypePUlPUcT4
NewtonErr
InstallPackage(char* buffer, SourceType type, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled)
{
	return InstallPackage(buffer, type, packageId, forDispatchOnly, patchInstalled, nil, 0);
}


// ROM 0x0015d53c LoadPackage__FPc10SourceTypePUl
// A package in memory loaded; anything else is for a TPackageLoader.
NewtonErr
LoadPackage(Ptr buffer, SourceType type, ULong* packageId)
{
	if (IsMemory(type))
		return InstallPackage(buffer, type, packageId, nil, nil, nil, 0);
	return kError_Bad_Parameters;
}


// ROM 0x0015d670 DeinstallPackage__FUl
// The package removed: the world forked and a 'pkrm event sent (the
// manager's answer is not looked at).  ==> the fork's error.
NewtonErr
DeinstallPackage(ULong packageId)
{
	TAppWorld* world = (TAppWorld*) GetGlobals();
	TPkRemoveEvent event(packageId, *world->GetMyPort(), *world->GetMyPort());
	long err = world->Fork(nil);
	if (err == noErr)
	{
		TUPort port(PackageManagerPortId());
		world->ReleaseMutex();
		ULong replySize;
		port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
		world->AcquireMutex();
	}
	return err;
}


// ROM 0x0015d748 RemovePackage__FUl
// A package taken away: one on a store is deallocated there, anything
// else deinstalled.  NOT YET RECONSTRUCTED: packages on a store
// (IdToStore, DeallocatePackage) - every package is in memory, so every
// one is deinstalled.
void
RemovePackage(TObjectId packageId)
{
	DeinstallPackage(packageId);
}


// ROM 0x00161ef0 SafeToDeactivatePackage__FUlPUc
NewtonErr
SafeToDeactivatePackage(ULong packageId, UChar* safe)
{
	TPkSafeToDeactivate event(packageId);
	TUPort port(PackageManagerPortId());
	ULong replySize;
	long err = port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	if (err == noErr)
		*safe = event.fSafe;
	return err;
}


// ROM 0x0015be7c cGetPackageBackupInfo__FUlPUsPUlT3PlP10SourceTypeT5N23
// One step of the walk of the package list: the package after *index
// (from the start when it is 0) - its name, size, id, version, source,
// flags and date - and its index in *index (-1 when there are no more).
// The world's mutex is let go while the manager answers.  ==> noErr,
// whatever happens.
NewtonErr
cGetPackageBackupInfo(ULong lastBackupDate, UniChar* name, ULong* size, ULong* packageId, Long* version,
					  SourceType* type, Long* index, ULong* flags, ULong* modifyDate)
{
	TAppWorld* world = (TAppWorld*) GetGlobals();
	PartSource source;		// (the ROM sends whatever is on its stack)
	source.stream.bufferId = 0;
	source.stream.messagePortId = 0;
	TPkBackupEvent event(*index, lastBackupDate, true, source, *world->GetMyPort(), *world->GetMyPort());
	TUPort port(PackageManagerPortId());
	world->ReleaseMutex();
	ULong replySize;
	port.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	world->AcquireMutex();
	*index = event.fIndex;
	*size = event.fPackageSize;
	*packageId = event.fPackageId;
	*version = event.fVersion;
	*type = event.fSourceType;
	*flags = event.fPackageFlags;
	*modifyDate = event.fModifyDate;
	Ustrncpy(name, event.fPackageName, kMaxPackageNameSize);
	name[kMaxPackageNameSize] = 0;
	return noErr;
}


/*------------------------------------------------------------------------------
	T P M I t e r a t o r
------------------------------------------------------------------------------*/

// ROM 0x0015bfc4 __ct__11TPMIteratorFv
TPMIterator::TPMIterator()
{
	fIndex = 0;
	fUnused60 = 0;
}


// ROM 0x0015c064 __dt__11TPMIteratorFv
TPMIterator::~TPMIterator()
{ }


// ROM 0x0015c070 Init__11TPMIteratorFv
// The walk begun: gPackageSemaphore taken (the world's mutex let go while
// it waits) and the first package asked for.
void
TPMIterator::Init(void)
{
	fIndex = 0;
	TForkWorld* world = (TForkWorld*) GetGlobals();
	world->ReleaseMutex();
	gPackageSemaphore->Acquire(kWaitOnBlock);
	world->AcquireMutex();
	cGetPackageBackupInfo(0xffffffff, fPackageName, &fPackageSize, &fPackageId, &fVersion,
						  &fSourceType, &fIndex, &fPackageFlags, &fModifyDate);
}


// ROM 0x0015c0e0 Done__11TPMIteratorFv
void
TPMIterator::Done(void)
{
	gPackageSemaphore->Release();
}


// ROM 0x0015c11c More__11TPMIteratorFv
// (a package of size 0 ends the walk too: the answer after the last one)
Boolean
TPMIterator::More(void)
{
	return fIndex != -1 && fPackageSize != 0;
}


// ROM 0x0015bff8 NextPackage__11TPMIteratorFv
void
TPMIterator::NextPackage(void)
{
	if (!More())
		return;
	fIndex++;
	if (cGetPackageBackupInfo(0xffffffff, fPackageName, &fPackageSize, &fPackageId, &fVersion,
							  &fSourceType, &fIndex, &fPackageFlags, &fModifyDate) != noErr)
		fIndex = -1;
}


// ROM 0x0015c108 IsCopyProtected__11TPMIteratorFv
Boolean
TPMIterator::IsCopyProtected(void)
{
	return (fPackageFlags & kCopyProtectFlag) != 0;
}
