/*
	File:		ink/CICEncoder.cpp

	Contains:	The ROM's ink encoder - the other way round from
				CICDecoder.cpp.  See CICCodec.h.
*/

#include "CICCodec.h"
#include "ByteOrder.h"

#include <string.h>


// ROM 0x00282aa0 PutBits__FP4_CDCUlUs
// n bits of a value written where the writer has got to, least
// significant first, which is the order GetNBit reads them in.  The
// bits are laid into the bytes a piece at a time, each piece as much of
// the current byte as is left, and only the bits being written are
// disturbed - so a buffer may be written over more than once, which is
// what the encoder does when it tries a stroke two ways and keeps the
// shorter.
//
// The writer refuses to pass the end of the buffer, and says which of
// the two things went wrong: no buffer at all, or no room in it.
Boolean
PutBits(CICEncoder* encoder, ULong value, ULong n)
{
	if (encoder->fOut == nil)
	{
		encoder->fError = kCICNoBuffer;
		return false;
	}
	ULong end = encoder->fBitPos + n;
	if (end > encoder->fBitLimit)
	{
		encoder->fError = kCICNoRoom;
		return false;
	}
	ULong bit = encoder->fBitPos & 7;
	UByte* at = encoder->fOut + (encoder->fBitPos >> 3);
	while (n != 0)
	{
		ULong take = n;
		if (n + bit > 7)
			take = 8 - bit;
		UByte mask = (UByte) ((0xff << bit) & (0xff >> (8 - (take + bit))));
		*at = (UByte) ((*at & ~mask) | ((value & 0xff) << bit));
		value >>= take;
		bit = 0;
		at++;
		n -= take;
	}
	encoder->fBitPos = end;
	if (encoder->fHighWater < end)
		encoder->fHighWater = end;
	return true;
}


// ROM 0x00282bb8 FindCodeWord__FsUsP9_CODEWORD
// Which of a book table's entries holds a value.  The decoder can walk
// the entries in order of code length; the encoder has only the value
// to go on, so it looks at all of them.  ==> -1 when none does.
long
FindCodeWord(short value, ULong count, const void* entries)
{
	for (ULong i = 0; i < count; i++)
		if ((short) GetBigEndianHalf((const char*) entries + i * kCodeTableEntrySize) == value)
			return (long) i;
	return -1;
}


// (the code and its length, out of a book table's entry array)
static Boolean
PutCodeWord(CICEncoder* encoder, const void* entries, long i)
{
	const char* at = (const char*) entries + i * kCodeTableEntrySize;
	ULong length = GetBigEndianHalf(at + 2);
	ULong code = ((ULong) GetBigEndianHalf(at + 4) << 16) | GetBigEndianHalf(at + 6);
	return PutBits(encoder, code, length);
}


// ROM 0x00282c04 EncodeWord_OLD__FP4_CDCsP10_CODETABLE
// A value written through one of a book's tables.  A value between the
// table's two bases goes out as its own code; one at or above the upper
// base goes out as the upward escape followed by what is left of it
// after the base is taken away, and one at or below the lower base the
// same downwards - and since what is left may still be out of range,
// that is the same call again.
Boolean
EncodeWord_OLD(CICEncoder* encoder, short value, const void* table)
{
	const char* entries = (const char*) table + kCodeTableHeaderSize;
	ULong count = CodeTableCount(table);
	long v = value;
	short base;
	if (v < CodeTableBaseUp(table))
	{
		if (v > CodeTableBaseDown(table))
		{
			long i = FindCodeWord(value, count, entries);
			if (i < 0)
				return false;
			return PutCodeWord(encoder, entries, i);
		}
		long i = FindCodeWord(CodeTableEscapeDown(table), count, entries);
		if (i < 0)
			return false;
		PutCodeWord(encoder, entries, i);
		base = CodeTableBaseDown(table);
	}
	else
	{
		long i = FindCodeWord(CodeTableEscapeUp(table), count, entries);
		if (i < 0)
			return false;
		PutCodeWord(encoder, entries, i);
		base = CodeTableBaseUp(table);
	}
	return EncodeWord_OLD(encoder, (short) (v - base), table);
}


// ROM 0x00282d38 EncodeWord_NEW__FP4_CDCsP9_CODEWORD
// And through one of the two static tables, which have no escapes: the
// entries are walked until one holds the value or the table runs out.
Boolean
EncodeWord_NEW(CICEncoder* encoder, short value, const unsigned short* table)
{
	long i = 0;
	while (CodeWordValue(table, i) != value && CodeWordLength(table, i) != 0)
		i++;
	if (CodeWordLength(table, i) == 0)
		return false;
	return PutBits(encoder, CodeWordCode(table, i), CodeWordLength(table, i));
}
