// Host unit test for the power manager (power/PowerManager.h): the 'pg&e
// world started over the host's battery driver (power/host/
// HostBatteryDriver.h, registered as the machine's own
// "PMainBatteryDriver"), and the batteries asked for by RPC as the
// scripts' natives ask - how many, a battery's status, what cells it
// holds; and the platform's sleep (hal/host/HostPower.h): a test's wake
// ends it with the power switch's bit, which the platform puts down to
// nothing in particular, and an alarm's bit to the alarm.
//
// The power manager is a world with a task of its own, so the test runs as
// the kernel services task of a booted OS.

#include "power/PowerManager.h"
#include "power/BatteryDriver.h"
#include "power/host/HostBatteryDriver.h"
#include "hal/Power.h"
#include "hal/host/HostPower.h"
#include "Boot.h"
#include "UserBoot.h"
#include "UserPorts.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static NewtonErr
Ask(ULong command, ULong which, ULong type, TPowerManagerEvent* reply, ULong size)
{
	TPowerManagerEvent request;
	request.fAEventClass = kNewtEventClass;
	request.fAEventID = kPowerManagerID;
	request.fCommand = command;
	request.fWhich = which;
	request.fType = type;
	ULong replySize = 0;
	return GetPowerPort()->SendRPC(&replySize, &request, size, reply, size);
}


static void
TestBatteries(void)
{
	TPowerManagerEvent reply;
	EXPECT(Ask(kPowerCmdCount, 0, 0, &reply, kPowerEventSize) == noErr);
	EXPECT(reply.fWhich == 1);

	EXPECT(Ask(kPowerCmdStatus, 0, 0, &reply, kPowerEventStatusSize) == noErr);
	EXPECT(reply.fCommand == noErr);
	EXPECT(reply.fStatus.fBatteryType == kBatteryAlkaline);
	EXPECT(reply.fStatus.fBatteryCapacity >= 0 && reply.fStatus.fBatteryCapacity <= 100);
	printf("test_PowerManager: battery at %ld%%, mains %ld\n", (long) reply.fStatus.fBatteryCapacity, (long) reply.fStatus.fACPower);

	// the cells told, and the next reading says so
	EXPECT(Ask(kPowerCmdSetType, 0, kBatteryNiMH, &reply, kPowerEventTypeSize) == noErr);
	EXPECT(reply.fWhich == noErr);
	EXPECT(Ask(kPowerCmdRawStatus, 0, 0, &reply, kPowerEventStatusSize) == noErr);
	EXPECT(reply.fStatus.fBatteryType == kBatteryNiMH);

	// there is no second battery
	EXPECT(Ask(kPowerCmdStatus, 1, 0, &reply, kPowerEventStatusSize) == noErr);
	EXPECT(reply.fCommand == (ULong) kError_Bad_Parameters);

	// commands 1 to 3 answer 100
	EXPECT(Ask(2, 0, 0, &reply, kPowerEventSize) == noErr);
	EXPECT(reply.fCommand == 100);
}


static void
TestSleep(void)
{
	ULong before = HostPowerSleeps();
	HostPowerWakeAfter(50);
	PlatformPowerOffSystem();
	EXPECT(HostPowerSleeps() == before + 1);
	EXPECT(PlatformPowerOnSystem() == 0);
	ULong event = PlatformPowerEvent();
	EXPECT(event == kHostPowerEventSwitch);
	EXPECT(TranslatePowerEvent(event) == kWokeBecause);
	EXPECT(PlatformPowerEvent() == 0);				// taken
	EXPECT(TranslatePowerEvent(kPowerEventAlarm) == kWokeAlarm);
	EXPECT(TranslatePowerEvent(kPowerEventSerialGPI) == kWokeSerialGPI);

	// with no window and no wake armed nothing could wake it: it does not
	// sleep, and says the switch was pressed
	PlatformPowerOffSystem();
	EXPECT(HostPowerSleeps() == before + 1);
	EXPECT(PlatformPowerEvent() == kHostPowerEventSwitch);
}


static void
PowerScenario(void)
{
	HostRegisterBatteryDriver();
	EXPECT(InitPowerManager() == noErr);
	EXPECT(GetPowerPort() != nil);
	EXPECT(GetBatteryDriver() != nil);
	if (GetPowerPort() != nil)
		TestBatteries();
	TestSleep();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = PowerScenario;
	OsBoot();
	if (failures == 0)
		printf("test_PowerManager: all passed\n");
	else
		printf("test_PowerManager: %d failures\n", failures);
	return failures != 0;
}
