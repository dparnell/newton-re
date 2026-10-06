/*
	File:		utility/DES.cpp

	Contains:	The ROM's DES (DES.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DES.h"

extern const unsigned char	DESIPInvTbl[68];
extern const unsigned char	DESPC1Tbl[60];
extern const unsigned char	DESPC2Tbl[52];
extern const unsigned char	DESPTbl[36];
extern const unsigned char	DESSBoxes[512];

static inline DESWord
RotateRight(DESWord value, unsigned int count)
{
	count &= 31;
	return count == 0 ? value : (value >> count) | (value << (32 - count));
}


// ROM 0x00329874 DESpermute__FPCUcUlT2PUl
// The bits the table names gathered into two words (DES.h).
void
DESpermute(const unsigned char* table, DESWord hi, DESWord lo, DESWord* out)
{
	DESWord current = 0, previous = 0;
	unsigned int bit;
	do
	{
		bit = *table++;
		while (bit < 0x40)
		{
			current <<= 1;
			DESWord source = (bit >= 0x20) ? hi : lo;
			if (source & ((DESWord) 1 << (bit & 0x1f)))
				current |= 1;
			bit = *table++;
		}
		DESWord swap = current;
		current = previous;
		previous = swap;
	} while (bit == 0x40);
	out[1] = previous;
	out[0] = current;
}


// ROM 0x002d9a00 DESip__FUlT1PUl
// The initial permutation, done by shuffling bits rather than by a table.
void
DESip(DESWord hi, DESWord lo, DESWord* out)
{
	DESWord a = hi, aShifted = hi << 16;
	DESWord b = lo, bShifted = lo << 16;
	DESWord r0 = 0, r1 = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		r0 = RotateRight(r0, 1);
		r1 = RotateRight(r1, 1);
		for (int i = 0; i < 8; i++)
		{
			r1 = (r1 << 1) | (bShifted >> 31);
			bShifted <<= 1;
			r1 = RotateRight(r1, 31);
			r1 = (r1 << 1) | (b >> 31);
			b <<= 1;
			r1 = RotateRight(r1, 31);
			r1 = (r1 << 1) | (aShifted >> 31);
			aShifted <<= 1;
			r1 = RotateRight(r1, 31);
			r1 = (r1 << 1) | (a >> 31);
			a <<= 1;
			DESWord next = RotateRight(r1, 31);
			r1 = r0;
			r0 = next;
		}
	}
	out[0] = r0;
	out[1] = r1;
}


// ROM 0x002d9a94 DESfrk__FUlN21
// The round function: the half block (turned left a bit) against the
// round's subkey, six bits at a time through the S-boxes, then the P
// permutation.
DESWord
DESfrk(DESWord k0, DESWord k1, DESWord r)
{
	const unsigned char* sbox = DESSBoxes;
	DESWord result = 0;
	DESWord bits = (r << 1) + (r >> 31);
	for (int i = 0; i < 8; i++)
	{
		result |= sbox[(bits ^ k1) & 0x3f];
		result = RotateRight(result, 4);
		sbox += 0x40;
		bits = RotateRight(bits, 4);
		k1 = (k1 >> 6) + (k0 << 26);
		k0 >>= 6;
	}
	DESWord out[2];
	DESpermute(DESPTbl, 0, result, out);
	return out[1];
}


// ROM 0x002f7264 DESKeySched
// The sixteen subkeys: PC1, then the two 28-bit halves (kept in the top of
// a word) turned left one bit or two - one where the bit of 0xc0810000
// that is shifted out is set - and each round's PC2.
void
DESKeySched(const DESWord* key, DESWord* schedule)
{
	DESWord halves[2];
	DESWord rounds = 0xc0810000;
	DESpermute(DESPC1Tbl, key[0] << 1, key[1] << 1, halves);
	DESWord c = halves[0] << 4;
	DESWord d = halves[1] << 4;
	for (;;)
	{
		c = ((c >> 27) & 0x10) + c * 2;
		d = ((d >> 27) & 0x10) + d * 2;
		for (;;)
		{
			DESpermute(DESPC2Tbl, c, d, schedule);
			schedule += 2;
			rounds <<= 1;
			if (rounds == 0)
				return;
			if (rounds > 0x7fffffff)
				break;
			c = ((c >> 26) & 0x30) + c * 4;
			d = ((d >> 26) & 0x30) + d * 4;
		}
	}
}


// ROM 0x002d9958 DESEncode
// Each whole block of the data encrypted in place; *data is left past the
// last.
void
DESEncode(const DESWord* schedule, long length, DESWord** data)
{
	DESWord* block = *data;
	while ((length -= 8) >= 0)
	{
		const DESWord* key = schedule;
		DESWord lr[2];
		DESip(block[0], block[1], lr);
		DESWord left = lr[0], right = lr[1];
		for (int i = 0; i < 8; i++)
		{
			left ^= DESfrk(key[0], key[1], right);
			right ^= DESfrk(key[2], key[3], left);
			key += 4;
		}
		DESpermute(DESIPInvTbl, right, left, lr);
		block[0] = lr[0];
		block[1] = lr[1];
		block += 2;
	}
	*data = block;
}


// ROM 0x002d4420 DESDecode
// The same with the subkeys taken from the last.
void
DESDecode(const DESWord* schedule, long length, DESWord** data)
{
	DESWord* block = *data;
	for (length -= 8; length >= 0; length -= 8)
	{
		DESWord lr[2];
		DESip(block[0], block[1], lr);
		DESWord left = lr[0], right = lr[1];
		const DESWord* key = schedule + 32;
		for (int i = 0; i < 8; i++)
		{
			left ^= DESfrk(key[-2], key[-1], right);
			right ^= DESfrk(key[-4], key[-3], left);
			key -= 4;
		}
		DESpermute(DESIPInvTbl, right, left, block);
		block += 2;
	}
	*data = block;
}


// ROM 0x002d4864 DESEncodeNonce
// One block encrypted under the key.
void
DESEncodeNonce(const DESWord* key, DESWord* block)
{
	DESWord schedule[32];
	DESKeySched(key, schedule);
	DESWord* p = block;
	DESEncode(schedule, 8, &p);
}


// ROM 0x002d4898 DESDecodeNonce
void
DESDecodeNonce(const DESWord* key, DESWord* block)
{
	DESWord schedule[32];
	DESKeySched(key, schedule);
	DESWord* p = block;
	DESDecode(schedule, 8, &p);
}


// ROM 0x002d45dc DESCharToKey
// A password's key (DES.h).  The ROM reads the string a byte at a time,
// each UniChar's high byte first; the host does the same from the
// characters.
void
DESCharToKey(const UniChar* password, DESWord* key)
{
	DESWord current[2] = { 0x57406860, 0x626d7464 };		// "W@h`bmtd"
	Boolean ended = false;
	long next = 0;
	do
	{
		DESWord schedule[32];
		DESKeySched(current, schedule);
		unsigned char bytes[8];
		for (int i = 0; i < 4; i++)
		{
			if (ended)
			{
				bytes[i * 2] = 0;
				bytes[i * 2 + 1] = 0;
			}
			else
			{
				UniChar c = password[next++];
				bytes[i * 2] = (unsigned char) (c >> 8);
				bytes[i * 2 + 1] = (unsigned char) c;
				if (bytes[i * 2] == 0 && bytes[i * 2 + 1] == 0)
					ended = true;
			}
		}
		DESWord block[2];
		block[0] = (DESWord) bytes[0] << 24 | (DESWord) bytes[1] << 16 | (DESWord) bytes[2] << 8 | bytes[3];
		block[1] = (DESWord) bytes[4] << 24 | (DESWord) bytes[5] << 16 | (DESWord) bytes[6] << 8 | bytes[7];
		DESWord* p = block;
		DESEncode(schedule, 8, &p);
		for (int i = 0; i < 4; i++)
		{
			bytes[i] = (unsigned char) (block[0] >> (24 - 8 * i));
			bytes[i + 4] = (unsigned char) (block[1] >> (24 - 8 * i));
		}
		// odd parity: a byte with an even number of bits has its low bit
		// flipped
		for (int i = 0; i < 8; i++)
		{
			int count = 0;
			for (int bit = 0; bit < 8; bit++)
				if (bytes[i] & (1 << bit))
					count++;
			if ((count & 1) == 0)
				bytes[i] ^= 1;
		}
		// ROM BUG: the corrected bytes are ORed into the encrypted block
		// rather than replacing it, so a byte whose low bit was cleared to
		// make its parity odd keeps it set, and its parity even (harmless:
		// DES ignores the parity bits).  Kept even with the ROM's bugs fixed
		// (docs/rom-bugs.md): the key is a persistent format - a store's
		// password is kept as these eight bytes and compared with them
		// byte for byte (CheckStorePassword, whose master key 0x39 byte has
		// the bug's even parity) - so a corrected key would not open a
		// store a Newton protected
		block[0] |= (DESWord) bytes[0] << 24 | (DESWord) bytes[1] << 16 | (DESWord) bytes[2] << 8 | bytes[3];
		block[1] |= (DESWord) bytes[4] << 24 | (DESWord) bytes[5] << 16 | (DESWord) bytes[6] << 8 | bytes[7];
		current[0] = block[0];
		current[1] = block[1];
	} while (!ended);
	key[0] = current[0];
	key[1] = current[1];
}
