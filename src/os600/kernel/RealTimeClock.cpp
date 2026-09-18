/*
	File:		os600/kernel/RealTimeClock.cpp

	Contains:	TRealTimeClock and RealTimeAlarm (RealTimeClock.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RealTimeClock.h"
#include "KernelGlobals.h"
#include "Port.h"
#include "os600/TaskGlobals.h"
#include "hal/Atomic.h"
#include "hal/RealTimeClock.h"
#include "OSErrors.h"
#include "UserPorts.h"


RealTimeAlarm	TRealTimeClock::fTable[kRealTimeAlarmCount];
ULong			TRealTimeClock::fAlarmSet = 0;
ULong			TRealTimeClock::fCurrentAlarm = 0;
ULong			TRealTimeClock::fChangingTime = 0;
ULong			TRealTimeClock::fUpdatingAlarm = 0;
ULong			TRealTimeClock::fFixUp = 0;
ULong			TRealTimeClock::fAssigningName = 0;
ULong			TRealTimeClock::fNextName = 0;


/* -------------------------------------------------------------------------------
	R e a l T i m e A l a r m
------------------------------------------------------------------------------- */

// ROM 0x0019be2c Init__13RealTimeAlarmFUlN21PvlT1
// A slot armed to send a message when the second arrives.  The name is not
// touched: it belongs to whoever checked the slot out.
void
RealTimeAlarm::Init(ULong time, TObjectId port, TObjectId message, void* data, long size, ULong type)
{
	fTime = time;
	fPort = port;
	fMessage = message;
	fType = type;
	fResult = noErr;
	fWakeUp = 1;
	fData = data;
	fSize = size;
	fDirect = 0;
	fRelative = 0;
	fEnabled = 1;
}


// ROM 0x0019be68 Init__13RealTimeAlarmFUlPFPv_lPvN21PUl
// A slot armed to call a handler.  A relative time is counted from the clock
// as it reads now, which has to be done under the caller's lock so that the
// clock cannot be set between the read and the arming.
void
RealTimeAlarm::Init(ULong time, RealTimeAlarmHandler handler, void* data, ULong wakeUp, ULong isRelative, ULong* lock)
{
	fRelative = isRelative;
	fWakeUp = wakeUp;
	fDirect = 1;
	fHandler = handler;
	fData = data;
	fResult = noErr;
	if (isRelative != 0)
	{
		while (Swap(lock, 1) != 0)
			;
		fTime = GetRealTimeClock() + time;
		fEnabled = 1;
		*lock = 0;
		return;
	}
	fTime = time;
	fEnabled = 1;
}


// ROM 0x0019c324 Fire__13RealTimeAlarmFUl
// The alarm delivered if its second has arrived: the flag is swapped away
// first, so a slot fires once however often the walk comes past it.
void
RealTimeAlarm::Fire(ULong now)
{
	if (now < fTime)
		return;
	if (Swap(&fEnabled, 0) == 0)
		return;
	if (fDirect == 0)
		fResult = SendForInterrupt(fPort, fMessage, 0, fData, fSize, fType, 0, nil, false);
	else
		fResult = fHandler(fData);
}


/* -------------------------------------------------------------------------------
	T R e a l T i m e C l o c k
------------------------------------------------------------------------------- */

// ROM 0x0019cbc8 FindSlot__14TRealTimeClockSFUl
long
TRealTimeClock::FindSlot(ULong name)
{
	for (long i = 0; i < kRealTimeAlarmCount; i++)
		if (fTable[i].fName == name)
			return i;
	return -1;
}


// ROM 0x0019cb2c CheckIn__14TRealTimeClockSFUl
// The first free slot taken for the name.  (A name already checked in gets a
// second slot: the ROM looks for a free slot, not for the name.)
long
TRealTimeClock::CheckIn(ULong name)
{
	for (long i = 0; i < kRealTimeAlarmCount; i++)
		if (fTable[i].fName == 0)
		{
			fTable[i].fName = name;
			return noErr;
		}
	return -2;
}


// ROM 0x0019cb84 CheckOut__14TRealTimeClockSFUl
void
TRealTimeClock::CheckOut(ULong name)
{
	long slot = FindSlot(name);
	if (slot < 0)
		return;
	fTable[slot].fName = 0;
	fTable[slot].fEnabled = 0;
}


// ROM 0x0019ca90 NewName__14TRealTimeClockSFPUl
// A name nobody has yet, checked in.  (The ROM walks the table between
// handing the counter out and checking in, and does nothing with what it
// reads - the check the walk was for is not there any more.)
//
// The counter starts at 0, and 0 is also what a free slot's name is, so
// the first name handed out is checked into a slot that still reads as
// free and the next CheckIn takes that slot away from it.  The bug never
// shows on the Newton because TNewtWorld::MainConstructor's alarm is the
// only name anything ever asks for.
long
TRealTimeClock::NewName(ULong* name)
{
	while (Swap(&fAssigningName, 1) != 0)
		;
	*name = fNextName;
	fNextName = fNextName + 1;
	long err = CheckIn(*name);
	if (err != noErr)
		*name = 0;
	fAssigningName = 0;
	return err;
}


// ROM 0x0019c2c4 PrimRawSetAlarm__14TRealTimeClockSFUl
// The hardware programmed for the second, if nothing earlier is programmed
// already.  ==> 0 when the second went by while it was being programmed.
long
TRealTimeClock::PrimRawSetAlarm(ULong time)
{
	long result = 1;
	if (fAlarmSet == 0 || time <= fCurrentAlarm)
	{
		SetRealTimeClockAlarm(time);
		if (GetRealTimeClock() < time)
		{
			fAlarmSet = 1;
			fCurrentAlarm = time;
		}
		else
			result = 0;
	}
	return result;
}


// ROM 0x0019c3a8 PrimSetAlarm__14TRealTimeClockSFUl
// The same, for a second that may already have passed: ==> false when it
// has, or when it arrived while the hardware was being programmed, and the
// caller then walks the table itself (Cleanup).
Boolean
TRealTimeClock::PrimSetAlarm(ULong time)
{
	if (GetRealTimeClock() < time)
	{
		if (fAlarmSet == 0)
		{
			SetRealTimeClockAlarm(time);
			Boolean armed = GetRealTimeClock() < time;
			if (armed)
			{
				fAlarmSet = 1;
				fCurrentAlarm = time;
			}
			return armed;
		}
		if (PrimRawSetAlarm(time) != 0)
			return true;
		Cleanup();
	}
	return false;
}


// ROM 0x0019bef0 Alarm__14TRealTimeClockSFv (and 0x0019beec Cleanup, 0x0019bee8
// InterruptEntry, which are the same code)
// The alarm interrupt: every alarm now due is fired and the earliest of those
// left is programmed.  fFixUp says the table changed while it was being
// walked, so the walk starts again; fUpdatingAlarm keeps two walks apart, and
// a walk that finds one already running leaves it to finish (it will see
// fFixUp).
long
TRealTimeClock::Alarm(void)
{
	ULong earliest = 0;
	fFixUp = 1;
	do
	{
		if (Swap(&fUpdatingAlarm, 1) != 0)
			return noErr;
		ClearRealTimeClockAlarm();
		GetRealTimeClock();
		fAlarmSet = 0;
		Boolean any;
		do
		{
			any = false;
			fFixUp = 0;
			for (long i = 0; i < kRealTimeAlarmCount; i++)
			{
				ULong now = GetRealTimeClock();
				fTable[i].Fire(now);
				if ((fTable[i].fEnabled & 0xff) != 0)
				{
					if (!any)
					{
						any = true;
						earliest = fTable[i].fTime;
					}
					else if (fTable[i].fEnabled != 0 && fTable[i].fTime <= earliest)
						earliest = fTable[i].fTime;
				}
			}
		} while (fFixUp != 0 || (any && PrimRawSetAlarm(earliest) == 0));
		fUpdatingAlarm = 0;
	} while (fFixUp != 0);
	fUpdatingAlarm = 0;
	return noErr;
}


// ROM 0x0019beec Cleanup__14TRealTimeClockSFv
long
TRealTimeClock::Cleanup(void)
{
	return Alarm();
}


// ROM 0x0019bffc CheckAlarmsStaySleeping__14TRealTimeClockSFv
// Asked before the machine goes back to sleep: an alarm that is due but
// wants the machine awake answers false, one that does not is fired where it
// stands, and the earliest of the rest is programmed.  ==> true: stay asleep.
long
TRealTimeClock::CheckAlarmsStaySleeping(void)
{
	ULong now = GetRealTimeClock();
	Boolean any;
	ULong earliest;
	do
	{
		any = false;
		earliest = 0;
		for (long i = 0; i < kRealTimeAlarmCount; i++)
		{
			if (fTable[i].fEnabled != 0 && fTable[i].fTime <= now)
			{
				if ((fTable[i].fWakeUp & 0xff) != 0)
					return 0;
				fTable[i].Fire(now);
				fAlarmSet = 0;
			}
			if ((fTable[i].fEnabled & 0xff) != 0)
			{
				if (!any)
				{
					any = true;
					earliest = fTable[i].fTime;
				}
				else if (fTable[i].fEnabled != 0 && fTable[i].fTime <= earliest)
					earliest = fTable[i].fTime;
			}
		}
		ClearRealTimeClockAlarm();
	} while (any && PrimRawSetAlarm(earliest) == 0);
	return 1;
}


// ROM 0x0019c0dc SetAlarm__14TRealTimeClockSFUl5TTimeN21PvlT1
// The named slot armed to send a message at the time.
long
TRealTimeClock::SetAlarm(ULong name, TTime time, TObjectId port, TObjectId message, void* obj, long sizeOfObj, ULong type)
{
	TTime when = time;
	long slot = FindSlot(name);
	if (slot == -1)
		return -1;
	fTable[slot].Init(when.ConvertTo(kSeconds), port, message, obj, sizeOfObj, type);
	if (!PrimSetAlarm(fTable[slot].fTime))
		Cleanup();
	return noErr;
}


// ROM 0x0019c180 SetAlarm__14TRealTimeClockSFUlT1PFPv_lPvN21
// The named slot armed to call a handler, at a second or that many seconds
// from now.
long
TRealTimeClock::SetAlarm(ULong name, ULong time, RealTimeAlarmHandler handler, void* obj, ULong wakeUp, ULong isRelative)
{
	long slot = FindSlot(name);
	if (slot == -1)
		return -1;
	fTable[slot].Init(time, handler, obj, wakeUp, isRelative, &fChangingTime);
	if (!PrimSetAlarm(fTable[slot].fTime))
		Cleanup();
	return noErr;
}


// ROM 0x0019c208 ClearAlarm__14TRealTimeClockSFUl
long
TRealTimeClock::ClearAlarm(ULong name)
{
	long slot = FindSlot(name);
	if (slot == -1)
		return -1;
	fTable[slot].fEnabled = 0;
	Cleanup();
	return noErr;
}


// ROM 0x0019c24c AlarmStatus__14TRealTimeClockSFUlPUlP5TTimePl
// Whether the named alarm is armed, when for (only when it is) and what its
// last send or call answered.
long
TRealTimeClock::AlarmStatus(ULong name, ULong* active, TTime* alarmTime, long* error)
{
	long slot = FindSlot(name);
	if (slot == -1)
		return -1;
	ULong enabled = fTable[slot].fEnabled;
	*active = enabled;
	if (enabled != 0)
	{
		TTime when(fTable[slot].fTime, kSeconds);
		*alarmTime = when;
	}
	*error = fTable[slot].fResult;
	return noErr;
}


// ROM 0x0019c404 SetRealTimeClock__14TRealTimeClockSFUl
// The clock set: the hardware written until it reads back, then every armed
// alarm moved by the same amount so that it still falls where it was meant
// to, and the alarm re-programmed (Alarm's own code).
long
TRealTimeClock::SetRealTimeClock(ULong seconds)
{
	while (Swap(&fChangingTime, 1) != 0)
		;
	ClearRealTimeClockAlarm();
	fAlarmSet = 0;
	ULong was = GetRealTimeClock();
	while (GetRealTimeClock() != seconds)
		WriteRealTimeClock(seconds);
	for (long i = 0; i < kRealTimeAlarmCount; i++)
		if (fTable[i].fEnabled != 0 && fTable[i].fRelative != 0)
			fTable[i].fTime = fTable[i].fTime - was + seconds;
	fChangingTime = 0;
	return Alarm();
}


/* -------------------------------------------------------------------------------
	T h e   s y s t e m   c a l l
------------------------------------------------------------------------------- */

// The caller's request block: the bottom of its task globals, which is the
// scratch the memory manager's requests use too (os600/TaskGlobals.h).
RealTimeClockRequest*
RealTimeClockRequestBlock(void)
{
	return (RealTimeClockRequest*) ((char*) gCurrentGlobals - kTaskGlobalsSize);
}


// ROM 0x0019c558 RealTimeClockDispatch__Fv
// GenericSWI's kGeneric_RealTimeClockDispatch: the selector in the caller's
// request block says which call, and the answers go back in the same block.
long
RealTimeClockDispatch(void)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	switch (request->fSelector)
	{
	case kRTC_SetAlarm:
		return TRealTimeClock::SetAlarm(request->fName, request->fTime, request->fPort,
										request->fMessage, request->fData, request->fSize, request->fType);
	case kRTC_SetAlarmDirect:
		return TRealTimeClock::SetAlarm(request->fName, request->fTime.time.lo, request->fHandler,
										request->fData, request->fWakeUp, request->fRelative);
	case kRTC_ClearAlarm:
		return TRealTimeClock::ClearAlarm(request->fName);
	case kRTC_AlarmStatus:
		// the answers go back over the request: whether it is armed where the
		// time's high word was, the time over the word after it, the error
		// over the message id
		return TRealTimeClock::AlarmStatus(request->fName, (ULong*) &request->fTime.time.hi,
										   (TTime*) &request->fTime.time.lo, (long*) &request->fMessage);
	case kRTC_CheckIn:
		return TRealTimeClock::CheckIn(request->fName);
	case kRTC_NewName:
		return TRealTimeClock::NewName(&request->fName);
	case kRTC_CheckOut:
		TRealTimeClock::CheckOut(request->fName);
		return noErr;
	case kRTC_Time:
		{
			TTime now(GetRealTimeClock(), kSeconds);
			request->fTime = now;
		}
		return noErr;
	case kRTC_SetTime:
		return TRealTimeClock::SetRealTimeClock(request->fTime.ConvertTo(kSeconds));
	}
	return kError_Bad_Parameters;
}
