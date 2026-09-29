// The async serial tool (comms/SerialTool.h) over the host's serial chip
// (hal/host/HostSerialChip.h): an endpoint for the 'aser service opened,
// bound (the external port claimed from the registry), connected (the chip
// turned on); a desktop on the other end of the chip's TCP socket echoes
// what it is sent, so "hello" goes out through the output buffer a byte at
// a time from the transmit interrupt and comes back through the receive
// interrupt and the input buffer; then a longer run that wraps both
// buffers; then disconnected, unbound and closed - the chip free again.

#include "Endpoint.h"
#include "SerialEndpoint.h"
#include "CommManager.h"
#include "SerialTool.h"
#include "HostSerialChip.h"
#include "HostSockets.h"
#include "FIQTimer.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <chrono>

static int failures = 0;
static bool sScenarioDone = false;
static std::atomic<bool> sDesktopStop(false);
static std::atomic<long> sDesktopEchoed(0);
static unsigned short sPort = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// The desktop: connects to the Newton's serial port and sends back all it
// gets.
static void
Desktop(void)
{
	int s = -1;
	for (int tries = 0; tries < 500 && s < 0 && !sDesktopStop; tries++)
	{
		if (HostTCPConnect(0x7F000001, sPort, &s) != kHostSocketOK)
		{
			s = -1;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	if (s < 0)
		return;
	UByte buf[512];
	while (!sDesktopStop)
	{
		size_t got = 0;
		int result = HostSocketReceive(s, buf, sizeof(buf), &got);
		if (result == kHostSocketClosed || result < 0)
			break;
		if (got == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			continue;
		}
		size_t off = 0;
		while (off < got)
		{
			size_t sent = 0;
			HostSocketSend(s, buf + off, got - off, &sent);
			off += sent;
		}
		sDesktopEchoed += got;
	}
	HostSocketClose(s);
}


static void
Scenario(void)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('aser');
	options.AppendOption(&service);

	TEndpoint* ep = nil;
	EXPECT(CMGetEndpoint(&options, &ep, false) == noErr && ep != nil);
	if (ep == nil)
		return;
	EXPECT(ep->Open(0) == noErr && ep->GetState() == kUnbnd);
	EXPECT(ep->Bind() == noErr && ep->GetState() == kIdle);		// (the external port claimed)
	EXPECT(ep->Connect() == noErr && ep->GetState() == kDataXfer);
	// the chip is the tool's (its interrupt handlers installed): its spec says so
	TCMOSerialChipSpec spec;
	GetSerialChipRegistry()->GetChipPtr(GetSerialChipRegistry()->FindByLocation(kHWLocExternalSerial))->ProcessOption(&spec);
	EXPECT(!spec.fChipNotInUse);

	UByte hello[] = "hello";
	Size n = 5;
	EXPECT(ep->Snd(hello, n, 0) == noErr && n == 5);
	UByte back[1200];
	memset(back, 0, sizeof(back));
	n = 5;
	ULong flags = 0;
	EXPECT(ep->Rcv(back, n, 5, &flags) == noErr && n == 5 && memcmp(back, "hello", 5) == 0);

	// more than either buffer holds (0x200 each): both wrap
	UByte run[1000];
	for (int i = 0; i < (int) sizeof(run); i++)
		run[i] = (UByte) (i * 7 + 3);
	n = sizeof(run);
	EXPECT(ep->Snd(run, n, 0) == noErr && n == (Size) sizeof(run));
	Size total = 0;
	for (int tries = 0; tries < 100 && total < (Size) sizeof(run); tries++)
	{
		n = sizeof(run) - total;
		flags = 0;
		NewtonErr err = ep->Rcv(back + total, n, 1, &flags);
		if (err != noErr)
			printf("Rcv after %ld bytes: %ld\n", (long) total, (long) err);
		EXPECT(err == noErr);
		if (err != noErr)
			break;
		total += n;
	}
	EXPECT(total == (Size) sizeof(run) && memcmp(back, run, sizeof(run)) == 0);
	EXPECT(sDesktopEchoed == 5 + (long) sizeof(run));

	EXPECT(ep->Disconnect() == noErr && ep->GetState() == kIdle);
	EXPECT(ep->UnBind() == noErr && ep->GetState() == kUnbnd);
	EXPECT(ep->Close() == noErr && ep->GetState() == kUninit);
	ep->Delete();
	TCMOSerialChipSpec after;
	GetSerialChipRegistry()->GetChipPtr(GetSerialChipRegistry()->FindByLocation(kHWLocExternalSerial))->ProcessOption(&after);
	EXPECT(after.fChipNotInUse);
}


class TTestWorld : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TTestWorld); }
	virtual void	TheMain()
	{
		EnableForking(false);
		Scenario();
		sScenarioDone = true;
		HostStopTasks();
	}
};


static std::thread sDesktop;

static void
Boot(void)
{
	EXPECT(InitFIQTimer() == noErr);
	EXPECT(HostSerialChipInstall(0) == noErr);
	sPort = HostSerialChipPort();
	EXPECT(sPort != 0);
	sDesktop = std::thread(Desktop);
	EXPECT(InitializeCommManager() == noErr);
	RegisterSerialCommServices();
	TTestWorld world;
	EXPECT(world.Init('test', false, 0x4000) == noErr);
}


int
main()
{
	gHostKernelServicesTask = Boot;
	OsBoot();
	sDesktopStop = true;
	if (sDesktop.joinable())
		sDesktop.join();
	EXPECT(sScenarioDone);
	if (failures == 0)
		printf("test_SerialTool: all passed\n");
	else
		printf("test_SerialTool: %d failures\n", failures);
	return failures != 0;
}
