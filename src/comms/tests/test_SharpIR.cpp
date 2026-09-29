// Sharp IR (comms/SharpIRTool.h, serv 'slir') between two host IR chips in
// one process (hal/host/HostIRChip.h: A at 'infr' listening on a socket, B
// connecting to it and registered at 'tblt', which the second endpoint names
// with 'schp'): one world listens and accepts, the other connects - the
// negotiation (an offer, the answer, the protocol agreed) - then data in
// packets: a short message (a stream: the get is not the end of a frame),
// a framed one (the last packet numbered 0xffff: the get sees the end of
// the frame), one of several packets (0x200 bytes each), and a framed
// reply the other way; then the statistics ('irst'), and both ends
// disconnected and closed.

#include "Endpoint.h"
#include "SerialEndpoint.h"
#include "CommManager.h"
#include "SerialTool.h"
#include "SharpIRTool.h"
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
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char kHello[] = "hello, beam";
static const char kFramed[] = "a framed one";
static UByte sLong[1500];
static TCMOSlowIRStats sListenerStats;


static TEndpoint*
MakeEndpoint(ULong location)
{
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('slir');
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
ReceiveAll(TEndpoint* ep, UByte* buf, Size size, ULong* flags)
{
	Size total = 0;
	while (total < size)
	{
		Size n = size - total;
		NewtonErr err = ep->Rcv(buf + total, n, 1, flags, 30 * kSeconds);
		if (err != noErr)
		{
			printf("Rcv after %ld of %ld: %ld\n", (long) total, (long) size, (long) err);
			failures++;
			return;
		}
		total += n;
	}
}


// A: listens, then receives the three messages and sends one back
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

	UByte buf[2000];
	ULong flags = 0;
	memset(buf, 0, sizeof(buf));
	ReceiveAll(ep, buf, sizeof(kHello) - 1, &flags);
	EXPECT(memcmp(buf, kHello, sizeof(kHello) - 1) == 0 && (flags & 1) != 0);	// (not the end of a frame)

	memset(buf, 0, sizeof(buf));
	flags = 2;
	Size n = sizeof(buf);
	err = ep->Rcv(buf, n, 1, &flags, 30 * kSeconds);
	EXPECT(err == noErr && n == (Size) sizeof(kFramed) - 1 && memcmp(buf, kFramed, n) == 0 && (flags & 1) == 0);

	memset(buf, 0, sizeof(buf));
	flags = 2;
	ReceiveAll(ep, buf, sizeof(sLong), &flags);
	EXPECT(memcmp(buf, sLong, sizeof(sLong)) == 0 && (flags & 1) == 0);

	Size reply = 5;
	err = ep->Snd((UByte*) "thank", reply, 2, 30 * kSeconds);
	if (err != noErr)
		printf("reply: %ld\n", (long) err);
	EXPECT(err == noErr && reply == 5);

	// the statistics: what came in (a get of the whole of the long message
	// is three packets: 0x200, 0x200, 0x1dc)
	TOptionArray stats;
	stats.Init();
	TCMOSlowIRStats get;
	get.SetOpCode(opGetCurrent);
	stats.AppendOption(&get);
	EXPECT(ep->OptMgmt(opProcess, &stats) == noErr);
	TCMOSlowIRStats* got = (TCMOSlowIRStats*) stats.OptionAt(0);
	if (got != nil)
		sListenerStats = *got;

	Sleep(2 * kSeconds);						// (the other side has its reply)
	ep->Disconnect();
	ep->UnBind();
	EXPECT(ep->Close() == noErr);
	ep->Delete();
}


// B: connects, sends the three messages, and receives the reply
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

	// what was agreed: the Newton's own protocol (both offered 7, the
	// answer's protocols its own and ours)
	TOptionArray protocol;
	protocol.Init();
	TCMOSlowIRProtocolType type;
	type.SetOpCode(opGetCurrent);
	protocol.AppendOption(&type);
	EXPECT(ep->OptMgmt(opProcess, &protocol) == noErr);
	TCMOSlowIRProtocolType* agreed = (TCMOSlowIRProtocolType*) protocol.OptionAt(0);
	EXPECT(agreed != nil && agreed->protocol == irUsingSeniorIR);
	if (agreed != nil)
		printf("agreed: protocol %lu options %lu\n", (unsigned long) agreed->protocol, (unsigned long) agreed->options);

	Size n = sizeof(kHello) - 1;
	EXPECT(ep->Snd((UByte*) kHello, n, 0, 30 * kSeconds) == noErr && n == (Size) sizeof(kHello) - 1);
	n = sizeof(kFramed) - 1;
	EXPECT(ep->Snd((UByte*) kFramed, n, 2, 30 * kSeconds) == noErr);
	n = sizeof(sLong);
	EXPECT(ep->Snd(sLong, n, 2, 30 * kSeconds) == noErr && n == (Size) sizeof(sLong));

	// (both ends number the packets with one counter, whichever way they
	// go, as the ROM's tool does: a reply after a stream put would be
	// refused as out of sequence, but the end of a frame - its last packet
	// numbered 0xffff - starts both again)
	UByte buf[16];
	memset(buf, 0, sizeof(buf));
	ULong flags = 2;
	ReceiveAll(ep, buf, 5, &flags);
	EXPECT(memcmp(buf, "thank", 5) == 0);

	ep->Disconnect();
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
	for (int i = 0; i < (int) sizeof(sLong); i++)
		sLong[i] = (UByte) (i * 13 + 5);
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
	HostUseRealClock(true);
	gHostKernelServicesTask = Boot;
	OsBoot();
	EXPECT(sDone == 2);
	printf("listener: %lu packets in, %lu checksum errors, %lu serial errors, %lu protocol errors\n",
		(unsigned long) sListenerStats.dataPacketsIn, (unsigned long) sListenerStats.checkSumErrs,
		(unsigned long) sListenerStats.serialErrs, (unsigned long) sListenerStats.protocolErrs);
	EXPECT(sListenerStats.dataPacketsIn == 1 + 1 + 3);
	if (failures == 0)
		printf("test_SharpIR: all passed\n");
	else
		printf("test_SharpIR: %d failures\n", failures);
	return failures != 0;
}
