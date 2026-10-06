// Host unit test for the GSM 06.10 coder (src/sound/GSM.h) and TGSMCodec.
//
// GSM full rate is lossy, so what can be checked is its known behaviour:
// every frame is 33 bytes with the magic nibble 0xD in front; decoding
// gives samples truncated to 13 bits (the post-processing masks the low
// three); a voiced signal - a vowel-like mix of harmonics - comes back
// recognisably (after the first frames, while the predictors settle, the
// decoded signal correlates closely with the original); silence codes to
// silence; the coder is deterministic; and a frame without the magic is
// refused.  TGSMCodec is driven as the codec channel drives it: whole
// frames only, both counts answered.

#include "GSM.h"
#include "SoundCodec.h"
#include "SampleOrder.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { kFrames = 25, kSamples = kFrames * 160 };

static short	gSignal[kSamples];		// host shorts
static short	gInMemory[kSamples];	// the same, big-endian
static unsigned char	gCoded[kFrames * 33];
static short	gDecoded[kSamples];

static void
MakeSignal(void)
{
	for (long i = 0; i < kSamples; i++)
	{
		double t = i / 8000.0;
		double v = 0;
		for (int h = 1; h <= 8; h++)
			v += sin(2 * 3.14159265358979 * 150 * h * t) / h;		// a 150 Hz voice's harmonics
		gSignal[i] = (short) (5000 * v);
	}
	memcpy(gInMemory, gSignal, sizeof(gInMemory));
	SamplesToMemory(gInMemory, kSamples);
}


static void
TestRoundTrip(void)
{
	gsm_state* coder = gsm_create();
	gsm_state* decoder = gsm_create();
	EXPECT(coder != nil && decoder != nil);
	EXPECT(coder->nrp == 40);
	for (int f = 0; f < kFrames; f++)
		gsm_encode(coder, gInMemory + 160 * f, gCoded + 33 * f);
	for (int f = 0; f < kFrames; f++)
		EXPECT((gCoded[33 * f] >> 4) == 0xD);
	for (int f = 0; f < kFrames; f++)
		EXPECT(gsm_decode(decoder, gCoded + 33 * f, gDecoded + 160 * f) == 0);
	SamplesFromMemory(gDecoded, kSamples);

	long masked = 0;
	for (long i = 0; i < kSamples; i++)
		if ((gDecoded[i] & 7) != 0)
			masked++;
	EXPECT(masked == 0);										// 13-bit samples

	// the best correlation over a small delay, from the fifth frame on
	double best = 0;
	for (int lag = 0; lag <= 4; lag++)
	{
		double xy = 0, xx = 0, yy = 0;
		for (long i = 800; i < kSamples - lag; i++)
		{
			xy += (double) gSignal[i] * gDecoded[i + lag];
			xx += (double) gSignal[i] * gSignal[i];
			yy += (double) gDecoded[i + lag] * gDecoded[i + lag];
		}
		double c = xy / sqrt(xx * yy);
		if (c > best)
			best = c;
	}
	printf("GSM: a voiced signal decodes with correlation %.3f\n", best);
	EXPECT(best > 0.9);

	// deterministic: a new coder makes the same frames
	gsm_state* again = gsm_create();
	unsigned char frame[33];
	for (int f = 0; f < 3; f++)
	{
		gsm_encode(again, gInMemory + 160 * f, frame);
		EXPECT(memcmp(frame, gCoded + 33 * f, 33) == 0);
	}
	gsm_destroy(again);

	// no magic, no decoding
	unsigned char bad[33];
	memset(bad, 0, sizeof(bad));
	short untouched[160];
	for (int i = 0; i < 160; i++)
		untouched[i] = 0x1234;
	EXPECT(gsm_decode(decoder, bad, untouched) == -1);
	EXPECT(untouched[0] == 0x1234 && untouched[159] == 0x1234);

	gsm_destroy(coder);
	gsm_destroy(decoder);
}


static void
TestSilence(void)
{
	gsm_state* coder = gsm_create();
	gsm_state* decoder = gsm_create();
	short silence[160];
	memset(silence, 0, sizeof(silence));
	short out[160];
	unsigned char frame[33];
	long loud = 0;
	for (int f = 0; f < 5; f++)
	{
		gsm_encode(coder, silence, frame);
		EXPECT(gsm_decode(decoder, frame, out) == 0);
		for (int i = 0; i < 160; i++)
			if (GetSampleWord(&out[i]) > 64 || GetSampleWord(&out[i]) < -64)
				loud++;
	}
	EXPECT(loud == 0);
	gsm_destroy(coder);
	gsm_destroy(decoder);
}


static void
TestCodec(void)
{
	TGSMCodec codec;
	codec.New();
	CodecBlock block;
	memset(&block, 0, sizeof(block));
	block.fBuffer = gCoded;
	block.fSize = 2 * 33 + 10;					// two whole frames and a piece
	block.fSampleBits = 16;
	block.fFormat = kSoundFormatLinear16;
	block.fSampleRate = 8000 << 16;
	EXPECT(codec.Init(&block) == noErr);
	EXPECT(codec.fStateTag == 'aloc');
	// a second Init: the ROM loses the first coder (a ROM bug); fixed, it
	// is freed and a fresh one made
	EXPECT(codec.Init(&block) == noErr && codec.fStateTag == 'aloc' && codec.fState != nil);
	EXPECT(codec.Reset(&block) == noErr);

	// coding: a frame and a half of samples makes one frame
	ULong srcSize = 240 * 2, codedSize = 0;
	EXPECT(codec.Consume(gInMemory, &srcSize, &codedSize, &block) == noErr);
	EXPECT(srcSize == 320 && codedSize == 33);
	EXPECT(!codec.BufferCompleted());
	srcSize = 3 * 320;
	EXPECT(codec.Consume(gInMemory + 160, &srcSize, &codedSize, &block) == noErr);
	EXPECT(srcSize == 320 && codedSize == 33);			// room for one more whole frame only

	// decoding: whole frames into the room given
	EXPECT(codec.Reset(&block) == noErr);
	short out[480];
	ULong dstSize = sizeof(out);
	EXPECT(codec.Produce(out, &dstSize, &codedSize, &block) == noErr);
	EXPECT(dstSize == 640 && codedSize == 66);
	EXPECT(block.fFormat == kSoundFormatLinear16 && block.fSampleBits == 16 && block.fSampleRate == (ULong) (8000 << 16));
	codec.Delete();
}


int
main()
{
	MakeSignal();
	TestRoundTrip();
	TestSilence();
	TestCodec();
	if (failures == 0)
		printf("test_GSM: all passed\n");
	else
		printf("test_GSM: %d failures\n", failures);
	return failures != 0;
}
