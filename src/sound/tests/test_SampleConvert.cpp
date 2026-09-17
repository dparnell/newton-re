// Host unit test for the sample converters (src/sound/SampleConvert.h):
// mu-law, and the offset-binary "standard" 8-bit coding.
//
// The ROM's variant is not the standard G.711 one (a 14-bit magnitude biased
// by 33, not a 13-bit one biased by 132), so there is no outside oracle to
// check it against.  What is checked instead is what the coding promises:
//
//  * every code decodes to a value that codes back to the same code, which
//    pins the exponent/mantissa split and the bias at both ends (the two
//    codes for zero are the one exception, as in every sign-magnitude
//    coding: negative zero comes back as positive zero);
//  * the decoded values rise with the code and their steps double once per
//    exponent, which is what companding means;
//  * the round trip of a linear ramp stays within the step of the code it
//    lands in;
//  * the block forms convert the same bytes as the sample forms and report
//    the smaller of the two counts;
//  * the two ways the loudest samples overflow the exponent search, the ROM
//    never having clamped them (bugs the reconstruction keeps).
//
// "Standard" 8-bit, being a plain shift, is checked directly: silence, the
// two extremes, the rounding toward zero, and that every code decodes within
// its own step and codes back to itself.
//
// Decoding dithers the two low bits with QuickDraw's Random, so the test
// seeds it and compares values only above those two bits where it matters.

#include "SampleConvert.h"
#include "Ports.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static short
Decode(UByte code)
{
	short sample;
	UByte in = code;
	SampleConvertMuLawToLin16(&sample, &in);
	return sample;
}

static UByte
Encode(short sample)
{
	UByte code;
	short in = sample;
	SampleConvertLin16ToMuLaw(&code, &in);
	return code;
}


// Every code but negative zero (0x7F) survives decode -> encode.
static void
TestRoundTripOfEveryCode()
{
	for (int c = 0; c <= 0xFF; c++)
	{
		short sample = Decode((UByte) c);
		UByte back = Encode(sample);
		if (c == 0x7F)
		{
			// negative zero decodes to zero, which codes as positive zero
			EXPECT((sample >> 2) == 0 && back == 0xFF);
			continue;
		}
		EXPECT(back == (UByte) c);
	}
}


// The decoded magnitudes rise with the mantissa and double their step with
// each exponent - the companding curve.
static void
TestCompandingCurve()
{
	long previous = -1;
	long step = 0;
	for (int exponent = 0; exponent < 8; exponent++)
	{
		long thisStep = 0;
		for (int mantissa = 0; mantissa < 16; mantissa++)
		{
			UByte code = (UByte) ~((UByte) ((exponent << 4) | mantissa));	// positive, complemented
			long value = Decode(code) >> 2;				// past the dithered bits
			EXPECT(value > previous);
			if (previous >= 0 && mantissa > 0)
			{
				if (thisStep == 0)
					thisStep = value - previous;
				EXPECT(value - previous == thisStep);	// even steps within an exponent
			}
			previous = value;
		}
		if (step != 0)
			EXPECT(thisStep == step * 2);				// and twice the step one exponent down
		step = thisStep;
	}
	EXPECT(step == 256);						// 2, 4, 8, ... 256 over the eight exponents
}


// A ramp through the coded range comes back within the step of the code it
// lands in.
static void
TestRampRoundTrip()
{
	for (long sample = -23000; sample <= 23000; sample += 37)
	{
		UByte code = Encode((short) sample);
		long back = Decode(code);
		long exponent = (((UByte) ~code) >> 4) & 7;
		long step = (1L << exponent) * 4;			// in 16-bit sample units
		long error = back - sample;
		if (error < 0)
			error = -error;
		EXPECT(error <= step + 3);					// the step, plus the dithered two bits
	}
}


// The block forms do what the sample forms do, and answer how many samples
// they converted.
static void
TestBlockConversion()
{
	static const short samples[8] = { 0, 100, -100, 4000, -4000, 20000, -20000, 1 };
	UByte codes[8];
	UByte oneAtATime[8];
	for (int i = 0; i < 8; i++)
		oneAtATime[i] = Encode(samples[i]);

	long dstCount = 8, srcCount = 8;
	BlockConvertLin16ToMuLaw(codes, &dstCount, (void*) samples, &srcCount);
	EXPECT(dstCount == 8 && srcCount == 8);
	EXPECT(memcmp(codes, oneAtATime, sizeof(codes)) == 0);

	// the shorter of the two counts wins, and both come back as it
	dstCount = 3;
	srcCount = 8;
	memset(codes, 0xAA, sizeof(codes));
	BlockConvertLin16ToMuLaw(codes, &dstCount, (void*) samples, &srcCount);
	EXPECT(dstCount == 3 && srcCount == 3);
	EXPECT(memcmp(codes, oneAtATime, 3) == 0 && codes[3] == 0xAA);

	// and back again
	short out[8];
	dstCount = 8;
	srcCount = 8;
	BlockConvertMuLawToLin16(out, &dstCount, oneAtATime, &srcCount);
	EXPECT(dstCount == 8 && srcCount == 8);
	for (int i = 0; i < 8; i++)
		EXPECT(Encode(out[i]) == oneAtATime[i]);

	dstCount = 8;
	srcCount = 2;
	out[2] = 0x1234;
	BlockConvertMuLawToLin16(out, &dstCount, oneAtATime, &srcCount);
	EXPECT(dstCount == 2 && srcCount == 2 && out[2] == 0x1234);
}


// The ROM never clamps the magnitude to what the coding can hold (G.711's
// implementations clip at the top of the last segment), so the loudest
// samples overflow the eight-position exponent search in two different ways,
// both of which the reconstruction keeps:
//
//  * 32636..32763, and -32760..-32633, leave no bit set in the low eight of
//    the shifted magnitude, so the search runs off the end, the exponent
//    stays -1 and the mantissa is shifted by 255 - zero, on the ARM.  The
//    code comes out 0x0F whatever the sign, and 0x0F stands for about
//    -16764: a loud positive sample is decoded as a loud negative one.
//  * louder still - 32764..32767 and -32768..-32761 - the search wraps round
//    to exponent 0 and the mantissa keeps only its bottom four bits, which
//    are zero, so the codes are 0xFF and 0x7F: silence.
static void
TestTheLoudSampleBugs()
{
	EXPECT(Encode(32635) == 0x80);				// just below: the loudest code
	for (long sample = 32636; sample <= 32763; sample++)
		EXPECT(Encode((short) sample) == 0x0F);
	for (long sample = 32764; sample <= 32767; sample++)
		EXPECT(Encode((short) sample) == 0xFF);	// positive zero

	EXPECT(Encode(-32632) == 0x00);				// just below: the loudest negative code
	for (long sample = -32760; sample <= -32633; sample++)
		EXPECT(Encode((short) sample) == 0x0F);
	for (long sample = -32768; sample <= -32761; sample++)
		EXPECT(Encode((short) sample) == 0x7F);	// negative zero

	// and 0x0F is a code the coding uses properly as well, for the loud
	// negative samples it really stands for
	EXPECT(Encode(-16764) == 0x0F);
	EXPECT((Decode(0x0F) >> 2) == -4191);
}


// "Standard" 8-bit is offset binary: the sample's top eight bits with 0x80
// added, so silence is 0x80, full positive 0xFF and full negative 0x00.  The
// decoder dithers the eight bits the coding dropped with five bits of
// Random, so a decoded sample is within 0x1F of the code's own value.
static void
TestStd8()
{
	UByte code;
	short sample;

	sample = 0;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x80);						// silence
	sample = 32767;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0xFF);
	sample = -32768;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x00);

	// the shift rounds toward zero, so the values either side of silence are
	// the codes either side of 0x80 only once they reach a whole step
	sample = 255;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x80);
	sample = 256;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x81);
	sample = -255;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x80);						// toward zero, not down
	sample = -256;
	SampleConvertLin16ToStd8(&code, &sample);
	EXPECT(code == 0x7F);

	// Every code decodes into its own step, and codes back to itself - except
	// that the dither always moves the value up, while the coder rounds
	// toward zero, so a negative code can come back one step higher.  That is
	// the ROM's arithmetic, not a slip in the reconstruction: the decoder ORs
	// its five random bits in whatever the sign.
	for (int c = 0; c <= 0xFF; c++)
	{
		UByte in = (UByte) c;
		SampleConvertStd8ToLin16(&sample, &in);
		long expected = ((long) c << 8) - 0x8000;
		EXPECT(sample >= expected && sample <= expected + 0x1F);	// the dither, and nothing more
		SampleConvertLin16ToStd8(&code, &sample);
		if (c < 0x80)
			EXPECT(code == (UByte) c || code == (UByte) (c + 1));
		else
			EXPECT(code == (UByte) c);
	}

	// the block forms match, and answer the smaller of the two counts
	static const short samples[6] = { 0, 8000, -8000, 32767, -32768, 300 };
	UByte codes[6];
	UByte oneAtATime[6];
	for (int i = 0; i < 6; i++)
	{
		short one = samples[i];
		SampleConvertLin16ToStd8(&oneAtATime[i], &one);
	}
	long dstCount = 6, srcCount = 6;
	BlockConvertLin16ToStd8(codes, &dstCount, (void*) samples, &srcCount);
	EXPECT(dstCount == 6 && srcCount == 6);
	EXPECT(memcmp(codes, oneAtATime, sizeof(codes)) == 0);

	short out[6];
	dstCount = 4;
	srcCount = 6;
	out[4] = 0x1234;
	BlockConvertStd8ToLin16(out, &dstCount, oneAtATime, &srcCount);
	EXPECT(dstCount == 4 && srcCount == 4 && out[4] == 0x1234);
	for (int i = 0; i < 4; i++)
	{
		SampleConvertLin16ToStd8(&code, &out[i]);
		EXPECT(code == oneAtATime[i] || code == (UByte) (oneAtATime[i] + 1));
	}
}


int
main()
{
	SetRandSeed(1);
	TestStd8();
	TestRoundTripOfEveryCode();
	TestCompandingCurve();
	TestRampRoundTrip();
	TestBlockConversion();
	TestTheLoudSampleBugs();
	if (failures == 0)
		printf("test_SampleConvert: all passed\n");
	else
		printf("test_SampleConvert: %d failures\n", failures);
	return failures != 0;
}
