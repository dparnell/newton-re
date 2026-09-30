/*
	File:		power/PowerManager.cpp

	Contains:	The power manager ('pg&e) and the machine's sleep
				(power/PowerManager.h).

				Reconstructed from the MP2x00 US ROM (0x001924d4-0x00192f7c);
				each function cites its origin.
*/

#include "PowerManager.h"
#include "BatteryDriver.h"
#include "hal/Power.h"
#include "hal/System.h"
#include "hal/Atomic.h"
#include "qd/Screen.h"
#include "recognition/TabletBuffer.h"
#include "stores/flash/Flash.h"
#include "os600/kernel/Scheduler.h"
#include "os600/kernel/RealTimeClock.h"
#include "os600/GenericSWISelectors.h"
#include "UserTasks.h"
#include "UserGlobals.h"
#include "VirtualMemory.h"
#include "OSErrors.h"
#include "power/host/HostPowerSwitch.h"

#include <stdio.h>
#include <stdlib.h>

extern TUPort*		gNewtPort;					// the application's (newt/NewtWorld.h)

TPowerManager*		gPowerMgr = nil;			// ROM 0x0c101798 gPowerMgr
static TUPort*		gPowerPort = nil;			// ROM 0x0c101794 gPowerPort

// what the power switch's interrupt sends the power manager ({'newt,
// 'pg&e} and the switch event's type), in the message kept for it
static TPowerNewtEvent	gPowerInterruptAEvent;			// ROM 0x0c101770 gPowerInterruptAEvent
static TUAsyncMessage	gPowerInterruptAsyncMessage;	// ROM 0x0c10177c gPowerInterruptAsyncMessage


// NEWTON_TRACE_POWER: the switch's presses, what the manager does with
// them, and each sleep and what ended it (host)
static bool
TracePower(void)
{
	static int trace = -1;
	if (trace < 0)
		trace = getenv("NEWTON_TRACE_POWER") != nil;
	return trace != 0;
}


/*------------------------------------------------------------------------------
	T P o w e r M a n a g e r
------------------------------------------------------------------------------*/

// ROM 0x00192cac __ct__13TPowerManagerFv
TPowerManager::TPowerManager()
{ }


// ROM 0x00192d44 __dt__13TPowerManagerFv
TPowerManager::~TPowerManager()
{ }


// ROM 0x00192dc4 GetSizeOf__13TPowerManagerFv
ULong
TPowerManager::GetSizeOf()
{
	return sizeof(TPowerManager);		// (the ROM: 0xe4)
}


// ROM 0x00192dcc MainConstructor__13TPowerManagerFv
// The world up: its handler taking {'newt, 'pg&e} events, the two
// messages it sends asynchronously - to the application and to the
// system event handlers - collected on its own port with the handler as
// their refcon, the system event sender made for 'ppen, the port made
// the power port, and the battery driver found and started.
long
TPowerManager::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	if ((err = fHandler.Init(kPowerManagerID, kNewtEventClass)) != noErr)
		return err;
	if ((err = fNewtMessage.Init(true)) != noErr)
		return err;
	if ((err = fNewtMessage.SetCollectorPort(GetMyPort()->fId)) != noErr)
		return err;
	if ((err = fNewtMessage.SetUserRefCon((ULong) (uintptr_t) &fHandler)) != noErr)
		return err;
	if ((err = fSysMessage.Init(true)) != noErr)
		return err;
	if ((err = fSysMessage.SetCollectorPort(GetMyPort()->fId)) != noErr)
		return err;
	if ((err = fSysMessage.SetUserRefCon((ULong) (uintptr_t) &fHandler)) != noErr)
		return err;
	if ((err = fSendSysEvent.Init()) != noErr)
		return err;
	fSendSysEvent.SetEvent(kSysEvent_PowerOffPending);
	gPowerPort = GetMyPort();
	BatteryInitialize();
	return noErr;
}


// ROM 0x00192edc MainDestructor__13TPowerManagerFv
void
TPowerManager::MainDestructor()
{ }


// ROM 0x00192ee0 DoCommand__13TPowerManagerFP10TUMsgTokenPUlP18TPowerManagerEvent
// A switch event: the power switch (PowerOffMessage's work, done here in
// line) or the backlight button (BacklightMessage's).
void
TPowerManager::DoCommand(TUMsgToken* /*token*/, ULong* /*size*/, TPowerManagerEvent* event)
{
	if (TracePower())
		printf("[power] %c%c%c%c\n", (char) (event->fCommand >> 24), (char) (event->fCommand >> 16), (char) (event->fCommand >> 8), (char) event->fCommand);
	if (event->fCommand == kPowerSwitchEvent)
		PowerOffMessage();
	else if (event->fCommand == kBacklightEvent)
		BacklightMessage();
}


// ROM 0x001929c0 BacklightMessage__13TPowerManagerFv
// The backlight turned over, and the application told ({'newt, 'idle,
// 'bklt}, which notes it as a touch).
void
TPowerManager::BacklightMessage(void)
{
	long on;
	GetGrafInfo(kGrafInfoBacklight, &on);
	SetGrafInfo(kGrafInfoBacklight, on == 0);
	fNewtEvent.fAEventClass = kNewtEventClass;
	fNewtEvent.fAEventID = 'idle';
	fNewtEvent.fType = kBacklightEvent;
	if (gNewtPort != nil)
		gNewtPort->SendRPC(&fNewtMessage, &fNewtEvent, sizeof(TPowerNewtEvent), &fNewtEvent, sizeof(TPowerNewtEvent));
}


// ROM 0x00192a8c PowerOffMessage__13TPowerManagerFv
// The power switch: every system event handler told the power is about to
// go ('ppen), and the application asked to go to sleep ({'newt, 'idle,
// 'powr}) - with ten seconds to answer, after which AECompletionProc
// takes it to have hung.
void
TPowerManager::PowerOffMessage(void)
{
	fSysEvent.fAEventID = kAESystemEventID;
	fSysEvent.fSysEventType = kSysEvent_PowerOffPending;
	fSendSysEvent.SendSystemEvent(&fSysMessage, &fSysEvent, sizeof(TAESystemEvent), &fSysEvent, sizeof(TAESystemEvent));
	fNewtEvent.fAEventClass = kNewtEventClass;
	fNewtEvent.fAEventID = 'idle';
	fNewtEvent.fType = kPowerSwitchEvent;
	if (gNewtPort == nil)
		return;
	gNewtPort->SendRPC(&fNewtMessage, &fNewtEvent, sizeof(TPowerNewtEvent), &fNewtEvent, sizeof(TPowerNewtEvent), 10 * kSeconds);
}


// The machine powered off at once and rebooted: the application has not
// answered the power switch.  (The ROM has this inline three times.)
// NOT YET RECONSTRUCTED: the GPIO interface's re-initialisation, the
// platform's power switch interrupt registered and enabled again, and the
// interrupt bits cleared (the platform driver).
static void
PowerOffAndRebootNow(void)
{
	EnterFIQAtomic();
	IOPowerOffAll();
	DisableAllInterrupts();
	PowerOffSystem();
	Reboot(noErr, 0, true);
	ExitFIQAtomic();
}


// ROM 0x00192b6c PowerOffTimeout__13TPowerManagerFv
// The application's answer timed out: the machine is hung.
void
TPowerManager::PowerOffTimeout(void)
{
	if (fNewtMessage.GetResult() != kError_Message_Timed_Out)
		return;
	PowerOffAndRebootNow();
}


// ROM 0x001929ac DoReply__13TPowerManagerFP10TUMsgTokenPUlP18TPowerManagerEvent
// The same for a reply to the message to the application.
void
TPowerManager::DoReply(TUMsgToken* token, ULong* /*size*/, TPowerManagerEvent* /*event*/)
{
	if (token->GetMsgId() != fNewtMessage.GetMsgId())
		return;
	if (fNewtMessage.GetResult() != kError_Message_Timed_Out)
		return;
	PowerOffAndRebootNow();
}


/*------------------------------------------------------------------------------
	T P o w e r E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x00192bac AEHandlerProc__18TPowerEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A command answered: 4 and 5 a battery's status (the result in +08, the
// status after the battery's number), 6 the count (in +0C), 7 a battery's
// cells set (the result in +0C); 1 to 3 answer 100.  Anything else is a
// switch event, which the manager carries out.
void
TPowerEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TPowerManagerEvent* request = (TPowerManagerEvent*) event;
	switch (request->fCommand)
	{
	case 1:
	case 2:
	case 3:
		request->fCommand = 100;
		SetReply(kPowerEventSize, event);
		break;
	case kPowerCmdStatus:
		request->fCommand = GetPowerPlantStatus(request->fWhich, &request->fStatus);
		SetReply(kPowerEventStatusSize, event);
		break;
	case kPowerCmdRawStatus:
		request->fCommand = GetRawPowerPlantStatus(request->fWhich, &request->fStatus);
		SetReply(kPowerEventStatusSize, event);
		break;
	case kPowerCmdCount:
		request->fWhich = GetPowerPlantCount();
		SetReply(kPowerEventSize, event);
		break;
	case kPowerCmdSetType:
		request->fWhich = SetBatteryType(request->fWhich, request->fType);
		SetReply(kPowerEventSize, event);
		break;
	default:
		((TPowerManager*) GetGlobals())->DoCommand(token, size, request);
		SetReply(kPowerEventSize, nil);
		break;
	}
}


// ROM 0x00192c7c AECompletionProc__18TPowerEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The application's answer to the power switch: if it timed out, the
// machine is hung and is powered off and rebooted.
void
TPowerEventHandler::AECompletionProc(TUMsgToken* token, ULong* /*size*/, TAEvent* /*event*/)
{
	TPowerManager* manager = (TPowerManager*) GetGlobals();
	if (token->GetMsgId() != manager->fNewtMessage.GetMsgId())
		return;
	if (manager->fNewtMessage.GetResult() != kError_Message_Timed_Out)
		return;
	PowerOffAndRebootNow();
}


/*------------------------------------------------------------------------------
	S t a r t i n g   i t
------------------------------------------------------------------------------*/

// ROM 0x0019294c InitPowerManager__Fv
// The 'pg&e world started (TLoader::TheMain does it), a heap object for
// the life of the machine, and the power switch's interrupt made ready.
NewtonErr
InitPowerManager(void)
{
	gPowerMgr = new TPowerManager;
	if (gPowerMgr == nil)
		return kError_No_Memory;
	NewtonErr err = gPowerMgr->Init(kPowerManagerID, true, 6000);
	if (err != noErr)
		return err;
	return InitializePowerInterrupt();
}


// ROM 0x001924d4 InitializePowerInterrupt__Fv
// The switch event's block and message made, and the platform driver's
// power switch interrupt registered and enabled.
// NOT YET RECONSTRUCTED: the platform driver (TVoyagerPlatform's
// RegisterPowerSwitchInterrupt / EnableSysPowerInterrupt - its GPIO line
// and SamplePowerSwitchStateMachine); DEVIATION: the host's power switch
// (a key of newton's window) is registered in their place.
NewtonErr
InitializePowerInterrupt(void)
{
	gPowerInterruptAEvent.fAEventClass = kNewtEventClass;
	gPowerInterruptAEvent.fAEventID = kPowerManagerID;
	gPowerInterruptAEvent.fType = kPowerSwitchEvent;
	gPowerInterruptAsyncMessage.Init(false);
	HostRegisterPowerSwitch();
	return noErr;
}


// ROM 0x0019299c GetPowerPort__Fv
TUPort*
GetPowerPort(void)
{
	return gPowerPort;
}


// ROM 0x001926b8 SendPowerSwitchEvent__FUl
// The power switch (or the backlight button: `type`) sent to the power
// manager from an interrupt.
void
SendPowerSwitchEvent(ULong type)
{
	gPowerInterruptAEvent.fType = type;
	SendForInterrupt(GetPowerPort()->fId, gPowerInterruptAsyncMessage.GetMsgId(), 0,
					 &gPowerInterruptAEvent, sizeof(TPowerNewtEvent), kMsgType_FromInterrupt, 0, nil, false);
}


/*------------------------------------------------------------------------------
	T h e   s l e e p
------------------------------------------------------------------------------*/

// ROM 0x001925bc PowerOffSystem__Fv
// The generic system call 0x44: the kernel has the platform driver turn
// the machine off, and it returns when the machine is on again.
long
PowerOffSystem(void)
{
	return GenericSWI(kGeneric_PowerOffSystem);
}


// ROM 0x001925a4 PowerOnSystem__Fv
long
PowerOnSystem(void)
{
	return PlatformPowerOnSystem();
}


// ROM 0x00192904 TranslatePowerEvent__FUl
long
TranslatePowerEvent(ULong event)
{
	return PlatformTranslatePowerEvent(event);
}


// ROM 0x00192764 CyclePower__Fv
// The machine to sleep and back.  Every system event handler is told the
// power is going off ('pwof), the battery driver, the screen and the
// tablet are shut down, and once no flash erase is still going on - with
// the stack locked and the scheduler held while it looks - if no message
// is waiting for the application the machine is turned off.  It is turned
// on again by whatever wakes it; if that was only the real-time clock's
// alarm and the alarm does not want the machine awake (SleepingCheckFire
// fires it where it stands), it goes straight back to sleep.  Then the
// screen, the battery and the tablet are woken, every handler told 'pwon
// with the event word, and the switch's message is cancelled (a press
// that woke it is not also a press to sleep).  ==> the event word: what
// woke it, 0 when it did not sleep.
// NOT YET RECONSTRUCTED: SCCPowerInit (0x001b8534, the serial chips'
// power brought back).
ULong
CyclePower(void)
{
	ULong event = 0;
	TSendSystemEvent sender(kSysEvent_PowerOff);
	TPowerEvent powerEvent;
	powerEvent.fAEventID = kAESystemEventID;
	powerEvent.fSysEventType = kSysEvent_PowerOff;
	sender.Init();
	sender.SendSystemEvent(&powerEvent, sizeof(TAESystemEvent));		// (the ROM: 0xc)
	BatteryShutDown();
	LCDPowerOff(true);
	TabShutDown();
	while (IsInternalFlashEraseActive())
		;
	TULockStack lock;
	if (LockStack(&lock, 0x100) == noErr)
	{
		EnterFIQAtomic();
		HoldSchedule();
		long waiting = gNewtPort->Receive((TUAsyncMessage*) nil, 0, 1, false);
		AllowSchedule();
		if (waiting != noErr)
		{
			do
			{
				do
				{
					IOPowerOffAll();
					PowerOffSystem();
				} while (PowerOnSystem() != noErr);
				event = PlatformPowerEvent();
			} while ((event & ~kPowerEventAlarm) == 0 && TRealTimeClock::SleepingCheckFire() != 0);
			// NOT YET RECONSTRUCTED: SCCPowerInit()
		}
		ExitFIQAtomic();
		UnlockStack(&lock);
	}
	if (TracePower())
		printf("[power] woke: event %#lx\n", (unsigned long) event);
	LCDPowerInit(true);
	sender.SetEvent(kSysEvent_PowerOn);
	powerEvent.fAEventID = kAESystemEventID;
	powerEvent.fSysEventType = kSysEvent_PowerOn;
	powerEvent.fReason = event;
	sender.SendSystemEvent(&powerEvent, sizeof(TPowerEvent));			// (the ROM: 0x10)
	TabWakeUp();
	LCDPowerOn(true);
	BatteryWakeUp();
	gPowerInterruptAsyncMessage.Abort();
	return event;
}
