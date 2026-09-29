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
#include "CRC16.h"
#include "HostSerialChip.h"
#include "HostSockets.h"
#include "FIQTimer.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include "hal/host/Host.h"

#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

static int failures = 0;
static bool sScenarioDone = false;
static std::atomic<bool> sDesktopStop(false);
static std::atomic<long> sDesktopEchoed(0);
static unsigned short sPort = 0;
static std::mutex sLogLock;
static UByte sLog[4096];				// what the desktop has had, in order
static long sLogCount = 0;
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
		{
			std::lock_guard<std::mutex> lock(sLogLock);
			for (size_t i = 0; i < got && sLogCount < (long) sizeof(sLog); i++)
				sLog[sLogCount++] = buf[i];
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


// The framed tool ('fser): a frame goes out as SYN DLE STX, the data with
// its DLE doubled, DLE ETX and the CRC-16 (ARC) of the data and the ETX low
// byte first; the desktop's echo of it is taken apart again into the frame.
static void
FramedScenario(void)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('fser');
	options.AppendOption(&service);
	TEndpoint* ep = nil;
	EXPECT(CMGetEndpoint(&options, &ep, false) == noErr && ep != nil);
	if (ep == nil)
		return;
	EXPECT(ep->Open(0) == noErr);
	EXPECT(ep->Bind() == noErr);
	EXPECT(ep->Connect() == noErr && ep->GetState() == kDataXfer);
	long logStart;
	{
		std::lock_guard<std::mutex> lock(sLogLock);
		logStart = sLogCount;
	}

	UByte frame[] = { 'a', 0x10, 'b' };
	Size n = sizeof(frame);
	EXPECT(ep->Snd(frame, n, 2) == noErr && n == (Size) sizeof(frame));	// (2: a frame, the whole of it)
	UByte back[64];
	memset(back, 0, sizeof(back));
	n = sizeof(back);
	ULong flags = 2;
	NewtonErr err = ep->Rcv(back, n, 1, &flags);
	EXPECT(err == noErr && n == 3 && memcmp(back, frame, 3) == 0 && (flags & 1) == 0);

	TCRC16 crc;
	crc.Reset();
	crc.ComputeCRC('a');
	crc.ComputeCRC(0x10);
	crc.ComputeCRC('b');
	crc.ComputeCRC(0x03);
	crc.Get();
	UByte wire[] = { 0x16, 0x10, 0x02, 'a', 0x10, 0x10, 'b', 0x10, 0x03, crc.fResult[1], crc.fResult[0] };
	{
		std::lock_guard<std::mutex> lock(sLogLock);
		EXPECT(sLogCount - logStart == (long) sizeof(wire) && memcmp(sLog + logStart, wire, sizeof(wire)) == 0);
	}

	EXPECT(ep->Disconnect() == noErr);
	EXPECT(ep->UnBind() == noErr);
	EXPECT(ep->Close() == noErr);
	ep->Delete();
}


class TTestWorld : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TTestWorld); }
	virtual void	TheMain()
	{
		EnableForking(false);
		Scenario();
		FramedScenario();
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
	// the desktop end is another process on the host's own time: a clock
	// that jumps to the next deadline whenever the tasks are idle runs the
	// link's timers out while the peer is still sending (seen under load)
	HostUseRealClock(true);
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
