/*
	File:		user/UserRealTimeAlarm.cpp

	Contains:	TURealTimeAlarm (the DDK's LongTime.h), the user side of the
				real-time clock's named alarms.  Every call fills in the
				request block at the bottom of the task's globals
				(os600/kernel/RealTimeClock.h) and goes through GenericSWI's
				kGeneric_RealTimeClockDispatch - or, when the caller is
				already in supervisor mode, straight into the dispatcher.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "LongTime.h"
#include "UserGlobals.h"
#include "os600/GenericSWISelectors.h"
#include "os600/kernel/RealTimeClock.h"
#include "hal/System.h"


static long
Dispatch(void)
{
	if (!IsSuperMode())
		return GenericSWI(kGeneric_RealTimeClockDispatch);
	return RealTimeClockDispatch();
}


// ROM 0x0019c6b8 SetAlarm__15TURealTimeAlarmSFUl5TTimeN21PvlT1
long
TURealTimeAlarm::SetAlarm(ULong name, TTime time, TObjectId port, TObjectId message, void* obj, long sizeOfObj, ULong type)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_SetAlarm;
	request->fTime = time;
	request->fPort = port;
	request->fMessage = message;
	request->fType = type;
	request->fData = obj;
	request->fSize = sizeOfObj;
	return Dispatch();
}


// ROM 0x0019c73c SetAlarm__15TURealTimeAlarmSFUl5TTimePFPv_lPvN21
long
TURealTimeAlarm::SetAlarm(ULong name, TTime time, InterruptHandler handler, void* obj, ULong wakeUp, ULong isRelative)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_SetAlarmDirect;
	request->fTime = time;
	request->fHandler = handler;
	request->fData = obj;
	request->fWakeUp = wakeUp;
	request->fRelative = isRelative;
	return Dispatch();
}


// ROM 0x0019c824 ClearAlarm__15TURealTimeAlarmSFUl
long
TURealTimeAlarm::ClearAlarm(ULong name)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_ClearAlarm;
	return Dispatch();
}


// ROM 0x0019c864 AlarmStatus__15TURealTimeAlarmSFUlPUcP5TTimePl
// The answers come back over the request block: whether it is armed where
// the time's high word was, the time over the word after it, the error over
// the message id (RealTimeClockDispatch).
long
TURealTimeAlarm::AlarmStatus(ULong name, Boolean* active, TTime* alarmTime, long* error)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_AlarmStatus;
	long err = Dispatch();
	*active = (Boolean) (*(ULong*) &request->fTime.time.hi != 0);
	*alarmTime = *(TTime*) &request->fTime.time.lo;
	*error = (long) request->fMessage;
	return err;
}


// ROM 0x0019c8f4 CheckIn__15TURealTimeAlarmSFUl
long
TURealTimeAlarm::CheckIn(ULong name)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_CheckIn;
	return Dispatch();
}


// ROM 0x0019c934 NewName__15TURealTimeAlarmSFPUl
long
TURealTimeAlarm::NewName(ULong* name)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fSelector = kRTC_NewName;
	long err = Dispatch();
	*name = request->fName;
	return err;
}


// ROM 0x0019c984 CheckOut__15TURealTimeAlarmSFUl
long
TURealTimeAlarm::CheckOut(ULong name)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fName = name;
	request->fSelector = kRTC_CheckOut;
	return Dispatch();
}


// ROM 0x0019c9c4 Time__15TURealTimeAlarmSFv
TTime
TURealTimeAlarm::Time(void)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fSelector = kRTC_Time;
	Dispatch();
	return request->fTime;
}


// ROM 0x0019ca30 SetTime__15TURealTimeAlarmSF5TTime
void
TURealTimeAlarm::SetTime(TTime time)
{
	RealTimeClockRequest* request = RealTimeClockRequestBlock();
	request->fSelector = kRTC_SetTime;
	request->fTime = time;
	Dispatch();
}
