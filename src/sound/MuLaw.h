/*
	File:		sound/MuLaw.h

	Contains:	The ROM's mu-law conversions - the 8-bit companded coding of
				telephony (CCITT G.711) that the Newton's sound system uses
				for 8-bit sampled sound.

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
	(0x001e9880-0x001e9cd8), each function citing its origin.  The ROM works
	on samples held in memory big-endian; the reconstruction reads and
	writes host-native shorts, as sound/IMACodec.h does, so that the sample
	values are the same on any host.
*/

#ifndef __MULAW_H
#define __MULAW_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


// One sample at a time: dst and src point at a mu-law byte and a 16-bit
// linear sample respectively (the ROM takes both as void*).
void	SampleConvertLin16ToMuLaw(void* dst, void* src);	// ROM 0x001e9880 SampleConvertLin16ToMuLaw__FPvT1
void	SampleConvertMuLawToLin16(void* dst, void* src);	// ROM 0x001e9b90 SampleConvertMuLawToLin16__FPvT1

// A block at a time: the counts are in samples, and both come back as the
// number converted - the smaller of the two.
void	BlockConvertLin16ToMuLaw(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001e98e0 BlockConvertLin16ToMuLaw__FPvPlT1T2
void	BlockConvertMuLawToLin16(void* dst, long* dstCount, void* src, long* srcCount);	// ROM 0x001e9c14 BlockConvertMuLawToLin16__FPvPlT1T2

#endif	/* __MULAW_H */
