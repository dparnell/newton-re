/*
	File:		sound/Resample.cpp

	Contains:	The two sample-rate converters (Resample.h) - Resample, the
				plain one, and ResampleFiltered over a ResampleState.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Resample's accumulator arithmetic is the ROM's: both rates are kept
	doubled, so that the half-step the decimating branch starts with is
	exact.
*/

#include "SampleWords.h"
#include "Resample.h"
#include "SampleConvert.h"


// One sample across: through the spec's converter, or its bytes copied.  The
// copy uses the *destination* sample's size, as the ROM's does.
static inline void
ResampleOne(const SampleSpec* spec, UByte* dst, UByte* src, long dstBytes)
{
	if (spec->fConvert != nil)
		spec->fConvert(dst, src);
	else
		for (long i = 0; i < dstBytes; i++)
			dst[i] = src[i];
}


// ROM 0x001e7560 Resample__FPC10SampleSpecPlT2
void
Resample(const SampleSpec* spec, long* dstCount, long* srcCount)
{
	long srcStep = spec->fSrcRate * 2;
	long dstStep = spec->fDstRate * 2;
	long dstBytes = spec->fDstSampleBits / 8;		// the ROM's shift rounds toward zero
	long srcBytes = spec->fSrcSampleBits / 8;
	UByte* dst = (UByte*) spec->fDstBuffer;
	UByte* src = (UByte*) spec->fSrcBuffer;

	if (*srcCount == 0 || *dstCount == 0)
	{
		*dstCount = 0;
		*srcCount = 0;
		return;
	}

	long out = 0;
	long in = 0;
	if (spec->fSrcRate > spec->fDstRate)
	{
		// down: every input sample is stepped over, only some are written
		long acc = dstStep - (srcStep / 2);			// start half an output step in
		while (in < *srcCount)
		{
			if (acc >= 0)
			{
				if (out == *dstCount)
					break;
				out++;
				ResampleOne(spec, dst, src, dstBytes);
				dst += dstBytes;
				acc -= srcStep;
			}
			acc += dstStep;
			src += srcBytes;
			in++;
		}
	}
	else
	{
		// up (or the same rate): every output sample is written, the input
		// only stepped on when the accumulator has caught up
		long acc = -dstStep;
		while (out < *dstCount)
		{
			ResampleOne(spec, dst, src, dstBytes);
			dst += dstBytes;
			acc += srcStep;
			out++;
			if (acc >= 0 || out == *dstCount)
			{
				in++;
				if (in == *srcCount)
					break;
				src += srcBytes;
				acc -= dstStep;
			}
		}
	}
	*dstCount = out;
	*srcCount = in;
}


/*------------------------------------------------------------------------------
	T h e   f i l t e r e d   c o n v e r t e r

	kResampleFilter is a windowed sinc of 261 points with its peak (1.0 in
	16.16) in the middle and thirteen points to a zero crossing, so it spans
	ten input samples either side of the centre.  An output sample walks the
	table from 0 to its end in steps of thirteen (scaled down when the rate
	is going down, which widens the filter and lowers its cut-off to the
	output's Nyquist limit), reading it at a fractional position and
	interpolating between the two neighbouring points, and multiplies each
	step by the input sample that far back.
------------------------------------------------------------------------------*/

extern const int	kResampleFilter[262];		// ResampleTables.cpp

// ROM 0x0c101b0c gHitInitResampleAgain
long	gHitInitResampleAgain = 0;

enum
{
	kMaxResampleInput	= 20000,		// input samples one call will look at
	kFilterStep			= 0x000d0000,	// 13.0: one input sample, in table points
	kFilterEnd			= 0x01050000,	// 261.0: past the last point of the table
	kResampleTaps		= 0x00140000,	// 20.0: the taps each side at the same rate
};


// ROM 0x001e78c0 InitResampleState__FP13ResampleState
void
InitResampleState(ResampleState* state)
{
	gHitInitResampleAgain++;
	state->fSrcShift = (state->fSrcSampleBits != 8);
	state->fDstShift = (state->fDstSampleBits != 8);
	state->fRatio = FixedDivide(state->fDstRate << 16, state->fSrcRate << 16);
	if (state->fRatio < kFix1)
	{
		// going down: the filter is stretched, so it needs more taps
		Fixed taps = FixedDivide(kResampleTaps, state->fRatio);
		state->fTapCount = (short) ((ULong) (taps + 0x8000) >> 16);
	}
	else
		state->fTapCount = 20;
	state->fPhase = 0;
	for (long i = 0; i < kResampleHistorySize; i++)
		state->fHistory[i] = 0;

	if (state->fSrcFormat == 0)
		state->fSrcConvert = SampleConvertStd8ToLin16;
	else if (state->fSrcFormat == 1)
		state->fSrcConvert = SampleConvertMuLawToLin16;
	else
		state->fSrcConvert = nil;

	if (state->fDstFormat == 0)
		state->fDstConvert = SampleConvertLin16ToStd8;
	else if (state->fDstFormat == 1)
		state->fDstConvert = SampleConvertLin16ToMuLaw;
	else
		state->fDstConvert = nil;
}


// ROM 0x001e7a18 GetSample__FP13ResampleStatel
// The ROM reads the sample with a word load at a halfword address and lets
// the ARM's rotation put it in place, which comes to the same thing.
int
GetSample(ResampleState* state, long index)
{
	if (state->fSrcConvert == nil)
		return GetSampleAt(state->fSrcBuffer, index);
	short sample;			// (a sample in memory, big-endian: SampleWords.h)
	state->fSrcConvert(&sample, (UByte*) state->fSrcBuffer + (index << state->fSrcShift));
	return GetSampleWord(&sample);
}


// ROM 0x001e7a74 PutSample__FP13ResampleStatesl
void
PutSample(ResampleState* state, short value, long index)
{
	if (state->fDstConvert == nil)
		PutSampleAt(state->fDstBuffer, index, value);
	else
	{
		short word;			// (a sample in memory, big-endian: SampleWords.h)
		PutSampleWord(&word, value);
		state->fDstConvert((UByte*) state->fDstBuffer + (index << state->fDstShift), &word);
	}
}


// ROM 0x001e79d0 ResampleFiltered__FP13ResampleStatePlT2
// Everything the long form wants is already in the state.
void
ResampleFiltered(ResampleState* state, long* dstCount, long* srcCount)
{
	ResampleFiltered(state, state->fDstBuffer, state->fSrcBuffer, dstCount, srcCount,
					state->fHistory, state->fTapCount, &state->fPhase, state->fRatio);
}


// ROM 0x001e7ad4 ResampleFiltered__FP13ResampleStatePsT2PlT4T2lT4T7
// dst and src are the state's own buffers: the ROM passes them and then
// reads and writes through GetSample/PutSample, which take them from the
// state, so they go unused here as they do there.
void
ResampleFiltered(ResampleState* state, short* /*dst*/, short* /*src*/,
				long* dstCount, long* srcCount, short* history, long tapCount,
				long* phasePtr, Fixed ratio)
{
	long maxIn = (*srcCount < kMaxResampleInput) ? *srcCount : kMaxResampleInput;
	long outIndex = 0;

	// how far along the table one input sample is, and what the sum has to
	// be scaled by to make up for the wider filter
	Fixed step;
	Fixed gain;
	if (ratio < kFix1)
	{
		step = FixedMultiply(kFilterStep, ratio);
		gain = ratio;
	}
	else
	{
		step = kFilterStep;
		gain = kFix1;
	}
	gain = FixedMultiply(gain, kFix1);			// the ROM's, and a no-op
	Fixed phaseStep = FixedDivide(kFix1, ratio);	// one output sample, in input samples
	Fixed phase = *phasePtr;

	long index;
	for (;;)
	{
		Boolean more = (phase < (maxIn << 16)) && (outIndex < *dstCount);
		index = phase >> 16;					// the input sample the output sits at or after
		if (!more)
			break;

		Fixed sum = 0;
		long tap = 0;
		Fixed at = FixedMultiply(phase & 0xffff, step);		// where in the table this output starts
		Boolean clearOfTheStart = (phase > (tapCount << 16));
		while (at < kFilterEnd && tap <= tapCount)
		{
			long point = at >> 16;
			long coefficient = kResampleFilter[point]
					+ (long) (((ULong) (at & 0xffff)
							* (ULong) (kResampleFilter[point + 1] - kResampleFilter[point])) >> 16);
			long sample;
			if (clearOfTheStart || index - tap >= 0)
				sample = GetSample(state, index - tap);
			else
				sample = history[index + (tapCount + 1) - tap];	// from the last call's tail
			sum += (coefficient * sample) >> 16;
			at += step;
			tap++;
		}

		sum = FixedMultiply(sum, gain);
		if (sum >= 0x8000)
			sum = 0x7fff;
		else if (sum < -0x8000)
			sum = -0x8000;
		PutSample(state, (short) sum, outIndex);
		phase += phaseStep;
		outIndex++;
	}

	// Keep the tail of the input for the next call: the history holds the
	// tapCount + 1 samples up to where the reading stopped, newest last.
	long newest = maxIn - 1;
	if (index - 1 < newest)
		newest = index - 1;
	long shift = tapCount - newest;
	for (long i = 0; i < shift; i++)
		history[i] = history[newest + i + 1];
	for (long slot = tapCount; slot >= 0 && newest >= 0; slot--, newest--)
		history[slot] = (short) GetSample(state, newest);

	long used = (index < maxIn) ? index : maxIn;
	*phasePtr = phase - (used << 16);
	*srcCount = used;
	*dstCount = outIndex;
}
