/*
	File:		ObjectManager.cpp

	Contains:	The object manager monitor's proc, its request handlers and the
				object table's scavenge proc.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ObjectManager.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "Scheduler.h"
#include "Monitor.h"
#include "Port.h"
#include "Semaphore.h"
#include "Domain.h"
#include "Environment.h"
#include "OSErrors.h"
#include "hal/Atomic.h"
#include "hal/System.h"


TObjectManager*	gTheObjectManager = nil;


// ROM 0x0014ad58 __ct__14TObjectManagerFv
TObjectManager::TObjectManager()
{
	fTaskToDelete = 0;
}


// The monitor proc as TMonitor calls it (MonitorEntryGlue: object, selector,
// user object); the result goes back through MonitorExitSWI.
void
TObjectManager::MonitorProcGlue(void* manager, ULong selector, void* msg)
{
	((TObjectManager*) manager)->MonitorProc((long) selector, (ObjectMessage*) msg);
}


// ROM 0x0014ad88 MonitorProc__14TObjectManagerFlP13ObjectMessage
// A task that kills itself (kObjectMgr_KillSelf) cannot be removed while it
// is the monitor's caller, so it is marked and removed on the next request.
NewtonErr
TObjectManager::MonitorProc(long selector, ObjectMessage* msg)
{
	TTask* caller = gTheObjectManagerMonitor->fCaller;
	TObjectId callerId = caller->fId;
	if (fTaskToDelete != 0)
	{
		gObjectTable->Remove(fTaskToDelete);
		fTaskToDelete = 0;
	}
	if (selector == kObjectMgr_KillSelf)
	{
		caller->fState |= kTaskState_KilledSelf;
		fTaskToDelete = callerId;
		return noErr;
	}

	NewtonErr err = kError_Bad_Parameters;
	ULong size = msg->fSize;
	switch (selector)
	{
	case kObjectMgr_Alloc:
		// NOT YET RECONSTRUCTED: ObjectAlloc 0x0014a768 (needs the object
		// Inits); the ROM stores the new id in msg->fSize on success
		err = kError_Call_Not_Implemented;
		break;
	case kObjectMgr_Destroy:
		err = ObjectDestroy(msg, size, callerId);
		break;
	case kObjectMgr_Unused2:
		break;
	case kObjectMgr_Start:
		err = ObjectStart(msg, size);
		break;
	case kObjectMgr_Suspend:
		err = ObjectSuspend(msg, size);
		break;
	case kObjectMgr_SetRegister:
		err = ObjectSetRegister(msg, size);
		break;
	case kObjectMgr_GetRegister:
		{
			ULong value;
			err = ObjectGetRegister(msg, size, &value);
			if (err == noErr)
				msg->fValue = value;
		}
		break;
	case kObjectMgr_AddDomain:
	case kObjectMgr_RemoveDomain:
		{
			if (size != (selector == kObjectMgr_AddDomain ? kObjectMessage_AddDomainSize : kObjectMessage_RemoveDomainSize))
				break;
			TKDomain* domain = ObjectType(msg->fEnvDomain.fDomainId) == kDomainType ? (TKDomain*) gObjectTable->Get(msg->fEnvDomain.fDomainId) : nil;
			if (domain == nil)
				break;
			TEnvironment* env = ObjectType(msg->fObjectId) == kEnvironmentType ? (TEnvironment*) gObjectTable->Get(msg->fObjectId) : nil;
			if (env == nil)
				break;
			if (selector == kObjectMgr_AddDomain)
				env->Add(domain, msg->fEnvDomain.fIsManager, msg->fEnvDomain.fIsHeap, msg->fEnvDomain.fIsStack);
			else
				env->Remove(domain);
			err = noErr;
		}
		break;
	case kObjectMgr_GetContent:
		err = GetObjectContent(msg, size, msg);
		break;
	case kObjectMgr_SetFaultMonitor:
		err = SetDomainFaultMonitor(msg, size, msg);
		break;
	case kObjectMgr_NewExtPageTracker:
	case kObjectMgr_DisposeExtPageTracker:
		// NOT YET RECONSTRUCTED: TExtPageTrackerMgr::MakeNewTracker /
		// DisposeTracker (gExtPageTrackerMgr, made on first use)
		err = kError_Call_Not_Implemented;
		break;
	}

	if (gTaskDestroyed)
	{
		gTaskDestroyed = false;
		gObjectTable->ScavengeAll();
	}
	return err;
}


/* -------------------------------------------------------------------------------
	Request handlers
------------------------------------------------------------------------------- */

static TTask*
TaskFromId(TObjectId id)
{
	return ObjectType(id) == kTaskType ? (TTask*) gObjectTable->Get(id) : nil;
}


// ROM 0x0014b2a8 ObjectDestroy__FP13ObjectMessageUlT2
// Only the owner may destroy an object (unless no requester is given).
// The removal is TObjectTable::Remove, inlined in the ROM.
NewtonErr
ObjectDestroy(ObjectMessage* msg, ULong size, TObjectId requesterId)
{
	if (size != kObjectMessage_HeaderSize)
		return kError_Bad_Parameters;
	TKernelObject* object = gObjectTable->Get(msg->fObjectId);
	if (object == nil)
		return kError_Bad_ObjectId;
	if (requesterId != 0 && object->fOwnerId != requesterId)
		return kError_Object_Not_Owned_By_Task;
	return gObjectTable->Remove(msg->fObjectId);
}


// ROM 0x0014b318 ObjectStart__FP13ObjectMessageUl
NewtonErr
ObjectStart(ObjectMessage* msg, ULong size)
{
	if (size != kObjectMessage_HeaderSize || ObjectType(msg->fObjectId) != kTaskType)
		return kError_Bad_Parameters;
	TTask* task = (TTask*) gObjectTable->Get(msg->fObjectId);
	EnterAtomic();
	gKernelScheduler->Add(task);
	ExitAtomic();
	return noErr;
}


// ROM 0x0014b394 ObjectSuspend__FP13ObjectMessageUl
NewtonErr
ObjectSuspend(ObjectMessage* msg, ULong size)
{
	if (size != kObjectMessage_HeaderSize || ObjectType(msg->fObjectId) != kTaskType)
		return kError_Bad_Parameters;
	TTask* task = TaskFromId(msg->fObjectId);
	if (task != nil)
	{
		if ((task->fState & kTaskState_Unknown0008) == 0)
			return kError_Cannot_Suspend_Blocked_Task;
		EnterAtomic();
		UnScheduleTask(task);
		ExitAtomic();
	}
	return noErr;
}


// ROM 0x0014b41c ObjectGetRegister__FP13ObjectMessageUlPUl
NewtonErr
ObjectGetRegister(ObjectMessage* msg, ULong size, ULong* outValue)
{
	if (size != kObjectMessage_GetRegisterSize || ObjectType(msg->fObjectId) != kTaskType)
		return kError_Bad_Parameters;
	TTask* task = (TTask*) gObjectTable->Get(msg->fObjectId);
	if (msg->fRegister.fNumber > 15)
		return kError_Bad_Register_Number;
	EnterAtomic();
	*outValue = (ULong) task->fRegister[msg->fRegister.fNumber];
	ExitAtomic();
	return noErr;
}


// ROM 0x0014b4ac ObjectSetRegister__FP13ObjectMessageUl
NewtonErr
ObjectSetRegister(ObjectMessage* msg, ULong size)
{
	if (size != kObjectMessage_SetRegisterSize || ObjectType(msg->fObjectId) != kTaskType)
		return kError_Bad_Parameters;
	TTask* task = (TTask*) gObjectTable->Get(msg->fObjectId);
	if (msg->fRegister.fNumber > 15)
		return kError_Bad_Register_Number;
	EnterAtomic();
	task->fRegister[msg->fRegister.fNumber] = msg->fRegister.fValue;
	ExitAtomic();
	return noErr;
}


// ROM 0x0014a698 GetObjectContent__FP13ObjectMessageUlT1
// A task's accounting figures, read with scheduling held off.
NewtonErr
GetObjectContent(ObjectMessage* msg, ULong size, ObjectMessage* reply)
{
	if (msg->fContentRequest.fKind != 1 || size != kObjectMessage_GetContentSize)
		return kError_Bad_Parameters;
	NewtonErr err;
	HoldSchedule();
	TTask* task = TaskFromId(msg->fObjectId);
	if (task == nil)
		err = kError_Task_No_Longer_Exists;
	else
	{
		ObjectContentReply* content = (ObjectContentReply*) reply;
		content->fPriority = task->fPriority;
		content->fName = task->fName;
		content->fTaskTimeLo = task->fTaskTime.lo;
		content->fTaskTimeHi = task->fTaskTime.hi;
		content->fStackSize = task->fStackSize;
		content->fHandlesUsed = task->fHandlesUsed;
		content->fPtrsUsed = task->fPtrsUsed;
		content->fMaxMemoryUsed = task->fMaxMemoryUsed;
		err = noErr;
	}
	AllowSchedule();
	return err;
}


// ROM 0x0014a5e0 SetDomainFaultMonitor__FP13ObjectMessageUlT1
// TKDomain::SetFaultMonitor, inlined.
NewtonErr
SetDomainFaultMonitor(ObjectMessage* msg, ULong size, ObjectMessage* /*reply*/)
{
	if (size != kObjectMessage_SetFaultMonitorSize)
		return kError_Bad_Parameters;
	TObjectId domainId = msg->fDomain.fMonitorId;		// +0x0c: the domain
	TKDomain* domain = ObjectType(domainId) == kDomainType ? (TKDomain*) gObjectTable->Get(domainId) : nil;
	if (domain == nil)
		return kError_Bad_Parameters;
	TObjectId monitorId = (TObjectId) msg->fDomain.fBase;	// +0x10: the monitor
	if (ObjectType(monitorId) != kMonitorType || gObjectTable->Get(monitorId) == nil)
		return kError_Bad_ObjectId;
	domain->fFaultMonitorId = monitorId;
	RegisterFaultMonitor(domain->fNumber, domain->fId, monitorId);
	return noErr;
}


/* -------------------------------------------------------------------------------
	Scavenging
------------------------------------------------------------------------------- */

// ROM 0x0014a508 ObjectScavenger__FP13TKernelObjectUl
// Names the destructor for an object the table wants to remove, or nil to
// leave it for now: a task inside a monitor is marked kill-pending and dies
// when the monitor lets it go, a monitor with a call in progress is
// suspended and destroyed on a later pass.  Environments and domains are
// never scavenged.
ObjectDestructorProcPtr
ObjectScavenger(TKernelObject* object, ULong /*unused*/)
{
	switch (ObjectType(object->fId))
	{
	case kPortType:
		return (ObjectDestructorProcPtr) DeletePort;
	case kTaskType:
		{
			TTask* task = (TTask*) object;
			if (task->fInsideMonitorId != 0)
			{
				task->fState |= kTaskState_KillPending;
				return nil;
			}
			// NOT YET RECONSTRUCTED: DeleteTask 0x0014b5e8 (needs ~TTask)
			return nil;
		}
	case kSemListType:
		return (ObjectDestructorProcPtr) DeleteSemList;
	case kSemGroupType:
		return (ObjectDestructorProcPtr) DeleteSemGroup;
	case kSharedMemType:
		return (ObjectDestructorProcPtr) DeleteSharedMem;
	case kSharedMemMsgType:
		return (ObjectDestructorProcPtr) DeleteSharedMemMsg;
	case kMonitorType:
		if (((TMonitor*) object)->Suspend(kMonitor_Suspended))
			return (ObjectDestructorProcPtr) DeleteMonitor;
		return nil;
	case kPhysType:
		// NOT YET RECONSTRUCTED: DeletePhys 0x0014a4f8 (TPhys)
		return nil;
	default:
		return nil;
	}
}
