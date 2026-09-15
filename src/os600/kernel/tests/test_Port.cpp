// Host unit test for ports and shared-memory messages (src/os600/kernel/Port.*).
// Plays the kernel's part: the "current task" is switched by hand around each
// system call, and completions are checked in the tasks' saved registers.

#include "Port.h"
#include "Task.h"
#include "Scheduler.h"
#include "TimerEngine.h"
#include "ObjectTable.h"
#include "KernelGlobals.h"
#include "OSErrors.h"
#include "hal/Timer.h"
#include "hal/host/Host.h"
#include "CompMath.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TScheduler scheduler;
static TTimerEngine timerEngine;
static TDoubleQContainer timerDeferred(offsetof(TSharedMemMsg, fTimerQItem));
static TDoubleQContainer copyTasks(offsetof(TTask, fCopyQItem));
static TDoubleQContainer deferredSends(offsetof(TSharedMemMsg, fTimerQItem));

static TTask* MakeTask(ULong priority)
{
	TTask* t = new TTask;
	t->fPriority = priority;
	table.Add(t, kTaskType, 1);
	return t;
}

// The running task is never in a scheduler bucket (Schedule() dequeues it),
// and a blocking call leaves gCurrentTask nil for the scheduler to fill.
static void Run(TTask* t)
{
	if (t->fState & kTaskState_Scheduled)
		scheduler.Remove(t);
	gCurrentTask = t;
}

static Boolean IsScheduled(TTask* t)	{ return (t->fState & kTaskState_Scheduled) != 0; }
static Boolean IsBlocked(TTask* t)		{ return !IsScheduled(t) && gCurrentTask != t; }
static Boolean IsRunnable(TTask* t)	{ return IsScheduled(t) || gCurrentTask == t; }

static TSharedMemMsg* MakeMsg(TTask* owner)
{
	TSharedMemMsg* m = new TSharedMemMsg;
	m->Init(nil);
	table.Add(m, kSharedMemMsgType, owner->fId);
	return m;
}

// the alarm interrupt, then the deferred completion the scheduler path would run
static void FireTimers()
{
	Int64 now;
	GetClock(&now);
	if (gHostAlarmArmed && CompCompare(&now, &gHostAlarmTime) >= 0)
	{
		gHostAlarmArmed = false;
		TimerInterruptHandler();
	}
	if (gWantDeferred)
	{
		gWantDeferred = false;
		DeferredNotify();
	}
}

int main()
{
	table.Init();
	gObjectTable = &table;
	gKernelScheduler = &scheduler;
	gTimerEngine = &timerEngine;
	gTimerDeferred = &timerDeferred;
	gCopyTasks = &copyTasks;
	gDeferredSends = &deferredSends;
	Int64 t0 = {0, 5000};
	HostSetClock(&t0);

	TTask* idle = MakeTask(0);
	gIdleTask = idle;
	Run(idle);
	TTask* server = MakeTask(10);
	TTask* client = MakeTask(5);
	TPort* port = new TPort;
	TObjectId portId = table.Add(port, kPortType, server->fId);
	TSharedMemMsg* rcv = MakeMsg(server);
	TSharedMemMsg* snd = MakeMsg(client);

	// --- receive first, then a synchronous send: rendezvous ------------------
	Run(server);
	PortReceiveKernelGlue(portId, rcv->fId, kMsgType_MatchAll, 0);
	EXPECT(server->fRegister[0] == noErr);
	EXPECT(IsBlocked(server) && rcv->fStatus == kSMemMsgStatus_InProgress);
	EXPECT(port->fReceivers.Peek() == rcv && rcv->fNotifyId == server->fId);

	Run(client);
	PortSendKernelGlue(portId, snd->fId, 0, 0x100, 0);
	EXPECT(client->fRegister[0] == noErr);
	EXPECT(IsBlocked(client));									// blocked until the reply
	EXPECT(IsScheduled(server) && scheduler.fPreferredTask == server);	// woken, runs next
	EXPECT(server->fRegister[0] == noErr && server->fRegister[1] == snd->fId && server->fRegister[3] == 0x100);
	ULong sequence = server->fRegister[4];
	EXPECT(sequence == 1 && rcv->fStatus == noErr && rcv->fSenders.Peek() == snd);
	EXPECT(snd->fMsgFlags == 0x100 && snd->fSenderTaskId == client->fId && snd->fCurrentSequence == 1);

	// the server replies on the sender's message (the token it received): the client gets the result
	Run(server);
	EXPECT(SMemMsgMsgDoneKernelGlue(snd->fId, 42, sequence + 1) == kError_Bad_Signature);
	EXPECT(SMemMsgMsgDoneKernelGlue(snd->fId, 42, sequence) == noErr);
	EXPECT(IsScheduled(client) && client->fRegister[0] == 42 && snd->fStatus == 42);
	EXPECT(rcv->fSenders.Peek() == nil && snd->fCurrentSequence == 0);
	EXPECT(SMemMsgMsgDoneKernelGlue(snd->fId, 0, 0) == kError_Call_Not_In_Progress);

	// --- send first, then receive; a message already posted is refused ------
	Run(client);
	PortSendKernelGlue(portId, snd->fId, 0, 0x200, 0);
	EXPECT(port->fSenders.Peek() == snd && IsBlocked(client));
	Run(client);
	PortSendKernelGlue(portId, snd->fId, 0, 0x200, 0);
	EXPECT(client->fRegister[0] == kError_Message_Already_Posted);
	Run(server);
	PortReceiveKernelGlue(portId, rcv->fId, 0x0F0, kPortFlags_ReceiveOnMsgAvail);	// filter does not match 0x200
	EXPECT(server->fRegister[0] == kError_No_Message_Waiting && port->fSenders.Peek() == snd);
	PortReceiveKernelGlue(portId, rcv->fId, 0x200, 0);
	EXPECT(server->fRegister[0] == noErr && server->fRegister[1] == snd->fId && IsRunnable(server));	// satisfied at once: no block
	EXPECT(SMemMsgMsgDoneKernelGlue(snd->fId, noErr, server->fRegister[4]) == noErr);
	EXPECT(IsScheduled(client) && client->fRegister[0] == noErr);

	// --- peek ---------------------------------------------------------------
	Run(server);
	PortReceiveKernelGlue(portId, rcv->fId, kMsgType_MatchAll, kPortFlags_IsMsgAvail);
	EXPECT(server->fRegister[0] == kError_No_Message_Waiting && rcv->fStatus == kError_No_Message_Waiting && IsRunnable(server));

	// --- a receive times out through the timer engine ------------------------
	Run(server);
	SMemMsgSetTimerParmsKernelGlue(rcv->fId, 1000, 0, 0);
	PortReceiveKernelGlue(portId, rcv->fId, kMsgType_MatchAll, kPortFlags_WantTimeout);
	EXPECT(IsBlocked(server) && timerEngine.Peek() == rcv && gHostAlarmArmed);
	HostAdvanceClock(1500);
	FireTimers();
	EXPECT(IsScheduled(server) && server->fRegister[0] == kError_Message_Timed_Out);
	EXPECT(rcv->fStatus == kError_Message_Timed_Out && port->fReceivers.Peek() == nil && timerEngine.Peek() == nil);

	// --- a send times out; ResetFilter delivers a waiting sender --------------
	Run(client);
	SMemMsgSetTimerParmsKernelGlue(snd->fId, 100, 0, 0);
	PortSendKernelGlue(portId, snd->fId, 0, 0x400, kPortFlags_WantTimeout);
	EXPECT(IsBlocked(client) && (snd->fTimerFlags & kSMemMsgTimer_SenderTimeout));
	HostAdvanceClock(200);
	FireTimers();
	EXPECT(IsScheduled(client) && client->fRegister[0] == kError_Message_Timed_Out && port->fSenders.Peek() == nil);
	Run(client);
	SMemMsgSetTimerParmsKernelGlue(snd->fId, 0, 0, 0);
	PortSendKernelGlue(portId, snd->fId, 0, 0x400, 0);
	Run(server);
	PortReceiveKernelGlue(portId, rcv->fId, 0x001, 0);				// waits: filter does not match
	EXPECT(IsBlocked(server) && port->fReceivers.Peek() == rcv);
	Run(idle);
	PortResetFilterKernelGlue(portId, rcv->fId, 0x400);
	EXPECT(server->fRegister[0] == noErr && IsScheduled(server) && server->fRegister[1] == snd->fId);
	EXPECT(SMemMsgMsgDoneKernelGlue(snd->fId, 7, server->fRegister[4]) == noErr && client->fRegister[0] == 7);

	// --- Reset fails whatever is queued ---------------------------------------
	Run(server);
	PortReceiveKernelGlue(portId, rcv->fId, kMsgType_MatchAll, 0);
	Run(idle);
	PortResetKernelGlue(portId, 0, kPortFlags_Abort);
	EXPECT(IsScheduled(server) && server->fRegister[0] == kError_Call_Aborted && rcv->fStatus == kError_Call_Aborted);

	// --- destroying a port fails its senders ----------------------------------
	Run(client);
	PortSendKernelGlue(portId, snd->fId, 0, 0x100, 0);
	EXPECT(IsBlocked(client));
	Run(idle);
	table.Remove(portId);
	DeletePort(port);
	EXPECT(IsScheduled(client) && client->fRegister[0] == kError_Port_No_Longer_Exists);

	// --- ref cons and sender task id ---------------------------------------
	Run(client);
	int refCon;
	SMemMsgSetUserRefConKernelGlue(snd->fId, &refCon);
	SMemMsgGetUserRefConKernelGlue(snd->fId);
	EXPECT(client->fRegister[1] == (TRegister) &refCon);
	SMemMsgGetSenderTaskIdKernelGlue(snd->fId);
	EXPECT(client->fRegister[1] == client->fId);

	if (failures == 0)
		printf("test_Port: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
