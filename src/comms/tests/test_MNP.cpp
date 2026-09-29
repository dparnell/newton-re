// MNP (comms/MNP.h) over the host's serial chip: an 'mnps endpoint
// connects (the Newton the originator, as it is when it docks) to the
// desktop end in tools/dock/mnp.py, which ctest runs this program under
// (mnp.py --spawn test_MNP --echo): the link negotiated, data sent in LT
// frames and echoed back - a short message and one of several frames -
// then disconnected.  This program prints "port N" once its serial port
// listens, for mnp.py to connect to.

#include "Endpoint.h"
#include "SerialEndpoint.h"
#include "CommManager.h"
#include "MNP.h"
#include "HostSerialChip.h"
#include "FIQTimer.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static bool sScenarioDone = false;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); } } while (0)


// All of n bytes back (the echo may come in several gets).
static NewtonErr
ReceiveAll(TEndpoint* ep, UByte* buf, Size n)
{
	Size total = 0;
	for (int tries = 0; tries < 200 && total < n; tries++)
	{
		Size got = n - total;
		ULong flags = 0;
		NewtonErr err = ep->Rcv(buf + total, got, 1, &flags);
		if (err != noErr)
		{
			printf("Rcv after %ld bytes: %ld\n", (long) total, (long) err);
			return err;
		}
		total += got;
	}
	return total == n ? noErr : -1;
}


static void
Scenario(void)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('mnps');
	options.AppendOption(&service);

	TEndpoint* ep = nil;
	EXPECT(CMGetEndpoint(&options, &ep, false) == noErr && ep != nil);
	if (ep == nil)
		return;
	EXPECT(ep->Open(0) == noErr);
	EXPECT(ep->Bind() == noErr);
	NewtonErr err = ep->Connect();
	if (err != noErr)
		printf("Connect: %ld\n", (long) err);
	EXPECT(err == noErr && ep->GetState() == kDataXfer);
	if (err == noErr)
	{
		UByte hello[] = "hello, desktop";
		Size n = sizeof(hello) - 1;
		EXPECT(ep->Snd(hello, n, 0) == noErr && n == (Size) sizeof(hello) - 1);
		UByte back[2000];
		memset(back, 0, sizeof(back));
		EXPECT(ReceiveAll(ep, back, sizeof(hello) - 1) == noErr && memcmp(back, hello, sizeof(hello) - 1) == 0);

		// several frames' worth, a DLE among them
		UByte run[1500];
		for (int i = 0; i < (int) sizeof(run); i++)
			run[i] = (UByte) (i * 13 + 7);
		run[100] = 0x10;
		n = sizeof(run);
		EXPECT(ep->Snd(run, n, 0) == noErr && n == (Size) sizeof(run));
		memset(back, 0, sizeof(back));
		EXPECT(ReceiveAll(ep, back, sizeof(run)) == noErr && memcmp(back, run, sizeof(run)) == 0);
		EXPECT(ep->Disconnect() == noErr);
	}
	ep->UnBind();
	ep->Close();
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
		sScenarioDone = true;
		HostStopTasks();
	}
};


static void
Boot(void)
{
	EXPECT(InitFIQTimer() == noErr);
	EXPECT(HostSerialChipInstall(0) == noErr);
	printf("port %u\n", (unsigned) HostSerialChipPort());
	fflush(stdout);
	EXPECT(InitializeCommManager() == noErr);
	RegisterSerialCommServices();
	RegisterMNPService();
	TTestWorld world;
	EXPECT(world.Init('test', false, 0x4000) == noErr);
}


int
main()
{
	gHostKernelServicesTask = Boot;
	OsBoot();
	EXPECT(sScenarioDone);
	if (failures == 0)
		printf("test_MNP: all passed\n");
	else
		printf("test_MNP: %d failures\n", failures);
	fflush(stdout);
	return failures != 0;
}
