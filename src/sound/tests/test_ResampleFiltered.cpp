// Host unit test for the filtered sample-rate converter
// (src/sound/Resample.h: ResampleState, InitResampleState, ResampleFiltered).
//
// The filter is a windowed sinc read at thirteen points to the zero
// crossing, so at the same rate in and out every tap but the middle one
// lands on a zero of the sinc: the conversion is then an exact identity
// delayed by ten input samples.  That makes the unity case checkable to the
// sample, and it pins the table, the tap spacing and the history together.
//
// Also checked: what InitResampleState works out from the rates, that the
// history makes a split conversion identical to a single one (the point of
// carrying a state), that the counts and the leftover phase say where each
// side got to, that DC survives a conversion down, and that the coded
// formats are read and written through the sample converters.

#include "SampleOrder.h"
#include "Resample.h"
#include "SampleConvert.h"
#include "Ports.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { kDelay = 10 };					// the filter's middle tap, in input samples


static void
Setup(ResampleState* state, long srcRate, long dstRate, long srcFormat, long dstFormat)
{
	memset(state, 0, sizeof(*state));
	state->fSrcRate = srcRate;
	state->fDstRate = dstRate;
	state->fSrcFormat = srcFormat;
	state->fDstFormat = dstFormat;
	state->fSrcSampleBits = (srcFormat == 2) ? 16 : 8;
	state->fDstSampleBits = (dstFormat == 2) ? 16 : 8;
	InitResampleState(state);
}


static void
TestInit()
{
	ResampleState state;
	long before = gHitInitResampleAgain;

	Setup(&state, 8000, 8000, 2, 2);
	EXPECT(gHitInitResampleAgain == before + 1);
	EXPECT(state.fRatio == kFix1);
	EXPECT(state.fTapCount == 20);
	EXPECT(state.fPhase == 0);
	EXPECT(state.fSrcShift == 1 && state.fDstShift == 1);	// 16-bit samples
	EXPECT(state.fSrcConvert == nil && state.fDstConvert == nil);
	for (int i = 0; i < kResampleHistorySize; i++)
		EXPECT(state.fHistory[i] == 0);

	// going up: still twenty taps, and the ratio is the rate ratio
	Setup(&state, 11025, 22050, 2, 2);
	EXPECT(state.fRatio == 2 * kFix1 && state.fTapCount == 20);

	// going down: the filter is stretched, so the taps are 20 / the ratio
	Setup(&state, 22050, 11025, 2, 2);
	EXPECT(state.fRatio == kFix1 / 2 && state.fTapCount == 40);

	// the coded formats pick their converters, and their samples are bytes
	Setup(&state, 8000, 8000, 1, 0);
	EXPECT(state.fSrcConvert == SampleConvertMuLawToLin16);
	EXPECT(state.fDstConvert == SampleConvertLin16ToStd8);
	EXPECT(state.fSrcShift == 0 && state.fDstShift == 0);
}


// At the same rate the filter is an exact identity, delayed by ten samples.
static void
TestUnityIsADelay()
{
	static short in[32];
	static short out[32];
	for (int i = 0; i < 32; i++)
		in[i] = (short) (1000 + 37 * i);
	SamplesToMemory(in, 32);

	ResampleState state;
	Setup(&state, 8000, 8000, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = out;
	long dstCount = 32, srcCount = 32;
	ResampleFiltered(&state, &dstCount, &srcCount);

	EXPECT(dstCount == 32 && srcCount == 32);
	EXPECT(state.fPhase == 0);					// nothing left over
	for (int i = 0; i < kDelay; i++)
		EXPECT(out[i] == 0);					// the history started empty
	for (int i = kDelay; i < 32; i++)
		EXPECT(out[i] == in[i - kDelay]);
}


// The state's history makes two calls come to the same thing as one.
static void
TestHistoryJoinsTheCalls()
{
	static short in[64];
	static short whole[64];
	static short halves[64];
	for (int i = 0; i < 64; i++)
		in[i] = (short) (i * i % 5000 - 2000);
	SamplesToMemory(in, 64);

	ResampleState state;
	Setup(&state, 8000, 8000, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = whole;
	long dstCount = 64, srcCount = 64;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 64 && srcCount == 64);

	Setup(&state, 8000, 8000, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = halves;
	dstCount = 30;
	srcCount = 30;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 30 && srcCount == 30);

	state.fSrcBuffer = in + 30;
	state.fDstBuffer = halves + 30;
	dstCount = 34;
	srcCount = 34;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 34 && srcCount == 34);

	EXPECT(memcmp(whole, halves, sizeof(whole)) == 0);
}


// Converting down halves the samples; a constant comes through as itself,
// and the count and the leftover phase say where each side got to.
static void
TestDownAndUp()
{
	static short in[64];
	static short out[64];
	ResampleState state;

	for (int i = 0; i < 64; i++)
		in[i] = 8000;						// DC
	SamplesToMemory(in, 64);

	Setup(&state, 22050, 11025, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = out;
	long dstCount = 64, srcCount = 64;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 32 && srcCount == 64);		// two in for one out
	EXPECT(state.fPhase == 0);
	// once the filter is full of the constant the output is the constant
	for (int i = 24; i < 32; i++)
		EXPECT(GetSampleWord(&out[i]) > 7800 && GetSampleWord(&out[i]) < 8200);

	// and up: one in for two out
	Setup(&state, 11025, 22050, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = out;
	dstCount = 64;
	srcCount = 32;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 64 && srcCount == 32);
	EXPECT(GetSampleWord(&out[63]) > 7800 && GetSampleWord(&out[63]) < 8200);

	// a destination too small stops the conversion where it ran out
	Setup(&state, 11025, 22050, 2, 2);
	state.fSrcBuffer = in;
	state.fDstBuffer = out;
	dstCount = 10;
	srcCount = 32;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 10 && srcCount == 5);		// five inputs made ten outputs
	EXPECT(state.fPhase == 0);
}


// Coded samples in and out: mu-law read through SampleConvertMuLawToLin16
// and written back through SampleConvertLin16ToMuLaw, so a unity conversion
// is the codes delayed by ten.
static void
TestCodedFormats()
{
	static short samples[32];
	static UByte codes[32];
	static UByte out[32];
	for (int i = 0; i < 32; i++)
	{
		PutSampleWord(&samples[i], (short) (2000 * ((i % 7) - 3)));
		SampleConvertLin16ToMuLaw(&codes[i], &samples[i]);
	}

	ResampleState state;
	Setup(&state, 8000, 8000, 1, 1);				// mu-law both ends
	state.fSrcBuffer = (short*) codes;
	state.fDstBuffer = (short*) out;
	long dstCount = 32, srcCount = 32;
	ResampleFiltered(&state, &dstCount, &srcCount);
	EXPECT(dstCount == 32 && srcCount == 32);
	for (int i = kDelay; i < 32; i++)
		EXPECT(out[i] == codes[i - kDelay]);		// through the codec and back
}


int
main()
{
	SetRandSeed(1);
	TestInit();
	TestUnityIsADelay();
	TestHistoryJoinsTheCalls();
	TestDownAndUp();
	TestCodedFormats();
	if (failures == 0)
		printf("test_ResampleFiltered: all passed\n");
	else
		printf("test_ResampleFiltered: %d failures\n", failures);
	return failures != 0;
}
