/*
	File:		compression/Pushpopper.cpp

	Contains:	Pushpopper (Pushpopper.h), the LZ coder's bit I/O.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The range complaints are the ROM's printfs.
*/

#include "Pushpopper.h"

#include <stdio.h>


// ROM 0x003131b0 __ct__10PushpopperFv
Pushpopper::Pushpopper()
{ }


// ROM 0x003131e4 __dt__10PushpopperFv
Pushpopper::~Pushpopper()
{ }


// ROM 0x003131fc setupreadbuffer__10PushpopperFPUcl
void
Pushpopper::setupreadbuffer(UByte* buffer, long size)
{
	fBuffer = buffer;
	fBits = 0;
	fBufferSize = size;
	fByteCount = 0;
	fBitCount = 0;
}


// ROM 0x00313218 setupwritebuffer__10PushpopperFPUcl
void
Pushpopper::setupwritebuffer(UByte* buffer, long size)
{
	fBuffer = buffer;
	fBits = 0;
	fBufferSize = size;
	fByteCount = 0;
	fBitCount = 32;
}


// ROM 0x00313238 restorebits__10PushpopperFl
void
Pushpopper::restorebits(long count)
{
	fBitCount += count;
}


// ROM 0x00313248 popbits__10PushpopperFl
// The accumulator is refilled to at least 23 bits first.
ULong32
Pushpopper::popbits(long count)
{
	while (fBitCount < 23)
	{
		fBits = (fBits << 8) | *fBuffer++;
		fByteCount++;
		fBitCount += 8;
	}
	fBitCount -= count;
	return (fBits >> fBitCount) & (0xffffffffU >> (32 - count));
}


// ROM 0x003132b0 popString__10PushpopperFPUcl
// Whole bytes: what the accumulator holds first, then straight from the
// buffer through it.
void
Pushpopper::popString(UByte* into, long count)
{
	long bits = fBitCount;
	ULong32 acc = fBits;
	while (bits > 7 && count-- != 0)
	{
		bits -= 8;
		*into++ = (UByte) (acc >> bits);
	}
	if (count > 0)
	{
		fByteCount += count;
		UByte* p = fBuffer;
		do
		{
			acc = (acc << 8) | *p++;
			*into++ = (UByte) (acc >> bits);
		} while (--count != 0);
		fBits = acc;
		fBuffer = p;
	}
	fBitCount = bits;
}


// ROM 0x00313324 popFewBits__10PushpopperFl
// At most eight bits: one byte of refill is enough.
ULong32
Pushpopper::popFewBits(long count)
{
	long bits = fBitCount - count;
	ULong32 acc = fBits;
	if (bits < 0)
	{
		fByteCount++;
		acc = (acc << 8) | *fBuffer;
		bits += 8;
		fBits = acc;
		fBuffer++;
	}
	fBitCount = bits;
	return (acc >> bits) & (0xffffffffU >> (32 - count));
}


// ROM 0x00313378 pushbits__10PushpopperFlT1
// The accumulator is drained to at least 23 free bits first.
void
Pushpopper::pushbits(long count, long value)
{
	if (count < 0 || count > 20 || value < 0 || (1L << count) <= value)
		printf("NOTE: pushbits(%d,%d) argument is out of range", (int) count, (int) value);
	while (fBitCount < 23)
	{
		*fBuffer++ = (UByte) (fBits >> 24);
		fBits <<= 8;
		fByteCount++;
		fBitCount += 8;
		if (fBufferSize < fByteCount)
			printf("NOTE: bytecount=%d overflow in pushbits()", (int) fByteCount);
	}
	fBitCount -= count;
	fBits |= (ULong32) value << fBitCount;
}


// ROM 0x0031349c flushbits__10PushpopperFv
// Everything in the accumulator goes to the buffer, zero-padded to bytes.
void
Pushpopper::flushbits()
{
	while (fBitCount < 32)
	{
		*fBuffer++ = (UByte) (fBits >> 24);
		fBits <<= 8;
		fByteCount++;
		fBitCount += 8;
		if (fBufferSize < fByteCount)
			printf("NOTE: bytecount=%d overflow in pushbits()", (int) fByteCount);
	}
}
