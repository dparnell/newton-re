/*
	File:		sound/SampleConvert.cpp

	Contains:	The sample converters (SampleConvert.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The ROM has the sample and the block forms as separate functions with
	the conversion written out in each; the reconstruction keeps the two
	entry points and shares the arithmetic, which is instruction for
	instruction the same in both.
*/

#include "SampleConvert.h"
#include "Ports.h"					// QuickDraw's Random, which the decoder dithers with


/*------------------------------------------------------------------------------
	"S t a n d a r d"  8 - b i t
	Offset binary: the sample's top eight bits with 0x80 added, so that
	silence is 0x80.  The eight bits the coding drops come back as five bits
	of QuickDraw's Random, the rest left zero.
------------------------------------------------------------------------------*/

// ROM 0x001e980c SampleConvertLin16ToStd8__FPvT1
// A negative sample gains 255 before the shift, so that the division rounds
// toward zero rather than down.
void
SampleConvertLin16ToStd8(void* dst, void* src)
{
	long value = *(const short*) src;
	if (value < 0)
		value += 0xff;
	*(UByte*) dst = (UByte) ((value >> 8) + 0x80);
}


// ROM 0x001ea21c SampleConvertStd8ToLin16__FPvT1
void
SampleConvertStd8ToLin16(void* dst, void* src)
{
	long random = Random();
	long value = ((long) *(const UByte*) src << 8) - 0x8000;
	*(short*) dst = (short) ((ULong) value | ((ULong) (random >> 8) & 0x1f));
}


// ROM 0x001e9828 BlockConvertLin16ToStd8__FPvPlT1T2
void
BlockConvertLin16ToStd8(void* dst, long* dstCount, void* src, long* srcCount)
{
	long count = (*dstCount <= *srcCount) ? *dstCount : *srcCount;
	UByte* out = (UByte*) dst;
	const short* in = (const short*) src;
	for (long i = 0; i < count; i++)
	{
		long value = *in++;
		if (value < 0)
			value += 0xff;
		*out++ = (UByte) ((value >> 8) + 0x80);
	}
	*srcCount = count;
	*dstCount = count;
}


// ROM 0x001ea260 BlockConvertStd8ToLin16__FPvPlT1T2
void
BlockConvertStd8ToLin16(void* dst, long* dstCount, void* src, long* srcCount)
{
	long count = (*srcCount < *dstCount) ? *srcCount : *dstCount;
	short* out = (short*) dst;
	const UByte* in = (const UByte*) src;
	for (long i = 0; i < count; i++)
	{
		long random = Random();
		long value = ((long) *in++ << 8) - 0x8000;
		*out++ = (short) ((ULong) value | ((ULong) (random >> 8) & 0x1f));
	}
	*dstCount = count;
	*srcCount = count;
}


/*------------------------------------------------------------------------------
	M u - l a w
------------------------------------------------------------------------------*/

// The bias the ROM adds before splitting a magnitude into exponent and
// mantissa (G.711's is 132, on a 13-bit magnitude; the ROM's is 33, on the
// 14-bit magnitude a 16-bit sample gives when shifted down by two).
enum { kMuLawBias = 0x21 };


// The coding of one sample.  The exponent is the position of the highest bit
// set in the biased magnitude shifted down by five, searched from 7 down.
//
// BUG (the ROM's): the magnitude is never clamped to what eight exponents can
// hold - G.711's implementations clip at the top of the last segment - so the
// loudest samples overflow the search two different ways.  From 32636 to
// 32763 (and -32760 to -32633) the shifted magnitude is 0x100, no bit in 0..7
// is set, the search runs off the end and the ROM shifts the mantissa by the
// exponent it left at -1; ARM takes a shift count modulo 256, so a count of
// 255 shifts everything out and the mantissa comes out zero.  The code is
// then 0x0F whatever the sign - and 0x0F stands for about -16764, so a loud
// positive sample decodes as a loud negative one.  Louder still (32764 and
// above, -32761 and below) the search finds bit 0, the mantissa keeps only
// its bottom four bits, which are zero, and the sample codes as silence.
// Ported as the ROM does it, with the ARM shift written out, rather than
// corrected.
static UByte
MuLawFromLin16(short sample)
{
	long value = (long) sample >> 2;		// the sample as a 14-bit magnitude
	UByte sign = 0;
	if (value < 0)
	{
		value = -value;
		sign = 0x80;
	}
	long biased = value + kMuLawBias;
	int exponent = 7;
	while (exponent >= 0 && (((biased >> 5) & (1L << exponent)) == 0))
		exponent--;
	long mantissa = (exponent >= 0) ? ((biased >> 1) >> exponent) : 0;	// ARM: a shift of 255 is zero
	// the shifts go through the unsigned form, as the ARM's do: the exponent
	// is -1 in the case above, and a negative left shift is undefined in C++
	return (UByte) ~((UByte) (mantissa & 0x0f) | (UByte) ((ULong) exponent << 4) | sign);
}


// The decoding of one byte, without the dither: the magnitude the code
// stands for, signed.
static short
Lin16FromMuLaw(UByte code)
{
	ULong bits = (ULong) (UByte) ~code;		// the byte is stored complemented
	long exponent = (bits >> 4) & 7;
	long mantissa = bits & 0x0f;
	long value = (long) (short) ((UShort) (((((ULong) mantissa << 1) | kMuLawBias) << exponent) - kMuLawBias));
	if ((bits & 0x80) != 0)
		value = (short) -value;
	return (short) value;
}


// ROM 0x001e9880 SampleConvertLin16ToMuLaw__FPvT1
// The ROM loads a whole word and takes its top half, which on the big-endian
// ARM is the sample itself (and reads two bytes past it); the reconstruction
// reads the sample.
void
SampleConvertLin16ToMuLaw(void* dst, void* src)
{
	*(UByte*) dst = MuLawFromLin16(*(const short*) src);
}


// ROM 0x001e9b90 SampleConvertMuLawToLin16__FPvT1
// The two bits the coding cannot carry come from bits 8 and 9 of QuickDraw's
// Random, so that quantisation noise is spread instead of sitting at a fixed
// level.  The ROM stores the result a byte at a time, high byte first.
void
SampleConvertMuLawToLin16(void* dst, void* src)
{
	long value = Lin16FromMuLaw(*(const UByte*) src);
	*(short*) dst = (short) (((ULong) value << 2) | ((ULong) (Random() >> 8) & 3));
}


// ROM 0x001e98e0 BlockConvertLin16ToMuLaw__FPvPlT1T2
void
BlockConvertLin16ToMuLaw(void* dst, long* dstCount, void* src, long* srcCount)
{
	long count = (*dstCount <= *srcCount) ? *dstCount : *srcCount;
	UByte* out = (UByte*) dst;
	const short* in = (const short*) src;
	for (long i = 0; i < count; i++)
		*out++ = MuLawFromLin16(*in++);
	*dstCount = count;
	*srcCount = count;
}


// ROM 0x001e9c14 BlockConvertMuLawToLin16__FPvPlT1T2
void
BlockConvertMuLawToLin16(void* dst, long* dstCount, void* src, long* srcCount)
{
	long count = (*srcCount < *dstCount) ? *srcCount : *dstCount;
	short* out = (short*) dst;
	const UByte* in = (const UByte*) src;
	for (long i = 0; i < count; i++)
	{
		long value = Lin16FromMuLaw(*in++);
		*out++ = (short) (((ULong) value << 2) | ((ULong) (Random() >> 8) & 3));
	}
	*dstCount = count;
	*srcCount = count;
}
