// Host unit test for the plain sample-rate converter (src/sound/Resample.h).
//
// The conversion is nearest-neighbour over an accumulator, so what it does
// is exactly predictable: with the output rate half the input's every other
// sample survives; with it twice, every sample is written twice; and at 3:2
// and 2:3 the pattern is the accumulator's.  The test states those patterns,
// checks that both counts come back as what was used (so a caller can carry
// on where it left off), that a short destination stops the conversion, and
// that the sample converter and the byte-copy paths both work.

#include "Resample.h"
#include "SampleConvert.h"
#include "Ports.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static UByte gSource[16];
static UByte gDest[32];


// Convert gSource (8-bit samples, values 1..16) at srcRate into gDest at
// dstRate, allowing maxOut outputs from inCount inputs; answer what came out
// as a string of values.
static void
Run(long srcRate, long dstRate, long inCount, long maxOut, long* outCount, long* usedCount)
{
	for (int i = 0; i < 16; i++)
		gSource[i] = (UByte) (i + 1);
	memset(gDest, 0, sizeof(gDest));

	SampleSpec spec;
	spec.fDstBuffer = gDest;
	spec.fDstRate = dstRate;
	spec.fDstSampleBits = 8;
	spec.fSrcBuffer = gSource;
	spec.fSrcRate = srcRate;
	spec.fSrcSampleBits = 8;
	spec.fConvert = nil;

	long dstCount = maxOut;
	long srcCount = inCount;
	Resample(&spec, &dstCount, &srcCount);
	*outCount = dstCount;
	*usedCount = srcCount;
}

static Boolean
Matches(const char* expected, long outCount)
{
	// expected is a list of sample values, comma separated
	long i = 0;
	const char* p = expected;
	while (*p != '\0')
	{
		long value = 0;
		while (*p >= '0' && *p <= '9')
			value = value * 10 + (*p++ - '0');
		if (*p == ',')
			p++;
		if (i >= outCount || gDest[i] != (UByte) value)
			return false;
		i++;
	}
	return i == outCount;
}


static void
TestRatios()
{
	long out, used;

	Run(2, 1, 16, 16, &out, &used);				// half the rate: every other sample
	EXPECT(out == 8 && used == 16);
	EXPECT(Matches("1,3,5,7,9,11,13,15", out));

	Run(1, 2, 16, 16, &out, &used);				// twice the rate: every sample twice
	EXPECT(out == 16 && used == 8);
	EXPECT(Matches("1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8", out));

	Run(1, 1, 16, 16, &out, &used);				// the same rate: straight through
	EXPECT(out == 16 && used == 16);
	EXPECT(Matches("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16", out));

	Run(3, 2, 16, 16, &out, &used);				// two out of every three
	EXPECT(out == 11 && used == 16);
	EXPECT(Matches("1,3,4,6,7,9,10,12,13,15,16", out));

	Run(2, 3, 16, 16, &out, &used);				// three out of every two
	EXPECT(out == 16 && used == 11);
	EXPECT(Matches("1,1,2,3,3,4,5,5,6,7,7,8,9,9,10,11", out));

	// the real rates behave the same way
	Run(44100, 22050, 16, 16, &out, &used);
	EXPECT(out == 8 && used == 16);
	EXPECT(Matches("1,3,5,7,9,11,13,15", out));

	Run(8000, 11025, 16, 16, &out, &used);
	EXPECT(out == 16 && used == 11);
	EXPECT(Matches("1,1,2,3,3,4,5,6,6,7,8,8,9,10,11,11", out));
}


static void
TestLimitsAndCounts()
{
	long out, used;

	// a destination too small stops the conversion where it ran out, and
	// both counts say how far each side got
	Run(2, 1, 16, 3, &out, &used);
	EXPECT(out == 3 && used == 6);
	EXPECT(Matches("1,3,5", out));

	Run(1, 2, 16, 5, &out, &used);
	EXPECT(out == 5 && used == 3);
	EXPECT(Matches("1,1,2,2,3", out));

	// an empty side converts nothing and zeroes both counts
	Run(1, 1, 0, 16, &out, &used);
	EXPECT(out == 0 && used == 0);
	Run(1, 1, 16, 0, &out, &used);
	EXPECT(out == 0 && used == 0);
}


// Samples wider than a byte are stepped over by their size, and a converter
// can change the width on the way through.
static void
TestWidthsAndConverter()
{
	static const short samples[8] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };
	short out16[8];

	SampleSpec spec;
	spec.fDstBuffer = out16;
	spec.fDstRate = 1;
	spec.fDstSampleBits = 16;
	spec.fSrcBuffer = (void*) samples;
	spec.fSrcRate = 2;
	spec.fSrcSampleBits = 16;
	spec.fConvert = nil;

	long dstCount = 8, srcCount = 8;
	Resample(&spec, &dstCount, &srcCount);
	EXPECT(dstCount == 4 && srcCount == 8);
	EXPECT(out16[0] == 1000 && out16[1] == 3000 && out16[2] == 5000 && out16[3] == 7000);

	// mu-law in, 16-bit linear out, at the same rate: the same thing the
	// sound channel sets up when a recording's coding is not the hardware's
	UByte codes[8];
	for (int i = 0; i < 8; i++)
	{
		short sample = samples[i];
		SampleConvertLin16ToMuLaw(&codes[i], &sample);
	}
	memset(out16, 0, sizeof(out16));
	spec.fDstRate = 1;
	spec.fSrcRate = 1;
	spec.fSrcBuffer = codes;
	spec.fSrcSampleBits = 8;
	spec.fConvert = SampleConvertMuLawToLin16;
	dstCount = 8;
	srcCount = 8;
	Resample(&spec, &dstCount, &srcCount);
	EXPECT(dstCount == 8 && srcCount == 8);
	for (int i = 0; i < 8; i++)
	{
		UByte back;
		SampleConvertLin16ToMuLaw(&back, &out16[i]);
		EXPECT(back == codes[i]);				// the decoded sample codes back the same
	}
}


int
main()
{
	SetRandSeed(1);
	TestRatios();
	TestLimitsAndCounts();
	TestWidthsAndConverter();
	if (failures == 0)
		printf("test_Resample: all passed\n");
	else
		printf("test_Resample: %d failures\n", failures);
	return failures != 0;
}
