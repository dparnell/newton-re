/*
	File:		ObjectManager.cpp

	Contains:	The object manager monitor's proc, its request handlers and the
				object table's scavenge proc.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
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
#include "KernelObjects.h"
#include "MonitorGlue.h"


TObjectManager*	gTheObjectManager = nil;


// ROM 0x001491fc __ct__14TObjectManagerFv
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


// ROM 0x0014922c MonitorProc__14TObjectManagerFlP13ObjectMessage
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
		{
			TObjectId id;
			err = ObjectAlloc(msg, size, callerId, &id);
			if (err == noErr)
				msg->fSize = id;
		}
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
				env->Add(domain, msg->fEnvDomain.fIsManager, msg->fEnvDomain.fIsStack, msg->fEnvDomain.fIsHeap);
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


// The environment a request names, or the requester's own when it names none.
static NewtonErr
EnvironmentForRequest(TObjectId envId, TObjectId requesterId, TEnvironment** outEnv)
{
	if (envId == 0)
	{
		TTask* requester = TaskFromId(requesterId);
		if (requester == nil)
			return kError_Bad_ObjectId;
		*outEnv = requester->fEnvironment;
		return noErr;
	}
	*outEnv = ObjectType(envId) == kEnvironmentType ? (TEnvironment*) gObjectTable->Get(envId) : nil;
	return *outEnv != nil ? noErr : kError_Bad_ObjectId;
}


// ROM 0x00148c0c ObjectAlloc__FP13ObjectMessageUlT2PUl
// Makes the object the message asks for and enters it in the object table
// owned by the requester - except a task, which is owned by nobody (1) and
// only assigned to the requester (TaskAcceptObject makes it theirs), and a
// domain, entered before Init so the domain has an id to register its fault
// monitor under.  A task's registers point it at its proc with itself as
// the task-id argument; its lr is BadExit.
NewtonErr
ObjectAlloc(ObjectMessage* msg, ULong size, TObjectId requesterId, TObjectId* outId)
{
	if (size < kObjectMessage_HeaderSize)
		return kError_Bad_Parameters;
	TKernelObject* object = nil;
	KernelTypes type = kNoType;
	TEnvironment* env;
	NewtonErr err;
	switch (msg->fType)
	{
	case kObjectPort:
		if (size != kObjectMessage_HeaderSize)
			return kError_Bad_Parameters;
		object = new TPort;
		type = kPortType;
		break;

	case kObjectTask:
		{
			if (size != kObjectMessage_TaskSize)
				return kError_Bad_Parameters;
			if ((err = EnvironmentForRequest(msg->fTask.fEnvironmentId, requesterId, &env)) != noErr)
				return err;
			TTask* task = new TTask;
			if (task == nil)
				return kError_Could_Not_Create_Object;
			TObjectId id = gObjectTable->Add(task, kTaskType, 1);
			err = task->Init(msg->fTask.fProc, msg->fTask.fStackSize, (void*) (uintptr_t) id, msg->fTask.fDataId, msg->fTask.fPriority, msg->fTask.fName, env);
			if (err != noErr)
			{
				gObjectTable->Remove(id);
				return kError_Could_Not_Create_Object;
			}
			task->fAssignedOwnerId = requesterId;
			task->fRegister[14] = (TRegister) BadExit;
			*outId = id;
			return noErr;
		}

	case kObjectEnvironment:
		{
			if (size != kObjectMessage_EnvironmentSize)
				return kError_Bad_Parameters;
			TEnvironment* newEnv = new TEnvironment;
			if (newEnv != nil && newEnv->Init(msg->fEnvironment.fHeap) != noErr)
			{
				delete newEnv;
				return kError_Could_Not_Create_Object;
			}
			object = newEnv;
			type = kEnvironmentType;
		}
		break;

	case kObjectDomain:
		{
			if (size != kObjectMessage_DomainSize)
				return kError_Bad_Parameters;
			TObjectId monitorId = msg->fDomain.fMonitorId;
			if (monitorId != 0)
			{
				TMonitor* monitor = ObjectType(monitorId) == kMonitorType ? (TMonitor*) gObjectTable->Get(monitorId) : nil;
				if (monitor == nil)
					return kError_Bad_ObjectId;
				if (!monitor->fFaultMonitor)
					return kError_Not_A_Fault_Monitor;
			}
			TKDomain* domain = new TKDomain;
			if (domain == nil)
				return kError_Could_Not_Create_Object;
			TObjectId id = gObjectTable->Add(domain, kDomainType, requesterId);
			err = domain->Init(monitorId, msg->fDomain.fBase, msg->fDomain.fSize);
			if (err != noErr)
			{
				gObjectTable->Remove(id);
				delete domain;
				return err;
			}
			*outId = id;
			return noErr;
		}

	case kObjectSemList:
		{
			if (size < kObjectMessage_SemListSize || msg->fSemList.fCount * sizeof(ULong) + kObjectMessage_SemListSize != size)
				return kError_Bad_Parameters;
			TSemaphoreOpList* list = new TSemaphoreOpList;
			if (list != nil && list->Init(msg->fSemList.fCount, msg->fSemList.fOps) != noErr)
			{
				delete list;
				return kError_Could_Not_Create_Object;
			}
			object = list;
			type = kSemListType;
		}
		break;

	case kObjectSemGroup:
		{
			if (size != kObjectMessage_SemGroupSize)
				return kError_Bad_Parameters;
			TSemaphoreGroup* group = new TSemaphoreGroup;
			if (group != nil && group->Init(msg->fSemGroup.fCount) != noErr)
			{
				delete group;
				return kError_Could_Not_Create_Object;
			}
			object = group;
			type = kSemGroupType;
		}
		break;

	case kObjectSharedMem:
		{
			if (size != kObjectMessage_HeaderSize)
				return kError_Bad_Parameters;
			if ((err = EnvironmentForRequest(0, requesterId, &env)) != noErr)
				return err;
			TSharedMem* mem = new TSharedMem;
			if (mem != nil && mem->Init(env) != noErr)
			{
				delete mem;
				return kError_Could_Not_Create_Object;
			}
			object = mem;
			type = kSharedMemType;
		}
		break;

	case kObjectSharedMemMsg:
		{
			if (size != kObjectMessage_HeaderSize)
				return kError_Bad_Parameters;
			if ((err = EnvironmentForRequest(0, requesterId, &env)) != noErr)
				return err;
			TSharedMemMsg* smsg = new TSharedMemMsg;
			if (smsg != nil && smsg->Init(env) != noErr)
			{
				delete smsg;
				return kError_Could_Not_Create_Object;
			}
			object = smsg;
			type = kSharedMemMsgType;
		}
		break;

	case kObjectMonitor:
		{
			if (size != kObjectMessage_MonitorSize)
				return kError_Bad_Parameters;
			TMonitor* monitor = new TMonitor;
			if (monitor == nil)
				return kError_Could_Not_Create_Object;
			if ((err = EnvironmentForRequest(msg->fMonitor.fEnvironmentId, requesterId, &env)) != noErr)
				return err;
			TObjectId id = gObjectTable->Add(monitor, kMonitorType, requesterId);
			err = monitor->Init(msg->fMonitor.fProc, msg->fMonitor.fStackSize, msg->fMonitor.fMonitorObject, env, msg->fMonitor.fFaultMonitor, msg->fMonitor.fName, msg->fMonitor.fRebootProtected);
			if (err != noErr)
			{
				gObjectTable->Remove(id);
				return kError_Could_Not_Create_Object;
			}
			*outId = id;
			return noErr;
		}

	case kObjectPhys:
		// NOT YET RECONSTRUCTED: TPhys / TLittlePhys (0x0014a768 case 9)
		return kError_Call_Not_Implemented;

	default:
		return kError_Bad_Parameters;
	}

	*outId = 0;
	if (object == nil)
		return kError_Could_Not_Create_Object;
	*outId = gObjectTable->Add(object, type, requesterId);
	return noErr;
}


// ROM 0x0014974c ObjectDestroy__FP13ObjectMessageUlT2
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


// ROM 0x001497bc ObjectStart__FP13ObjectMessageUl
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


// ROM 0x00149838 ObjectSuspend__FP13ObjectMessageUl
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


// ROM 0x001498c0 ObjectGetRegister__FP13ObjectMessageUlPUl
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


// ROM 0x00149950 ObjectSetRegister__FP13ObjectMessageUl
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


// ROM 0x00148b3c GetObjectContent__FP13ObjectMessageUlT1
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


// ROM 0x00148a84 SetDomainFaultMonitor__FP13ObjectMessageUlT1
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

// ROM 0x001489ac ObjectScavenger__FP13TKernelObjectUl
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
			return (ObjectDestructorProcPtr) DeleteTask;
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


// ROM 0x00149a8c DeleteTask__FP5TTask
// The task's bequeath chain is spliced (whoever bequeathed to it now
// bequeaths to its heir), it is forgotten as the scheduler's preferred task,
// destroyed, its objects handed to the heir, and every copy in progress whose
// shared memory no longer has a live owner is failed.
void
DeleteTask(TTask* task)
{
	TObjectId heirId = task->fBequeathId;
	TObjectId id = task->fId;
	if (task->fInheritedId != 0)
	{
		TTask* benefactor;
		if (ConvertIdToObj(kTaskType, task->fInheritedId, &benefactor) == noErr && benefactor->fBequeathId == id)
			benefactor->SetBequeathId(heirId);
	}
	if (gKernelScheduler->fPreferredTask == task)
		gKernelScheduler->fPreferredTask = nil;
	delete task;
	gObjectTable->ReassignOwnership(id, heirId);
	gTaskDestroyed = true;

	TTask* copier = (TTask*) gCopyTasks->Peek();
	while (copier != nil)
	{
		TTask* next = (TTask*) gCopyTasks->GetNext(copier);
		TKernelObject* mem = gObjectTable->Get(copier->fCopyMemId);
		Boolean alive = mem != nil && (mem->fOwnerId == mem->fId || gObjectTable->Exists(mem->fOwnerId));
		if (!alive)
			LowLevelCopyDoneFromKernelGlue(kError_Bad_ObjectId, copier, 0);
		copier = next;
	}
}


// ROM 0x00149544 InitObjectManager__Fv
// The ROM looks the kernel environment ('krnl') up in the memory object
// manager; the caller passes it here.  The monitor is owned by nobody (1).
void
InitObjectManager(TEnvironment* environment)
{
	gTheObjectManager = new TObjectManager;
	gTheObjectManagerMonitor = new TMonitor;
	TObjectId id;
	RegisterObject(gTheObjectManagerMonitor, kMonitorType, 1, &id);
	gTheObjectManagerMonitor->Init(TObjectManager::MonitorProcGlue, 0x800, gTheObjectManager, environment, false, 'OBJM', false);
	gObjectTable->SetScavengeProc(ObjectScavenger);
}
