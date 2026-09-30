// TEzEndpointPipe (comms/EzEndpointPipe.h): an easy pipe over an 'mnps
// endpoint, made from option arrays (through the modem navigator, which
// lets a non-modem service by), connected to the desktop end in
// tools/dock/mnp.py (ctest runs this program under mnp.py --echo): bytes
// written to the pipe come back through it, 'sbav says how many have come,
// and TearDown closes the endpoint.  Prints "port N" once its serial port
// listens.

#include "EzEndpointPipe.h"
#include "Endpoint.h"
#include "CommManager.h"
#include "SerialTool.h"
#include "SerialOptions.h"
#include "MNP.h"
#include "ModemOptions.h"
#include "CommErrors.h"
#include "HostSerialChip.h"
#include "FIQTimer.h"
#include "AppWorld.h"
#include "objects.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include "hal/host/Host.h"

#include <stdio.h>
#include <string.h>

extern const ExceptionName exPipeException;

static int failures = 0;
static volatile bool sDone = false;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fflush(stdout); } } while (0)


// the options the connection types other than serial and MNP serial are
// made with, and ADSP, which is not yet, throwing
static void
ConnectionTypeOptions(void)
{
	TOptionArray ir;
	EXPECT(ir.Init() == noErr);
	EXPECT(EzSharpIROptions(&ir, nil) == noErr);
	EXPECT(ir.GetArrayCount() == 1 && ir.OptionAt(0)->Label() == 'slir' && ir.OptionAt(0)->IsService());
	TOptionArray irda;
	EXPECT(irda.Init() == noErr);
	EXPECT(EzIrDAOptions(&irda, nil) == noErr);
	EXPECT(irda.GetArrayCount() == 1 && irda.OptionAt(0)->Label() == 'irda' && irda.OptionAt(0)->IsService());

	// the modem: MNP required, and the dialling out of the preferences
	RefVar prefs(AllocateFrame());
	SetFrameSlot(prefs, RSSYMmodemsoundvolume, MAKEINT(2));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, prefs);
	TOptionArray modem;
	EXPECT(modem.Init() == noErr);
	EXPECT(EzMNPModemOptions(&modem, nil) == noErr);
	EXPECT(modem.GetArrayCount() == 3);
	if (modem.GetArrayCount() == 3)
	{
		EXPECT(modem.OptionAt(0)->Label() == 'mods' && modem.OptionAt(0)->IsService());
		TCMOModemECType* ec = (TCMOModemECType*) modem.OptionAt(1);
		EXPECT(ec->Label() == kCMOModemECType && ec->fType == 2 && ec->GetOpCode() == opSetRequired);
		TCMOModemDialing* dialing = (TCMOModemDialing*) modem.OptionAt(2);
		EXPECT(dialing->Label() == kCMOModemDialing && dialing->fSpeakerOn && dialing->fSpeakerVolume == '2');
	}

	TEzEndpointPipe* pipe = new TEzEndpointPipe;
	NewtonErr err = noErr;
	newton_try
	{
		pipe->Init(kADSPConnection, nil, 30 * kSeconds);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	EXPECT(err == kCommErrMethodNotImplemented);
	delete pipe;
}


static void
Scenario(void)
{
	ConnectionTypeOptions();

	// the navigator the ROM's boot makes (with no modem navigator in it)
	SetFrameSlot(RefVar(gVarFrame), RSSYMnavigator, RefVar(AllocateFrame()));

	TOptionArray open;
	EXPECT(open.Init() == noErr);
	EXPECT(EzMNPSerialOptions(&open, nil) == noErr);
	TOptionArray connect;
	EXPECT(connect.Init() == noErr);
	EXPECT(EzMNPConnectOptions(&connect, nil) == noErr);

	TEzEndpointPipe* pipe = new TEzEndpointPipe;
	newton_try
	{
		pipe->Init(&open, nil, &connect, false, 30 * kSeconds);
		const char* text = "newtdockrtdk";
		pipe->WriteChunk(text, 12, false);
		pipe->FlushWrite();
		ULong available = 0;
		for (int tries = 0; tries < 200 && available < 12; tries++)
		{
			available = pipe->BytesAvailable();
			if (available < 12)
				Sleep(10 * kMilliseconds);
		}
		EXPECT(available == 12);
		char back[16];
		memset(back, 0, sizeof(back));
		long count = 12;
		Boolean eof = false;
		pipe->ReadChunk(back, count, eof);
		EXPECT(count == 12 && memcmp(back, text, 12) == 0);
		EXPECT(pipe->TearDown() == noErr);
	}
	newton_catch_all
	{
		failures++;
		printf("FAIL: exception %s (%ld)\n", CurrentException()->name, (long) (Long) CurrentException()->data);
	}
	end_try;
	delete pipe;
}


class TTestWorld : public TAppWorld
{
public:
	virtual ULong	GetSizeOf()		{ return sizeof(TTestWorld); }
	virtual long	MainConstructor()
	{
		long err = TAppWorld::MainConstructor();
		if (err != noErr)
			return err;
		InitObjects();
		return noErr;
	}
	virtual void	TheMain()
	{
		EnableForking(false);
		Scenario();
		sDone = true;
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
	// the desktop end is another process on the host's own time: a clock
	// that jumps to the next deadline whenever the tasks are idle runs the
	// link's timers out while the peer is still sending (seen under load)
	HostUseRealClock(true);
	gHostKernelServicesTask = Boot;
	OsBoot();
	EXPECT(sDone);
	if (failures == 0)
		printf("test_EzEndpointPipe: all passed\n");
	else
		printf("test_EzEndpointPipe: %d failures\n", failures);
	fflush(stdout);
	return failures != 0;
}
