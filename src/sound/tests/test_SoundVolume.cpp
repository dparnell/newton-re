// Host unit test for the volume as a script sees it
// (src/sound/SoundSettings.h, src/sound/SoundChannel.h).
//
// The Newton has five volume settings and the sound server works in
// decibels, 16.16 fixed; this checks the table between them, the state the
// sound channel keeps (which is all a host with no sound server has) and
// the NewtonScript functions over both.

#include "SoundSettings.h"
#include "SoundChannel.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <math.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a fixed 16.16 value as decibels, for the expectations below
static double
DB(long fixed)
{
	return fixed / 65536.0;
}


int
main()
{
	// ---- the five settings' levels -------------------------------------
	EXPECT(VolumeToDecibels(0) == kSilenceDecibels);
	EXPECT(fabs(DB(VolumeToDecibels(1)) - -18.0618) < 0.001);
	EXPECT(fabs(DB(VolumeToDecibels(2)) -  -6.0206) < 0.001);
	EXPECT(fabs(DB(VolumeToDecibels(3)) -  -3.0103) < 0.001);
	EXPECT(VolumeToDecibels(4) == 0);
	// they rise
	EXPECT(VolumeToDecibels(1) < VolumeToDecibels(2));
	EXPECT(VolumeToDecibels(2) < VolumeToDecibels(3));
	EXPECT(VolumeToDecibels(3) < VolumeToDecibels(4));
	// off the ends: below 0 is silence, above 4 is full
	EXPECT(VolumeToDecibels(-1) == kSilenceDecibels);
	EXPECT(VolumeToDecibels(5) == 0 && VolumeToDecibels(1000) == 0);

	// ---- and the way back ----------------------------------------------
	for (long v = 0; v <= 4; v++)
		EXPECT(DecibelsToVolume(VolumeToDecibels(v)) == v);
	// a level between two settings answers the lower one
	EXPECT(DecibelsToVolume(VolumeToDecibels(1) - 1) == 1);
	EXPECT(DecibelsToVolume(VolumeToDecibels(2) - 1) == 1);
	EXPECT(DecibelsToVolume(VolumeToDecibels(2) + 1) == 2);
	EXPECT(DecibelsToVolume(VolumeToDecibels(3) - 1) == 2);
	EXPECT(DecibelsToVolume(VolumeToDecibels(3) + 1) == 3);
	EXPECT(DecibelsToVolume(-1) == 3);
	EXPECT(DecibelsToVolume(0) == 4);
	// anything above full is still full
	EXPECT(DecibelsToVolume(6 * 65536) == 4);

	// ---- the channel ----------------------------------------------------
	TUSoundChannel* channel = GlobalSoundChannel();
	EXPECT(channel != nil && channel == gSoundChannel && GlobalSoundChannel() == channel);
	// with no sound server the gestalt is not asked, so GetVolume answers
	// what the channel was told
	EXPECT((channel->fFlags & kSoundChannelAskForVolume) == 0);
	EXPECT(channel->SetVolume(VolumeToDecibels(2)) == VolumeToDecibels(2));
	EXPECT(channel->GetVolume() == VolumeToDecibels(2));
	EXPECT(channel->SetVolume(-3 * 65536) == -3 * 65536);
	EXPECT(channel->GetVolume() == -3 * 65536);
	// the input gain is the channel's own either way
	EXPECT(channel->GetInputGain() == 0x80);
	channel->SetInputGain(0x40);
	EXPECT(channel->GetInputGain() == 0x40);

	// ---- the NewtonScript functions --------------------------------------
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x40000;
	InitObjects();

	RefVar nilArg(NILREF);
	EXPECT(fabs(CoerceToDouble(RefVar(FVolumeToDecibels(nilArg, RefVar(MAKEINT(3))))) - -3.0103) < 0.001);
	EXPECT(RINT(FDecibelsToVolume(nilArg, RefVar(MakeReal(-3.0103)))) == 3);
	EXPECT(RINT(FDecibelsToVolume(nilArg, RefVar(MakeReal(0.0)))) == 4);

	// SetVolume takes a setting, SetSystemVolume decibels, and both end up
	// in the same place
	EXPECT(ISNIL(FSetVolume(nilArg, RefVar(MAKEINT(1)))));
	EXPECT(RINT(FGetVolume(nilArg)) == 1);
	EXPECT(fabs(CoerceToDouble(RefVar(FGetSystemVolume(nilArg))) - -18.0618) < 0.001);

	EXPECT(fabs(CoerceToDouble(RefVar(FSetSystemVolume(nilArg, RefVar(MakeReal(-6.0206))))) - -6.0206) < 0.001);
	EXPECT(fabs(CoerceToDouble(RefVar(FGetSystemVolume(nilArg))) - -6.0206) < 0.001);
	EXPECT(RINT(FGetVolume(nilArg)) == 2);

	// nil is silence
	EXPECT(ISNIL(FSetVolume(nilArg, nilArg)));
	EXPECT(RINT(FGetVolume(nilArg)) == 0);
	EXPECT(channel->GetVolume() == kSilenceDecibels);

	printf("test_SoundVolume: %d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
