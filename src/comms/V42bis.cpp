/*
	File:		comms/V42bis.cpp

	Contains:	V.42bis compression for MNP (MNP.h): the ROM's BTLZ coder -
				a dictionary of strings kept as a tree (each node a
				character, its parent, its first child and its next
				sibling, the siblings in character order), codewords of
				9 to N2 bits packed low bit first, and the transparent mode
				with its escape character that moves on by 0x33 each time
				it is sent; the encoder switches between the two modes by
				comparing, every so many characters, the bits it has spent
				against the bits the characters would have taken.

				The state is the ROM's 0x39d4-byte block, laid out as the
				ROM lays it out and read the way the ROM reads it: the
				node arrays are big-endian halfwords (characters at 0x60,
				siblings at 0x860, first children at 0x1860, parents at
				0x2860, each indexed by node; the decoder's nodes follow
				the encoder's when both directions are compressed), the
				numbers are words:
					0x00 directions (1 compress, 2 decompress, 3 both)
					0x04 the dictionary size N2 (P1), 0x08 the longest
					string N7 (P2), 0x0c where the decoder's nodes start,
					0x10 the largest codeword in bits, 0x14 N2, 0x18 the
					first codeword size less one (8), 0x1c the characters
					(256), 0x20 the first free codeword (259), 0x24 the
					control codewords (3), 0x28 N7, 0x2c the masks
					(halfwords); the decoder: 0x3860 transparent, 0x3861
					the last string overflowed its stack, 0x3862 the
					dictionary is full, 0x3863 an escape is pending,
					0x3864 just back in compressed mode, 0x3868 the string
					length, 0x386c the escape character, 0x3870 the
					codeword size, 0x3874 the string so far, 0x3878 the
					last codeword, 0x387c the next free node, 0x3880 the
					node last added, 0x3884.. the string stack; the
					encoder: 0x3988 the string so far, 0x398c the node last
					added, 0x3990 the next free node, 0x3994 characters
					since the last test, 0x3998 bits spent since it, 0x399c
					the escape character, 0x39a0 the string length, 0x39a4
					the test interval, 0x39a8 the codeword size, 0x39ac the
					next step-up threshold, 0x39b0 transparent, 0x39b1 the
					dictionary is full, 0x39b2 just flushed; the bit packer
					0x39b4/0x39b8, the unpacker 0x39bc/0x39c0.

				DEVIATION (pointer size): the ROM keeps the two output
				procedures and their refCon at 0x39c4-0x39cc and a pointer
				into the stack at 0x3984; the host keeps the procedures in
				fields after the block and the stack pointer as an offset.
				NOT YET: the internal output buffers (Internal_putByte,
				Internal_dte_to_char) V42InitCompress uses when given no
				procedures - MNP always gives its own.

	Reconstructed from the MP2x00 US ROM (0x0025cba8-0x0025dc00); each
	function cites its origin.  tools/dock/v42bis.py is an independent coder
	for the other end (ctest comms.V42bis).
*/

#include "MNP.h"
#include "NewtErrors.h"

#include <stdlib.h>
#include <string.h>

struct TCompressVars
{
	UByte			fBlock[0x39c4];			// the ROM's layout (above)
	MNPByteProc		fCompressOut;			// ROM +0x39c4
	MNPByteProc		fDecompressOut;			// ROM +0x39c8
	void*			fRefCon;				// ROM +0x39cc
	ULong			fField39d0;				// ROM +0x39d0
};

// the block's words (host order: the ROM's are only ever read as words) and
// its big-endian halfwords
static inline ULong
W(TCompressVars* v, ULong off)
{
	uint32_t w;
	memcpy(&w, v->fBlock + off, 4);
	return w;
}

static inline void
SetW(TCompressVars* v, ULong off, ULong value)
{
	uint32_t w = (uint32_t) value;
	memcpy(v->fBlock + off, &w, 4);
}

static inline ULong
H(TCompressVars* v, ULong off)
{
	return (ULong) (v->fBlock[off] << 8 | v->fBlock[off + 1]);
}

static inline void
SetH(TCompressVars* v, ULong off, ULong value)
{
	v->fBlock[off] = (UByte) (value >> 8);
	v->fBlock[off + 1] = (UByte) value;
}

static inline UByte&
B(TCompressVars* v, ULong off)
{
	return v->fBlock[off];
}

// the node arrays
#define kChar		0x60
#define kSibling	0x860
#define kChild		0x1860
#define kParent		0x2860
#define kStack		0x3884


// ROM 0x0025ce1c V42CreateCompressVars__FPP13TCompressVars
NewtonErr
V42CreateCompressVars(TCompressVars** vars)
{
	*vars = (TCompressVars*) malloc(sizeof(TCompressVars));
	return (*vars != nil) ? noErr : -10007;		// (kOSErrNoMemory)
}


// ROM 0x0025ce50 V42DisposeCompressVars__FP13TCompressVars
void
V42DisposeCompressVars(TCompressVars* vars)
{
	if (vars != nil)
		free(vars);
}


// ROM 0x0025cba8 xbitpack__FP13TCompressVarsUiT2
// A codeword of so many bits (up to sixteen) packed low bit first, each
// octet sent as it fills; nought bits: what is left sent, padded.
static void
xbitpack(TCompressVars* v, ULong value, ULong bits)
{
	if (bits == 0)
	{
		if (W(v, 0x39b8) == 0)
			return;
		bits = 8 - W(v, 0x39b8);
	}
	ULong more;
	if (bits < 9)
		more = 0;
	else
	{
		more = bits - 8;
		bits = 8;
	}
	ULong acc = W(v, 0x39b4) | (value & 0xff) << (W(v, 0x39b8) & 0xff);
	ULong count = W(v, 0x39b8) + bits;
	SetW(v, 0x39b8, count);
	if (count > 7)
	{
		SetW(v, 0x39b8, count - 8);
		v->fCompressOut(v->fRefCon, (UByte) acc);
		acc >>= 8;
	}
	acc |= (value & 0xff00) >> ((8 - W(v, 0x39b8)) & 0xff);
	count = W(v, 0x39b8) + more;
	SetW(v, 0x39b8, count);
	if (count > 7)
	{
		SetW(v, 0x39b8, count - 8);
		v->fCompressOut(v->fRefCon, (UByte) acc);
		acc >>= 8;
	}
	SetW(v, 0x39b4, acc);
}


// ROM 0x0025cc74 unpack__FP13TCompressVarsUiT2
// The next codeword of so many bits out of the octets coming in (one each
// call); -1 when this octet did not complete it.  Nought bits: the rest of
// the octet in hand thrown away (after an ETM or a FLUSH).
static long
unpack(TCompressVars* v, ULong bits, ULong byte)
{
	ULong result;
	ULong count;
	if (bits == 0)
	{
		result = 0;
		ULong n = W(v, 0x39bc);
		if (n < 9)
		{
			if (n < 8)
			{
				SetW(v, 0x39c0, 0);
				SetW(v, 0x39bc, 0);
			}
			return result;
		}
		SetW(v, 0x39c0, W(v, 0x39c0) >> ((n - 8) & 0xff));
		count = 8;
	}
	else
	{
		ULong high, low;
		if (bits < 9)
		{
			high = 0;
			low = bits;
		}
		else
		{
			high = bits - 8;
			low = 8;
		}
		ULong had = W(v, 0x39bc);
		if (had < low)
		{
			SetW(v, 0x39c0, W(v, 0x39c0) | byte << (had & 0xff));
			SetW(v, 0x39bc, had + 8);
		}
		ULong now = W(v, 0x39bc);
		if (now < bits)
		{
			if (had < low)
				return -1;
			result = W(v, 0x39c0) & H(v, 0x2c + low * 2);
			SetW(v, 0x39c0, W(v, 0x39c0) >> (low & 0xff) | byte << ((now - low) & 0xff));
			count = now - low + 8;
		}
		else
		{
			result = W(v, 0x39c0) & H(v, 0x2c + low * 2);
			SetW(v, 0x39c0, W(v, 0x39c0) >> (low & 0xff));
			count = now - low;
		}
		SetW(v, 0x39bc, count);
		result |= (W(v, 0x39c0) & H(v, 0x2c + high * 2)) << 8;
		SetW(v, 0x39c0, W(v, 0x39c0) >> (high & 0xff));
		count = W(v, 0x39bc) - high;
	}
	SetW(v, 0x39bc, count);
	return (long) result;
}


// the codeword packed at the current size, stepping the size up first as
// far as the codeword needs (BTEncode and its flush, in line in the ROM)
static void
SendCodeword(TCompressVars* v, ULong code)
{
	if (W(v, 0x39a8) < W(v, 0x10))
	{
		while (W(v, 0x39ac) <= code)
		{
			xbitpack(v, 2, W(v, 0x39a8));			// STEPUP
			SetW(v, 0x39a8, W(v, 0x39a8) + 1);
			SetW(v, 0x39ac, W(v, 0x39ac) * 2);
		}
	}
	xbitpack(v, code, W(v, 0x39a8));
}


// ROM 0x0025ce5c BTEncode__FP13TCompressVarsUi
// A character compressed - or, 0xfffe, the string so far sent and a FLUSH
// (BTFlush).  The string is extended while the dictionary has it; when it
// cannot be, its codeword is sent (in compressed mode) and the string and
// the character added as a new node - the next free one, or once the
// dictionary is full the next leaf, taken from its parent - unless the
// string is too long or the link was just flushed.
void
BTEncode(TCompressVars* v, ULong c)
{
	ULong current = W(v, 0x3988);
	ULong next = W(v, 0x3990);
	if (c == 0xfffe)
	{
		if (B(v, 0x39b0) == 0 && B(v, 0x39b2) == 0)
		{
			if (current != 0)
			{
				if (W(v, 0x39a8) < W(v, 0x10))
				{
					while (W(v, 0x39ac) <= current)
					{
						xbitpack(v, 2, W(v, 0x39a8));
						SetW(v, 0x39a8, W(v, 0x39a8) + 1);
						SetW(v, 0x39ac, W(v, 0x39ac) * 2);
					}
				}
				B(v, 0x39b2) = 1;
				xbitpack(v, current, W(v, 0x39a8));
				SetW(v, 0x3998, W(v, 0x3998) + W(v, 0x39a8));
			}
			xbitpack(v, 1, W(v, 0x39a8));			// FLUSH
			xbitpack(v, 0, 0);
		}
		SetW(v, 0x3988, current);
		SetW(v, 0x3990, next);
		return;
	}
	Boolean found;
	Boolean dontAdd = false;
	ULong string = 0;						// (the ROM's r9: set only when there is a string)
	ULong before = 0;						// the sibling a new node goes after
	if (current == 0)
	{
		current = W(v, 0x24) + c;
		SetW(v, 0x39a0, 1);
		found = true;
	}
	else
	{
		SetW(v, 0x39a0, W(v, 0x39a0) + 1);
		string = current;
		before = current;
		current = H(v, kChild + string * 2);
		found = false;
		while (current != 0)
		{
			if (B(v, kChar + current) >= c)
			{
				found = B(v, kChar + current) == c;
				break;
			}
			before = current;
			current = H(v, kSibling + current * 2);
		}
		if (found && (B(v, 0x39b2) != 0 || W(v, 0x398c) == current))
		{
			// (the node just added, or the string just flushed: not
			// extended)
			found = false;
			dontAdd = true;
		}
	}
	SetW(v, 0x3994, W(v, 0x3994) + 1);
	if (!found)
	{
		if (B(v, 0x39b2) == 0)
		{
			if (B(v, 0x39b0) == 0)
				SendCodeword(v, string);
			SetW(v, 0x3998, W(v, 0x3998) + W(v, 0x39a8));
		}
		// the compressibility test
		ULong chars = W(v, 0x3994);
		if (chars > W(v, 0x39a4))
		{
			if (B(v, 0x39b0) != 0)
			{
				if (W(v, 0x3998) + 16 < chars * 8)
				{
					SetW(v, 0x39a4, 0x100);
					v->fCompressOut(v->fRefCon, (UByte) W(v, 0x399c));	// ESC ECM: to compressed
					v->fCompressOut(v->fRefCon, 0);
					B(v, 0x39b0) = 0;
				}
			}
			else if (W(v, 0x3998) - 16 > chars * 8)
			{
				SetW(v, 0x39a4, 0x40);
				xbitpack(v, 0, W(v, 0x39a8));			// ETM: to transparent
				xbitpack(v, 0, 0);
				B(v, 0x39b0) = 1;
			}
			SetW(v, 0x3998, 0);
			SetW(v, 0x3994, 0);
		}
		if (W(v, 0x39a0) > W(v, 0x28) || dontAdd)
			SetW(v, 0x398c, 0);
		else
		{
			B(v, kChar + next) = (UByte) c;
			SetH(v, kParent + next * 2, string);
			SetH(v, kChild + next * 2, 0);
			SetH(v, kSibling + next * 2, current);
			if (before == string)
				SetH(v, kChild + string * 2, next);
			else
				SetH(v, kSibling + before * 2, next);
			SetW(v, 0x398c, next);
			ULong last = W(v, 0x14) - 1;
			Boolean recycle = B(v, 0x39b1) != 0;
			if (!recycle)
			{
				next++;
				if (next > last)
				{
					B(v, 0x39b1) = 1;
					recycle = true;
				}
			}
			if (recycle)
			{
				// the next leaf taken out of the tree
				do
				{
					next++;
					if (next > last)
						next = W(v, 0x1c) + W(v, 0x24);
				}
				while (H(v, kChild + next * 2) != 0);
				ULong parent = H(v, kParent + next * 2);
				ULong node = H(v, kChild + parent * 2);
				if (node == next)
					SetH(v, kChild + parent * 2, H(v, kSibling + next * 2));
				else
				{
					ULong prev = node;
					while ((node = H(v, kSibling + prev * 2)) != next)
						prev = node;
					SetH(v, kSibling + prev * 2, H(v, kSibling + next * 2));
				}
			}
		}
		current = W(v, 0x24) + c;
		SetW(v, 0x39a0, 1);
	}
	if (B(v, 0x39b0) != 0)
	{
		v->fCompressOut(v->fRefCon, (UByte) c);
		if (W(v, 0x399c) == c)
			v->fCompressOut(v->fRefCon, 1);			// ESC EID: the escape character itself
	}
	if (W(v, 0x399c) == c)
		SetW(v, 0x399c, (W(v, 0x399c) + 0x33) & 0xff);
	B(v, 0x39b2) = 0;
	SetW(v, 0x3988, current);
	SetW(v, 0x3990, next);
}


// ROM 0x0025cd84 dict_init__FP13TCompressVarsUi
// A dictionary's nodes (from base on) emptied, and its first nodes made the
// characters.
static void
dict_init(TCompressVars* v, ULong base)
{
	for (ULong i = 0; i < W(v, 0x14); i++)
	{
		SetH(v, kParent + (base + i) * 2, 0);
		SetH(v, kChild + (base + i) * 2, 0);
	}
	ULong node = W(v, 0x24) + base;
	for (ULong i = 0; i < W(v, 0x1c); i++, node++)
	{
		SetH(v, kChild + node * 2, 0);
		SetH(v, kSibling + node * 2, 0);
		B(v, kChar + node) = (UByte) i;
	}
}


// ROM 0x0025d34c BTInitEn__FP13TCompressVars
// The encoder started afresh: transparent, the dictionary the characters.
static void
BTInitEn(TCompressVars* v)
{
	B(v, 0x39b0) = 1;
	SetW(v, 0x3994, 0);
	SetW(v, 0x3998, 0);
	SetW(v, 0x3990, W(v, 0x1c) + W(v, 0x24));
	SetW(v, 0x398c, 0);
	SetW(v, 0x39a8, W(v, 0x18) + 1);
	SetW(v, 0x39ac, W(v, 0x1c) << 1);
	SetW(v, 0x3988, 0);
	SetW(v, 0x39a4, 0x40);
	B(v, 0x39b1) = 0;
	SetW(v, 0x399c, 0);
	B(v, 0x39b2) = 0;
	dict_init(v, 0);
}


// ROM 0x0025d3b4 BTFlush__FP13TCompressVars
void
BTFlush(TCompressVars* v)
{
	BTEncode(v, 0xfffe);
}


// ROM 0x0025d3c0 BTInitDe__FP13TCompressVars
// The decoder started afresh (its nodes after the encoder's).
static void
BTInitDe(TCompressVars* v)
{
	ULong base = W(v, 0x0c);
	B(v, 0x3863) = 0;
	SetW(v, 0x3878, 0);
	SetW(v, 0x3984, W(v, 0x28) + 0x3883);			// (the stack's top, as an offset)
	SetW(v, 0x3874, 0);
	SetW(v, 0x3880, 0);
	SetW(v, 0x387c, W(v, 0x1c) + base + W(v, 0x24));
	SetW(v, 0x3870, W(v, 0x18) + 1);
	B(v, 0x3860) = 1;
	B(v, 0x3861) = 0;
	B(v, 0x3862) = 0;
	SetW(v, 0x386c, 0);
	B(v, 0x3864) = 0;
	dict_init(v, base);
}


// ROM 0x0025d438 BTDecode__FP13TCompressVarsUi
// An octet decompressed: in transparent mode a character (or an escape
// sequence: ECM to compressed mode, EID the escape character itself, RESET),
// in compressed mode the bits of a codeword (ETM to transparent mode,
// FLUSH, STEPUP, or a string, sent out from its last node back to its
// first), the dictionary grown as the encoder grew it.  ==> 0, 1 when an
// octet was only part of something, 4 an unknown codeword, -4 an unknown
// escape sequence.
long
BTDecode(TCompressVars* v, ULong byte)
{
	long result = 0;
	ULong string = W(v, 0x3874);					// r6
	ULong next = W(v, 0x387c);						// r7
	ULong base = W(v, 0x0c);
	ULong c = 0;									// r8
	ULong prev = 0;									// r10
	ULong last = 0;									// sp+8: the string a new node hangs off
	Boolean dontAdd = false;						// sp+0x18
	// ROM BUG: when the last string overflowed the stack or there was
	// none, the ROM tests "found" (sp+0x1c) without having set it - a stack
	// slot left from earlier; the host takes it as not found
	Boolean found = false;							// sp+0x1c
	Boolean overflowed = false;						// sp+0x14

	if (B(v, 0x3860) != 0)
	{
		// transparent
		if (B(v, 0x3863) != 0)
		{
			if (byte == 0)
			{
				// ECM: to compressed mode
				B(v, 0x3861) = W(v, 0x3868) >= W(v, 0x28);
				SetW(v, 0x3878, string);
				B(v, 0x3863) = 0;
				goto toggle;
			}
			if (byte == 1)
			{
				// EID: the escape character itself
				v->fDecompressOut(v->fRefCon, (UByte) W(v, 0x386c));
				c = W(v, 0x386c) & 0xff;
				SetW(v, 0x386c, (W(v, 0x386c) + 0x33) & 0xff);
				B(v, 0x3863) = 0;
			}
			else if (byte == 2)
			{
				// RESET
				// ROM BUG: the decoder's string and next node are put back
				// from before the reset on the way out
				BTInitDe(v);
				goto done;
			}
			else
			{
				result = -4;
				goto done;
			}
		}
		else
		{
			c = byte & 0xff;
			if (W(v, 0x386c) == c)
			{
				B(v, 0x3863) = 1;
				result = 1;
				goto done;
			}
			v->fDecompressOut(v->fRefCon, (UByte) c);
		}
		// the dictionary follows the characters
		if (string == 0)
		{
			string = base + c + W(v, 0x24);
			B(v, 0x3861) = 0;
			SetW(v, 0x3868, 1);
			goto done;
		}
		ULong length = W(v, 0x3868) + 1;
		SetW(v, 0x3868, length);
		last = string;
		prev = string;
		string = H(v, kChild + string * 2);
		found = false;
		while (string != 0)
		{
			if (B(v, kChar + string) >= c)
			{
				found = B(v, kChar + string) == c;
				break;
			}
			prev = string;
			string = H(v, kSibling + string * 2);
		}
		if (B(v, 0x3864) == 1)
		{
			if (found)
			{
				found = false;
				dontAdd = true;
			}
			B(v, 0x3864) = 0;
		}
		if (found)
		{
			if (W(v, 0x3880) == string)
				dontAdd = true;
			else
				goto extended;
		}
		if (length > W(v, 0x28))
			goto noAdd;
		goto add;
	}

	// compressed
	{
		long code = unpack(v, W(v, 0x3870), byte);
		if (code < 0)
		{
			result = 1;
			goto done;
		}
		if ((ULong) code < W(v, 0x24))
		{
			if (code == 0)
			{
				// ETM: to transparent mode
				unpack(v, 0, 0);
				B(v, 0x3864) = 1;
				goto toggle;
			}
			if (code == 1)
			{
				// FLUSH
				unpack(v, 0, 0);
				goto done;
			}
			// STEPUP
			SetW(v, 0x3870, W(v, 0x3870) + 1);
			if (W(v, 0x3870) > W(v, 0x10))
				result = 4;
			goto done;
		}
		overflowed = B(v, 0x3861) != 0;
		last = W(v, 0x3878);
		ULong node = base + code;
		SetW(v, 0x3878, node);
		if (!((ULong) code < W(v, 0x1c) + W(v, 0x24) || H(v, kParent + node * 2) != 0)
		 || !((ULong) code <= W(v, 0x14) - 1))
		{
			result = 4;
			goto done;
		}
		// the string onto the stack, last character first
		SetW(v, 0x3868, 0);
		ULong top = W(v, 0x3984);
		do
		{
			B(v, top) = B(v, kChar + node);
			top--;
			SetW(v, 0x3868, W(v, 0x3868) + 1);
			node = H(v, kParent + node * 2);
		}
		while (node != 0 && kStack <= top);
		B(v, 0x3861) = kStack > top;
		ULong p = top + 1;
		c = B(v, p);
		do
		{
			if (W(v, 0x386c) == B(v, p))
				SetW(v, 0x386c, (W(v, 0x386c) + 0x33) & 0xff);
			v->fDecompressOut(v->fRefCon, B(v, p));
			p++;
		}
		while (W(v, 0x3984) >= p);
		if (overflowed || last == 0)
			dontAdd = true;
		else
		{
			prev = last;
			string = H(v, kChild + last * 2);
			found = false;
			while (string != 0)
			{
				if (B(v, kChar + string) >= c)
				{
					found = B(v, kChar + string) == c;
					break;
				}
				prev = string;
				string = H(v, kSibling + string * 2);
			}
			dontAdd = found;
		}
		if (found)
			goto extended;
	}

add:
	if (dontAdd)
		goto noAdd;
	B(v, kChar + next) = (UByte) c;
	SetH(v, kParent + next * 2, last);
	SetH(v, kChild + next * 2, 0);
	SetH(v, kSibling + next * 2, string);
	if (prev == last)
		SetH(v, kChild + last * 2, next);
	else
		SetH(v, kSibling + prev * 2, next);
	SetW(v, 0x3880, next);
	{
		ULong limit = W(v, 0x14) + base - 1;
		Boolean recycle = B(v, 0x3862) != 0;
		if (!recycle)
		{
			next++;
			if (next > limit)
			{
				B(v, 0x3862) = 1;
				recycle = true;
			}
		}
		if (recycle)
		{
			do
			{
				next++;
				if (next > limit)
					next = W(v, 0x1c) + base + W(v, 0x24);
			}
			while (H(v, kChild + next * 2) != 0);
			ULong parent = H(v, kParent + next * 2);
			ULong node = H(v, kChild + parent * 2);
			if (node == next)
				SetH(v, kChild + parent * 2, H(v, kSibling + next * 2));
			else
			{
				ULong q = node;
				while ((node = H(v, kSibling + q * 2)) != next)
					q = node;
				SetH(v, kSibling + q * 2, H(v, kSibling + next * 2));
			}
		}
		SetH(v, kChild + next * 2, 0);
		SetH(v, kParent + next * 2, 0);
	}
	goto restart;

noAdd:
	SetW(v, 0x3880, 0);

restart:
	if (B(v, 0x3860) != 0)
	{
		// transparent: the next string starts at this character
		string = base + c + W(v, 0x24);
		SetW(v, 0x3868, 1);
	}
	goto done;

extended:
	// (the string goes on: nothing added)
	goto done;

toggle:
	string = W(v, 0x3878);
	B(v, 0x3860) = B(v, 0x3860) == 0;

done:
	SetW(v, 0x3874, string);
	SetW(v, 0x387c, next);
	return result;
}


// ROM 0x0025dab0 V42InitCompress__FP13TCompressVarsUiN22PFUlUc_vT5l
// The coder set up for the directions negotiated (1 compress, 2
// decompress, 3 both), N2 and N7, with the procedures that take its output.
void
V42InitCompress(TCompressVars* v, ULong directions, ULong dictionarySize, ULong maxString,
				MNPByteProc compressOut, MNPByteProc decompressOut, void* refCon)
{
	// NOT YET: no procedures, the ROM's internal buffers
	v->fCompressOut = compressOut;
	v->fDecompressOut = decompressOut;
	v->fRefCon = refCon;
	SetW(v, 0x00, directions);
	SetW(v, 0x04, dictionarySize);
	SetW(v, 0x08, maxString);
	SetW(v, 0x14, dictionarySize);
	SetW(v, 0x39b4, 0);
	SetW(v, 0x39b8, 0);
	SetW(v, 0x39bc, 0);
	SetW(v, 0x39c0, 0);
	static const UShort kMasks[9] = { 0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };
	for (int i = 0; i < 9; i++)
		SetH(v, 0x2c + i * 2, kMasks[i]);
	ULong size = 0x200;
	SetW(v, 0x10, 9);
	while (size < W(v, 0x14))
	{
		size *= 2;
		SetW(v, 0x10, W(v, 0x10) + 1);
	}
	SetW(v, 0x18, 8);
	SetW(v, 0x1c, 0x100);
	SetW(v, 0x20, 0x103);
	SetW(v, 0x24, 3);
	SetW(v, 0x28, maxString);
	SetW(v, 0x0c, 0);
	if (directions == 3)
		SetW(v, 0x0c, W(v, 0x04));
	SetW(v, 0x39b4, 0);
	SetW(v, 0x39b8, 0);
	SetW(v, 0x39bc, 0);
	SetW(v, 0x39c0, 0);
	BTInitEn(v);
	BTInitDe(v);
	v->fField39d0 = 0;
}


// MNP's hooks (DEVIATION: MNP.h's byte procedures; the ROM stores BTEncode,
// BTFlush and BTDecode in the hooks themselves)
void
V42EncodeHook(void* vars, UByte byte)
{
	BTEncode((TCompressVars*) vars, byte);
}

void
V42FlushHook(void* vars, UByte /*byte*/)
{
	BTFlush((TCompressVars*) vars);
}

void
V42DecodeHook(void* vars, UByte byte)
{
	BTDecode((TCompressVars*) vars, byte);
}
