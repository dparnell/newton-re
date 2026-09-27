/*
	File:		packages/PackageEvents.cpp

	Contains:	The package manager's events (PackageEvents.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PackageEvents.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x0015c13c __ct__12TPkBaseEventFv
TPkBaseEvent::TPkBaseEvent()
{
	fAEventID = kPackageEventId;
}


// ROM 0x0015c17c __ct__16TPkRegisterEventFUlT1
TPkRegisterEvent::TPkRegisterEvent(ULong type, ULong portId)
{
	fEventCode = kPkRegisterEvent;
	fEventError = noErr;
	fPartType = type;
	fPortId = portId;
}


// ROM 0x0015c2a0 __ct__18TPkUnregisterEventFUl
TPkUnregisterEvent::TPkUnregisterEvent(ULong type)
{
	fEventCode = kPkUnregisterEvent;
	fEventError = noErr;
	fPartType = type;
}


// ROM 0x0015c2f4 __ct__17TPkBeginLoadEventF10SourceTypeRC10PartSourceUlT3Uc
// (the answer's fields are left as they were, but for the id)
TPkBeginLoadEvent::TPkBeginLoadEvent(SourceType type, const PartSource& source, ULong senderPortId, ULong forwardPortId, Boolean flag)
{
	fEventCode = kPkBeginLoadEvent;
	fEventError = noErr;
	fPackageId = 0;
	fForwardPortId = forwardPortId;
	fSenderPortId = senderPortId;
	fSourceType = type;
	fSource.stream.bufferId = source.stream.bufferId;
	fSource.stream.messagePortId = source.stream.messagePortId;
	fFlag = flag;
	// ROM BUG: the two answer flags are left as they were, and only a load
	// that got as far as its parts sets them; a refused package's answer
	// carries whatever the sender's stack held (InstallPackage then says
	// "dispatched only" and answers id 0 at random).  The host's start
	// false, as the stack most often held.
	fPatchInstalled = false;
	fForDispatchOnly = false;
}


// ROM 0x0015c380 __ct__14TPkRemoveEventFUlN21
TPkRemoveEvent::TPkRemoveEvent(ULong packageId, ULong senderPortId, ULong forwardPortId)
{
	fEventCode = kPkRemoveEvent;
	fEventError = noErr;
	fPackageId = packageId;
	fForwardPortId = forwardPortId;
	fSenderPortId = senderPortId;
}


// ROM 0x0015c1d8 __ct__18TPkPartRemoveEventF6PartIdlUlT2
// (fEventError is left as it was)
TPkPartRemoveEvent::TPkPartRemoveEvent(PartId partId, PartKind kind, PartType type, RemoveObjPtr removeObj)
{
	fEventCode = kPkPartRemoveEvent;
	fPartId.partIndex = partId.partIndex;
	fPartId.packageId = partId.packageId;
	fPartKind = kind;
	fPartType = type;
	fRemoveObjPtr = removeObj;
}


// ROM 0x0015c248 __ct__19TPkSafeToDeactivateFUl
TPkSafeToDeactivate::TPkSafeToDeactivate(ULong packageId)
{
	fEventCode = kPkSafeToDeactivateEvent;
	fEventError = noErr;
	fPackageId = packageId;
	fSafe = false;
}


// ROM 0x0015c3e4 __ct__14TPkBackupEventFlUlUcRC10PartSourceN22
TPkBackupEvent::TPkBackupEvent(Long index, ULong lastBackupDate, Boolean flag, const PartSource& source, ULong senderPortId, ULong forwardPortId)
{
	fLastBackupDate = lastBackupDate;
	fIndex = index;
	fSource.stream.bufferId = source.stream.bufferId;
	fSource.stream.messagePortId = source.stream.messagePortId;
	fEventCode = kPkBackupEvent;
	fEventError = noErr;
	fForwardPortId = forwardPortId;
	fSenderPortId = senderPortId;
	fFlag = flag;
}


// ROM 0x0015c464 __ct__19TPkPartInstallEventFRC6PartIdRC16ExtendedPartInfo10SourceTypeRC10PartSource
// The part's info bytes and its compressor's name are copied into the
// event, since the pointers to them mean nothing in the task that gets it.
// ROM BUG: the info is copied for as long as infoSize says, with no check
// against the 64 bytes the event has for it; a longer info runs on into
// the compressor's name (which a compressed part then writes over) and
// past the end of the event.  DEVIATION: the host keeps the first part of
// that - the run into the compressor's name - and stops at the end of the
// two, where the ROM goes on over whatever follows the event.
TPkPartInstallEvent::TPkPartInstallEvent(const PartId& partId, const ExtendedPartInfo& info, SourceType type, const PartSource& source)
{
	fEventCode = kPkPartInstallEvent;
	fPartId.packageId = partId.packageId;
	fPartId.partIndex = partId.partIndex;
	fPartInfo = info;
	fSourceType = type;
	fSource.stream.bufferId = source.stream.bufferId;
	fSource.stream.messagePortId = source.stream.messagePortId;
	ULong size = fPartInfo.infoSize;
	if (size > sizeof(fInfo) + sizeof(fCompressor))
		size = sizeof(fInfo) + sizeof(fCompressor);
	if (size != 0)
		memmove(fInfo, info.info, size);
	if (fPartInfo.compressed)
		strncpy(fCompressor, info.compressor, kMaxCompressorNameSize);
}


// ROM 0x0015c524 __ct__24TPkPartInstallEventReplyFv
TPkPartInstallEventReply::TPkPartInstallEventReply()
{ }
