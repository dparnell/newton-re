// comms/HostOptionLayouts.h: every option class the host constructs is
// listed, and each listing's fields add up to its class's size on the host
// (so a class that gained a pointer-sized field, or a new class, fails here
// rather than being misread from a script's bytes); and a script's 'siop'
// comes through with its speed, and goes back as the device's bytes.

#include "HostOptionLayouts.h"
#include "SerialOptions.h"
#include "MNPOptions.h"
#include "ModemNavigator.h"
#include "HALOptions.h"
#include "NewtonMemory.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


template <class T>
static void
Check(const char* name)
{
	T option;
	const HostOptionLayout* layout = HostOptionLayoutFor(option.Label());
	if (layout == nil)
	{
		failures++;
		printf("FAIL: %s is not listed in HostOptionLayouts.cpp\n", name);
		return;
	}
	size_t size = HostOptionLayoutSize(layout, true);
	if (size != sizeof(T) - sizeof(TOption) || (size_t) option.Length() != size)
	{
		failures++;
		printf("FAIL: %s: its listing \"%s\" is %lu bytes on the host, the class %lu (length %ld)\n",
			name, layout->fFields, (unsigned long) size, (unsigned long) (sizeof(T) - sizeof(TOption)), (long) option.Length());
	}
}

#define CHECK(T) Check<T>(#T)


static void
Scenario(void)
{
	CHECK(TCMOSerialChipSpec);
	CHECK(TCMOSerialHWChipLoc);
	CHECK(TCMOSerialMiscConfig);
	CHECK(TCMOBreakFraming);
	CHECK(TCMOSerialEventEnables);
	CHECK(TCMOSerialIOStats);
	CHECK(TCMOInputFlowControlParms);
	CHECK(TCMOOutputFlowControlParms);
	CHECK(TCMOSerialHardware);
	CHECK(TCMOSerialBuffers);
	CHECK(TCMOSerialIOParms);
	CHECK(TCMOSerialBitRate);
	CHECK(TCMOSerialHalfDuplex);
	CHECK(TCMOSerialDTRControl);
	CHECK(THMOHiSpeedClockOption);
	CHECK(TCMOSerialBytesAvailable);
	CHECK(TCMOFramingParms);
	CHECK(TCMOFramedAsyncStats);
	CHECK(TCMOMNPAllocate);
	CHECK(TCMOMNPCompression);
	CHECK(TCMOMNPDataRate);
	CHECK(TCMOMNPSpeedNegotiation);
	CHECK(TCMOMNPStatistics);
	CHECK(TCMOMNPDebugConnect);
	CHECK(TCMOModemPrefs);
	CHECK(TCMOModemConnectType);

	// the Connection application's 'siop': [0, 0, 8, 38400] as the device's
	// four big-endian words
	const size_t deviceSize = 16;
	TOption* device = (TOption*) NewPtrClear(sizeof(TOption) + deviceSize);
	device->SetLabel(kCMOSerialIOParms);
	device->SetAsOption(kCMOSerialIOParms);
	device->SetLength(deviceSize);
	UByte* bytes = (UByte*) (device + 1);
	bytes[11] = 8;
	bytes[14] = 0x96;
	TCMOSerialIOParms* host = (TCMOSerialIOParms*) HostOptionFromDevice(device);
	EXPECT(host != nil);
	if (host != nil)
	{
		EXPECT(host->fStopBits == 0 && host->fParity == 0 && host->fDataBits == 8 && host->fSpeed == 38400);
		EXPECT((size_t) host->Length() == sizeof(TCMOSerialIOParms) - sizeof(TOption));
		UByte back[32];
		memset(back, 0xff, sizeof(back));
		EXPECT(HostOptionToDevice(host, back, sizeof(back)) == 16);
		UByte expected[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,8, 0,0,0x96,0 };
		EXPECT(memcmp(back, expected, 16) == 0);
		DisposPtr((Ptr) host);
	}
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;		// (a heap to allocate options in)
	OsBoot();
	if (failures == 0)
		printf("test_HostOptionLayouts: all passed\n");
	else
		printf("test_HostOptionLayouts: %d failures\n", failures);
	return failures != 0;
}
