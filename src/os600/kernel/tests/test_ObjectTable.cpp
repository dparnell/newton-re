// Host unit test for TObjectTable / TObjectTableIterator and the object
// ownership calls (src/os600/kernel/ObjectTable.*, KernelObjects.*).

#include "ObjectTable.h"
#include "KernelObjects.h"
#include "KernelGlobals.h"
#include "OSErrors.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct Object : public TKernelObject
{
	int payload;
};

static int destroyedCount = 0;
static void CountingDestructor(TKernelObject*)						{ destroyedCount++; }
static ObjectDestructorProcPtr AlwaysScavenge(TKernelObject*, ULong)	{ return CountingDestructor; }
static ObjectDestructorProcPtr NeverScavenge(TKernelObject*, ULong)	{ return nil; }

int main()
{
	TObjectTable table;
	table.Init();
	TObjectTable memArch;
	memArch.Init();
	gObjectTable = &table;
	gTheMemArchObjTbl = &memArch;
	gNextGlobalUniqueId = 0;
	gWrappedGlobalUniqueId = false;

	// ids carry their type in the low bits and are unique
	Object task, port, sem;
	TObjectId taskId = table.Add(&task, kTaskType, 1);			// owns itself
	TObjectId portId = table.Add(&port, kPortType, taskId);
	TObjectId semId = table.Add(&sem, kSemListType, taskId);
	EXPECT(ObjectType(taskId) == kTaskType && ObjectType(portId) == kPortType && ObjectType(semId) == kSemListType);
	EXPECT(taskId != portId && portId != semId);
	EXPECT(task.fOwnerId == taskId && task.fAssignedOwnerId == taskId);
	EXPECT(port.fOwnerId == taskId);

	// lookup
	EXPECT(table.Get(portId) == &port);
	EXPECT(table.Get(0) == nil);
	EXPECT(table.Get(portId + 0x10000) == nil);
	EXPECT(table.Exists(semId));
	EXPECT(!table.Exists(semId ^ 1));

	// iteration in id order from a start id, wrapping round
	TObjectTableIterator it(&table, taskId);
	TObjectId ids[4] = {0, 0, 0, 0};
	int n = 0;
	for (TObjectId id = it.GetNextTableId(); id != 0 && n < 4; id = it.GetNextTableId())
		ids[n++] = id;
	EXPECT(n == 2 && ids[0] == portId && ids[1] == semId);
	TObjectTableIterator it2(&table, 0);
	EXPECT(it2.GetNextTypedObject(kPortType) == &port);
	EXPECT(it2.GetNextTypedId(kPortType) == 0);

	// ownership transfer: giving to a task that exists and is alive is refused (as in the ROM);
	// giving to a not-yet-registered task id works, then that task accepts
	gCurrentTask = (TTask*) &task;
	TObjectId newTaskId = (0x77 << kTypeBits) | kTaskType;		// not registered
	EXPECT(GiveObject(portId, taskId) == kError_Bad_ObjectId);
	EXPECT(GiveObject(portId, newTaskId) == noErr);
	EXPECT(port.fAssignedOwnerId == newTaskId && port.fOwnerId == taskId);
	EXPECT(GiveObject(portId + 0x10000, newTaskId) == kError_Bad_ObjectId);
	EXPECT(AcceptObject(portId) == kError_Object_Not_Assigned_To_Task);	// current task is not newTaskId
	Object newTask;
	newTask.fId = newTaskId;
	gCurrentTask = (TTask*) &newTask;
	EXPECT(AcceptObject(portId) == noErr);
	EXPECT(port.fOwnerId == newTaskId);
	gCurrentTask = (TTask*) &task;
	EXPECT(GiveObject(portId, newTaskId) == kError_Object_Not_Owned_By_Task);

	// RegisterObject
	Object mon;
	TObjectId monId = 0;
	EXPECT(RegisterObject(&mon, kMonitorType, taskId, &monId) == noErr && ObjectType(monId) == kMonitorType);
	EXPECT(RegisterObject(nil, kMonitorType, taskId, nil) == kError_Could_Not_Create_Object);

	// Remove honours the scavenge proc's verdict
	table.SetScavengeProc(NeverScavenge);
	EXPECT(table.Remove(monId) == noErr);
	EXPECT(table.Get(monId) == &mon);						// kept
	table.SetScavengeProc(AlwaysScavenge);
	EXPECT(table.Remove(monId) == noErr);
	EXPECT(table.Get(monId) == nil && destroyedCount == 1);
	EXPECT(table.Remove(monId) == kError_Item_Not_Found);
	EXPECT(table.Remove(0) == kError_Item_Not_Found);

	// Scavenging removes objects whose owner is gone: port's owner newTaskId was never registered
	table.ScavengeAll();
	EXPECT(table.Get(portId) == nil && destroyedCount == 2);
	EXPECT(table.Get(semId) == &sem && table.Get(taskId) == &task);	// owned by a live task / itself

	// ReassignOwnership
	Object other;
	TObjectId otherId = table.Add(&other, kTaskType, 1);
	table.ReassignOwnership(taskId, otherId);
	EXPECT(sem.fOwnerId == otherId);
	table.ReassignOwnership(otherId, 0x12345);					// unknown target: nothing happens
	EXPECT(sem.fOwnerId == otherId);

	// id wrap: after 256 numbers NewId avoids ids still in use
	gNextGlobalUniqueId = 0xff;
	Object a, b;
	TObjectId aId = table.Add(&a, kPortType, 1);
	EXPECT(gWrappedGlobalUniqueId);
	gNextGlobalUniqueId = (aId >> kTypeBits) - 1;				// would hand out aId again
	TObjectId bId = table.Add(&b, kPortType, 1);
	EXPECT(bId != aId && table.Get(bId) == &b);

	if (failures == 0)
		printf("test_ObjectTable: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
