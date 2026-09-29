// The serial chip registry (hal/HALSerialChip.h: PTheSerChipRegistry) and the
// host's serial port over a TCP socket (hal/host/HostSerialChip.h), driven
// as a serial tool drives a chip: found by its location, claimed with the
// tool's interrupt handlers, bytes from a "desktop" (a TCP client) read in
// the receive interrupt and bytes put in the transmit-empty interrupt
// reaching it.  Runs as the kernel services task (the registry names its
// chips with the name server).

#include "HALSerialChip.h"
#include "HostSerialChip.h"
#include "HostSockets.h"
#include "NewtonTime.h"
#include "UserTasks.h"
#include "HALOptions.h"
#include "Options.h"
#include "SerialOptions.h"
#include "Boot.h"
#include "UserBoot.h"
#include "NewtonTime.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the "tool": what its interrupts saw
struct FakeTool
{
	TSerialChip*	chip;
	char			received[64];
	long			receivedCount;
	const char*		toSend;
	long			sent;
	long			statusInts;
};

static void
RxInt(void* p)
{
	FakeTool* tool = (FakeTool*) p;
	while (tool->chip->RxBufFull())
	{
		UByte b = tool->chip->GetByte();
		if (tool->receivedCount < (long) sizeof(tool->received) - 1)
			tool->received[tool->receivedCount++] = b;
	}
}

static void
TxInt(void* p)
{
	FakeTool* tool = (FakeTool*) p;
	if (tool->toSend != nil && tool->toSend[tool->sent] != 0)
		tool->chip->PutByte(tool->toSend[tool->sent++]);
	else
		tool->chip->ResetTxBEmpty();
}

static void
StatusInt(void* p)
{
	((FakeTool*) p)->statusInts++;
}

static void
SpecialInt(void*)
{ }


static void
Scenario(void)
{
	EXPECT(HostSerialChipInstall(0) == noErr);
	PSerialChipRegistry* registry = GetSerialChipRegistry();
	EXPECT(registry != nil);
	unsigned short port = HostSerialChipPort();
	EXPECT(port != 0);

	// the registry: the chip by its location, and back
	SerialChipID id = registry->FindByLocation(kHWLocExternalSerial);
	EXPECT(id == 0x81);							// (the first free slot after 0)
	TSerialChip* chip = registry->GetChipPtr(id);
	EXPECT(chip != nil);
	EXPECT(registry->FindByChip(chip) == id);
	EXPECT(registry->GetChipLocation(id) == kHWLocExternalSerial);
	EXPECT(registry->FindByLocation('infr') == kNilSerChipID);
	TCMOSerialChipSpec wanted;
	wanted.fHWLoc = kHWLocExternalSerial;
	EXPECT(registry->FindByOption(&wanted) == id);
	wanted.fHWLoc = 'infr';
	EXPECT(registry->FindByOption(&wanted) == kNilSerChipID);
	ULong location = kHWLocExternalSerial;
	EXPECT(registry->SetDefaultChip('mnps', &location, false) == noErr);
	location = 0;
	EXPECT(registry->GetDefaultChip('mnps', &location) == noErr && location == kHWLocExternalSerial);

	// the tool takes the chip
	static FakeTool tool;
	memset(&tool, 0, sizeof(tool));
	tool.chip = chip;
	SCCChannelInts handlers = { TxInt, StatusInt, RxInt, SpecialInt };
	EXPECT(chip->InstallChipHandler(&tool, &handlers) == noErr);
	EXPECT(chip->InstallChipHandler(&tool, &handlers) != noErr);		// (once)
	chip->SetInterruptEnable(true);
	EXPECT((chip->GetSerialStatus() & kSerialDCDAsserted) == 0);

	// a desktop plugs in and sends
	int desktop = -1;
	EXPECT(HostTCPConnect(0x7F000001, port, &desktop) == kHostSocketOK);
	for (int i = 0; i < 100 && HostSocketConnected(desktop) == kHostSocketWouldBlock; i++)
		Sleep(10 * kMilliseconds);
	size_t count = 0;
	EXPECT(HostSocketSend(desktop, "hello", 5, &count) == kHostSocketOK && count == 5);
	for (int i = 0; i < 100 && tool.receivedCount < 5; i++)
		Sleep(10 * kMilliseconds);
	EXPECT(tool.receivedCount == 5 && memcmp(tool.received, "hello", 5) == 0);
	EXPECT(tool.statusInts >= 1);
	EXPECT((chip->GetSerialStatus() & kSerialDCDAsserted) != 0);

	// the tool sends: the first byte put, the rest in the transmit interrupt
	tool.toSend = "newtdock";
	tool.sent = 1;
	chip->PutByte('n');
	char back[16];
	size_t got = 0;
	for (int i = 0; i < 100 && got < 8; i++)
	{
		size_t n = 0;
		HostSocketReceive(desktop, back + got, sizeof(back) - got, &n);
		got += n;
		if (got < 8)
			Sleep(10 * kMilliseconds);
	}
	EXPECT(got == 8 && memcmp(back, "newtdock", 8) == 0);
	EXPECT(chip->AllSent());

	// the desktop goes: DCD falls
	HostSocketClose(desktop);
	long before = tool.statusInts;
	for (int i = 0; i < 100 && tool.statusInts == before; i++)
		Sleep(10 * kMilliseconds);
	EXPECT(tool.statusInts > before);
	EXPECT((chip->GetSerialStatus() & kSerialDCDAsserted) == 0);

	EXPECT(chip->RemoveChipHandler(&tool) == noErr);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	printf("test_HostSerialChip: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
