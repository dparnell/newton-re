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

#include "SampleOrder.h"
#include "NewtWorld.h"
#include "HostViews.h"
#include "HostSoundDriver.h"
#include "SoundDriver.h"
#include "SoundSettings.h"
#include "FrameSoundChannel.h"
#include "IMACodec.h"
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

// an IMA-coded sound frame: a second of a rising tone at the hardware's
// rate (so nothing is resampled), coded by CompressIMA, played through
// the codec channel; what was played must be exactly what ExpandIMA
// makes of the same bytes
static const char* const	kIMASource = (const char*) 1;
static const long			kIMASamples = 21600 / 64 * 64;
static short*				gIMAExpected = nil;
static const char* const	kRecordSource = (const char*) 2;
static const char* const	kIMACheckSource = (const char*) 3;
static const char* const	kDTMFSource = (const char*) 4;
static const char* const	kStoredCheckSource = (const char*) 5;

// a touch tone, as TDTMFCodec's score: version 1, algorithm 0 (each tone
// on its own), no repeats, two tones - 697 Hz and 1209 Hz, the "1" key -
// each rising over 5 ms to 0x3000, falling over 5 ms to 0x2000, held
// 80 ms and released over 10 ms: 100 ms, 2200 samples (22 a millisecond)
static void
PutHalf(unsigned char* p, unsigned v)
{
	p[0] = (unsigned char) (v >> 8);
	p[1] = (unsigned char) v;
}

static long
PlayDTMF(void)
{
	RefVar samples(AllocateBinary(RSSYMsamples, 0x0a + 2 * 0x14));
	unsigned char* score = (unsigned char*) BinaryData(samples);
	memset(score, 0, 0x0a + 2 * 0x14);
	PutHalf(score + 0, 1);
	PutHalf(score + 2, 0);
	PutHalf(score + 6, 0);
	PutHalf(score + 8, 2);
	const unsigned frequencies[2] = { 697, 1209 };
	for (int k = 0; k < 2; k++)
	{
		unsigned char* tone = score + k * 0x14;
		PutHalf(tone + 0x0a, frequencies[k]);		// 16.16 Hz
		PutHalf(tone + 0x0c, 0);
		PutHalf(tone + 0x0e, 0x2000);				// the sustain level
		PutHalf(tone + 0x10, 0);					// silent
		PutHalf(tone + 0x12, 5);					// attack
		PutHalf(tone + 0x14, 5);					// decay
		PutHalf(tone + 0x16, 80);					// sustain
		PutHalf(tone + 0x18, 10);					// release
		PutHalf(tone + 0x1a, 0x3000);				// the peak
		PutHalf(tone + 0x1c, 0);					// tail
	}
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMsndframetype, RSSYMcodec);
	SetFrameSlot(frame, RSSYMcodecname, RefVar(MakeString("TDTMFCodec")));
	SetFrameSlot(frame, RSSYMsamples, samples);
	SetFrameSlot(frame, RSSYMcompressiontype, RefVar(MAKEINT(6)));
	SetFrameSlot(frame, RSSYMdatatype, RefVar(MAKEINT(16)));
	SetFrameSlot(frame, RSSYMsamplingrate, RefVar(MAKEINT(kHostSoundRate)));
	SetFrameSlot(frame, RSSYMbuffersize, RefVar(MAKEINT(4096)));
	SetFrameSlot(frame, RSSYMbuffercount, RefVar(MAKEINT(2)));
	RefVar result(FPlaySoundSync(RefVar(NILREF), frame));
	return NOTNIL(result) ? 1 : 0;
}

// the power of one frequency in a run of samples (Goertzel)
static double
Power(const short* x, long n, double frequency)
{
	double w = 2 * 3.14159265358979 * frequency / kHostSoundRate, c = 2 * cos(w), s1 = 0, s2 = 0;
	for (long i = 0; i < n; i++)
	{
		double s0 = x[i] + c * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static long
PlayIMA(void)
{
	short* tone = new short[kIMASamples];
	for (long i = 0; i < kIMASamples; i++)
		tone[i] = (short) (12000.0 * sin(i * (0.02 + i * 0.000002)));
	long bytes = kIMASamples / kIMABlockSize * kIMABlockBytes;
	RefVar samples(AllocateBinary(RSSYMsamples, bytes));
	IMAState state;
	SamplesToMemory(tone, kIMASamples);		// (samples lie big-endian in memory: SampleWords.h)
	CompressIMA(tone, (signed char*) BinaryData(samples), kIMASamples, &state, 1, 0);
	gIMAExpected = new short[kIMASamples];
	IMAState expand;
	ExpandIMA((const signed char*) BinaryData(samples), gIMAExpected, &expand, kIMASamples / kIMABlockSize, 1, 2);
	SamplesFromMemory(gIMAExpected, kIMASamples);
	delete[] tone;
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMsndframetype, RSSYMcodec);
	SetFrameSlot(frame, RSSYMcodecname, RefVar(MakeString("TIMACodec")));
	SetFrameSlot(frame, RSSYMsamples, samples);
	SetFrameSlot(frame, RSSYMcompressiontype, RefVar(MAKEINT(6)));
	SetFrameSlot(frame, RSSYMdatatype, RefVar(MAKEINT(16)));
	SetFrameSlot(frame, RSSYMsamplingrate, RefVar(MAKEINT(kHostSoundRate)));
	SetFrameSlot(frame, RSSYMbuffersize, RefVar(MAKEINT(4096)));		// a whole number of IMA blocks' output
	SetFrameSlot(frame, RSSYMbuffercount, RefVar(MAKEINT(4)));
	RefVar result(FPlaySoundSync(RefVar(NILREF), frame));
	return NOTNIL(result) ? 1 : 0;
}

// recording: the null backend's microphone plays kRecordSamples of a
// tone; a protoSoundChannel with direction 'record fills a frame's
// samples from it, plainly and through TIMACodec
static const long	kRecordSamples = 64 * 150;		// (9600: under half a second)
static short		gRecordSource[kRecordSamples];
static long			gRecordBinaries = 0;

static void
MakeRecordBuffers(void)
{
	RefVar globals(gVarFrame);
	SetFrameSlot(globals, RefVar(MakeSymbol("recBuffer")), RefVar(AllocateBinary(RSSYMsamples, kRecordSamples * 2)));
	SetFrameSlot(globals, RefVar(MakeSymbol("recIMA")), RefVar(AllocateBinary(RSSYMsamples, kRecordSamples / kIMABlockSize * kIMABlockBytes)));
	gRecordBinaries = 1;
}

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
			if (play->fSource == kStoredCheckSource)
			{
				// the stored samples are the source, big-endian
				RefVar sound(GetFrameSlotRef(gVarFrame, RefVar(MakeSymbol("storedSound"))));
				RefVar samples(GetFrameSlotRef(sound, RSSYMsamples));
				const unsigned char* bytes = (const unsigned char*) BinaryData(samples);
				long same = 0;
				for (long i = 0; i < kRecordSamples; i++)
					if (((bytes[2 * i] << 8) | bytes[2 * i + 1]) == (unsigned short) gRecordSource[i])
						same++;
				printf("the soup entry's samples: %ld of %ld big-endian as recorded\n", same, kRecordSamples);
				play->fResult = (Length(samples) == kRecordSamples * 2 && same == kRecordSamples) ? 1 : 0;
			}
			else if (play->fSource == kDTMFSource)
				play->fResult = PlayDTMF();
			else if (play->fSource == kIMACheckSource)
			{
				RefVar coded(GetFrameSlotRef(gVarFrame, RefVar(MakeSymbol("recIMA"))));
				long bytes = kRecordSamples / kIMABlockSize * kIMABlockBytes;
				signed char* expected = new signed char[bytes];
				IMAState state;
				static short inMemory[kRecordSamples];
				memcpy(inMemory, gRecordSource, sizeof(inMemory));
				SamplesToMemory(inMemory, kRecordSamples);
				CompressIMA(inMemory, expected, kRecordSamples, &state, 1, 0);
				long same = 0;
				for (long i = 0; i < bytes; i++)
					if (((const signed char*) BinaryData(coded))[i] == expected[i])
						same++;
				printf("recorded through TIMACodec: %ld coded bytes of %ld as CompressIMA makes them\n", same, bytes);
				play->fResult = (same == bytes) ? 1 : 0;
				delete[] expected;
			}
			else if (play->fSource == kRecordSource)
			{
				MakeRecordBuffers();
				play->fResult = 1;
			}
			else if (play->fSource == kIMASource)
				play->fResult = PlayIMA();
			else if (play->fSource == nil)
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
	// whatever the boot played - its boot sound (TNotebook::InitToolbox)
	// may not have reached the driver yet, so until nothing more arrives
	for (long tries = 0, before = -1; tries < 100; tries++)
	{
		WaitForSilence();
		long now;
		HostSoundCaptured(&now);
		if (now == before)
			break;
		before = now;
		Sleep(100 * kMilliseconds);
	}
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

	// coded sound: IMA through the codec channel
	HostSoundClearCapture();
	EXPECT(Send(newtPort, kIMASource) == 1);
	WaitForSilence();
	played = HostSoundCaptured(&count);
	printf("IMA: %ld samples played of %ld\n", count, kIMASamples);
	EXPECT(count == kIMASamples);
	EXPECT(gIMAExpected != nil && played != nil && memcmp(played, gIMAExpected, kIMASamples * sizeof(short)) == 0);

	// a touch tone synthesised by TDTMFCodec: the two frequencies are
	// there, and the tone ends when its envelope does
	HostSoundClearCapture();
	EXPECT(Send(newtPort, kDTMFSource) == 1);
	WaitForSilence();
	played = HostSoundCaptured(&count);
	long lastSound = -1;
	for (long i = 0; i < count; i++)
		if (played[i] > 64 || played[i] < -64)
			lastSound = i;
	double low = Power(played, 2000, 697), high = Power(played, 2000, 1209), between = Power(played, 2000, 950);
	printf("DTMF: %ld samples, sound to %ld; power at 697 Hz %.3g, 1209 Hz %.3g, 950 Hz %.3g\n", count, lastSound, low, high, between);
	EXPECT(count == 4096);
	EXPECT(lastSound > 2150 && lastSound <= 2201);		// (the ROM rounds 21.6 samples a millisecond to 22: 100 ms is 2200)
	EXPECT(low > 100 * between && high > 100 * between);

	// recording, plain: 16-bit samples at the hardware's rate, so what is
	// recorded is the source itself
	for (long i = 0; i < kRecordSamples; i++)
		gRecordSource[i] = (short) (9000.0 * sin(i * 0.05) + 3000.0 * sin(i * 0.31));
	HostSoundSetSource(gRecordSource, kRecordSamples);
	EXPECT(Send(newtPort, kRecordSource) == 1);
	EXPECT(Send(newtPort,
		"begin "
		"  GetRoot().rec := {state: nil}; "
		"  local ch := {_proto: vars.protoSoundChannel, direction: 'record}; "
		"  ch:Open(); "
		"  local snd := {sndFrameType: 'simpleSound, samples: vars.recBuffer, compressionType: 6, dataType: 16, samplingRate: 21600, "
		"    callback: func(state, error) begin GetRoot().rec.state := state; GetRoot().rec.error := error end}; "
		"  ch:Schedule(snd); "
		"  ch:Start(true); "
		"  GetRoot().rec.chan := ch; GetRoot().rec.snd := snd; "
		"  1 "
		"end") == 1);
	Sleep(1000 * kMilliseconds);
	EXPECT(Send(newtPort,
		"begin "
		"  local r := GetRoot().rec; "
		"  r.chan:Close(); "
		"  if r.state = 0 and r.error = 0 then 1 else 0 "
		"end") == 1);
	// ...and played back: the same samples come out
	HostSoundClearCapture();
	EXPECT(Send(newtPort, "begin PlaySoundSync(GetRoot().rec.snd); 1 end") == 1);
	WaitForSilence();
	played = HostSoundCaptured(&count);
	long same = 0;
	for (long i = 0; i < count && i < kRecordSamples; i++)
		if (played[i] == gRecordSource[i])
			same++;
	printf("recorded and played back: %ld samples, %ld the same as the source\n", count, same);
	EXPECT(count == kRecordSamples && same == kRecordSamples);

	// the recording kept in a soup entry and read back: the samples binary
	// holds them big-endian, as the ROM keeps them (the persistent form, the
	// same on every host), and plays the same
	EXPECT(Send(newtPort,
		"begin "
		"  local store := GetStores()[0]; "
		"  local soup := store:GetSoup(\"SoundTest\"); "
		"  if soup then soup:RemoveFromStoreXmit(nil); "
		"  soup := store:CreateSoupXmit(\"SoundTest\", [], nil); "
		"  local snd := GetRoot().rec.snd; "
		"  soup:AddXmit({sound: {sndFrameType: 'simpleSound, samples: snd.samples, compressionType: 6, dataType: 16, samplingRate: 21600}}, nil); "
		"  vars.storedSound := Query(soup, {type: 'index}):Entry().sound; "
		"  1 "
		"end") == 1);
	EXPECT(Send(newtPort, kStoredCheckSource) == 1);
	HostSoundClearCapture();
	EXPECT(Send(newtPort, "begin PlaySoundSync(vars.storedSound); 1 end") == 1);
	WaitForSilence();
	played = HostSoundCaptured(&count);
	same = 0;
	for (long i = 0; i < count && i < kRecordSamples; i++)
		if (played[i] == gRecordSource[i])
			same++;
	printf("kept in a soup entry and played back: %ld samples, %ld the same as the source\n", count, same);
	EXPECT(count == kRecordSamples && same == kRecordSamples);

	// recording through the IMA compressor: the coded bytes are what
	// CompressIMA makes of the source
	HostSoundSetSource(gRecordSource, kRecordSamples);
	EXPECT(Send(newtPort,
		"begin "
		"  GetRoot().rec := {state: nil}; "
		"  local ch := {_proto: vars.protoSoundChannel, direction: 'record}; "
		"  ch:Open(); "
		"  local snd := {sndFrameType: 'codec, codecName: \"TIMACodec\", samples: vars.recIMA, compressionType: 6, dataType: 16, "
		"    samplingRate: 21600, bufferSize: 4096, bufferCount: 4, "
		"    callback: func(state, error) begin GetRoot().rec.state := state; GetRoot().rec.error := error end}; "
		"  ch:Schedule(snd); "
		"  ch:Start(true); "
		"  GetRoot().rec.chan := ch; "
		"  1 "
		"end") == 1);
	Sleep(1500 * kMilliseconds);
	EXPECT(Send(newtPort,
		"begin "
		"  local r := GetRoot().rec; "
		"  r.chan:Close(); "
		"  if r.state = 0 and r.error = 0 then 1 else 0 "
		"end") == 1);
	EXPECT(Send(newtPort, kIMACheckSource) == 1);

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
	HostConfigureNewtWorld(NEWTON_OBJECTS, 0x400000, 320, 480, 4);
	HostInstallSoundDriver(nil);
	gNewtHostBoot = TestBoot;
	NewtInstallUserMain();
	gHostKernelServicesTask = Scenario;
	OsBoot();
	printf(failures ? "test_PlaySound: %d failures\n" : "test_PlaySound: all passed\n", failures);
	return failures != 0;
}
