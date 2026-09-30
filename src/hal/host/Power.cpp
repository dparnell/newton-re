/*
	File:		hal/host/Power.cpp

	Contains:	The host's platform power (hal/Power.h, hal/host/HostPower.h):
				the machine asleep in PlatformPowerOffSystem until the power
				switch, a tap or a key, a real-time clock alarm or a test
				wakes it, and what woke it as the power event word.

				DEVIATION (hardware): a MessagePad is turned off, and turned
				on again by the Voyager's wake-up logic; the host waits.
*/

#include "hal/Power.h"
#include "hal/host/HostPower.h"
#include "hal/host/Host.h"
#include "hal/RealTimeClock.h"

#include <atomic>
#include <chrono>
#include <thread>

static std::atomic<ULong>	gHostPowerEvents(0);		// what has woken it (read and cleared by PlatformPowerEvent)
static std::atomic<bool>	gHostAsleep(false);
static std::atomic<ULong>	gHostSleeps(0);
static std::atomic<bool>	gHostPowerWindow(false);
static std::atomic<ULong>	gHostWakeAfter(0);			// a test's wake, in milliseconds (0: none)


void
HostPowerWake(ULong events)
{
	if (gHostAsleep.load())
		gHostPowerEvents.fetch_or(events);
}


Boolean
HostPowerAsleep(void)
{
	return gHostAsleep.load();
}


ULong
HostPowerSleeps(void)
{
	return gHostSleeps.load();
}


void
HostPowerWindowOpened(void)
{
	gHostPowerWindow.store(true);
}


void
HostPowerWakeAfter(ULong milliseconds)
{
	gHostWakeAfter.store(milliseconds != 0 ? milliseconds : 1);
}


// The machine off until something wakes it.  With no window and no test
// wake nothing could, so it does not sleep: the host's machine comes
// straight back as if the switch had been pressed at once (the word must
// say something woke it, or CyclePower would put it back to sleep).
// An alarm of the real-time clock falling due wakes it with the alarm bit,
// and CyclePower asks the clock whether that alarm wants it awake.
extern "C" void
PlatformPowerOffSystem(void)
{
	ULong wakeAfter = gHostWakeAfter.exchange(0);
	if (!gHostPowerWindow.load() && wakeAfter == 0)
	{
		gHostPowerEvents.fetch_or(kHostPowerEventSwitch);
		return;
	}
	gHostSleeps.fetch_add(1);
	gHostAsleep.store(true);
	auto start = std::chrono::steady_clock::now();
	for (;;)
	{
		if (gHostPowerEvents.load() != 0)
			break;
		if (gHostRTCAlarmArmed && GetRealTimeClock() >= gHostRTCAlarmSeconds)
		{
			gHostPowerEvents.fetch_or(kPowerEventAlarm);
			break;
		}
		if (wakeAfter != 0
		 && std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(wakeAfter))
		{
			gHostPowerEvents.fetch_or(kHostPowerEventSwitch);
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	gHostAsleep.store(false);
}


// Up again straight away.
extern "C" long
PlatformPowerOnSystem(void)
{
	gHostPoweredOff = false;
	return 0;
}


// What woke it, taken.
extern "C" ULong
PlatformPowerEvent(void)
{
	return gHostPowerEvents.exchange(0);
}


// ROM 0x0026ca40 TranslatePowerEvent__16TVoyagerPlatformFUl
// The power event word as a reason.  The interconnect and the card lock
// answer whatever the platform driver was told to call them (the fields
// RegisterPowerSwitchInterrupt and its like fill in), and with nothing
// there they fall through to the tests below them; so does a word with
// neither bit in it - the power switch's among them.
//
// NOT YET RECONSTRUCTED: the platform driver (TVoyagerPlatform) and the
// two registered reasons, so the fields are always empty here and the
// three plain tests are what is left.
extern "C" long
PlatformTranslatePowerEvent(ULong event)
{
	if ((event & kPowerEventSerialGPI) != 0)
		return kWokeSerialGPI;
	if ((event & kPowerEventAlarm) != 0)
		return kWokeAlarm;
	return kWokeBecause;
}
