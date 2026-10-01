/*
	File:		sound/DTMFCodec.cpp

	Contains:	TDTMFCodec, the tone synthesiser behind the dialler's touch
				tones (SoundCodec.h), and the sine lookups it runs on.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The arithmetic is the ROM's 32-bit arithmetic: products wrap, and the
	envelope times compare unsigned.
*/

#include "SampleWords.h"
#include "SoundCodec.h"
#include "FixedMath.h"

#include <stdint.h>

extern ULong	gMainCPUType;		// ROM 0x0c1008dc gMainCPUType

PROTOCOL_IMPL_SOURCE_MACRO(TDTMFCodec)		// ROM 0x00088128 Sizeof__10TDTMFCodecSFv
PROTOCOL_CLASSINFO(TDTMFCodec, "TSoundCodec", "", 0, 0, nil)	// ROM 0x00388fa4 ClassInfo__10TDTMFCodecSFv


static inline int32_t
Mul32(int32_t a, int32_t b)
{
	return (int32_t) ((uint32_t) a * (uint32_t) b);
}


// The ARM's add: a sum past the word wraps.  The ROM's mixing and phase
// arithmetic relies on it (the host's signed add would trap instead -
// the tour stack of NewtCard dialled into it).
static inline int32_t
Add32(int32_t a, int32_t b)
{
	return (int32_t) ((uint32_t) a + (uint32_t) b);
}


// A 1/8-degree step of the sine's whole turn (0..0xb40) out of the
// quarter-wave table, 2.30.
static int32_t
QuarterSine(long i)
{
	if (i <= 0x2d0)
		return quarterSineWaveTable[i];
	if (i <= 0x5a0)
		return quarterSineWaveTable[0x5a0 - i];
	if (i <= 0x870)
		return -quarterSineWaveTable[i - 0x5a0];
	if (i <= 0xb40)
		return -quarterSineWaveTable[0xb40 - i];
	return 0;
}


// ROM 0x00088140 SinTable__Fl
// The sine of an angle in 16.16 degrees, 16.16, to the nearest eighth of a
// degree.  (The ROM reads the table through its uncached alias, 0x03870744.)
long
SinTable(long degrees)
{
	int32_t r = (int32_t) degrees % 0x1680000;
	if (r < 0)
		r += 0x1680000;
	long i = (short) (r >> 13);
	return (QuarterSine(i) + 0x2000) >> 14;
}


// ROM 0x000881e4 SinTableInterp__Fl
// The same, interpolated between the two eighths of a degree either side.
long
SinTableInterp(long degrees)
{
	int32_t r = (int32_t) degrees % 0x1680000;
	if (r < 0)
		r += 0x1680000;
	uint32_t eighths = (uint32_t) r << 3;
	Fixed fraction = (Fixed) ((eighths & 0xffff) << 14);
	long i = eighths >> 16;
	int32_t v0 = QuarterSine(i);
	int32_t v1 = QuarterSine(i + 1);
	int32_t v = FractMultiply(v1 - v0, fraction) + v0;
	return (v + 0x2000) >> 14;
}


// ROM 0x00088130 New__10TDTMFCodecFv
TDTMFCodec*
TDTMFCodec::New()
{
	for (size_t i = 0; i < sizeof(fWords) / sizeof(fWords[0]); i++)
		fWords[i] = 0;
	fScore = nil;
	W(0x2a0) = 0;
	return this;
}


// ROM 0x00088310 Delete__10TDTMFCodecFv
void
TDTMFCodec::Delete()
{ }


// ROM 0x00088314 Init__10TDTMFCodecFP10CodecBlock
NewtonErr
TDTMFCodec::Init(CodecBlock* /*block*/)
{
	return noErr;
}


// ROM 0x0008831c Reset__10TDTMFCodecFP10CodecBlock
// A new score: the tones' phases and envelopes silent, the repeats not
// begun.
NewtonErr
TDTMFCodec::Reset(CodecBlock* block)
{
	fScore = (const unsigned char*) block->fBuffer;
	W(0x2a8) = block->fFormat;
	W(0x2ac) = block->fSampleRate;
	W(0x2b0) = block->fSampleBits;
	W(0x29c) = block->fSize;
	W(0x2a0) = 0;
	W(0x138) = 0;
	W(0x13c) = 0;
	W(0x134) = 0;
	W(0x140) = 0;
	W(0x2a4) = 0;
	for (long i = 0; i < 12; i++)
	{
		W(0x204 + 4 * i) = 0;
		W(0x234 + 4 * i) = 0;
		W(0x264 + 4 * i) = 0;
	}
	return noErr;
}


// A halfword of the score (big-endian, as the ARM's unaligned loads
// rotated it into place).
static inline uint32_t
Half(const unsigned char* p)
{
	return ((uint32_t) p[0] << 8) | p[1];
}


// The slopes Produce works out, as the ROM lays them out on its stack -
// the release, decay and attack of the modulators' envelopes, then the
// carriers' - so a chained algorithm reading past twelve tones reads the
// next array, as the ROM's does.
enum { kR2 = 0, kD2 = 12, kA2 = 24, kR = 36, kD = 48, kA = 60, kSlopes = 72 + 2 };


// One tone's envelope moved on to this tick: silent until t0, rising to
// t1, falling to t2, held at the sustain level to t3, released to t4.
static inline void
StepEnvelope(int32_t& env, uint32_t tick, uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4,
			 int32_t attack, int32_t decay, int32_t release, int32_t level)
{
	if (tick <= t0)
		env = 0;
	if (t0 <= tick && tick <= t1)
		env = Add32(env, attack);
	if (t1 <= tick && tick <= t2)
		env = Add32(env, (int32_t) (0u - (uint32_t) decay));
	if (t2 <= tick && tick <= t3)
		env = level;
	if (t3 != t4)
	{
		if (t3 < tick && tick <= t4)
			env = Add32(env, (int32_t) (0u - (uint32_t) release));
		if (t4 < tick)
			env = 0;
	}
	if (env < 0)
		env = 0;
}


// ROM 0x00088388 Produce__10TDTMFCodecFPvPUlT2P10CodecBlock
// As many 16-bit samples of the score as the buffer holds - whatever the
// block's sample size says - the envelopes worked out afresh each time
// from the score and the rate; when the longest tone has ended the score
// starts again, until it has been played its repeat count more times.
NewtonErr
TDTMFCodec::Produce(void* dst, ULong* dstSize, ULong* codedSize, CodecBlock* block)
{
	NewtonErr err = noErr;
	if (fScore == nil)
		return kSoundErrNoBuffer;
	const unsigned char* score = fScore;
	if (Half(score) != 1)
		return kSoundErrNoBuffer;
	int32_t rate = W(0x2ac);
	W(0x10) = FixedDivide(0x10000, rate);
	long perMillisecond = (short) ((uint32_t) (FixedDivide(rate, 0x3e80000) + 0x8000) >> 16);
	uint32_t tones = Half(score + 8);
	if (tones > 12)
		tones = 12;
	long (*sine)(long) = (gMainCPUType == 3) ? SinTableInterp : SinTable;
	W(0x138) = Half(score + 6);
	int32_t slopes[kSlopes];
	for (long i = 0; i < kSlopes; i++)
		slopes[i] = 0;
	uint32_t k;
	for (k = 0; k < tones; k++)
	{
		const unsigned char* tone = score + k * 0x14;
		uint32_t t = perMillisecond * Half(tone + 0x10);
		W(0x14 + 4 * k) = t;
		t = perMillisecond * Half(tone + 0x12) + t;
		W(0x44 + 4 * k) = t;
		t = perMillisecond * Half(tone + 0x14) + t;
		W(0x74 + 4 * k) = t;
		t = perMillisecond * Half(tone + 0x16) + t;
		W(0xa4 + 4 * k) = t;
		t = perMillisecond * Half(tone + 0x18) + t;
		W(0xd4 + 4 * k) = t;
		W(0x104 + 4 * k) = perMillisecond * Half(tone + 0x1c) + t;
		Fixed frequency = (Fixed) ((Half(tone + 0xa) << 16) | Half(tone + 0xc));
		W(0x144 + 4 * k) = frequency;
		int32_t peak = (int32_t) (Half(tone + 0xe) << 16);
		W(0x174 + 4 * k) = peak;
		int32_t peak2 = peak;
		W(0x1a4 + 4 * k) = peak2;
		uint32_t sustain = Half(tone + 0x1a);
		if (peak <= (int32_t) (sustain << 16))
		{
			peak = sustain << 16;
			peak2 = sustain << 16;
		}
		W(0x1d4 + 4 * k) = FixedMultiplyDivide(frequency, 0x1680000, rate);
		if ((uint32_t) W(0x104 + 4 * k) > (uint32_t) W(0x134))
			W(0x134) = W(0x104 + 4 * k);
		int32_t t0 = W(0x14 + 4 * k), t1 = W(0x44 + 4 * k), t2 = W(0x74 + 4 * k), t3 = W(0xa4 + 4 * k), t4 = W(0xd4 + 4 * k);
		slopes[kA + k] = FixedDivide(peak, (t1 - t0) * 0x10000);
		slopes[kA2 + k] = FixedDivide(peak2, (t1 - t0) * 0x10000);
		slopes[kD + k] = FixedDivide(peak - W(0x174 + 4 * k), (t2 - t1) * 0x10000);
		slopes[kD2 + k] = FixedDivide(peak2 - W(0x1a4 + 4 * k), (t2 - t1) * 0x10000);
		slopes[kR + k] = FixedDivide(W(0x174 + 4 * k), (t4 - t3) * 0x10000);
		slopes[kR2 + k] = FixedDivide(W(0x1a4 + 4 * k), (t4 - t3) * 0x10000);
	}
	for (; k < 12; k++)
	{
		W(0x14 + 4 * k) = 0;
		W(0x44 + 4 * k) = 0;
		W(0x74 + 4 * k) = 0;
		W(0xa4 + 4 * k) = 0;
		W(0xd4 + 4 * k) = 0;
		W(0x104 + 4 * k) = 0;
		W(0x144 + 4 * k) = 0;
		W(0x174 + 4 * k) = 0;
		W(0x1a4 + 4 * k) = 0;
		W(0x1d4 + 4 * k) = 0;
	}
	// (the two stack words above the carriers' attacks, which a chain
	// reading past twelve tones would reach: the sample count and index)
	uint32_t samples = *dstSize >> 1;
	slopes[72] = samples;
	uint32_t algorithm = Half(score + 2);
	long group = 0;
	switch (algorithm)
	{
	case 0:	group = 1;	break;
	case 1:	group = 2;	break;
	case 2:	group = 3;	break;
	case 3:	group = 3;	break;
	case 4:	group = 4;	break;
	}
	short* out = (short*) dst;
	for (uint32_t n = 0; group != 0 && n < samples; n++)
	{
		slopes[73] = n;
		int32_t acc = 0;
		uint32_t tick = (uint32_t) ++W(0x2a4);
		for (uint32_t c = 0; c < tones; c += group)
		{
			int32_t s;
			switch (algorithm)
			{
			case 0:
				s = sine(W(0x204 + 4 * c));
				break;
			case 1:
				s = sine(Add32(Mul32(W(0x268 + 4 * c) >> 16, sine(W(0x208 + 4 * c))), W(0x204 + 4 * c)));
				break;
			case 2:
				s = sine(Add32(Add32(Mul32(W(0x26c + 4 * c) >> 16, sine(W(0x20c + 4 * c))),
								 Mul32(W(0x268 + 4 * c) >> 16, sine(W(0x208 + 4 * c)))), W(0x204 + 4 * c)));
				break;
			case 3:
				s = sine(Add32(Mul32(W(0x26c + 4 * c) >> 16, sine(W(0x20c + 4 * c))), W(0x208 + 4 * c)));
				s = sine(Add32(Mul32(W(0x268 + 4 * c) >> 16, s), W(0x204 + 4 * c)));
				break;
			default:
				s = sine(Add32(Mul32(W(0x270 + 4 * c) >> 16, sine(W(0x210 + 4 * c))), W(0x20c + 4 * c)));
				s = sine(Add32(Mul32(W(0x26c + 4 * c) >> 16, s), W(0x208 + 4 * c)));
				s = sine(Add32(Mul32(W(0x268 + 4 * c) >> 16, s), W(0x204 + 4 * c)));
				break;
			}
			acc = Add32(Mul32(W(0x234 + 4 * c) >> 16, s), acc);
			for (long j = 0; j < group; j++)
			{
				int32_t phase = Add32(W(0x204 + 4 * (c + j)), W(0x1d4 + 4 * (c + j)));
				W(0x204 + 4 * (c + j)) = phase;
				if (0x1680000 < phase)
					W(0x204 + 4 * (c + j)) = phase - 0x1680000;
			}
			StepEnvelope(W(0x234 + 4 * c), tick, W(0x14 + 4 * c), W(0x44 + 4 * c), W(0x74 + 4 * c), W(0xa4 + 4 * c), W(0xd4 + 4 * c),
						 slopes[kA + c], slopes[kD + c], slopes[kR + c], W(0x174 + 4 * c));
			for (long j = 1; j < group; j++)
			{
				uint32_t m = c + j;
				StepEnvelope(W(0x264 + 4 * m), tick, W(0x14 + 4 * m), W(0x44 + 4 * m), W(0x74 + 4 * m), W(0xa4 + 4 * m), W(0xd4 + 4 * m),
							 slopes[kA2 + m], slopes[kD2 + m], slopes[kR2 + m], W(0x1a4 + 4 * m));
			}
		}
		if ((uint32_t) W(0x134) < (uint32_t) W(0x2a4))
		{
			if (W(0x13c) == W(0x138))
				W(0x140) = 1;
			else
			{
				W(0x13c) = W(0x13c) + 1;
				W(0x2a4) = 0;
			}
		}
		// (the sample's two bytes high first, as the ROM writes them)
		PutSampleWord(out++, (short) ((uint32_t) Add32(acc, 0x8000) >> 16));
	}
	*dstSize = samples << 1;
	*codedSize = 0;
	block->fSampleBits = W(0x2b0);
	block->fFormat = kSoundFormatLinear16;
	block->fSampleRate = W(0x2ac);
	return err;
}


// ROM 0x00089a5c Consume__10TDTMFCodecFPCvPUlT2PC10CodecBlock
// (A synthesiser records nothing.)
NewtonErr
TDTMFCodec::Consume(const void* /*src*/, ULong* /*srcSize*/, ULong* codedSize, const CodecBlock* /*block*/)
{
	W(0x2a0) = W(0x29c);
	*codedSize = 0;
	return kSoundErrNoBuffer;
}


// ROM 0x00089a78 Start__10TDTMFCodecFv
void
TDTMFCodec::Start()
{ }


// ROM 0x00089a7c Stop__10TDTMFCodecFi
void
TDTMFCodec::Stop(int /*reason*/)
{ }


// ROM 0x00089a80 BufferCompleted__10TDTMFCodecFv
// Every repeat of the score played.
Boolean
TDTMFCodec::BufferCompleted()
{
	return W(0x140) & 0xff;
}
