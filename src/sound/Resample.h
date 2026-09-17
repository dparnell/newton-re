/*
	File:		sound/Resample.h

	Contains:	The ROM's plain sample-rate converter - what the sound DMA
				channel puts a buffer through when the sound's rate is not
				the hardware's (TDMAChannel::SetupNode fills a SampleSpec and
				calls Resample).

				The conversion is nearest-neighbour: an accumulator is
				carried from sample to sample, the input rate paying for the
				output rate, so that input samples are dropped when the
				output rate is the lower and repeated when it is the higher.
				Nothing is interpolated and nothing is filtered - that is
				what ResampleFiltered, over a ResampleState and a 160-tap
				history, is for (NOT YET).

				Each sample can be converted on the way through: fConvert is
				one of the sample converters (SampleConvertMuLawToLin16 and
				friends, MuLaw.h), and when it is nil the sample's bytes are
				copied as they are.

	The DDK has no header for these; reconstructed from the MP2100 D ROM
	(0x001e9978), citing its origin.  SampleSpec's field names are ours: the
	structure is not in the DDK either, and its only caller is the sound
	channel, which is not reconstructed.
*/

#ifndef __RESAMPLE_H
#define __RESAMPLE_H

#ifndef __NEWTON_H
#include "Newton.h"
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
void	Resample(const SampleSpec* spec, long* dstCount, long* srcCount);	// ROM 0x001e9978 Resample__FPC10SampleSpecPlT2

#endif	/* __RESAMPLE_H */
