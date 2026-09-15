// Host unit test for the kernel semaphores (src/os600/kernel/Semaphore.*).

#include "Semaphore.h"
#include "Scheduler.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "OSErrors.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TTask* MakeTask(ULong priority)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	return t;
}

static TSemaphoreOpList* MakeList(TObjectTable& table, int count, ULong a, ULong b = 0)
{
	ULong ops[2] = {a, b};
	TSemaphoreOpList* list = new TSemaphoreOpList;
	list->Init(count, ops);
	table.Add(list, kSemListType, 1);
	return list;
}

int main()
{
	TObjectTable table;
	table.Init();
	gObjectTable = &table;
	TScheduler scheduler;
	gKernelScheduler = &scheduler;
	TTask* idle = MakeTask(0);
	gIdleTask = idle;
	gCurrentTask = idle;
	gHoldScheduleLevel = 0;

	TSemaphoreGroup* group = new TSemaphoreGroup;
	EXPECT(group->Init(2) == noErr && group->fCount == 2 && group->fRefCon == nil);
	TObjectId groupId = table.Add(group, kSemGroupType, 1);

	// op lists: acquire semaphore 0 (-1), release it (+1), wait for it to be zero (0)
	TSemaphoreOpList* acquire = MakeList(table, 1, MAKESEMLISTITEM(0, -1));
	TSemaphoreOpList* release = MakeList(table, 1, MAKESEMLISTITEM(0, 1));
	TSemaphoreOpList* waitZero = MakeList(table, 1, MAKESEMLISTITEM(0, 0));
	TSemaphoreOpList* both = MakeList(table, 2, MAKESEMLISTITEM(0, 1), MAKESEMLISTITEM(1, -1));

	TTask* t1 = MakeTask(5);
	TTask* t2 = MakeTask(5);
	scheduler.Add(t1);
	scheduler.Add(t2);

	// release then acquire: no blocking
	EXPECT(DoSemaphoreOp(groupId, release->fId, kWaitOnBlock, t1) == noErr);
	EXPECT(group->fSemaphores[0].fValue == 1);
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kWaitOnBlock, t1) == noErr);
	EXPECT(group->fSemaphores[0].fValue == 0);

	// acquiring at zero blocks: the task leaves the scheduler and waits for an increment
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kWaitOnBlock, t1) == kError_Semaphore_Would_Cause_Block);
	EXPECT(group->fSemaphores[0].fValue == 0);							// unwound
	EXPECT((t1->fState & kTaskState_BlockedOnSemaphore) && !(t1->fState & kTaskState_Scheduled));
	EXPECT(t1->fContainer == &group->fSemaphores[0]);
	EXPECT(group->fSemaphores[0].fIncTasks.Peek() == t1);

	// with kNoWaitOnBlock the caller just gets the error
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kNoWaitOnBlock, t2) == kError_Semaphore_Would_Cause_Block);
	EXPECT((t2->fState & kTaskState_Scheduled) && !(t2->fState & kTaskState_BlockedOnSemaphore));

	// a release wakes the waiter and asks for a reschedule
	gSchedule = false;
	EXPECT(DoSemaphoreOp(groupId, release->fId, kWaitOnBlock, t2) == noErr);
	EXPECT(group->fSemaphores[0].fValue == 1);
	EXPECT((t1->fState & kTaskState_Scheduled) && !(t1->fState & kTaskState_BlockedOnSemaphore));
	EXPECT(gSchedule);

	// waiting for zero blocks while the value is 1; the next acquire releases it
	EXPECT(DoSemaphoreOp(groupId, waitZero->fId, kWaitOnBlock, t2) == kError_Semaphore_Would_Cause_Block);
	EXPECT(group->fSemaphores[0].fZeroTasks.Peek() == t2);
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kWaitOnBlock, t1) == noErr);
	EXPECT(group->fSemaphores[0].fValue == 0 && (t2->fState & kTaskState_Scheduled));

	// a multi-op list is all or nothing: op 0 (+1) is applied then unwound when op 1 (-1 on an empty semaphore) blocks
	EXPECT(DoSemaphoreOp(groupId, both->fId, kNoWaitOnBlock, t1) == kError_Semaphore_Would_Cause_Block);
	EXPECT(group->fSemaphores[0].fValue == 0 && group->fSemaphores[1].fValue == 0);

	// TTaskContainer::Remove takes a waiter out (e.g. when its task dies)
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kWaitOnBlock, t1) == kError_Semaphore_Would_Cause_Block);
	t1->fContainer->Remove(t1);
	EXPECT(t1->fState == 0 && group->fSemaphores[0].fIncTasks.Peek() == nil);

	// bad ids
	EXPECT(DoSemaphoreOp(acquire->fId, acquire->fId, kWaitOnBlock, t1) == kError_Bad_Semaphore_GroupId);
	EXPECT(DoSemaphoreOp(groupId, groupId, kWaitOnBlock, t1) == kError_Bad_Semaphore_Op_ListId);

	// ref con
	int refCon;
	void* got = nil;
	EXPECT(SemGroupSetRefCon(groupId, &refCon) == noErr);
	EXPECT(SemGroupGetRefCon(groupId, &got) == noErr && got == &refCon);
	EXPECT(SemGroupSetRefCon(acquire->fId, &refCon) == kError_Bad_Semaphore_GroupId);

	// destroying a group releases its waiters with an error in their r0 and a skipped instruction
	EXPECT(DoSemaphoreOp(groupId, acquire->fId, kWaitOnBlock, t2) == kError_Semaphore_Would_Cause_Block);
	t2->fRegister[15] = 0x1000;
	table.Remove(groupId);
	DeleteSemGroup(group);
	EXPECT((t2->fState & kTaskState_Scheduled) && t2->fRegister[0] == (ULong) kError_Semaphore_Group_No_Longer_Exists);
	EXPECT(t2->fRegister[15] == 0x1004);

	DeleteSemList(acquire);
	DeleteSemList(release);
	DeleteSemList(waitZero);
	DeleteSemList(both);
	if (failures == 0)
		printf("test_Semaphore: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
