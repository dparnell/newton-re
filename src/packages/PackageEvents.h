/*
	File:		packages/PackageEvents.h

	Contains:	The package manager's events: the messages that pass between
				a task loading or removing a package, the package manager
				task ('pckm') and the part handlers of the worlds that
				install parts.  Every one is a 'newt-class, 'pckm-id TAEvent
				whose third word says which (fEventCode) and whose fourth
				is the answer (fEventError); what follows is the event's
				own.

				- TPkBeginLoadEvent ('pkbl): load the package at a source -
				  a buffer for a memory source, a shared ring buffer and a
				  port for a stream; the package manager answers the id it
				  gave the package and what the package says about itself.
				- TPkRemoveEvent ('pkrm): remove a package by id.
				- TPkRegisterEvent ('rgtr) / TPkUnregisterEvent ('urgr): a
				  part handler says which part type it installs and which
				  port to send the parts to.
				- TPkPartInstallEvent ('prti): the package manager hands a
				  part handler one part - its PartId, its PartInfo with the
				  package's name after it, its source, and copies of the
				  part's info and compressor name - and the handler answers
				  a TPkPartInstallEventReply: an error, the "remove object"
				  it wants back when the part goes, and whether it took the
				  part.
				- TPkPartRemoveEvent ('prtr): a part to take out again.
				- TPkSafeToDeactivate ('pksc): whether a package's protocol
				  parts are still in use.
				- TPkBackupEvent ('pkbu): the package list walked one
				  package at a time (TPMIterator, GetPackages).

				The ROM's layouts (the offsets below) are those of its
				constructors; the ROM sends each by its fixed size.
				DEVIATION: a host word that carries an id, a size or an
				address is pointer-sized, so the host's events are larger
				than the ROM's and are sent by sizeof; the fields keep the
				ROM's order.

	Reconstructed from the MP2x00 US ROM (0x0015c13c-0x0015c558); each
	function cites its origin.
*/

#ifndef __PACKAGEEVENTS_H
#define __PACKAGEEVENTS_H

#ifndef __AEVENTS_H
#include "AEvents.h"
#endif
#ifndef __PACKAGETYPES_H
#include "PackageTypes.h"
#endif

// the events' codes (fEventCode)
enum
{
	kPkBeginLoadEvent		= 'pkbl',
	kPkRemoveEvent			= 'pkrm',
	kPkRegisterEvent		= 'rgtr',
	kPkUnregisterEvent		= 'urgr',
	kPkPartInstallEvent		= 'prti',
	kPkPartRemoveEvent		= 'prtr',
	kPkSafeToDeactivateEvent	= 'pksc',
	kPkBackupEvent			= 'pkbu'
};


// 0x10 bytes
class TPkBaseEvent : public TAEvent
{
public:
					TPkBaseEvent();

	ULong			fEventCode;				// +0x08
	NewtonErr		fEventError;			// +0x0c  the answer
};


// 0x18 bytes
class TPkRegisterEvent : public TPkBaseEvent
{
public:
					TPkRegisterEvent(ULong type, ULong portId);

	ULong			fPartType;				// +0x10
	TObjectId		fPortId;				// +0x14  where the parts are to go
};


// 0x14 bytes
class TPkUnregisterEvent : public TPkBaseEvent
{
public:
					TPkUnregisterEvent(ULong type);

	ULong			fPartType;				// +0x10
};


// 0x84 bytes
class TPkBeginLoadEvent : public TPkBaseEvent
{
public:
					TPkBeginLoadEvent(SourceType type, const PartSource& source, ULong senderPortId, ULong forwardPortId, Boolean flag);

	TObjectId		fSenderPortId;			// +0x10  the port of the task that sent it
	TObjectId		fForwardPortId;			// +0x14  a part handler registered for the sender's port gets its parts here instead
	SourceType		fSourceType;			// +0x18
	PartSource		fSource;				// +0x20
	Boolean			fFlag;					// +0x28  (1 from every caller in this ROM; nothing reads it)
	// the answer
	TObjectId		fPackageId;				// +0x2c
	ULong			fPackageSize;			// +0x30
	ULong			fNumParts;				// +0x34
	ULong			fVersion;				// +0x38
	ULong			fSizeInMemory;			// +0x3c  the parts' sizes in memory, together
	UniChar			fPackageName[kMaxPackageNameSize];	// +0x40  (not terminated when it fills the array)
	Boolean			fPatchInstalled;		// +0x80  a system patch went in: reboot
	Boolean			fForDispatchOnly;		// +0x81  the package was dispatched and removed again
};


// 0x1c bytes
class TPkRemoveEvent : public TPkBaseEvent
{
public:
					TPkRemoveEvent(ULong packageId, ULong senderPortId, ULong forwardPortId);

	TObjectId		fPackageId;				// +0x10
	TObjectId		fSenderPortId;			// +0x14
	TObjectId		fForwardPortId;			// +0x18
};


// 0x24 bytes
class TPkPartRemoveEvent : public TPkBaseEvent
{
public:
					TPkPartRemoveEvent(PartId partId, PartKind kind, PartType type, RemoveObjPtr removeObj);

	PartId			fPartId;				// +0x10
	PartKind		fPartKind;				// +0x18
	PartType		fPartType;				// +0x1c
	RemoveObjPtr	fRemoveObjPtr;			// +0x20
};


// 0x18 bytes
class TPkSafeToDeactivate : public TPkBaseEvent
{
public:
					TPkSafeToDeactivate(ULong packageId);

	TObjectId		fPackageId;				// +0x10
	Boolean			fSafe;					// +0x14
};


// 0x88 bytes
class TPkBackupEvent : public TPkBaseEvent
{
public:
					TPkBackupEvent(Long index, ULong lastBackupDate, Boolean flag, const PartSource& source, ULong senderPortId, ULong forwardPortId);

	Long			fIndex;					// +0x10  0: from the start; the package's index in the list comes back (-1: no more)
	ULong			fLastBackupDate;		// +0x14
	PartSource		fSource;				// +0x18
	TObjectId		fSenderPortId;			// +0x20
	TObjectId		fForwardPortId;			// +0x24
	// the answer: the package's block
	ULong			fPackageSize;			// +0x28
	TObjectId		fPackageId;				// +0x2c
	ULong			fVersion;				// +0x30
	SourceType		fSourceType;			// +0x34
	ULong			fPackageFlags;			// +0x3c
	ULong			fModifyDate;			// +0x40
	UniChar			fPackageName[kMaxPackageNameSize];	// +0x44
	Boolean			fFlag;					// +0x84
};


// 0xf4 bytes
class TPkPartInstallEvent : public TPkBaseEvent
{
public:
					TPkPartInstallEvent(const PartId& partId, const ExtendedPartInfo& info, SourceType type, const PartSource& source);

	PartId			fPartId;				// +0x10
	ExtendedPartInfo	fPartInfo;			// +0x18  (its info and compressor point into this event once it arrives)
	SourceType		fSourceType;			// +0x84
	PartSource		fSource;				// +0x8c
	char			fInfo[kMaxInfoSize];	// +0x94
	char			fCompressor[kMaxCompressorNameSize];	// +0xd4
};


// 0x18 bytes
class TPkPartInstallEventReply : public TPkBaseEvent
{
public:
					TPkPartInstallEventReply();

	RemoveObjPtr	fRemoveObjPtr;			// +0x10
	Boolean			fAccepted;				// +0x14
};

#endif	/* __PACKAGEEVENTS_H */
