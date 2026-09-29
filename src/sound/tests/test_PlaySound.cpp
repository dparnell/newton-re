// A ROM sound played the way a script plays one: the OS booted into the
// Newt world with the host's null sound driver (hal/host/HostSoundDriver.h)
// installed, and PlaySoundSync of the ROM's click (magic pointer 51) run in
// the world - through GlobalSoundChannel (a TFrameSoundChannel opened on
// the sound server), Convert, TUSoundChannel::Schedule and Start, the
// server's DMA channel converting and resampling it, and the driver's
// buffers - and then what the driver was given to play is checked
// against the click's own samples: as many as the frame's rate comes to
// at the hardware's 21600 a second, and the same waveform.
//
// Then a protoSoundChannel of the script's own: Open, Schedule, Start
// without waiting, and the frame's callback method called with the state
// and error when the click has been played; Close.

#include "NewtWorld.h"
#include "HostViews.h"
#include "HostSoundDriver.h"
#include "SoundDriver.h"
#include "SoundSettings.h"
#include "FrameSoundChannel.h"
#include "Frames.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "UserPorts.h"
#include "UserTasks.h"
#include "UserBoot.h"
#include "NameServer.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "os600/kernel/Boot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

class TQuitHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
	{
		SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
		((TAppWorld*) GetGlobals())->AETerminateLoop();
	}
};

// the click, as its frame says it is
static long		gClickSamples = 0;
static long		gClickBits = 0;
static long		gClickFormat = -1;
static double	gClickRate = 0;
static short*	gClickLinear = nil;		// its samples as 16-bit linear

// 'host/'play: PlaySoundSync(click) in the world; 1 when it came back
struct TPlayEvent : public TAEvent
{
	const char*	fSource;
	long		fResult;
};
class TPlayHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
	{
		TPlayEvent* play = (TPlayEvent*) event;
		play->fResult = 0;
		newton_try
		{
			if (play->fSource == nil)
			{
				RefVar click(FConvertToSoundFrame(RefVar(NILREF), RefVar(MAKEMAGICPTR(kClickSoundMagicPtr))));
				RefVar samples(GetProtoVariable(click, RSSYMsamples, nil));
				RefVar value(GetProtoVariable(click, RSSYMcompressiontype, nil));
				gClickFormat = ISINT(value) ? RVALUE(value) : 0;
				value = GetProtoVariable(click, RSSYMdatatype, nil);
				gClickBits = ISINT(value) ? (RVALUE(value) == 16 || RVALUE(value) == 2 ? 16 : 8) : (gClickFormat == 0 ? 8 : 16);
				gClickSamples = Length(samples) / (gClickBits / 8);
				value = GetProtoVariable(click, RSSYMsamplingrate, nil);
				gClickRate = IsReal(value) ? CDouble(value) : (ISINT(value) ? RVALUE(value) : 22026.43);
				gClickLinear = new short[gClickSamples];
				const unsigned char* bytes = (const unsigned char*) BinaryData(samples);
				for (long i = 0; i < gClickSamples; i++)
					gClickLinear[i] = (gClickBits == 8) ? (short) (((int) bytes[i] - 128) * 256)
														: (short) ((bytes[2 * i] << 8) | bytes[2 * i + 1]);
				RefVar result(FPlaySoundSync(RefVar(NILREF), RefVar(MAKEMAGICPTR(kClickSoundMagicPtr))));
				play->fResult = NOTNIL(result) ? 1 : 0;
			}
			else
			{
				// the NTK's names for them, which a compiled package would have
				RefVar globals(gVarFrame);
				SetFrameSlot(globals, RefVar(MakeSymbol("protoSoundChannel")), RefVar(Rprotosoundchannel));
				SetFrameSlot(globals, RefVar(MakeSymbol("ROM_click")), RefVar(MAKEMAGICPTR(kClickSoundMagicPtr)));
				RefVar fn(ParseString(RefVar(MakeString(play->fSource))));
				RefVar result(InterpretBlock(fn, RefVar(gVarFrame)));
				play->fResult = ISINT(result) ? RINT(result) : 0;
			}
		}
		newton_catch_all
		{
			fprintf(stderr, "exception %s\n", CurrentException()->name);
			play->fResult = -1;
		}
		end_try;
		SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
	}
};

static void
TestBoot(void)
{
	HostBootNewtWorld();
	(new TQuitHandler)->Init('quit', 'host');
	(new TPlayHandler)->Init('play', 'host');
}


static long
Send(TUPort& port, const char* source)
{
	TPlayEvent play;
	play.fAEventClass = 'host';
	play.fAEventID = 'play';
	play.fSource = source;
	play.fResult = 0;
	ULong replySize = 0;
	long err = port.SendRPC(&replySize, &play, sizeof(play), &play, sizeof(play));
	return err != noErr ? err : play.fResult;
}


static void
WaitForSilence(void)
{
	for (long i = 0; i < 200 && gSndDriver != nil && gSndDriver->OutputIsRunning(); i++)
		Sleep(20 * kMilliseconds);
}


// The best normalised correlation of what was played with the click
// stretched to the hardware's rate, over a few samples' lag either way
// (the filtered resampler delays its output).
static double
Correlation(const short* played, long count)
{
	double best = 0;
	double step = gClickRate / kHostSoundRate;
	for (long lag = -16; lag <= 16; lag++)
	{
		double xy = 0, xx = 0, yy = 0;
		for (long i = 0; i < count; i++)
		{
			double t = (i + lag) * step;
			long at = (long) floor(t);
			double a = (at >= 0 && at < gClickSamples) ? gClickLinear[at] : 0;
			double b = (at + 1 >= 0 && at + 1 < gClickSamples) ? gClickLinear[at + 1] : 0;
			double x = a + (b - a) * (t - at);
			double y = played[i];
			xy += x * y; xx += x * x; yy += y * y;
		}
		if (xx > 0 && yy > 0 && xy / sqrt(xx * yy) > best)
			best = xy / sqrt(xx * yy);
	}
	return best;
}


static void
Scenario(void)
{
	TUNameServer nameServer;
	TObjectId portId = 0;
	ULong spec = 0;
	for (long tries = 0; tries < 200 && nameServer.Lookup("newt", "TUPort", &portId, &spec) != noErr; tries++)
		Sleep(10 * kMilliseconds);
	EXPECT(portId != 0);
	if (portId == 0)
	{
		HostStopTasks();
		return;
	}
	TUPort newtPort(portId);
	Sleep(50 * kMilliseconds);
	WaitForSilence();								// whatever the boot played
	EXPECT(gSndPort != 0);

	// PlaySoundSync(click)
	HostSoundClearCapture();
	EXPECT(Send(newtPort, nil) == 1);
	WaitForSilence();
	long count;
	const short* played = HostSoundCaptured(&count);
	long expected = (long) (gClickSamples * kHostSoundRate / gClickRate);
	printf("click: %ld samples, %ld bits, format %ld, %.2f a second; played %ld (expected about %ld)\n",
		   gClickSamples, gClickBits, gClickFormat, gClickRate, count, expected);
	EXPECT(gClickSamples > 0);
	EXPECT(labs(count - expected) <= 4);
	double correlation = Correlation(played, count);
	printf("click: correlation with its own samples %.4f\n", correlation);
	EXPECT(correlation > 0.95);

	// a channel of the script's own, and its callback
	HostSoundClearCapture();
	EXPECT(Send(newtPort,
		"begin "
		"  GetRoot().soundTest := {state: nil, error: nil}; "
		"  local chan := {_proto: vars.protoSoundChannel}; "
		"  chan:Open(); "
		"  chan:Schedule({_proto: vars.ROM_click, callback: func(state, error) begin "
		"    GetRoot().soundTest.state := state; GetRoot().soundTest.error := error end}); "
		"  chan:Start(true); "
		"  GetRoot().soundTest.chan := chan; "
		"  if chan:IsActive() then 1 else 2 "
		"end") == 1);
	WaitForSilence();
	Sleep(100 * kMilliseconds);						// the completion idled through
	EXPECT(Send(newtPort,
		"begin "
		"  local t := GetRoot().soundTest; "
		"  t.chan:Close(); "
		"  if t.state = 0 and t.error = 0 then 1 else 0 "
		"end") == 1);
	HostSoundCaptured(&count);
	EXPECT(labs(count - expected) <= 4);

	TAEvent quit;
	quit.fAEventClass = 'host';
	quit.fAEventID = 'quit';
	ULong replySize = 0;
	newtPort.SendRPC(&replySize, &quit, sizeof(quit), &quit, sizeof(quit));
	HostStopTasks();
}


int main()
{
	HostConfigureNewtWorld(NEWTON_ROM_BIN, 0x400000, 320, 480, 4);
	HostInstallSoundDriver(nil);
	gNewtHostBoot = TestBoot;
	NewtInstallUserMain();
	gHostKernelServicesTask = Scenario;
	OsBoot();
	printf(failures ? "test_PlaySound: %d failures\n" : "test_PlaySound: all passed\n", failures);
	return failures != 0;
}
