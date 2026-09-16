/*
	File:		toolbox/ByteOrder.h

	Contains:	Reading and writing the words of Newton's persistent formats.
				The MessagePad is big-endian, and so is everything it writes
				out - package files, stores, compressed chunks: a word in
				those is its four bytes most significant first, whatever the
				host's order.  Code that reads or writes such a format goes
				through these instead of casting to a word pointer, so that
				data made by a real Newton reads back byte for byte.  (Our
				own in-memory structures use the host's order.)
*/

#ifndef __BYTEORDER_H
#define __BYTEORDER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

inline unsigned int
GetBigEndianWord(const void* p)
{
	const unsigned char* b = (const unsigned char*) p;
	return ((unsigned int) b[0] << 24) | ((unsigned int) b[1] << 16) | ((unsigned int) b[2] << 8) | b[3];
}

inline void
PutBigEndianWord(void* p, unsigned int value)
{
	unsigned char* b = (unsigned char*) p;
	b[0] = (unsigned char) (value >> 24);
	b[1] = (unsigned char) (value >> 16);
	b[2] = (unsigned char) (value >> 8);
	b[3] = (unsigned char) value;
}

inline unsigned short
GetBigEndianHalf(const void* p)
{
	const unsigned char* b = (const unsigned char*) p;
	return (unsigned short) (((unsigned int) b[0] << 8) | b[1]);
}

inline void
PutBigEndianHalf(void* p, unsigned short value)
{
	unsigned char* b = (unsigned char*) p;
	b[0] = (unsigned char) (value >> 8);
	b[1] = (unsigned char) value;
}

// whether the host keeps its words most significant byte first (the
// MessagePad does): when it does not, UniChar text and other halfword
// data must be swapped on its way to and from a persistent format
inline bool
HostIsBigEndian()
{
	const unsigned short one = 1;
	return *(const unsigned char*) &one == 0;
}

// count UniChars (2n bytes) swapped between the host's and the big-endian
// order, in place; nothing on a big-endian host
inline void
SwapUniChars(void* text, long count)
{
	if (HostIsBigEndian())
		return;
	unsigned char* b = (unsigned char*) text;
	for (long i = 0; i < count; i++, b += 2)
	{
		unsigned char t = b[0];
		b[0] = b[1];
		b[1] = t;
	}
}

#endif	/* __BYTEORDER_H */
