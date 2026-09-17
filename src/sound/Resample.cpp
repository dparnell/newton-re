/*
	File:		sound/Resample.cpp

	Contains:	Resample (Resample.h).

	Reconstructed from the MP2100 D ROM; the function cites its origin.
	The accumulator arithmetic is the ROM's: both rates are kept doubled, so
	that the half-step the decimating branch starts with is exact.
*/

#include "Resample.h"


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


// ROM 0x001e9978 Resample__FPC10SampleSpecPlT2
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
