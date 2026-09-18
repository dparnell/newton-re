/*
	File:		os600/kernel/RealTimeClock.h

	Contains:	TRealTimeClock, the kernel's named alarms on the real-time
				clock (hal/RealTimeClock.h), and RealTimeClockDispatch, the
				GenericSWI selector the user-side TURealTimeAlarm
				(os600/user/UserRealTimeAlarm.cpp, declared in the DDK's
				LongTime.h) reaches them through.

				A caller checks in under a *name* - a word of its own, or one
				NewName hands out - and that name keeps a slot in a table of
				sixteen.  Setting an alarm on the slot says when (an absolute
				second of the clock) and what to do when the second arrives:
				send a message to a port, or call a handler.  Only the earliest
				armed alarm is programmed into the hardware; when it fires
				every alarm now due is fired and the next one programmed
				(Alarm).  There is no queue and no ordering: the sixteen slots
				are walked each time.

	Reconstructed from the MP2x00 US ROM (0x0019be2c-0x0019c520,
	0x0019ca90-0x0019cbe0); each function cites its origin.

				NOT YET RECONSTRUCTED: SleepingCheckFire and the sleep path
				(CheckAlarmsStaySleeping is here, but nothing sleeps yet), and
				RegisterRealTimeClockHandler, which hands the alarm interrupt
				to a driver instead of to this.
*/

#ifndef __REALTIMECLOCK_H
#define __REALTIMECLOCK_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONTIME_H
#include "NewtonTime.h"
#endif

typedef long (*RealTimeAlarmHandler)(void*);

// one slot of the table (0x34 bytes)
struct RealTimeAlarm
{
	ULong			fName;			// +0x00  0: the slot is free
	ULong			fTime;			// +0x04  the second it is due
	TObjectId		fPort;			// +0x08  message form: the port to send to
	TObjectId		fMessage;		// +0x0c  the message object
	ULong			fType;			// +0x10  the message type
	void*			fData;			// +0x14  the message's content, or the handler's argument
	ULong			fSize;			// +0x18  the content's size
	long			fResult;		// +0x1c  what the last send or call answered
	ULong			fEnabled;		// +0x20  armed (Swap'd to 0 as it fires, so it fires once)
	ULong			fWakeUp;		// +0x24  wake the machine when it is asleep
	ULong			fDirect;		// +0x28  0: send the message; 1: call fHandler
	RealTimeAlarmHandler fHandler;	// +0x2c
	ULong			fRelative;		// +0x30  the handler form's time was a delta, not an absolute

	void	Init(ULong time, TObjectId port, TObjectId message, void* data, long size, ULong type);	// ROM 0x0019be2c Init__13RealTimeAlarmFUlN21PvlT1
	void	Init(ULong time, RealTimeAlarmHandler handler, void* data, ULong wakeUp, ULong isRelative, ULong* lock);	// ROM 0x0019be68 Init__13RealTimeAlarmFUlPFPv_lPvN21PUl
	void	Fire(ULong now);		// ROM 0x0019c324 Fire__13RealTimeAlarmFUl
};

const long kRealTimeAlarmCount = 16;

class TRealTimeClock
{
public:
	static long		CheckIn(ULong name);								// ROM 0x0019cb2c
	static void		CheckOut(ULong name);								// ROM 0x0019cb84
	static long		NewName(ULong* name);								// ROM 0x0019ca90
	static long		SetAlarm(ULong name, TTime time, TObjectId port, TObjectId message, void* obj, long sizeOfObj, ULong type);	// ROM 0x0019c0dc
	static long		SetAlarm(ULong name, ULong time, RealTimeAlarmHandler handler, void* obj, ULong wakeUp, ULong isRelative);	// ROM 0x0019c180
	static long		ClearAlarm(ULong name);								// ROM 0x0019c208
	static long		AlarmStatus(ULong name, ULong* active, TTime* alarmTime, long* error);	// ROM 0x0019c24c
	static long		SetRealTimeClock(ULong seconds);					// ROM 0x0019c404
	static long		Alarm(void);										// ROM 0x0019bef0 Alarm__14TRealTimeClockSFv - the alarm interrupt
	static long		Cleanup(void);										// ROM 0x0019beec Cleanup__14TRealTimeClockSFv (Alarm's own code)
	static long		CheckAlarmsStaySleeping(void);						// ROM 0x0019bffc
	static long		FindSlot(ULong name);								// ROM 0x0019cbc8 FindSlot__14TRealTimeClockSFUl - the index, or -1
	static Boolean	PrimSetAlarm(ULong time);							// ROM 0x0019c3a8
	static long		PrimRawSetAlarm(ULong time);						// ROM 0x0019c2c4

	static RealTimeAlarm	fTable[kRealTimeAlarmCount];	// 0x0c106a44
	static ULong			fAlarmSet;						// 0x0c101828  the hardware alarm is programmed
	static ULong			fCurrentAlarm;					// 0x0c10182c  the second it is programmed for
	static ULong			fChangingTime;					// 0x0c101830  SetRealTimeClock's lock
	static ULong			fUpdatingAlarm;					// 0x0c101834  Alarm's lock
	static ULong			fFixUp;							// 0x0c101838  the table changed while Alarm walked it
	static ULong			fAssigningName;					// 0x0c10183c  NewName's lock
	static ULong			fNextName;						// 0x0c101840  the next name NewName hands out
};

long	RealTimeClockDispatch(void);				// ROM 0x0019c558 RealTimeClockDispatch__Fv - GenericSWI's kGeneric_RealTimeClockDispatch

// the request block the user side fills in and this reads, at the bottom of
// the task's globals (os600/TaskGlobals.h: gCurrentGlobals - kTaskGlobalsSize)
struct RealTimeClockRequest
{
	ULong		fSelector;		// +0x00
	ULong		fName;			// +0x04
	TTime		fTime;			// +0x08  (the handler form's delta is fTime.time.lo, +0x0c)
	TObjectId	fPort;			// +0x10
	TObjectId	fMessage;		// +0x14
	ULong		fType;			// +0x18
	void*		fData;			// +0x1c
	long		fSize;			// +0x20
	ULong		fWakeUp;		// +0x24
	ULong		fUnused28;		// +0x28
	RealTimeAlarmHandler fHandler;	// +0x2c
	ULong		fRelative;		// +0x30
};

// the selectors, as both sides use them
enum RealTimeClockSelectors
{
	kRTC_SetAlarm = 0,			// the message form
	kRTC_SetAlarmDirect,		// the handler form
	kRTC_ClearAlarm,
	kRTC_AlarmStatus,			// answers fTime+0 (active), fTime (the time) and fMessage (the error)
	kRTC_CheckIn,
	kRTC_NewName,
	kRTC_CheckOut,
	kRTC_Time,					// answers fTime
	kRTC_SetTime
};

RealTimeClockRequest*	RealTimeClockRequestBlock(void);		// the caller's, in its task globals

#endif	/* __REALTIMECLOCK_H */
