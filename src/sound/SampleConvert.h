/*
	File:		sound/SampleConvert.h

	Contains:	The ROM's sample converters - the two 8-bit codings that
				16-bit linear sound is turned into and back from, a sample or
				a block at a time.  The sound DMA channel picks a pair by the
				formats at each end of a buffer, and Resample.h's filtered
				converter calls them for every sample it reads and writes.

				"Standard" 8-bit is offset binary: the sample's top eight
				bits with 0x80 added, so silence is 0x80.  Going the other
				way dithers the eight bits it cannot carry with five bits
				from QuickDraw's Random.

				Mu-law is the 8-bit companded coding of telephony (CCITT
				G.711).

				A mu-law byte is a sign bit, a three-bit exponent and a
				four-bit mantissa, stored complemented (so silence is 0xFF
				and the byte's ordering follows the sample's).  The ROM's
				variant works on the 16-bit sample shifted down by two - a
				14-bit magnitude - and biases it by 33 rather than the usual
				132, so its loudest code stands for about 0x5D7C rather than
				full scale.

				Decoding dithers the two bits the coding cannot carry with
				QuickDraw's Random (qd/Ports.h), which is why the sound code
				reaches into the graphics library for a random number.

	The DDK has no header for these; reconstructed from the MP2100 D ROM
	(0x001e980c-0x001e9cd8 and 0x001ea21c-0x001ea2e0), each function citing
	its origin.  The ROM works
	on samples held in memory big-endian; the reconstruction reads and
	writes host-native shorts, as sound/IMACodec.h does, so that the sample
	values are the same on any host.
*/

#ifndef __SAMPLECONVERT_H
#define __SAMPLECONVERT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


// One sample at a time: dst and src point at the coded byte and the 16-bit
// linear sample respectively (the ROM takes both as void*).
void	SampleConvertLin16ToStd8(void* dst, void* src);		// ROM 0x001e980c SampleConvertLin16ToStd8__FPvT1
void	SampleConvertStd8ToLin16(void* dst, void* src);		// ROM 0x001ea21c SampleConvertStd8ToLin16__FPvT1
void	SampleConvertLin16ToMuLaw(void* dst, void* src);	// ROM 0x001e9880 SampleConvertLin16ToMuLaw__FPvT1
void	SampleConvertMuLawToLin16(void* dst, void* src);	// ROM 0x001e9b90 SampleConvertMuLawToLin16__FPvT1

// A block at a time: the counts are in samples, and both come back as the
// number converted - the smaller of the two.
void	BlockConvertLin16ToStd8(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001e9828 BlockConvertLin16ToStd8__FPvPlT1T2
void	BlockConvertStd8ToLin16(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001ea260 BlockConvertStd8ToLin16__FPvPlT1T2
void	BlockConvertLin16ToMuLaw(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001e98e0 BlockConvertLin16ToMuLaw__FPvPlT1T2
void	BlockConvertMuLawToLin16(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001e9c14 BlockConvertMuLawToLin16__FPvPlT1T2

#endif	/* __MULAW_H */
