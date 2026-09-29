// The IrDA stack (comms/irda/, serv 'irda') between two host IR chips in
// one process (A at 'infr' listening on a socket, B connecting to it and
// registered at 'tblt', named by the second endpoint's 'schp'): A listens;
// B discovers A, registers its class, looks A's class up in A's IAS
// database, connects to the LSAP it finds and sends a short message and a
// long one (more than a frame); A answers.  Both then read what was
// agreed (the receive buffers option: the data size and window) and B
// disconnects.

#include "Endpoint.h"
#include "CommManager.h"
#include "SerialTool.h"
#include "SharpIRTool.h"
#include "IrDATool.h"
#include "HostIRChip.h"
#include "HALOptions.h"
#include "FIQTimer.h"
#include "AppWorld.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "NewtonTime.h"
#include "host/TaskRuntime.h"

#include "hal/host/Host.h"

#include <stdio.h>
#include <string.h>
#include <atomic>

static int failures = 0;
static std::atomic<int> sDone(0);
static std::atomic<bool> sListening(false);
static std::atomic<bool> sReplied(false);
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char kHello[] = "hello, IrDA";
static UByte sLong[1500];
static ULong sAgreed[2][2];					// [listener, connector][data size, window]


static TEndpoint*
MakeEndpoint(ULong location)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('irda');
	options.AppendOption(&service);
	TCMOSerialHWChipLoc where;
	where.fHWLoc = location;
	if (location != kHWLocBuiltInIR)
		options.AppendOption(&where);
	TEndpoint* ep = nil;
	NewtonErr err = CMGetEndpoint(&options, &ep, false);
	EXPECT(err == noErr && ep != nil);
	if (err != noErr)
		printf("CMGetEndpoint: %ld\n", (long) err);
	return ep;
}


static void
ReceiveAll(TEndpoint* ep, UByte* buf, Size size)
{
	Size total = 0;
	while (total < size)
	{
		Size n = size - total;
		ULong flags = 0;
		NewtonErr err = ep->Rcv(buf + total, n, 1, &flags, 30 * kSeconds);
		if (err != noErr)
		{
			printf("Rcv after %ld of %ld: %ld\n", (long) total, (long) size, (long) err);
			failures++;
			return;
		}
		total += n;
	}
}


static void
Agreed(TEndpoint* ep, ULong* agreed)
{
	TOptionArray options;
	options.Init();
	TCMOIrDAReceiveBuffers buffers;
	buffers.SetOpCode(opGetCurrent);
	options.AppendOption(&buffers);
	EXPECT(ep->OptMgmt(opProcess, &options) == noErr);
	TCMOIrDAReceiveBuffers* answer = (TCMOIrDAReceiveBuffers*) options.OptionAt(0);
	if (answer != nil)
	{
		agreed[0] = answer->fSize;
		agreed[1] = answer->fCount;
	}
}


// A: listens, receives the two messages and answers
static void
Listener(void)
{
	TEndpoint* ep = MakeEndpoint(kHWLocBuiltInIR);
	if (ep == nil)
		return;
	EXPECT(ep->Open(0) == noErr);
	EXPECT(ep->nBind(nil, kNoTimeout, true) == noErr);
	sListening = true;
	NewtonErr err = ep->nListen(nil, nil, nil, 60 * kSeconds, true);
	EXPECT(err == noErr);
	if (err == noErr)
		EXPECT(ep->nAccept(ep, nil, nil, 0, kNoTimeout, true) == noErr && ep->GetState() == kDataXfer);
	else
		printf("listen: %ld\n", (long) err);
	Agreed(ep, sAgreed[0]);

	UByte buf[2000];
	memset(buf, 0, sizeof(buf));
	ReceiveAll(ep, buf, sizeof(kHello) - 1);
	EXPECT(memcmp(buf, kHello, sizeof(kHello) - 1) == 0);
	memset(buf, 0, sizeof(buf));
	ReceiveAll(ep, buf, sizeof(sLong));
	EXPECT(memcmp(buf, sLong, sizeof(sLong)) == 0);

	Size reply = 5;
	err = ep->Snd((UByte*) "thank", reply, 0, 30 * kSeconds);
	if (err != noErr)
		printf("reply: %ld\n", (long) err);
	EXPECT(err == noErr && reply == 5);
	sReplied = true;

	// the other side disconnects
	for (int i = 0; i < 1000 && ep->GetState() == kDataXfer; i++)
		Sleep(10 * kMilliseconds);
	ep->Disconnect();
	ep->UnBind();
	EXPECT(ep->Close() == noErr);
	ep->Delete();
}


// B: connects, sends the two messages, receives the answer, disconnects
static void
Connector(void)
{
	for (int i = 0; i < 500 && !sListening; i++)
		Sleep(10 * kMilliseconds);
	TEndpoint* ep = MakeEndpoint(kHWLocBuiltInExtra);
	if (ep == nil)
		return;
	EXPECT(ep->Open(0) == noErr);
	EXPECT(ep->nBind(nil, kNoTimeout, true) == noErr);
	NewtonErr err = ep->nConnect(nil, nil, nil, 60 * kSeconds, true);
	EXPECT(err == noErr && ep->GetState() == kDataXfer);
	if (err != noErr)
		printf("connect: %ld\n", (long) err);
	Agreed(ep, sAgreed[1]);

	Size n = sizeof(kHello) - 1;
	EXPECT(ep->Snd((UByte*) kHello, n, 0, 30 * kSeconds) == noErr && n == (Size) sizeof(kHello) - 1);
	n = sizeof(sLong);
	EXPECT(ep->Snd(sLong, n, 0, 30 * kSeconds) == noErr && n == (Size) sizeof(sLong));

	UByte buf[16];
	memset(buf, 0, sizeof(buf));
	ReceiveAll(ep, buf, 5);
	EXPECT(memcmp(buf, "thank", 5) == 0);

	for (int i = 0; i < 500 && !sReplied; i++)
		Sleep(10 * kMilliseconds);
	err = ep->Disconnect();
	if (err != noErr)
		printf("disconnect: %ld\n", (long) err);
	ep->UnBind();
	EXPECT(ep->Close() == noErr);
	ep->Delete();
}


class TListenWorld : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TListenWorld); }
	virtual void	TheMain()
	{
		EnableForking(false);
		Listener();
		if (++sDone == 2)
			HostStopTasks();
	}
};

class TConnectWorld : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TConnectWorld); }
	virtual void	TheMain()
	{
		EnableForking(false);
		Connector();
		if (++sDone == 2)
			HostStopTasks();
	}
};


static void
Boot(void)
{
	EXPECT(InitFIQTimer() == noErr);
	EXPECT(HostIRChipInstall("listen:0") == noErr);
	char peer[32];
	snprintf(peer, sizeof(peer), "127.0.0.1:%u", (unsigned) HostIRChipPort(HostIRChipInstalled()));
	TSerialChip* b = nil;
	EXPECT(HostIRChipMake(peer, &b) == noErr);
	EXPECT(GetSerialChipRegistry()->Register(b, kHWLocBuiltInExtra) == noErr);
	EXPECT(InitializeCommManager() == noErr);
	RegisterSerialCommServices();
	RegisterIRCommServices();
	static TListenWorld listener;
	static TConnectWorld connector;
	EXPECT(listener.Init('lstn', false, 0x4000) == noErr);
	EXPECT(connector.Init('conn', false, 0x4000) == noErr);
}


int
main()
{
	for (ULong i = 0; i < sizeof(sLong); i++)
		sLong[i] = (UByte) (i * 7 + 3);
	HostUseRealClock(true);
	gHostKernelServicesTask = Boot;
	OsBoot();
	EXPECT(sDone == 2);
	printf("agreed: listener data size %lu window %lu, connector data size %lu window %lu\n",
		(unsigned long) sAgreed[0][0], (unsigned long) sAgreed[0][1], (unsigned long) sAgreed[1][0], (unsigned long) sAgreed[1][1]);
	EXPECT(sAgreed[0][0] == sAgreed[1][0] && sAgreed[0][0] != 0);
	if (failures == 0)
		printf("test_IrDA: all passed\n");
	else
		printf("test_IrDA: %d failures\n", failures);
	return failures != 0;
}
