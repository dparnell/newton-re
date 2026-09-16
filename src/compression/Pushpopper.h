/*
	File:		compression/Pushpopper.h

	Contains:	Pushpopper - the bit-level reader/writer the LZ coder uses:
				bits are pushed into (popped from) a 32-bit accumulator that
				spills to (fills from) a byte buffer, most significant bit
				first.  Not in the DDK; follows the ROM (0x003131b0-
				0x00313510).

	Layout (ROM 0x18): vptr +0, fByteCount +4, fBufferSize +8, fBits +0xc,
	fBuffer +0x10, fBitCount +0x14.  The accumulator is the ARM's 32-bit
	word whatever ULong is on the host.
*/

#ifndef __PUSHPOPPER_H
#define __PUSHPOPPER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

typedef unsigned int	ULong32;

class Pushpopper
{
public:
					Pushpopper();
	virtual			~Pushpopper();

	void			setupreadbuffer(UByte* buffer, long size);
	void			setupwritebuffer(UByte* buffer, long size);
	void			restorebits(long count);				// give back bits popped too soon
	ULong32			popbits(long count);					// up to 24 bits
	void			popString(UByte* into, long count);		// whole bytes
	ULong32			popFewBits(long count);					// at most 8 bits
	void			pushbits(long count, long value);		// up to 20 bits
	void			flushbits();							// the accumulator to the buffer

	long			fByteCount;			// +0x04  bytes moved between buffer and accumulator
	long			fBufferSize;		// +0x08
	ULong32			fBits;				// +0x0c  the accumulator
	UByte*			fBuffer;			// +0x10  the next byte
	long			fBitCount;			// +0x14  reading: bits held; writing: bits free (32 when empty)
};

#endif	/* __PUSHPOPPER_H */
