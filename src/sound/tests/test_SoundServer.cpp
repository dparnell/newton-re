// Host test of the sound server (sound/SoundServer.h) over the host's null
// sound driver (hal/host/HostSoundDriver.h): a client opens an output
// channel on gSndPort, schedules a block of samples, starts the channel
// and waits for it to run dry, the way TUSoundChannel does it - and what
// the driver was given to play is the block's own samples, followed by
// silence to the end of the last DMA buffer.
//
// A second block at 11025 samples a second and 8 bits is resampled to the
// hardware's 21600 by the DMA channel; it must come out about twice as
// long, converted from offset binary.
//
// Runs as the kernel services task of a booted OS: the server is an app
// world and the protocol registry is a monitor.  The driver's buffer
// interrupts are host interrupt sources falling due on the system clock.

#include "SampleOrder.h"
#include "SoundServer.h"
#include "SoundDriver.h"
#include "SoundCodec.h"
#include "SoundChannel.h"
#include "HostSoundDriver.h"
#include "Boot.h"
#include "UserBoot.h"
#include "UserPorts.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void
MakeRequest(TUSoundNodeRequest* request, ULong channel, ULong command, ULong value)
{
	memset(request, 0, sizeof(*request));
	request->fEvent.fAEventClass = kNewtEventClass;
	request->fEvent.fAEventID = 'usnd';
	request->fChannel = channel;
	request->fCommand = command;
	request->fNodeId = value;
}


static long
Immediate(TUPort& port, ULong channel, ULong command, ULong value, TUSoundNodeReply* reply)
{
	TUSoundNodeRequest request;
	MakeRequest(&request, channel, command, value);
	memset(reply, 0, sizeof(*reply));
	ULong size = 0;
	long err = port.SendRPC(&size, &request, sizeof(TAEvent) + 3 * sizeof(ULong), reply, sizeof(*reply));
	return err != noErr ? err : reply->fError;
}


// A block played through the channel: scheduled (answered when it has
// been played), then the channel started and waited for.
static void
Play(TUPort& port, ULong channel, void* data, long count, long bits, long format, Fixed rate)
{
	TUSoundNodeRequest request;
	MakeRequest(&request, channel, kSndSchedule, 1);
	request.fBlock.fData = data;
	request.fBlock.fCount = count;
	request.fBlock.fSampleBits = bits;
	request.fBlock.fFormat = format;
	request.fBlock.fSampleRate = rate;
	request.fBlock.fVolume = 0;
	request.fBlock.fStart = 0;
	request.fBlock.fPlayCount = count;
	request.fBlock.fLoops = 0;
	TUSoundNodeReply scheduled;
	memset(&scheduled, 0, sizeof(scheduled));
	TUAsyncMessage message;
	EXPECT(message.Init(true) == noErr);
	EXPECT(port.SendRPC(&message, &request, sizeof(request), &scheduled, sizeof(scheduled)) == noErr);

	TUSoundNodeReply started;
	EXPECT(Immediate(port, channel, kSndStartWait, 0, &started) == noErr);
	EXPECT(message.BlockTillDone() == noErr);
	EXPECT(scheduled.fError == noErr);
	EXPECT(scheduled.fNodeId == 1);
	// Both answers come when the last samples have gone into a DMA buffer
	// (the node freed), not when they have been heard: wait for the
	// hardware to play the buffers out and stop.
	for (long i = 0; i < 100 && gSndDriver->OutputIsRunning(); i++)
		Sleep(20 * kMilliseconds);
}


static short	gLinear[3000];
static UByte	gStd8[1000];

static void
SoundScenario(void)
{
	InitializeSound();
	EXPECT(gSndPort != 0);
	EXPECT(gSndDriver != nil);
	TUPort port(gSndPort);

	TUSoundNodeReply reply;
	EXPECT(Immediate(port, 0, kSndOpenOutput, 0, &reply) == noErr);
	ULong channel = reply.fChannel;
	EXPECT(channel != 0);

	// 16-bit samples at the hardware's rate are copied as they are
	for (long i = 0; i < 3000; i++)
		gLinear[i] = (short) ((i * 37) % 20000 - 10000);
	HostSoundClearCapture();
	static short inMemory[3000];			// (the block's samples big-endian, as the ROM keeps them)
	memcpy(inMemory, gLinear, sizeof(inMemory));
	SamplesToMemory(inMemory, 3000);
	Play(port, channel, inMemory, 3000, 16, kSoundFormatLinear16, kHostSoundRate << 16);
	long count;
	const short* played = HostSoundCaptured(&count);
	printf("16-bit: %ld samples played\n", count);
	EXPECT(count == 3000);						// the last buffer only as long as what was put in it
	EXPECT(played != nil && memcmp(played, gLinear, 3000 * sizeof(short)) == 0);
	long nonSilent = 0;
	for (long i = 3000; i < count; i++)
		if (played[i] != 0)
			nonSilent++;
	EXPECT(nonSilent == 0);
	EXPECT(!gSndDriver->OutputIsRunning());		// stopped once there was nothing more

	// 8-bit offset binary at 11025 a second, resampled
	for (long i = 0; i < 1000; i++)
		gStd8[i] = (UByte) (0x80 + ((i % 50) < 25 ? 0x40 : -0x40));
	HostSoundClearCapture();
	Play(port, channel, gStd8, 1000, 8, kSoundFormatStd8, 11025 << 16);
	played = HostSoundCaptured(&count);
	long last = -1;
	for (long i = 0; i < count; i++)
		if (played[i] != 0)
			last = i;
	printf("8-bit at 11025: %ld samples played, sound to %ld\n", count, last);
	EXPECT(last > 1900 && last < 2020);			// 1000 x 21600/11025 = 1959
	short highest = 0, lowest = 0;
	for (long i = 0; i < count; i++)
	{
		if (played[i] > highest)
			highest = played[i];
		if (played[i] < lowest)
			lowest = played[i];
	}
	printf("8-bit at 11025: from %d to %d\n", lowest, highest);
	EXPECT(highest > 0x3000 && lowest < -0x3000);	// +-0x40 of offset binary is +-0x4000 of linear

	EXPECT(Immediate(port, channel, kSndClose, 0, &reply) == noErr);
	EXPECT(Immediate(port, channel, kSndClose, 0, &reply) == kSndErrNoChannel);
	// fixed: a channel there is not is looked for (the ROM writes through nil)
	if (RomBugFixed())
		EXPECT(Immediate(port, channel, kSndSetInputDevice, 1, &reply) == kSndErrNoChannel);

	printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
	HostStopTasks();
}


int
main()
{
	HostInstallSoundDriver(nil);
	gHostKernelServicesTask = SoundScenario;
	OsBoot();
	return failures ? 1 : 0;
}
