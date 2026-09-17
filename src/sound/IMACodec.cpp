/*
	File:		sound/IMACodec.cpp

	Contains:	The IMA/DVI ADPCM codec (IMACodec.h): CompressIMA, ExpandIMA
				and CheckState, and the standard IMA step-size and
				index-adjustment tables.

	The tables have no debug symbol in the ROM (Ghidra names them
	DAT_0034f5fc / DAT_0034f5dc), so tools/newton-rom/analysis/romtable.py -
	which addresses tables by symbol name - cannot emit them; they are
	written here with their ROM address as the citation, and are the
	canonical IMA tables (verified byte for byte against the ROM image).

	The reconstruction works on the sample *values*: it reads and writes
	host-native 16-bit PCM (the ROM, big-endian, holds those samples
	big-endian in memory, but the values are the same), while the
	compressed stream it produces - the big-endian 2-byte block header and
	the packed nibbles - is byte-for-byte the ROM's, so the two interoperate.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "IMACodec.h"


// ROM 0x0034f5fc (unnamed) - the 89-entry IMA step-size table
const short kIMAStepTable[89] =
{
	    7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
	   19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
	   50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
	  130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
	  337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
	  876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
	 2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
	 5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// ROM 0x0034f5dc (unnamed) - the per-nibble step-index adjustment (the
// sign bit 0x8 does not change the adjustment, so 8..15 mirror 0..7)
const short kIMAIndexTable[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};


// clamp a reconstructed sample to the 16-bit range
static inline long
ClampSample(long value)
{
	if (value > 0x7fff)
		return 0x7fff;
	if (value < -0x8000)
		return -0x8000;
	return value;
}


// The delta a nibble reconstructs at a given step size: step * (1/8 + the
// set fraction bits), negated when the sign bit is set.  Shared by the
// coder and decoder so they track the same predictor.
static inline long
NibbleToDelta(ULong code, long step)
{
	long delta = step >> 3;
	if (code & 4)
		delta += step;
	if (code & 2)
		delta += step >> 1;
	if (code & 1)
		delta += step >> 2;
	if (code & 8)
		delta = -delta;
	return delta;
}


// ROM 0x000e98d0 CompressIMA__FPsPScUlP8IMAStateN23
void
CompressIMA(const short* src, signed char* dst, ULong numSamples, IMAState* state, ULong srcStride, ULong channel)
{
	ULong numBlocks = numSamples >> 6;
	if (numBlocks == 0)
		return;

	long blockSkip = 0;
	if (srcStride == 2)			// one channel of an interleaved pair
	{
		src += channel - 1;
		dst += (channel - 1) * kIMABlockBytes;
		blockSkip = kIMABlockBytes;
	}

	long predictor = state->fPredictor;
	long index = state->fStepIndex;
	long step = kIMAStepTable[index];

	do
	{
		// the block header: the predictor's top 9 bits, the step index
		ULong header = ((ULong) predictor & 0xff80) | (ULong) index;
		dst[0] = (signed char) (header >> 8);
		dst[1] = (signed char) header;
		signed char* out = dst + 2;

		ULong held = 0;
		for (ULong i = 0x40; i != 0; i--)
		{
			long delta = (long) *src - predictor;
			src += srcStride;
			ULong code;
			if (delta < 0)
			{
				code = 8;
				delta = -delta;
			}
			else
				code = 0;

			// quantise |delta| into three magnitude bits against the step
			long threshold = step;
			for (ULong bit = 4; bit != 0; bit >>= 1)
			{
				if (threshold <= delta)
				{
					code |= bit;
					delta -= threshold;
				}
				threshold >>= 1;
			}

			// pack two nibbles per byte: the first of a pair low, the second high
			if (i & 1)
			{
				*out++ = (signed char) (held | (code << 4));
			}
			else
				held = code;

			// reconstruct as the decoder will, and adapt the step
			predictor = ClampSample(predictor + NibbleToDelta(code, step));
			index += kIMAIndexTable[code];
			if (index < 0)
				index = 0;
			else if (index > kIMAMaxStepIndex)
				index = kIMAMaxStepIndex;
			step = kIMAStepTable[index];
		}

		dst = out + blockSkip;
		numBlocks--;
	}
	while (numBlocks != 0);

	state->fPredictor = predictor;
	state->fStepIndex = (short) index;
}


// ROM 0x000e9a7c CheckState__FPScP8IMAState
void
CheckState(const signed char* src, IMAState* state)
{
	ULong header = ((ULong) (unsigned char) src[0] << 8) | (unsigned char) src[1];
	long index = header & 0x7f;
	long predictor = (short) (header & 0xff80);		// sign-extended top bits

	if (index == state->fStepIndex)
	{
		long drift = predictor - state->fPredictor;
		if (drift < 0)
			drift = -drift;
		if (drift < 0x80)
			return;				// still in sync: keep the running state
	}
	state->fPredictor = predictor;
	state->fStepIndex = (short) index;
}


// ROM 0x000e9ad8 ExpandIMA__FPScT1P8IMAStateUlN24
void
ExpandIMA(const signed char* src, void* dst, IMAState* state, ULong numBlocks, ULong numChannels, ULong outFormat)
{
	if (numBlocks == 0)
		return;

	long blockSkip = 2;				// skip the next block's header
	signed char* out8 = (signed char*) dst;
	short* out16 = (short*) dst;
	if (outFormat & 1)				// one channel of an interleaved pair
	{
		long off = numChannels - 1;
		src += off * kIMABlockBytes;
		out8 = (signed char*) dst + off;
		out16 = (short*) dst + off;
		blockSkip = 0x24;
	}

	CheckState(src, state);
	const signed char* in = src + 2;
	long predictor = state->fPredictor;
	long index = state->fStepIndex;
	long step = kIMAStepTable[index];

	do
	{
		ULong byte = 0;
		for (ULong i = 0x40; i != 0; i--)
		{
			ULong code;
			if ((i & 1) == 0)		// even sample: fetch a byte, take the low nibble
			{
				byte = (unsigned char) *in++;
				code = byte;
			}
			else					// odd sample: the high nibble
				code = byte >> 4;
			code &= 0xf;

			predictor = ClampSample(predictor + NibbleToDelta(code, step));

			switch (outFormat)
			{
			case 0:					// 8-bit unsigned, mono
				*out8 = (signed char) ((predictor >> 8) ^ 0x80);
				out8 += 1;
				break;
			case 1:					// 8-bit unsigned, stride 2
				*out8 = (signed char) ((predictor >> 8) ^ 0x80);
				out8 += 2;
				break;
			case 2:					// 16-bit, contiguous
				*out16 = (short) predictor;
				out16 += 1;
				break;
			case 3:					// 16-bit, one channel of a pair
				*out16 = (short) predictor;
				out16 += 2;
				break;
			}

			index += kIMAIndexTable[code];
			if (index < 0)
				index = 0;
			else if (index > kIMAMaxStepIndex)
				index = kIMAMaxStepIndex;
			step = kIMAStepTable[index];
		}
		in += blockSkip;
		numBlocks--;
	}
	while (numBlocks != 0);

	state->fPredictor = predictor;
	state->fStepIndex = (short) index;
}
