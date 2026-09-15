/*
	File:		TimerEngine.cpp

	Contains:	TTimerEngine and the alarm interrupt.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "TimerEngine.h"
#include "KernelGlobals.h"
#include "CompMath.h"
#include "hal/Atomic.h"
#include "hal/Timer.h"

#include <stddef.h>


// ROM 0x002536d8 SetAlarmAtomic__FR5TTime
Boolean
SetAlarmAtomic(const TTime* time)
{
	EnterFIQAtomic();
	Boolean armed = SetAlarm(time);
	ExitFIQAtomic();
	return armed;
}


// ROM 0x00253cb8 ClearAlarmAtomic__Fv
void
ClearAlarmAtomic()
{
	EnterFIQAtomic();
	DisableAlarm1();
	ExitFIQAtomic();
}


// ROM 0x00253e00 QueueNotify__FP13TSharedMemMsg
// Timeouts and delayed sends are not completed from the interrupt: the
// message goes onto gTimerDeferred and gWantDeferred asks the scheduler
// path (SWI 34 on the next ExitAtomic) to deal with it.
void
QueueNotify(void* msg)
{
	gWantDeferred = true;
	gTimerDeferred->Add(msg);
}


// ROM 0x00253fa0 __ct__12TTimerEngineFv
TTimerEngine::TTimerEngine()
	: TDoubleQContainer(offsetof(TSharedMemMsg, fTimerQItem))
{
}


// ROM 0x00253fd8 Init__12TTimerEngineFv
void
TTimerEngine::Init()
{
}


// ROM 0x00253f9c Start__12TTimerEngineFv
void
TTimerEngine::Start()
{
}


static inline const TTime*
ExpiryOf(void* msg)
{
	return (const TTime*) &((TSharedMemMsg*) msg)->fExpiryTime;
}


// ROM 0x00253d64 Alarm__12TTimerEngineFv
// Fires every message whose time has come, then arms the alarm for the new
// head; if that one is already due, goes round again.
void
TTimerEngine::Alarm()
{
	EnterAtomic();
	TSharedMemMsg* msg = (TSharedMemMsg*) Peek();
	if (msg == nil)
		ClearAlarmAtomic();
	else
	{
		do
		{
			Int64 now;
			GetClock(&now);
			if (CompCompare(&now, &msg->fExpiryTime) >= 0)
			{
				TDoubleQContainer::Remove();
				if ((msg->fTimerFlags & kSMemMsgTimer_Generic) == kSMemMsgTimer_Generic)
					msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
				msg->fNotifyProc(msg->fNotifyData);
			}
			msg = (TSharedMemMsg*) Peek();
		} while (msg != nil && !SetAlarmAtomic(ExpiryOf(msg)));
	}
	ExitAtomic();
}


// ROM 0x0013eb48 TimerInterruptHandler
// The alarm interrupt; the same as Alarm() on gTimerEngine, counted.
void
TimerInterruptHandler()
{
	gTimerInterruptCount++;
	gTimerEngine->Alarm();
}


// ROM 0x00254200 Queue__12TTimerEngineFP13TSharedMemMsg
// Inserts in time order.  A message that would become the head is only
// queued if the alarm can still be armed for it; otherwise it is already due,
// the engine runs Alarm() for whatever is queued and returns false, and the
// caller treats the message as expired.
Boolean
TTimerEngine::Queue(TSharedMemMsg* msg)
{
	Boolean queued = true;
	EnterAtomic();
	void* entry = Peek();
	if (entry == nil)
	{
		queued = SetAlarmAtomic(ExpiryOf(msg));
		if (queued)
			AddToFront(msg);
		else
			Alarm();
	}
	else
	{
		for (; entry != nil; entry = GetNext(entry))
		{
			if (CompCompare(&msg->fExpiryTime, &((TSharedMemMsg*) entry)->fExpiryTime) < 0)
			{
				if (Peek() == entry)
				{
					queued = SetAlarmAtomic(ExpiryOf(msg));
					if (queued)
						AddToFront(msg);
					else
						Alarm();
				}
				else
					AddBefore(entry, msg);
				ExitAtomic();
				return queued;
			}
		}
		Add(msg);
	}
	ExitAtomic();
	return queued;
}


// ROM 0x00253fdc QueueTimer__12TTimerEngineFP13TSharedMemMsgUlPvPFPv_v
// A generic one-shot timer `delay` ticks from now; does nothing if the
// message is already queued.
Boolean
TTimerEngine::QueueTimer(TSharedMemMsg* msg, ULong delay, void* data, TimerNotifyProcPtr proc)
{
	CheckBeforeAdd(msg);
	if (msg->fTimerFlags & kSMemMsgTimer_Generic)
		return false;
	msg->fNotifyData = data;
	msg->fNotifyProc = proc;
	Int64 when;
	GetClock(&when);
	Int64 delta = {0, delay};
	CompAdd(&delta, &when);
	msg->fExpiryTime = when;

	EnterAtomic();
	msg->fTimerFlags |= kSMemMsgTimer_Generic;
	Boolean queued = Queue(msg);
	if (!queued)
		msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
	ExitAtomic();
	return queued;
}


// ROM 0x002540b0 QueueTimeout__12TTimerEngineFP13TSharedMemMsg
Boolean
TTimerEngine::QueueTimeout(TSharedMemMsg* msg)
{
	if (msg->fTimeout == kSMemMsgNoTimeout)
		return false;
	msg->fNotifyData = msg;
	msg->fNotifyProc = QueueNotify;
	Int64 when;
	GetClock(&when);
	Int64 delta = {0, msg->fTimeout};
	CompAdd(&delta, &when);
	msg->fExpiryTime = when;

	EnterAtomic();
	msg->fTimerFlags |= kSMemMsgTimer_Timeout;
	Boolean queued = Queue(msg);
	if (!queued)
		msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
	ExitAtomic();
	return queued;
}


// ROM 0x0025417c QueueDelay__12TTimerEngineFP13TSharedMemMsg
// A delayed send: fExpiryTime is already the absolute time wanted.
Boolean
TTimerEngine::QueueDelay(TSharedMemMsg* msg)
{
	Int64 zero = {0, 0};
	if (CompCompare(&msg->fExpiryTime, &zero) == 0)
		return false;
	msg->fNotifyData = msg;
	msg->fNotifyProc = QueueNotify;

	EnterAtomic();
	msg->fTimerFlags |= kSMemMsgTimer_Delay;
	Boolean queued = Queue(msg);
	if (!queued)
		msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
	ExitAtomic();
	return queued;
}


// ROM 0x00253cd4 Remove__12TTimerEngineFP13TSharedMemMsg
// Takes a message out of the timer (or deferred) queue, re-arming the alarm
// if it was the head.
void
TTimerEngine::Remove(TSharedMemMsg* msg)
{
	EnterAtomic();
	if (!gTimerDeferred->RemoveFromQueue(msg))
	{
		if (Peek() == msg)
		{
			TDoubleQContainer::Remove();
			if (Peek() == nil)
				ClearAlarmAtomic();
			else
				Alarm();
		}
		else
			RemoveFromQueue(msg);
	}
	msg->fTimerFlags &= ~kSMemMsgTimer_Generic;
	ExitAtomic();
}


// ROM 0x0013eb6c InitTime
void
InitTime()
{
	gTimerDeferred = new TDoubleQContainer(offsetof(TSharedMemMsg, fTimerQItem));
	gTimerEngine = new TTimerEngine;
}
