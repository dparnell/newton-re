/*
	File:		sound/Resample.h

	Contains:	The ROM's two sample-rate converters - what a sound buffer
				goes through when its rate is not the hardware's.

				Resample is the plain one, which the sound DMA channel uses
				(TDMAChannel::SetupNode fills a SampleSpec and calls it).

				The conversion is nearest-neighbour: an accumulator is
				carried from sample to sample, the input rate paying for the
				output rate, so that input samples are dropped when the
				output rate is the lower and repeated when it is the higher.
				Nothing is interpolated and nothing is filtered.

				ResampleFiltered is the good one: a windowed sinc, held in
				the ROM as 261 points at thirteen to the zero crossing
				(kResampleFilter, ResampleTables.cpp), read at a fractional
				position and interpolated between neighbours.  Each output
				sums up to fTapCount + 1 input samples through it; the
				samples before the buffer's start come from the history the
				last call left behind, which is why the converter carries a
				ResampleState rather than a spec.  Twenty taps are used when
				the rate is going up, twenty divided by the ratio when it is
				going down, so that the filter stays at the lower of the two
				Nyquist limits.

				Each sample can be converted on the way through: fConvert is
				one of the sample converters (SampleConvertMuLawToLin16 and
				friends, SampleConvert.h), and when it is nil the sample's bytes are
				copied as they are.

	The DDK has no header for these; reconstructed from the MP2x00 US ROM
	(0x001e7560 and 0x001e78c0-0x001e7e04), each function citing its origin.
	SampleSpec's and ResampleState's field names are ours: neither structure
	is in the DDK, and their callers - the sound channel and the codecs - are
	not reconstructed.
*/

#ifndef __RESAMPLE_H
#define __RESAMPLE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __FIXEDMATH_H
#include "FixedMath.h"
#endif


struct SampleSpec
{
	void*	fDstBuffer;			// +0x00  where the converted samples go
	long	fDstRate;			// +0x04  samples a second out
	long	fDstSampleBits;		// +0x08  8 or 16 (the ROM divides by 8 for the stride)
	void*	fSrcBuffer;			// +0x0c  where they come from
	long	fSrcRate;			// +0x10  samples a second in
	long	fSrcSampleBits;		// +0x14
	void	(*fConvert)(void* dst, void* src);	// +0x18  a sample converter, or nil to copy the bytes
};


// Convert as many samples as both counts allow; each count comes back as
// what was used - outputs written in *dstCount, inputs read in *srcCount.
void	Resample(const SampleSpec* spec, long* dstCount, long* srcCount);	// ROM 0x001e7560 Resample__FPC10SampleSpecPlT2


/*------------------------------------------------------------------------------
	R e s a m p l e S t a t e
	What the filtered converter carries from call to call: where the two
	buffers are and how their samples are coded, the rate ratio, how far
	into the input the next output sits, and the tail of the input the next
	call will need for its filter.  A format of 0 is "standard" 8-bit and 1
	is mu-law (SampleConvert.h); anything else is 16-bit linear, and then
	the converter is nil and the samples are read and written as they are.
------------------------------------------------------------------------------*/

enum { kResampleHistorySize = 160 };		// the largest fTapCount + 1 the state holds

struct ResampleState
{
	long	fTapCount;			// +0x00  input samples either side of an output (20, or 20 / the ratio)
	Fixed	fPhase;				// +0x04  where in the input the next output sits
	Fixed	fRatio;				// +0x08  fDstRate / fSrcRate
	short*	fDstBuffer;			// +0x0c
	long	fDstRate;			// +0x10
	long	fDstSampleBits;		// +0x14
	long	fDstFormat;			// +0x18
	long	fDstShift;			// +0x1c  0 for 8-bit samples, 1 for wider
	short*	fSrcBuffer;			// +0x20
	long	fSrcRate;			// +0x24
	long	fSrcSampleBits;		// +0x28
	long	fSrcFormat;			// +0x2c
	long	fSrcShift;			// +0x30
	void	(*fSrcConvert)(void* dst, void* src);	// +0x34  a sample into 16-bit linear
	void	(*fDstConvert)(void* dst, void* src);	// +0x38  16-bit linear into a sample
	short	fHistory[kResampleHistorySize];			// +0x3c  the tail of the input, newest last
};

// Work out fTapCount, fRatio, the shifts and the converters from the rates,
// sample sizes and formats already in the state, and empty the history.
void	InitResampleState(ResampleState* state);			// ROM 0x001e78c0 InitResampleState__FP13ResampleState

// One sample in or out of the state's buffers, through its converter.
int		GetSample(ResampleState* state, long index);			// ROM 0x001e7a18 GetSample__FP13ResampleStatel
void	PutSample(ResampleState* state, short value, long index);	// ROM 0x001e7a74 PutSample__FP13ResampleStatesl

// The filtered conversion.  The short form takes everything from the state;
// the long one is what it calls, with the buffers, the history, the tap
// count, the phase and the ratio spelled out (the ROM passes the two buffers
// but reads and writes through the state, so they are unused).
void	ResampleFiltered(ResampleState* state, long* dstCount, long* srcCount);	// ROM 0x001e79d0 ResampleFiltered__FP13ResampleStatePlT2
void	ResampleFiltered(ResampleState* state, short* dst, short* src,
					long* dstCount, long* srcCount, short* history, long tapCount,
					long* phase, Fixed ratio);			// ROM 0x001e7ad4 ResampleFiltered__FP13ResampleStatePsT2PlT4T2lT4T7

extern long	gHitInitResampleAgain;		// ROM 0x0c101b0c gHitInitResampleAgain - how often a state has been set up

#endif	/* __RESAMPLE_H */
