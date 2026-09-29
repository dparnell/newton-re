// Endpoint test (comms/Endpoint.h, comms/SerialEndpoint.h) - milestone M2 of
// docs/comms/README.md: an endpoint got from the comm manager
// (CMGetEndpoint with the NIE's 'inet service option and its 'itrs remote
// socket - the endpoint a TSerialEndpoint over the host's TCP tool) opened,
// bound, connected to a TCP echo server this test runs, "hello" sent and
// received back through Snd and Rcv, then disconnected, unbound and
// closed - the synchronous calls, from an application world's task, as a
// client makes them.  The world does not fork (EnableForking(false)), so a
// synchronous call simply waits with the world's mutex given up.
//
// Also: the endpoint states along the way; a Connect with an address is
// refused (kEndpointErrNoAddresses); a Snd before the connect is
// kEndpointErrBadState; a Rcv with a threshold below the buffer answers
// what has come.

#include "Endpoint.h"
#include "SerialEndpoint.h"
#include "CommManager.h"
#include "HostServices.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include "EchoServer.h"

static int failures = 0;
static bool sScenarioDone = false;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
Scenario(void)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('inet');
	options.AppendOption(&service);
	TInetRemoteSocket remote(0x7F000001, sServerPort);
	options.AppendOption(&remote);

	TEndpoint* ep = nil;
	EXPECT(CMGetEndpoint(&options, &ep, false) == noErr && ep != nil);
	if (ep == nil)
		return;
	EXPECT(ep->GetState() == kUninit && ep->IsSync());

	UByte hello[] = "hello";
	Size n = 5;
	EXPECT(ep->Snd(hello, n, 0) == kEndpointErrBadState);

	EXPECT(ep->Open(0) == noErr && ep->GetState() == kUnbnd);
	EXPECT(ep->Bind() == noErr && ep->GetState() == kIdle);
	EXPECT(ep->Connect(&options) == -36001);				// no addresses
	EXPECT(ep->Connect() == noErr && ep->GetState() == kDataXfer);

	n = 5;
	EXPECT(ep->Snd(hello, n, 0) == noErr && n == 5);
	UByte back[8];
	memset(back, 0, sizeof(back));
	n = 5;
	ULong flags = 0;
	EXPECT(ep->Rcv(back, n, 5, &flags) == noErr && n == 5 && memcmp(back, "hello", 5) == 0);

	// a threshold below the buffer: what has come
	UByte two[] = "ok";
	n = 2;
	EXPECT(ep->Snd(two, n, 0) == noErr && n == 2);
	memset(back, 0, sizeof(back));
	n = sizeof(back);
	flags = 0;
	EXPECT(ep->Rcv(back, n, 1, &flags) == noErr && n >= 1 && back[0] == 'o');
	if (n == 1)
	{
		n = sizeof(back) - 1;
		EXPECT(ep->Rcv(back + 1, n, 1, &flags) == noErr && n == 1);
	}
	EXPECT(memcmp(back, "ok", 2) == 0);

	EXPECT(ep->Disconnect() == noErr && ep->GetState() == kIdle);
	EXPECT(ep->UnBind() == noErr && ep->GetState() == kUnbnd);
	EXPECT(ep->Close() == noErr && ep->GetState() == kUninit);
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
		sServerStop = true;
		sScenarioDone = true;
		HostStopTasks();
	}
};


static void
Boot(void)
{
	EXPECT(HostSocketsInit() == kHostSocketOK);
	EXPECT(HostTCPListen(0x7F000001, &sServerPort, &sServerListener) == kHostSocketOK);
	EXPECT(InitializeCommManager() == noErr);
	RegisterHostCommServices();
	TTestWorld world;
	EXPECT(world.Init('test', false, 0x4000) == noErr);
}


int
main()
{
	std::thread server(EchoServer);
	gHostKernelServicesTask = Boot;
	OsBoot();
	sServerStop = true;
	server.join();
	HostSocketClose(sServerListener);
	EXPECT(sScenarioDone);
	if (failures == 0)
		printf("test_Endpoint: all passed\n");
	else
		printf("test_Endpoint: %d failures\n", failures);
	return failures != 0;
}
