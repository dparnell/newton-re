/*
	File:		user/UserTasks.cpp

	Contains:	TUTask and TUTaskWorld (UserTasks.h) and the task-related free
				functions of UserTasks.h / UserObjects.h.

				TUTask::Init hands the kernel a proc, a stack size and an object
				to copy onto the new task's stack (through a temporary shared
				memory); the task starts at proc(theObject, objectSize, taskId).
				TUTaskWorld spawns a task that *is* the object: the child gets a
				copy of the TUTaskWorld on its own stack and runs
				TaskConstructor / TaskMain / TaskDestructor on it, reporting the
				constructor's result to the parent through fMotherPort when asked.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "UserTasks.h"
#include "UserSharedMem.h"
#include "UserMonitor.h"
#include "UserGlobals.h"
#include "os600/ObjectMessage.h"
#include "os600/GenericSWISelectors.h"
#include "OSErrors.h"

// the message type the parent and child of a TUTaskWorld use on the mother port
const ULong kTaskWorldStartMsgType = 0x00800000;


/* -------------------------------------------------------------------------------
	TUTask
------------------------------------------------------------------------------- */

// ROM 0x00259c9c Init__6TUTaskFPFPvUlT2_vUlT2PvN22
long
TUTask::Init(TaskProcPtr pc, ULong stackSize, ULong objectSize, void* theObject, ULong priority, ULong taskName)
{
	return Init(pc, stackSize, objectSize, theObject, priority, taskName, 0);
}


// ROM 0x00259cdc Init__6TUTaskFPFPvUlT2_vUlT2PvN32
// The object is exposed through a shared memory for the kernel to copy; the
// temporary handle goes once the task exists (environment 0: the caller's).
long
TUTask::Init(TaskProcPtr pc, ULong stackSize, ULong objectSize, void* theObject, ULong priority, ULong taskName, TObjectId environment)
{
	TUSharedMem object;
	long err = object.Init();
	if (err == noErr && (err = object.SetBuffer(theObject, objectSize, kSMemReadOnly)) == noErr)
	{
		ObjectMessage msg;
		msg.fTask.fProc = pc;
		msg.fTask.fStackSize = stackSize;
		msg.fTask.fDataId = object;
		msg.fTask.fPriority = priority;
		msg.fTask.fName = taskName;
		msg.fTask.fEnvironmentId = environment;
		err = MakeObject(kObjectTask, &msg, kObjectMessage_TaskSize);
	}
	return err;
}


// ROM 0x00259d8c Start__6TUTaskFv
long
TUTask::Start()
{
	ObjectMessage msg;
	msg.fSize = kObjectMessage_HeaderSize;
	msg.fObjectId = fId;
	return MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_Start, &msg);
}


// ROM 0x00259dc8 Suspend__6TUTaskFv
long
TUTask::Suspend()
{
	ObjectMessage msg;
	msg.fSize = kObjectMessage_HeaderSize;
	msg.fObjectId = fId;
	return MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_Suspend, &msg);
}


// ROM 0x00259e04 GetRegister__6TUTaskFUlPUl
long
TUTask::GetRegister(ULong reg, ULong* value)
{
	ObjectMessage msg;
	msg.fSize = kObjectMessage_GetRegisterSize;
	msg.fObjectId = fId;
	msg.fRegister.fNumber = reg;
	long err = MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_GetRegister, &msg);
	*value = msg.fValue;
	return err;
}


// ROM 0x00259e50 SetRegister__6TUTaskFUlT1
long
TUTask::SetRegister(ULong reg, ULong value)
{
	ObjectMessage msg;
	msg.fSize = kObjectMessage_SetRegisterSize;
	msg.fObjectId = fId;
	msg.fRegister.fNumber = reg;
	msg.fRegister.fValue = value;
	return MonitorDispatchSWI(*gUObjectMgrMonitor, kObjectMgr_SetRegister, &msg);
}


/* -------------------------------------------------------------------------------
	TUTaskWorld
------------------------------------------------------------------------------- */

// ROM 0x00259940 __ct__11TUTaskWorldFv
TUTaskWorld::TUTaskWorld()
{
	fIsSpawned = false;
	fIsOwnedByParent = false;
	fWantResult = false;
}


// ROM 0x00259994 __dt__11TUTaskWorldFv
TUTaskWorld::~TUTaskWorld()
{
}


// ROM 0x00259b14 StartTask__11TUTaskWorldFUcT1UlN33
long
TUTaskWorld::StartTask(Boolean wantResultFromChild, Boolean wantOwnerShip, TTimeout startTimeout, ULong stackSize, ULong priority, ULong taskName)
{
	return StartTask(wantResultFromChild, wantOwnerShip, startTimeout, stackSize, priority, taskName, 0);
}


// ROM 0x002599e0 StartTask__11TUTaskWorldFUcT1UlN43
// Spawn: make a task running TaskEntry on a copy of this object, start it,
// take ownership of it if wanted (the kernel made it owned by nobody and
// assigned to us; the destroy the CopyObject(0) attempts is refused, then
// TaskAcceptObject makes it ours), and if a result is wanted RPC the mother
// port until the child answers from TaskEntry.
long
TUTaskWorld::StartTask(Boolean wantResultFromChild, Boolean wantOwnerShip, TTimeout startTimeout, ULong stackSize, ULong priority, ULong taskName, TObjectId environment)
{
	long err;
	fWantResult = wantResultFromChild;
	fIsOwnedByParent = wantOwnerShip;
	if (fWantResult && (err = fMotherPort.Init()) != noErr)
		return err;
	err = fChildTask.Init(TaskEntryProc, stackSize, GetSizeOf(), this, priority, taskName, environment);
	if (err != noErr)
		return err;
	if ((err = fChildTask.Start()) != noErr)
		return err;
	if (fIsOwnedByParent)
	{
		TObjectId id = fChildTask;
		fChildTask.CopyObject(0);
		fChildTask.CopyObject(id);
		if ((err = TaskAcceptObject(id)) != noErr)
			return err;
	}
	if (fWantResult)
		err = fMotherPort.SendRPCGoo(kBuiltInSMemMsgId, kBuiltInSMemId, nil, nil, 0, kTaskWorldStartMsgType, 0, false, nil, 0, startTimeout, nil);
	return err;
}


// ROM 0x00259b5c TaskEntry__11TUTaskWorldFUlT1
// The child's side, running on the copy: wait for the parent's RPC if it
// wants a result, construct, answer, run, destruct, and unmake the copy in
// place.
void
TUTaskWorld::TaskEntry(ULong /*size*/, TObjectId taskId)
{
	TUMsgToken token;
	fChildTask.CopyObject(taskId);
	fIsSpawned = true;
	if (fWantResult)
	{
		if (fMotherPort.Receive(nil, nil, 0, &token, nil, kNoTimeout, kTaskWorldStartMsgType, false, false) != noErr)
		{
			TaskDestructor();
			this->~TUTaskWorld();
			return;
		}
	}
	long err = TaskConstructor();
	if (!fWantResult || token.ReplyRPC(nil, 0, err) == noErr)
	{
		if (err == noErr)
			TaskMain();
	}
	TaskDestructor();
	this->~TUTaskWorld();
}


// The TaskProcPtr the child task starts at: the copy of the object, its size
// and the task's id, exactly TaskEntry's arguments.  (In the ROM the member
// function's address is passed directly, which a modern compiler forbids.)
void
TUTaskWorld::TaskEntryProc(void* theObject, ULong size, TObjectId taskId)
{
	((TUTaskWorld*) theObject)->TaskEntry(size, taskId);
}


// ROM 0x00259c50 TaskConstructor__11TUTaskWorldFv
long
TUTaskWorld::TaskConstructor()
{
	return noErr;
}


// ROM 0x00259c58 TaskDestructor__11TUTaskWorldFv
void
TUTaskWorld::TaskDestructor()
{
}


/* -------------------------------------------------------------------------------
	Free functions
------------------------------------------------------------------------------- */

// ROM 0x002577b8 TaskGiveObject__FUlT1
long
TaskGiveObject(TObjectId id, TObjectId assignToTaskId)
{
	return GenericSWI(kGeneric_GiveObject, id, assignToTaskId);
}


// ROM 0x002577c8 TaskAcceptObject__FUl
long
TaskAcceptObject(TObjectId id)
{
	return GenericSWI(kGeneric_AcceptObject, id);
}


// ROM 0x00259c5c Yield__FUl
long
Yield(TObjectId taskId)
{
	return GenericSWI(kGeneric_YieldToTask, taskId);
}


// ROM 0x000da6b8 SetBequeathId__FUl
long
SetBequeathId(TObjectId to)
{
	return GenericSWI(kGeneric_SetBequeathId, to);
}


// ROM 0x00259e94 Sleep__FUl
// A send to the null port (which never receives) that times out.
void
Sleep(TTimeout timeout)
{
	if (timeout == kNoTimeout)
		return;
	if (timeout == (TTimeout) kTimeOutImmediate)
		timeout = 0;
	gUNullPort->SendGoo(kBuiltInSMemMsgId, 0, nil, 0, 0, 0, false, timeout, nil);
}


// ROM 0x002598e8 SleepTill__FP5TTime
void
SleepTill(TTime* futureTime)
{
	gUNullPort->SendGoo(kBuiltInSMemMsgId, 0, nil, 0, 0, 0, false, kTimeOutImmediate, futureTime);
}
