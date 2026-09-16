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

#endif	/* __BYTEORDER_H */
