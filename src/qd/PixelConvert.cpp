/*
	File:		qd/PixelConvert.cpp

	Contains:	The row converters - see PixelConvert.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PixelConvert.h"


// (host) A byte of the row as the ARM reads it: unsigned.
static inline ULong
B(const char* p, long i)
{
	return (UChar) p[i];
}


// ROM 0x00074c08 ConvertDirect16to4__FPcPUcl
// 16-bit pixels (x, then five bits each of red, green and blue), two at a
// time, into a byte of two four-bit grays.
void
ConvertDirect16to4(char* row, const UChar* /*table*/, long count)
{
	UChar* dst = (UChar*) row;
	const char* src = row;
	for (long n = count >> 2; n >= 1; n--)
	{
		ULong first = RGBtoGray((B(src, 0) & 0x7c) << 9, (B(src, 0) & 3) << 14 | (B(src, 1) & 0xe0) << 6, (B(src, 1) & 0x1f) << 11, 5, 4);
		ULong second = RGBtoGray((B(src, 2) & 0x7c) << 9, (B(src, 2) & 3) << 14 | (B(src, 3) & 0xe0) << 6, (B(src, 3) & 0x1f) << 11, 5, 4);
		src += 4;
		*dst++ = (UChar) (second | (first << 4));
	}
}


// ROM 0x00074dbc ConvertDirect32to4__FPcPUcl
// 32-bit pixels (a pad byte, then red, green, blue), two at a time; an odd
// last one fills the high half of a byte of its own.
void
ConvertDirect32to4(char* row, const UChar* /*table*/, long count)
{
	UChar* dst = (UChar*) row;
	const char* src = row;
	long n;
	for (n = count >> 2; n > 1; n -= 2)
	{
		ULong first = RGBtoGray(B(src, 1) << 8, B(src, 2) << 8, B(src, 3) << 8, 8, 4);
		ULong second = RGBtoGray(B(src, 5) << 8, B(src, 6) << 8, B(src, 7) << 8, 8, 4);
		src += 8;
		*dst++ = (UChar) (second | (first << 4));
	}
	if (n != 0)
		*dst = (UChar) (RGBtoGray(B(src, 1) << 8, B(src, 2) << 8, B(src, 3) << 8, 8, 4) << 4);
}


// ROM 0x00074f90 ConvertDirectComp32to4__FPcPUcl
// A row stored a component at a time (all the reds, all the greens, all
// the blues), two pixels to a byte.
//
// ROM QUIRK, kept: the planes are taken to be (count / 4 - 1) bytes apart
// and only half that many pixels are made, an odd one dropped.
void
ConvertDirectComp32to4(char* row, const UChar* /*table*/, long count)
{
	long plane = (count >> 2) - 1;
	UChar* dst = (UChar*) row;
	const char* src = row;
	for (long n = plane >> 1; n >= 1; n--)
	{
		ULong first = RGBtoGray(B(src, 0) << 8, B(src, plane) << 8, B(src, plane * 2) << 8, 8, 4);
		ULong second = RGBtoGray(B(src, 1) << 8, B(src, 1 + plane) << 8, B(src, 1 + plane * 2) << 8, 8, 4);
		src += 2;
		*dst++ = (UChar) (second | (first << 4));
	}
}


// ROM 0x0007510c ConvertDirectNoPad32to4__FPcPUcl
// 24-bit pixels (red, green, blue with no pad byte), two at a time.
//
// ROM QUIRK, kept: the pairs are counted from count / 4 as if the pixels
// were four bytes.
void
ConvertDirectNoPad32to4(char* row, const UChar* /*table*/, long count)
{
	UChar* dst = (UChar*) row;
	const char* src = row;
	long n;
	for (n = count >> 2; n > 1; n -= 2)
	{
		ULong first = RGBtoGray(B(src, 0) << 8, B(src, 1) << 8, B(src, 2) << 8, 8, 4);
		ULong second = RGBtoGray(B(src, 3) << 8, B(src, 4) << 8, B(src, 5) << 8, 8, 4);
		src += 6;
		*dst++ = (UChar) (second | (first << 4));
	}
	if (n != 0)
		*dst = (UChar) (RGBtoGray(B(src, 0) << 8, B(src, 1) << 8, B(src, 2) << 8, 8, 4) << 4);
}


// ROM 0x000752d0 ConvertIndex2__FPcPUcl
// Two-bit indices, four to a byte, through the table.
void
ConvertIndex2(char* row, const UChar* table, long count)
{
	for (; count > 0; count--, row++)
	{
		ULong b = (UChar) *row;
		*row = (char) (table[(b & 0xc0) >> 6] << 6 | table[(b & 0x30) >> 4] << 4 | table[(b & 0x0c) >> 2] << 2 | table[b & 3]);
	}
}


// ROM 0x00075430 ConvertIndex4__FPcPUcl
// Four-bit indices, two to a byte, through the table.
void
ConvertIndex4(char* row, const UChar* table, long count)
{
	for (; count >= 1; count--, row++)
	{
		ULong b = (UChar) *row;
		*row = (char) (table[b & 0xf] | table[(b & 0xf0) >> 4] << 4);
	}
}


// ROM 0x00075574 ConvertIndex8__FPcPUcl
void
ConvertIndex8(char* row, const UChar* table, long count)
{
	for (; count >= 1; count--, row++)
		*row = (char) table[(UChar) *row];
}


// ROM 0x00075598 ConvertIndex8to4__FPcPUcl
// Eight-bit indices through the table (eight-bit grays), two made a byte
// of four-bit ones.
//
// ROM BUG, kept: the first pixel of each pair takes its gray's *low* four
// bits (shifted up) where the second takes the high four.
void
ConvertIndex8to4(char* row, const UChar* table, long count)
{
	UChar* dst = (UChar*) row;
	const char* src = row;
	for (long n = count >> 1; n >= 1; n--)
	{
		*dst = (UChar) (table[(UChar) src[0]] << 4);
		*dst = (UChar) (*dst | (table[(UChar) src[1]] >> 4));
		src += 2;
		dst++;
	}
}
