/*
	File:		packages/PackageManager.h

	Contains:	The package manager: the task that installs and removes
				packages, and the calls the rest of the system makes on it.

				The manager is an application world of its own
				(TPackageManager, registered as 'pckm' - PackageManagerPortId
				finds its port) with one event handler, TPackageEventHandler,
				which keeps two lists:

				- the package list: a TPackageBlock per installed package
				  (its id, version, size, source, flags, date, name and
				  copyright), each with a TInstalledPart per part that went
				  in (its type and kind, the object its handler wants back to
				  remove it, the class info of a protocol part, and flags);
				- the part registry: a TRegistryInfo per part type a part
				  handler has registered for, saying which port its parts go
				  to.

				A package is loaded by sending the manager a
				TPkBeginLoadEvent (InstallPackage, LoadPackage,
				LoadHighROMFramesPackages): BeginLoadPackage reads the
				package's directory with a TPackageIterator, refuses a
				package whose name is already installed (with the answer
				saying whether the one there is older, newer or the same),
				gives the package an id at random, and then LoadNextPart
				installs its parts one at a time - a protocol part's code
				registered with the protocol registry, a system patch
				applied, and every part whose notify flag is set sent to
				the handler registered for its type (a TPkPartInstallEvent,
				InstallPart).  A part that fails takes the whole package
				out again (RemovePackage, each installed part sent a
				TPkPartRemoveEvent).  Nothing but the sender of an event
				waits on the manager: the sender forks first (TForkWorld)
				so that its own world goes on taking events while the
				manager sends it parts.  gPackageSemaphore keeps one load
				at a time.

				The script's view of the list is TPMIterator, which walks it
				a package at a time with TPkBackupEvents.

				DEVIATION: the host has one heap, so the manager's
				persistent heap (the 'prot' domain's, which keeps the
				package list through a warm reboot) is the ordinary one.

				NOT YET RECONSTRUCTED: TPackageLoader and the streamed
				sources (a package read through a pipe: CShadowRingBuffer,
				CPartPipe, TPipeApp), the validation driver's package check
				(ValidatePackage; no TValidatePackageDriver is ever
				registered on the host), system patches
				(CheckAndInstallPatch), packages on a store (the ROM domain
				manager: IdToStore and the rest).

	Reconstructed from the MP2x00 US ROM (0x0015bf00-0x0015fe48,
	0x00161b68-0x00161f90, 0x00182174); each function cites its origin.
*/

#ifndef __PACKAGEMANAGER_H
#define __PACKAGEMANAGER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __PACKAGEEVENTS_H
#include "PackageEvents.h"
#endif
#ifndef __DYNAMICARRAY_H
#include "DynamicArray.h"
#endif
#ifndef __ARRAYITERATOR_H
#include "ArrayIterator.h"
#endif

class TPackageIterator;
class CPartPipe;
class CShadowRingBuffer;
class TStore;
class TClassInfo;
class TValidatePackageDriver;


// what a package block's fState says the package is doing
enum
{
	kPackageLoading		= 'spbl',		// its parts are going in
	kPackageValid		= 'spvd',		// installed
	kPackageRemoving	= 'spcm'		// its parts are coming out (fRemoveIndex: the next)
};


// One part type a handler has registered for (8 bytes).
class TRegistryInfo
{
public:
					TRegistryInfo(ULong type, ULong portId);

	ULong			fPartType;				// +0x00
	TObjectId		fPortId;				// +0x04
};


// One installed package (0x30 bytes); the package list is an array of them.
class TPackageBlock
{
public:
	NewtonErr		Init(ULong packageId, ULong version, ULong size, SourceType source, ULong flags,
						 const UniChar* name, const UniChar* copyright, ULong numParts, ULong modifyDate);

	TObjectId		fPackageId;				// +0x00
	ULong			fVersion;				// +0x04
	ULong			fSize;					// +0x08
	SourceType		fSourceType;			// +0x0c
	ULong			fFlags;					// +0x14  the package's flags
	ULong			fModifyDate;			// +0x18
	UniChar*		fName;					// +0x1c  a copy
	UniChar*		fCopyright;				// +0x20  a copy (nil: none)
	CDynamicArray*	fParts;					// +0x24  its TInstalledParts
	ULong			fState;					// +0x28  kPackageLoading, ...
	ArrayIndex		fRemoveIndex;			// +0x2c  the part a removal has got to
};


// One installed part (0x14 bytes).
class TInstalledPart
{
public:
					TInstalledPart(ULong type, PartKind kind, RemoveObjPtr removeObj,
								   Boolean autoLoad, Boolean ownsCode, Boolean notify, Boolean accepted, ULong classInfo);

	PartType		fType;					// +0x00
	PartKind		fKind;					// +0x04
	RemoveObjPtr	fRemoveObjPtr;			// +0x08  what its handler wants back to remove it
	ULong			fClassInfo;				// +0x0c  a protocol part's registered class info (a TClassInfo*)
	// +0x10, the ROM's bit-fields, top bit first
	unsigned		fAutoLoad : 1;			// bit 31
	unsigned		fNotify : 1;			// bit 30  its handler was sent it (and is sent its removal)
	unsigned		fAccepted : 1;			// bit 29
	unsigned		fOwnsCode : 1;			// bit 28  the class info is a copy the manager frees
	unsigned		fUnused : 28;
};


/*------------------------------------------------------------------------------
	T P a c k a g e E v e n t H a n d l e r

	0x6c bytes: a TAEventHandler (0x14) and the manager's state.
------------------------------------------------------------------------------*/

class TPackageEventHandler : public TAEventHandler
{
public:
					TPackageEventHandler();
	virtual			~TPackageEventHandler();

	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual	void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);

	void			BeginLoadPackage(TPkBeginLoadEvent* event);
	Boolean			LoadNextPart(long* result, UChar* forDispatchOnly, UChar* patchInstalled);
	long			InstallPart(ULong* classInfo, RemoveObjPtr* removeObj, UChar* accepted, const PartId& partId,
								ExtendedPartInfo& info, SourceType type, const PartSource& source);
	NewtonErr		LoadProtocolCode(void** classInfo, PartInfo& info, SourceType type, const PartSource& source);
	NewtonErr		CheckAndInstallPatch(PartInfo& info, SourceType type, const PartSource& source);
	NewtonErr		ValidatePackage(TPkBeginLoadEvent* event, TPackageIterator* iter);
	void			InitValidatePackageDriver(void);
	ULong			GetUniquePackageId(void);
	long			GetPartSize(void);
	void			RemovePart(const PartId& partId, const TInstalledPart& part, UChar notify);
	void			RemovePackage(TPkRemoveEvent* event, UChar reply, UChar notify);
	NewtonErr		SearchPackageList(long* index, ULong packageId);
	NewtonErr		SearchPackageList(long* index, const UniChar* name, ULong unused);
	NewtonErr		SearchRegistry(long* index, ULong* portId, long type);
	void			Register(TPkRegisterEvent* event);
	void			Unregister(TPkUnregisterEvent* event);
	void			SafeToDeactivatePackage(TPkSafeToDeactivate* event);
	void			GetBackupInfo(TPkBackupEvent* event);
	void			SetPersistentHeap(void);
	void			SetDefaultHeap(void);

	TPackageBlock*	PackageAt(long index)	{ return (TPackageBlock*) fPackages->ElementPtrAt(index); }

	CDynamicArray*		fPackages;			// +0x14  the package list: TPackageBlocks
	CDynamicArray*		fRegistry;			// +0x18  TRegistryInfos
	Boolean				fForwarding;		// +0x1c  an event is being handled whose sender's parts go elsewhere
	TObjectId			fSenderPortId;		// +0x20
	TObjectId			fForwardPortId;		// +0x24
	TPackageIterator*	fIter;				// +0x28  the package being loaded
	CPartPipe*			fPipe;				// +0x2c  its pipe, for a streamed source
	PartSource			fSource;			// +0x30  its source
	ULong				fPartIndex;			// +0x38  the part being loaded
	TPackageBlock*		fPackage;			// +0x3c  the package being loaded
	CShadowRingBuffer*	fBuffer;			// +0x40  a streamed source's ring buffer
	CArrayIterator		fBackupIter;		// +0x44  GetBackupInfo's place in the package list
	ULong				fUnused60;			// +0x60
	Boolean				fPatchInstalled;	// +0x64
	TValidatePackageDriver*	fValidator;		// +0x68
};


/*------------------------------------------------------------------------------
	T P a c k a g e M a n a g e r

	The 'pckm' world (0x80 bytes: a TAppWorld and three words).
------------------------------------------------------------------------------*/

class TPackageManager : public TAppWorld
{
public:
	virtual ULong		GetSizeOf();
	virtual long		MainConstructor();
	virtual void		MainDestructor();

	TPackageEventHandler*	fHandler;		// +0x70
	Heap				fPersistentHeap;	// +0x74
	Heap				fDefaultHeap;		// +0x78
	ULong				fUnused7C;			// +0x7c
};

extern TULockingSemaphore*	gPackageSemaphore;		// one load (or walk of the list) at a time

void		InitializePackageManager(TObjectId environment);		// the 'pckm' task started (InitialKSRVTask)
TObjectId	PackageManagerPortId(void);							// 0: there is no package manager


/*------------------------------------------------------------------------------
	T h e   c a l l s   o n   t h e   m a n a g e r

	Each forks the caller's world first (except where noted): the call
	blocks until the manager answers, and the manager may send parts to
	the caller's own world meanwhile.
------------------------------------------------------------------------------*/

// The package at buffer installed (a memory source).  ==> its id, whether
// the package was only dispatched (and has gone again) and whether a
// system patch went in.
NewtonErr	InstallPackage(char* buffer, SourceType type, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled);
NewtonErr	InstallPackage(char* buffer, SourceType type, ULong* packageId, UChar* forDispatchOnly, UChar* patchInstalled, TStore* store, ULong storeId);
NewtonErr	LoadPackage(Ptr buffer, SourceType type, ULong* packageId);		// kError_Bad_Parameters for anything but a memory source
NewtonErr	DeinstallPackage(ULong packageId);
NewtonErr	SafeToDeactivatePackage(ULong packageId, UChar* safe);		// (no fork)
NewtonErr	cGetPackageBackupInfo(ULong lastBackupDate, UniChar* name, ULong* size, ULong* packageId, Long* version,
								  SourceType* type, Long* index, ULong* flags, ULong* modifyDate);


// The package list walked a package at a time, under gPackageSemaphore
// (0x64 bytes).
class TPMIterator
{
public:
					TPMIterator();
					~TPMIterator();

	void			Init(void);
	void			Done(void);
	Boolean			More(void);
	void			NextPackage(void);
	ULong			PackageSize(void)		{ return fPackageSize; }		// ROM 0x0015c0f0 PackageSize__11TPMIteratorFv
	ULong			PackageId(void)			{ return fPackageId; }			// ROM 0x0015c0f8 PackageId__11TPMIteratorFv
	UniChar*		PackageName(void)		{ return fPackageName; }		// ROM 0x0015c100 PackageName__11TPMIteratorFv
	Boolean			IsCopyProtected(void);

	Long			fIndex;					// +0x00  -1: done
	ULong			fPackageSize;			// +0x04
	ULong			fPackageId;				// +0x08
	UniChar			fPackageName[kMaxPackageNameSize + 1];	// +0x0c
	Long			fVersion;				// +0x4c
	SourceType		fSourceType;			// +0x50
	ULong			fModifyDate;			// +0x58
	ULong			fPackageFlags;			// +0x5c
	ULong			fUnused60;				// +0x60
};

#endif	/* __PACKAGEMANAGER_H */
