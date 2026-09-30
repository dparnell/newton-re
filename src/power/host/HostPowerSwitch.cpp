/*
	File:		power/host/HostPowerSwitch.cpp

	Contains:	The host's power switch and backlight button
				(power/host/HostPowerSwitch.h).  Host only (not in the ROM).
*/

#include "power/host/HostPowerSwitch.h"
#include "power/PowerManager.h"
#include "hal/host/HostPower.h"
#include "hal/host/HostInterruptSources.h"
#include "hal/Timer.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"

#include <atomic>

static std::atomic<ULong>	gHostPowerPress(0);		// the press not yet sent ('powr or 'bklt; 0 none)


// A press waiting is due now.
static Boolean
PowerSwitchDeadline(Int64* when)
{
	if (gHostPowerPress.load() == 0 || GetPowerPort() == nil)
		return false;
	GetClock(when);
	return true;
}


// The press sent to the power manager, as the platform's interrupt does.
static void
PowerSwitchDeliver(void)
{
	ULong type = gHostPowerPress.exchange(0);
	if (type != 0)
		SendPowerSwitchEvent(type);
}


void
HostRegisterPowerSwitch(void)
{
	HostRegisterInterruptSource(PowerSwitchDeadline, PowerSwitchDeliver);
}


// Asleep, the switch wakes the machine (and the backlight button does
// nothing); awake, the press goes to the power manager.
void
HostPowerSwitchPress(ULong type)
{
	if (HostPowerAsleep())
	{
		if (type == kPowerSwitchEvent)
			HostPowerWake(kHostPowerEventSwitch);
		return;
	}
	gHostPowerPress.store(type);
}


static Ref
FHostPowerSwitch(RefArg /*rcvr*/)
{
	HostPowerSwitchPress(kPowerSwitchEvent);
	return NILREF;
}


static Ref
FHostBacklightButton(RefArg /*rcvr*/)
{
	HostPowerSwitchPress(kBacklightEvent);
	return NILREF;
}


// HostWakeAfter(ms): the next sleep ends after so long, as if the pen had
// tapped the window
static Ref
FHostWakeAfter(RefArg /*rcvr*/, RefArg milliseconds)
{
	HostPowerWakeAfter(ISINT(milliseconds) ? (ULong) RINT(milliseconds) : 1000);
	return NILREF;
}


static Ref
FHostSleepCount(RefArg /*rcvr*/)
{
	return MAKEINT(HostPowerSleeps());
}


void
HostInstallPowerGlobals(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPowerSwitch")), RefVar(MakeCFunction((void*) FHostPowerSwitch, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostBacklightButton")), RefVar(MakeCFunction((void*) FHostBacklightButton, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWakeAfter")), RefVar(MakeCFunction((void*) FHostWakeAfter, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSleepCount")), RefVar(MakeCFunction((void*) FHostSleepCount, 0, nil)));
}
