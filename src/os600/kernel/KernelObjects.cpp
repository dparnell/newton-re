/*
	File:		KernelObjects.cpp

	Contains:	Registering kernel objects and passing ownership between
				tasks.  GiveObject/AcceptObject implement GenericSWI selectors
				1 and 2 (TaskGiveObject / TaskAcceptObject in UserObjects.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	ObjectScavenger (0x001489ac), the scavenge proc installed in
	gObjectTable that picks the per-type destructor, will follow once the
	destructors it dispatches to (DeletePort, DeleteTask, ...) exist.
*/

#include "KernelObjects.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "Task.h"
#include "Monitor.h"
#include "SharedMem.h"
#include "OSErrors.h"


static inline TObjectId
CurrentTaskId()
{
	return gCurrentTask->fId;
}


// ROM 0x00191e80 LocalToGlobalId__FUl
TObjectId
LocalToGlobalId(TObjectId id)
{
	switch (id)
	{
	case kBuiltInSMemMsgId:
		if (gCurrentTask != nil)
			id = gCurrentTask->fSharedMemMsgId;
		break;
	case kBuiltInSMemId:
		if (gCurrentTask != nil)
			id = gCurrentTask->fSharedMemId;
		break;
	case kBuiltInSMemMonitorFaultId:
		{
			TObjectId monitorId = gCurrentTask->fMonitorId;
			if (ObjectType(monitorId) == kMonitorType)
			{
				TMonitor* monitor = (TMonitor*) gObjectTable->Get(monitorId);
				if (monitor != nil)
					id = monitor->fMsgId;
			}
		}
		break;
	}
	return id;
}


// ROM 0x00191f14 ConvertIdToObj__F11KernelTypesUlPv
NewtonErr
ConvertIdToObj(KernelTypes type, TObjectId id, void* outObject)
{
	id = LocalToGlobalId(id);
	TKernelObject* object = (ObjectType(id) == type) ? gObjectTable->Get(id) : nil;
	if (outObject != nil)
		*(TKernelObject**) outObject = object;
	return (object == nil) ? kError_Bad_ObjectId : noErr;
}


// ROM 0x001e0754 ConvertMemOrMsgIdToObj__FUlPP10TSharedMem
NewtonErr
ConvertMemOrMsgIdToObj(TObjectId id, TSharedMem** outObject)
{
	TSharedMem* object = (TSharedMem*) gObjectTable->Get(LocalToGlobalId(id));
	*outObject = object;
	if (object == nil || (ObjectType(object->fId) != kSharedMemType && ObjectType(object->fId) != kSharedMemMsgType))
		return kError_Bad_ObjectId;
	return noErr;
}


// ROM 0x001488cc RegisterObject__FP13TKernelObject11KernelTypesUlPUl
NewtonErr
RegisterObject(TKernelObject* object, KernelTypes type, TObjectId owner, TObjectId* outId)
{
	TObjectId id;
	if (outId == nil)
		outId = &id;
	*outId = 0;
	if (object == nil)
		return kError_Could_Not_Create_Object;
	*outId = gObjectTable->Add(object, type, owner);
	return noErr;
}


// ROM 0x00149610 GiveObject__FUlT1
// Offers an object the current task owns to another task, which must then
// AcceptObject it.  As in the ROM, the offer is refused when the receiving
// task exists and is itself alive (owner == itself, or its owner exists);
// giving to a not-yet-registered or orphaned task goes through.
NewtonErr
GiveObject(TObjectId id, TObjectId assignToTaskId)
{
	TKernelObject* object = gObjectTable->Get(id);
	if (object == nil)
		return kError_Bad_ObjectId;

	TKernelObject* task = gObjectTable->Get(assignToTaskId);
	if (task != nil && (task->fOwnerId == task->fId || gObjectTable->Exists(task->fOwnerId)))
		return kError_Bad_ObjectId;

	if (object->fOwnerId != CurrentTaskId())
		return kError_Object_Not_Owned_By_Task;
	object->fAssignedOwnerId = assignToTaskId;
	return noErr;
}


// ROM 0x001496c4 AcceptObject__FUl
// The task an object was given to takes ownership of it.
NewtonErr
AcceptObject(TObjectId id)
{
	TKernelObject* object = gObjectTable->Get(id);
	if (object == nil)
		return kError_Bad_ObjectId;
	if (object->fAssignedOwnerId != CurrentTaskId())
		return kError_Object_Not_Assigned_To_Task;
	object->fOwnerId = CurrentTaskId();
	return noErr;
}
