/*
	File:		ObjectMessage.h

	Contains:	ObjectMessage, the request block the user side (TUObject::MakeObject
				and friends, UserObjects.h) hands to the object manager monitor,
				and the selectors of that monitor.  The DDK only forward-declares
				the struct; the layouts here are what the ROM's user-side classes
				build and the kernel's handlers read.

				Every request starts with its size and, at +0x08, either the type
				of object to make (ObjectTypes, for kObjectMgr_Alloc) or the id of
				the object it concerns.  The reply overwrites the head: the new
				object's id at +0x00, a fetched register at +0x04.

	Reconstructed from:	TObjectManager::MonitorProc 0x0014ad88, ObjectAlloc 0x0014a768,
				the Object* handlers 0x0014a5e0-0x0014b4ac and the TU* Init /
				Start / Suspend / GetRegister / SetRegister / SetFaultMonitor
				wrappers (0x00256000-0x00259e00) that fill the messages in.
*/

#ifndef __OBJECTMESSAGE_H
#define __OBJECTMESSAGE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __KERNELTYPES_H
#include "KernelTypes.h"			// ObjectTypes
#endif
#ifndef __SHAREDTYPES_H
#include "SharedTypes.h"			// TaskProcPtr, MonitorProcPtr
#endif

// selectors of the object manager monitor (TObjectManager::MonitorProc)
enum ObjectManagerSelectors
{
	kObjectMgr_Alloc = 0,			// make an object; reply: id at +0x00
	kObjectMgr_Destroy,				// destroy fObjectId
	kObjectMgr_Unused2,
	kObjectMgr_Start,				// schedule task fObjectId
	kObjectMgr_Suspend,				// unschedule task fObjectId
	kObjectMgr_SetRegister,			// task fObjectId: fRegister.fNumber = fRegister.fValue
	kObjectMgr_GetRegister,			// task fObjectId: reply fValue = register fRegister.fNumber
	kObjectMgr_AddDomain,			// environment fObjectId gains fDomain.fDomainId
	kObjectMgr_GetContent,			// task fObjectId: reply fContent
	kObjectMgr_RemoveDomain,		// environment fObjectId loses fDomain.fDomainId
	kObjectMgr_SetFaultMonitor,		// domain fDomain.fDomainId gets fDomain.fMonitorId
	kObjectMgr_NewExtPageTracker,
	kObjectMgr_DisposeExtPageTracker,
	kObjectMgr_KillSelf = 0xff		// TaskKillSelf: the calling task is deleted at the next request
};


struct ObjectMessage
{
	ULong			fSize;				// +0x00  bytes in the request, including this header
	ULong			fValue;				// +0x04  reply of kObjectMgr_GetRegister
	union
	{
		ObjectTypes		fType;			// +0x08  kObjectMgr_Alloc: what to make
		TObjectId		fObjectId;		// +0x08  everything else: the object concerned
	};
	union
	{
		struct {						// kObjectTask, size 0x28
			ULong			fUnused;
			TaskProcPtr		fProc;			// +0x10
			ULong			fStackSize;		// +0x14
			TObjectId		fDataId;		// +0x18  shared memory holding the task's object, copied onto its stack
			ULong			fPriority;		// +0x1c
			ULong			fName;			// +0x20
			TObjectId		fEnvironmentId;	// +0x24  0: the requester's
		} fTask;
		struct {						// kObjectEnvironment, size 0x10
			void*			fUnknown;		// +0x0c  TEnvironment::Init's argument
		} fEnvironment;
		struct {						// kObjectDomain, size 0x18; also kObjectMgr_AddDomain (0x14),
			TObjectId		fMonitorId;		// +0x0c    RemoveDomain (0x10), SetFaultMonitor (0x14), where +0x0c is
			VAddr			fBase;			// +0x10    the domain and +0x10 the monitor / the booleans
			ULong			fSize;			// +0x14
		} fDomain;
		struct {						// kObjectMgr_AddDomain
			TObjectId		fDomainId;		// +0x0c
			Boolean			fIsManager;		// +0x10
			Boolean			fIsHeap;		// +0x11
			Boolean			fIsStack;		// +0x12
		} fEnvDomain;
		struct {						// kObjectSemList, size 0x10 + 4 * fCount
			ULong			fCount;			// +0x0c
			ULong			fOps[1];		// +0x10  MAKESEMLISTITEM words
		} fSemList;
		struct {						// kObjectSemGroup, size 0x10
			ULong			fCount;			// +0x0c
		} fSemGroup;
		struct {						// kObjectMonitor, size 0x24
			MonitorProcPtr	fProc;			// +0x0c
			ULong			fStackSize;		// +0x10
			void*			fMonitorObject;	// +0x14
			TObjectId		fEnvironmentId;	// +0x18  0: the requester's
			Boolean			fFaultMonitor;	// +0x1c
			Boolean			fRebootProtected;	// +0x1d
			ULong			fName;			// +0x20
		} fMonitor;
		struct {						// kObjectPhys, size 0x24
			ULong			fUnused;		// +0x0c  0
			PAddr			fBase;			// +0x10
			ULong			fUnused2[2];
			ULong			fSize;			// +0x1c
			Boolean			fReadOnly;		// +0x20
			Boolean			fCache;			// +0x21
		} fPhys;
		struct {						// kObjectMgr_GetRegister (0x10) / SetRegister (0x14)
			ULong			fNumber;		// +0x0c  0-15
			ULong			fValue;			// +0x10
		} fRegister;
		struct {						// kObjectMgr_GetContent, request 0x10 (reply: ObjectContentReply)
			ULong			fKind;			// +0x0c  1 is the only kind
		} fContentRequest;
		struct {						// kObjectMgr_NewExtPageTracker: fObjectId and these two
			ULong			fArg[2];		// +0x0c
		} fTracker;
	};
};

// what kObjectMgr_GetContent leaves in the message
struct ObjectContentReply
{
	ULong			fSize;				// +0x00
	ULong			fValue;				// +0x04
	ULong			fPriority;			// +0x08
	ULong			fName;				// +0x0c
	ULong			fTaskTimeLo;		// +0x10
	SLong			fTaskTimeHi;		// +0x14
	ULong			fStackSize;			// +0x18
	ULong			fHandlesUsed;		// +0x1c
	ULong			fPtrsUsed;			// +0x20
	ULong			fMaxMemoryUsed;		// +0x24
};

// fSize of each request, as the ROM's wrappers send and its handlers check
// them (the ROM's 32-bit layout; the same numbers are used on every host)
enum
{
	kObjectMessage_HeaderSize		= 0x0c,		// port, shared memory, message, destroy, start, suspend
	kObjectMessage_TaskSize			= 0x28,
	kObjectMessage_EnvironmentSize	= 0x10,
	kObjectMessage_DomainSize		= 0x18,
	kObjectMessage_SemListSize		= 0x10,		// + 4 per op
	kObjectMessage_SemGroupSize		= 0x10,
	kObjectMessage_MonitorSize		= 0x24,
	kObjectMessage_PhysSize			= 0x24,
	kObjectMessage_GetRegisterSize	= 0x10,
	kObjectMessage_SetRegisterSize	= 0x14,
	kObjectMessage_AddDomainSize	= 0x14,
	kObjectMessage_RemoveDomainSize	= 0x10,
	kObjectMessage_GetContentSize	= 0x10,
	kObjectMessage_SetFaultMonitorSize = 0x14
};

#endif	/* __OBJECTMESSAGE_H */
