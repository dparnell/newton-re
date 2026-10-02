// The host's IR port (hal/host/HostIRChip.h): two chips in one process, one
// listening and one connecting, driven as the IR tools drive a chip - a
// byte sent on one heard on the other only in the modulation it listens
// for (Sharp's ASK or IrDA, the 'irlk' option), or either in auto-receive
// with the status saying which; nothing heard while the receiver is turned
// off for output (half duplex).  Then three chips on the LAN medium (a
// multicast group on the loopback interface): what one sends the others
// hear, never itself; once two have heard each other they face each other
// and the third hears neither; five seconds' silence and they face nobody.
// Runs as the kernel services task.

#include "HALSerialChip.h"
#include "HostIRChip.h"
#include "HALOptions.h"
#include "Options.h"
#include "SerialOptions.h"
#include "NewtonTime.h"
#include "hal/Timer.h"
#include "UserTasks.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct FakeTool
{
	TSerialChip*	chip;
	char			received[64];
	long			receivedCount;
	const char*		toSend;
	long			sent;
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

static void StatusInt(void*) { }
static void SpecialInt(void*) { }


static void
Send(FakeTool* tool, const char* text)
{
	tool->toSend = text;
	tool->sent = 1;
	tool->chip->PutByte(text[0]);
}


// what the other side heard within a while (and cleared for the next)
static bool
Heard(FakeTool* tool, const char* text)
{
	long n = (long) strlen(text);
	for (int i = 0; i < 100 && tool->receivedCount < n; i++)
		Sleep(10 * kMilliseconds);
	Sleep(50 * kMilliseconds);					// (and nothing more)
	bool ok = tool->receivedCount == n && memcmp(tool->received, text, n) == 0;
	if (!ok)
		printf("heard %ld bytes \"%.*s\", not \"%s\"\n", tool->receivedCount, (int) tool->receivedCount, tool->received, text);
	tool->receivedCount = 0;
	return ok;
}


static NewtonErr
SetLink(TSerialChip* chip, UByte mode, UByte flags)
{
	THMOSerIRLinkConfig link;
	link.fIRLinkMode = mode;
	link.fConfigFlags = flags;
	link.SetOpCode(opSetRequired);
	return chip->ProcessOption(&link);
}


static UByte
LinkStatus(TSerialChip* chip)
{
	THMOSerIRLinkConfig link;
	link.fStatus = 0xff;
	link.SetOpCode(opGetCurrent);
	chip->ProcessOption(&link);
	return link.fStatus;
}


static void
LanScenario(void)
{
	// a port of the test's own (another run of it at the same time is
	// unlikely to pick the same)
	Int64 clock;
	GetClock(&clock);
	char lan[48];
	snprintf(lan, sizeof(lan), "lan:%lu@127.0.0.1", 40000 + (unsigned long) (clock.lo % 20000));
	TSerialChip* chips[3] = { nil, nil, nil };
	for (int i = 0; i < 3; i++)
		EXPECT(HostIRChipMake(lan, &chips[i]) == noErr && chips[i] != nil);
	if (chips[0] == nil || chips[1] == nil || chips[2] == nil)
	{
		printf("no LAN medium here: %s\n", lan);
		return;
	}
	EXPECT(HostIRChipConnected(chips[0]));
	TSerialChip* bad = nil;
	EXPECT(HostIRChipMake("lan:70000", &bad) != noErr && bad == nil);		// (no such port)

	static FakeTool tools[3];
	SCCChannelInts handlers = { TxInt, StatusInt, RxInt, SpecialInt };
	for (int i = 0; i < 3; i++)
	{
		memset(&tools[i], 0, sizeof(tools[i]));
		tools[i].chip = chips[i];
		EXPECT(chips[i]->InstallChipHandler(&tools[i], &handlers) == noErr);
		chips[i]->SetInterruptEnable(true);
		chips[i]->SetSpeed(38400);
	}
	FakeTool* a = &tools[0];
	FakeTool* b = &tools[1];
	FakeTool* c = &tools[2];

	// A to whoever is there: B and C both hear it, A not its own
	Send(a, "hello");
	EXPECT(Heard(b, "hello"));
	EXPECT(Heard(c, "hello"));
	EXPECT(Heard(a, ""));
	// B answers: A hears it and now faces B; C, facing A, does not hear B
	Send(b, "here");
	EXPECT(Heard(a, "here"));
	EXPECT(Heard(c, ""));
	// what A sends now goes to B alone
	Send(a, "to b");
	EXPECT(Heard(b, "to b"));
	EXPECT(Heard(c, ""));
	// the modulation still counts: B listening for IrDA does not hear ASK
	EXPECT(SetLink(chips[1], kSerIRLink_IRDA_3_16, 0) == noErr);
	Send(a, "ask");
	EXPECT(Heard(b, ""));
	EXPECT(SetLink(chips[1], kSerIRLink_SharpIR, 0) == noErr);
	// five seconds without a word: nobody faced, and A is heard by both
	Sleep(5500 * kMilliseconds);
	Send(a, "again");
	EXPECT(Heard(b, "again"));
	EXPECT(Heard(c, "again"));

	for (int i = 0; i < 3; i++)
	{
		EXPECT(chips[i]->RemoveChipHandler(&tools[i]) == noErr);
		chips[i]->Delete();
	}
}


static void
Scenario(void)
{
	TSerialChip* a = nil;
	TSerialChip* b = nil;
	EXPECT(HostIRChipMake("listen:0", &a) == noErr && a != nil);
	unsigned short port = HostIRChipPort(a);
	EXPECT(port != 0);
	char peer[32];
	snprintf(peer, sizeof(peer), "127.0.0.1:%u", (unsigned) port);
	EXPECT(HostIRChipMake(peer, &b) == noErr && b != nil);
	TSerialChip* none = nil;
	EXPECT(HostIRChipMake("nowhere", &none) != noErr && none == nil);

	// the two find each other (the interrupt source polls them)
	for (int i = 0; i < 200 && !(HostIRChipConnected(a) && HostIRChipConnected(b)); i++)
		Sleep(10 * kMilliseconds);
	EXPECT(HostIRChipConnected(a) && HostIRChipConnected(b));

	// what it is
	TCMOSerialChipSpec spec;
	spec.SetOpCode(opGetCurrent);
	EXPECT(a->ProcessOption(&spec) == noErr && spec.fHWLoc == kHWLocBuiltInIR && spec.fChipNotInUse);

	static FakeTool toolA, toolB;
	memset(&toolA, 0, sizeof(toolA));
	memset(&toolB, 0, sizeof(toolB));
	toolA.chip = a;
	toolB.chip = b;
	SCCChannelInts handlers = { TxInt, StatusInt, RxInt, SpecialInt };
	EXPECT(a->InstallChipHandler(&toolA, &handlers) == noErr);
	EXPECT(b->InstallChipHandler(&toolB, &handlers) == noErr);
	a->SetInterruptEnable(true);
	b->SetInterruptEnable(true);
	a->SetSpeed(38400);
	b->SetSpeed(38400);

	// both in Sharp's ASK (the default): heard, either way
	Send(&toolA, "sharp");
	EXPECT(Heard(&toolB, "sharp"));
	Send(&toolB, "back");
	EXPECT(Heard(&toolA, "back"));

	// B listening for IrDA: A's ASK is not heard; A's IrDA is
	EXPECT(SetLink(b, kSerIRLink_IRDA_3_16, 0) == noErr);
	Send(&toolA, "lost");
	EXPECT(Heard(&toolB, ""));
	EXPECT(SetLink(a, kSerIRLink_IRDA_3_16, 0) == noErr);
	Send(&toolA, "irda");
	EXPECT(Heard(&toolB, "irda"));
	EXPECT(SetLink(b, 7, 0) != noErr);				// (no such mode)

	// B in auto-receive: both heard, the status saying which came last
	EXPECT(SetLink(b, kSerIRLink_SharpIR, kSerIRLinkCfg_AutoRx) == noErr);
	Send(&toolA, "x");
	EXPECT(Heard(&toolB, "x"));
	EXPECT(LinkStatus(b) == kSerIRLinkSts_IRDADetect);
	EXPECT(SetLink(a, kSerIRLink_SharpIR, 0) == noErr);
	Send(&toolA, "y");
	EXPECT(Heard(&toolB, "y"));
	EXPECT(LinkStatus(b) == 0);

	// half duplex: B's receiver off while it transmits
	b->ConfigureForOutput(true);
	Send(&toolA, "deaf");
	EXPECT(Heard(&toolB, ""));
	b->ConfigureForOutput(false);
	Send(&toolA, "ok");
	EXPECT(Heard(&toolB, "ok"));

	// nobody claiming the chip: what arrives is lost, not kept for later
	EXPECT(b->RemoveChipHandler(&toolB) == noErr);
	Send(&toolA, "gone");
	Sleep(200 * kMilliseconds);
	EXPECT(b->InstallChipHandler(&toolB, &handlers) == noErr);
	b->SetInterruptEnable(true);
	EXPECT(Heard(&toolB, ""));

	// registered: the one at 'infr'
	EXPECT(HostIRChipInstall("listen:0") == noErr);
	EXPECT(HostIRChipInstalled() != nil);
	PSerialChipRegistry* registry = GetSerialChipRegistry();
	SerialChipID id = registry->FindByLocation(kHWLocBuiltInIR);
	EXPECT(id != kNilSerChipID && registry->GetChipPtr(id) == HostIRChipInstalled());

	EXPECT(a->RemoveChipHandler(&toolA) == noErr);
	EXPECT(b->RemoveChipHandler(&toolB) == noErr);
	a->Delete();
	b->Delete();
	LanScenario();
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	printf("test_HostIRChip: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
