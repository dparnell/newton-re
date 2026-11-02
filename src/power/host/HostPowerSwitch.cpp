/*
	File:		power/host/HostPowerSwitch.cpp

	Contains:	The host's power switch and backlight button
				(power/host/HostPowerSwitch.h).  Host only (not in the ROM).
*/

#include "power/host/HostPowerSwitch.h"
#include "power/PowerManager.h"
#include "power/host/HostBattery.h"
#include "hal/host/HostPower.h"
#include "hal/host/HostInterruptSources.h"
#include "hal/host/HostIdleWait.h"
#include "hal/Timer.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"

#include <atomic>

static std::atomic<ULong>	gHostPowerPress(0);		// the press not yet sent ('powr or 'bklt; 0 none)


// A press waiting is due now: time nought, long past.  (It was the clock
// read here, which can be later than the now HostDeliverInterruptSources
// read before asking, and so not yet due there - the kicks of
// os600/user/host/HostKick.cpp were never delivered that way.)
static Boolean
PowerSwitchDeadline(Int64* when)
{
	if (gHostPowerPress.load() == 0 || GetPowerPort() == nil)
		return false;
	when->hi = 0;
	when->lo = 0;
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
	HostIdleWake();
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


// a slot of HostSetBattery's frame: an integer, or not said
static long
IntegerSlot(RefArg frame, const char* name, long otherwise)
{
	RefVar value(GetFrameSlot(frame, RefVar(Intern((char*) name))));
	return ISINT(value) ? (long) RINT(value) : otherwise;
}


// HostSetBattery(frame): the host's battery pinned, as the battery driver
// will read it (power/host/HostBattery.h) - {percent, mains (true or nil),
// charge ('charging, 'full or nil: discharging), millivolts, milliamps
// (+ into it, - drawn from it), tenthsCelsius} - charging or full, it is
// on the mains; HostSetBattery('none) a host with no battery;
// HostSetBattery(nil) the host's own again
static Ref
FHostSetBattery(RefArg /*rcvr*/, RefArg frame)
{
	if (ISNIL(frame))
		HostBatteryPin(nullptr);
	else if (IsSymbol(frame))
	{
		HostBatteryReading none = { 100, kHostBatteryUnknown, kHostBatteryUnknown, kHostBatteryUnknown, 0, kHostBatteryDischarging };
		HostBatteryPin(&none, false);
	}
	else
	{
		HostBatteryReading reading;
		reading.percent = IntegerSlot(frame, "percent", 100);
		reading.millivolts = IntegerSlot(frame, "millivolts", kHostBatteryUnknown);
		reading.milliamps = IntegerSlot(frame, "milliamps", kHostBatteryUnknown);
		reading.tenthsCelsius = IntegerSlot(frame, "tenthsCelsius", kHostBatteryUnknown);
		reading.mains = NOTNIL(GetFrameSlot(frame, RefVar(Intern((char*) "mains")))) ? 1 : 0;
		RefVar charge(GetFrameSlot(frame, RefVar(Intern((char*) "charge"))));
		reading.charge = kHostBatteryDischarging;
		if (IsSymbol(charge) && symcmp(SymbolName(charge), (char*) "charging") == 0)
			reading.charge = kHostBatteryCharging;
		else if (IsSymbol(charge) && symcmp(SymbolName(charge), (char*) "full") == 0)
			reading.charge = kHostBatteryFull;
		if (reading.charge != kHostBatteryDischarging)
			reading.mains = 1;							// (charging, it is on the mains)
		HostBatteryPin(&reading);
	}
	return NILREF;
}


void
HostInstallPowerGlobals(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPowerSwitch")), RefVar(MakeCFunction((void*) FHostPowerSwitch, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostBacklightButton")), RefVar(MakeCFunction((void*) FHostBacklightButton, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWakeAfter")), RefVar(MakeCFunction((void*) FHostWakeAfter, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSleepCount")), RefVar(MakeCFunction((void*) FHostSleepCount, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSetBattery")), RefVar(MakeCFunction((void*) FHostSetBattery, 1, nil)));
}
