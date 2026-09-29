// The IR probe (comms/IrProbeTool.h, serv 'pkir') between two host IR chips
// in one process (A at 'infr' listening on a socket, B connecting to it and
// registered at 'tblt', named by the second endpoint's 'schp'), in two
// rounds:
//   1. a probe listening and a probe connecting: the TEST frame is echoed,
//      and both answer IrDA (8) - two 2.1 MessagePads;
//   2. Sharp IR listening and a probe connecting: the TEST frames go
//      unheard, the probe's Sharp offer is answered, and the probe answers
//      what the answer says (7, the Newton's protocols) - an older Newton.
// The answer is each endpoint's 'irpt' option, as the beamer reads it.

#include "Endpoint.h"
#include "CommManager.h"
#include "SerialTool.h"
#include "SharpIRTool.h"
#include "IrProbeTool.h"
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
static std::atomic<int> sListening(0);		// the round the listener is listening in
static std::atomic<int> sFinished(0);		// ends of rounds, both sides
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ULong sAnswer[2][2];					// [round][listener, connector]


static TEndpoint*
MakeEndpoint(ULong service, ULong location)
{
	TOptionArray options;
	options.Init();
	TOption serviceOption;
	serviceOption.SetAsService(service);
	options.AppendOption(&serviceOption);
	TCMOSerialHWChipLoc where;
	where.fHWLoc = location;
	if (location != kHWLocBuiltInIR)
		options.AppendOption(&where);
	TEndpoint* ep = nil;
	NewtonErr err = CMGetEndpoint(&options, &ep, false);
	EXPECT(err == noErr && ep != nil);
	return ep;
}


static ULong
Protocol(TEndpoint* ep)
{
	TOptionArray options;
	options.Init();
	TCMOSlowIRProtocolType type;
	type.SetOpCode(opGetCurrent);
	options.AppendOption(&type);
	if (ep->OptMgmt(opProcess, &options) != noErr)
		return 0xdead;
	TCMOSlowIRProtocolType* answer = (TCMOSlowIRProtocolType*) options.OptionAt(0);
	return answer != nil ? answer->protocol : 0xdead;
}


static void
Close(TEndpoint* ep)
{
	ep->Disconnect();
	ep->UnBind();
	ep->Close();
	ep->Delete();
}


static void
WaitForRound(int finished)
{
	for (int i = 0; i < 3000 && sFinished < finished; i++)
		Sleep(10 * kMilliseconds);
}


static void
Listener(void)
{
	for (int round = 0; round < 2; round++)
	{
		TEndpoint* ep = MakeEndpoint(round == 0 ? 'pkir' : 'slir', kHWLocBuiltInIR);
		if (ep == nil)
			return;
		EXPECT(ep->Open(0) == noErr);
		EXPECT(ep->nBind(nil, kNoTimeout, true) == noErr);
		sListening = round + 1;
		NewtonErr err = ep->nListen(nil, nil, nil, 60 * kSeconds, true);
		if (err == noErr)
			err = ep->nAccept(ep, nil, nil, 0, kNoTimeout, true);
		if (err != noErr)
			printf("round %d: listen: %ld\n", round + 1, (long) err);
		EXPECT(err == noErr);
		sAnswer[round][0] = Protocol(ep);
		Sleep(500 * kMilliseconds);			// (the connector has its answer)
		Close(ep);
		sFinished++;
		WaitForRound(2 * (round + 1));
	}
}


static void
Connector(void)
{
	for (int round = 0; round < 2; round++)
	{
		for (int i = 0; i < 500 && sListening < round + 1; i++)
			Sleep(10 * kMilliseconds);
		TEndpoint* ep = MakeEndpoint('pkir', kHWLocBuiltInExtra);
		if (ep == nil)
			return;
		EXPECT(ep->Open(0) == noErr);
		EXPECT(ep->nBind(nil, kNoTimeout, true) == noErr);
		NewtonErr err = ep->nConnect(nil, nil, nil, 60 * kSeconds, true);
		if (err != noErr)
			printf("round %d: connect: %ld\n", round + 1, (long) err);
		EXPECT(err == noErr);
		sAnswer[round][1] = Protocol(ep);
		Close(ep);
		sFinished++;
		WaitForRound(2 * (round + 1));
	}
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
	HostUseRealClock(true);
	gHostKernelServicesTask = Boot;
	OsBoot();
	EXPECT(sDone == 2);
	for (int round = 0; round < 2; round++)
		printf("round %d: the listener answers %lu, the connector %lu\n", round + 1,
			(unsigned long) sAnswer[round][0], (unsigned long) sAnswer[round][1]);
	EXPECT(sAnswer[0][0] == irUsingIrDA && sAnswer[0][1] == irUsingIrDA);
	EXPECT(sAnswer[1][1] == 7);
	if (failures == 0)
		printf("test_IrProbe: all passed\n");
	else
		printf("test_IrProbe: %d failures\n", failures);
	return failures != 0;
}
