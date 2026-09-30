// comms/HostOptionLayouts.h: every option class the host constructs is
// listed, and each listing's fields add up to its class's size on the host
// (so a class that gained a pointer-sized field, or a new class, fails here
// rather than being misread from a script's bytes); an extended option's
// result (a NewtonErr, a C long - four bytes on Windows, eight elsewhere) is
// listed as 'l' and comes through as the error it is; and a script's 'siop'
// comes through with its speed, and goes back as the device's bytes.

#include "HostOptionLayouts.h"
#include "SerialOptions.h"
#include "MNPOptions.h"
#include "ModemNavigator.h"
#include "ModemOptions.h"
#include "CommToolOptions.h"
#include "CommAddresses.h"
#include "HALOptions.h"
#include "FaxOptions.h"
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


// An extended option: its service label and its result first, the result
// listed as 'l', and a device's result of -2 read as -2 on the host.
template <class T>
static void
CheckExtended(const char* name)
{
	Check<T>(name);
	T option;
	const HostOptionLayout* layout = HostOptionLayoutFor(option.Label());
	if (layout == nil)
		return;
	if (layout->fFields[0] != 'u' || layout->fFields[1] != 'l')
	{
		failures++;
		printf("FAIL: %s: an extended option's listing must start \"ul\" (the result a C long), not \"%s\"\n", name, layout->fFields);
		return;
	}
	size_t deviceSize = HostOptionLayoutSize(layout, false);
	TOption* device = (TOption*) NewPtrClear(sizeof(TOption) + deviceSize);
	device->SetLabel(option.Label());
	device->SetAsOption(option.Label());
	device->SetLength(deviceSize);
	UByte* bytes = (UByte*) (device + 1);
	bytes[4] = bytes[5] = bytes[6] = 0xff;
	bytes[7] = 0xfe;
	TOptionExtended* host = (TOptionExtended*) HostOptionFromDevice(device);
	EXPECT(host != nil);
	if (host != nil)
	{
		if (host->GetExtendedResult() != -2)
		{
			failures++;
			printf("FAIL: %s: a result of -2 came through as %ld\n", name, (long) host->GetExtendedResult());
		}
		DisposPtr((Ptr) host);		// (the device's option went with the rewriting)
	}
}

#define CHECK_EXTENDED(T) CheckExtended<T>(#T)


static void
Scenario(void)
{
	CHECK(TCMOToolSpecificOptions);
	CHECK(TCMOPassiveClaim);
	CHECK(TCMOPassiveState);
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
	CHECK(TCMOModemDialing);
	CHECK(TCMOModemECType);
	CHECK(TCMOModemConnectSpeed);
	CHECK(TCMOModemVoiceSupport);
	CHECK_EXTENDED(TCMOModemFaxCapabilities);
	CHECK_EXTENDED(TCMOModemFaxEnabledCaps);
	CHECK_EXTENDED(TCMOModemFaxClassesSupported);
	CHECK_EXTENDED(TCMOModemFaxClass);
	CHECK_EXTENDED(TCMOModemFaxClass1Cap);
	CHECK(TCMOFaxPageSetUp);
	CHECK(TCMOFaxPassThru);
	CHECK(TCMOFaxEnableProgressEvent);
	CHECK(TCMOFaxDirection);
	CHECK(TCMOFaxSessionInfo);
	CHECK(TCMOFaxMinScanLineTime);
	CHECK(TCMOFaxConfigSendBand);
	CHECK_EXTENDED(TCMOFaxStartPage);
	CHECK_EXTENDED(TCMOFaxEndMessage);
	CHECK(TCMOTAPIService);
	CHECK(TCMOTAPISpeaker);
	CHECK(TCMOHandsetManagement);
	CHECK(TCMOListenTimer);

	// a phone number address: two words, then its characters as they are
	{
		const char* digits = "5551212";
		TOption* device = (TOption*) NewPtrClear(sizeof(TOption) + 8 + 7);
		device->SetAsAddress(kCMARouteLabel);
		device->SetLength(8 + 7);
		UByte* data = (UByte*) (device + 1);
		data[3] = kPhoneNumber;
		data[7] = 7;
		memcpy(data + 8, digits, 7);
		TOption* host = HostOptionFromDevice(device);
		TCMAPhoneNumber* number = (TCMAPhoneNumber*) host;
		EXPECT(number->fType == kPhoneNumber);
		EXPECT(number->fPhoneLen == 7);
		EXPECT(memcmp(number + 1, digits, 7) == 0);
		UByte back[32];
		EXPECT(HostOptionToDevice(host, back, sizeof(back)) == 15);
		EXPECT(back[3] == kPhoneNumber && back[7] == 7 && memcmp(back + 8, digits, 7) == 0);
		DisposPtr((Ptr) host);
	}

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
