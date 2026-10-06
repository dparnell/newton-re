/*
	File:		frames/MarshalOut.cpp

	Contains:	Marshalling (Marshalling.h): NewtonScript values into a block
				of bytes, by a template - the direction Marshalling.cpp's
				unmarshalling does not go.

				The values are an argument list and the template a list of
				types, one each; a 'struct template packs its fields, an
				'array template repeats one type ('cstring and 'unicode
				strings stay behind a pointer, or go into a separate string
				area when there is one).  The byte order of what is written
				is the MessagePad's - big-endian - so that what a script
				marshals for a comm tool or a wire is the bytes the device
				would have sent: a two-byte value the ROM writes a byte at a
				time, a word it stores whole (DEVIATION: the host writes that
				word's big-endian bytes, where the ROM simply stores it in
				its own order).  The block's pointers (a 'cstring's, a
				binary's) are DEVIATION too: the host keeps the low four bytes
				of a pointer, all the ROM's layout has room for.

	Reconstructed from the MP2x00 US ROM (0x000cd634, 0x000cd750,
	0x000cda08, 0x000ce38c, 0x000ce85c-0x000cf7c0); each function cites its
	origin.
*/

#include "Marshalling.h"
#include "NarrowRef.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "Frames.h"
#include "NewtonMemory.h"
#include "host/RomBugs.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>


// the template kinds, as TranslateTypeMarshalingSymbol codes them
static long
TypeCode(RefArg type)
{
	return ISINT((Ref) type) ? RINT(type) : TranslateTypeMarshalingSymbol(type);
}


// ROM 0x000cd750 RefToULong__FRC6RefVarPUl
// A value as a word: an integer, a character, nil (nought), a boolean (one),
// a pair [high, low] of halfwords, four bytes [a, b, c, d], a string of hex
// digits, or a binary's address.
long
RefToULong(RefArg value, ULong* result)
{
	Ref r = value;
	ULong word;
	if (ISINT(r))
		word = NarrowToWord(RINT(r), "marshalled word");		// NEWTON_NS64: a 32-bit field (NarrowRef.h)
	else if (ISCHAR(r))
		word = RCHAR(r) & 0xffff;
	else if (r == NILREF)
		word = 0;
	else if ((r & 3) == 2 && ((r >> 2) & 3) == 2)
		word = 1;
	else if (IsArray(value))
	{
		long length = Length(value);
		if (length == 2)
			word = (RINT(GetArraySlotRef(value, 1)) & 0xffff) | (RINT(GetArraySlotRef(value, 0)) << 16);
		else if (length == 4)
			word = (RINT(GetArraySlotRef(value, 0)) << 24) | ((RINT(GetArraySlotRef(value, 1)) & 0xff) << 16)
				 | ((RINT(GetArraySlotRef(value, 2)) & 0xff) << 8) | (RINT(GetArraySlotRef(value, 3)) & 0xff);
		else
			return kNSErrBadMarshalValue;
	}
	else if (IsString(value))
	{
		// the digits are read as bytes, the low byte of each character
		const UniChar* s = GetCString(value);
		word = 0;
		for ( ; *s != 0; s++)
		{
			UniChar c = *s & 0xff;
			if (c >= '0' && c <= '9')
				word = word * 16 + (c & 0xf);
			else if (c >= 'a' && c <= 'f')
				word = word * 16 + c - 0x57;
			else if (c >= 'A' && c <= 'F')
				word = word * 16 + c - 0x37;
			else
				return kNSErrBadMarshalValue;
		}
	}
	else if (IsBinary(value))
		word = (ULong) (uintptr_t) BinaryData(value);		// DEVIATION: truncated (above)
	else
		return kNSErrBadMarshalValue;
	if (result != nil)
		*result = word;
	return noErr;
}


// ROM 0x000ce85c StuffScalar__FUlPPvPUlT1
// A value of n bytes, on an n-byte boundary.  (A size that is not 1, 2 or
// 4 is counted but nothing is written and the pointer does not move.)
void
StuffScalar(ULong value, void** buf, ULong* size, ULong n)
{
	if (buf != nil)
		*buf = (void*) (((uintptr_t) *buf + n - 1) & ~(uintptr_t) (n - 1));
	if (size != nil)
		*size = (*size + n - 1) & ~(n - 1);
	if (buf != nil)
	{
		UByte* p = (UByte*) *buf;
		if (n == 1)
		{
			p[0] = value;
			*buf = p + 1;
		}
		else if (n == 2)
		{
			p[1] = value;
			p[0] = value >> 8;
			*buf = p + 2;
		}
		else if (n == 4)
		{
			p[0] = value >> 24;
			p[1] = value >> 16;
			p[2] = value >> 8;
			p[3] = value;
			*buf = p + 4;
		}
	}
	if (size != nil)
		*size += n;
}


// ROM 0x000ce9ac StuffPtr__FPvPPvPUlUl
// (A pointer is stuffed as a scalar - DEVIATION: its low four bytes.)
void
StuffPtr(void* ptr, void** buf, ULong* size, ULong n)
{
	StuffScalar((ULong) (uintptr_t) ptr, buf, size, n);
}


// ROM 0x000ce918 StuffDouble__FdPPvPUl
// Eight bytes on a four-byte boundary (the ARM's double: big-endian).
void
StuffDouble(double value, void** buf, ULong* size)
{
	if (buf != nil)
	{
		UByte* p = (UByte*) (((uintptr_t) *buf + 3) & ~(uintptr_t) 3);
		uint64_t bits;
		memcpy(&bits, &value, 8);
		for (int i = 0; i < 8; i++)
			p[i] = bits >> (56 - 8 * i);
		*buf = p + 8;
	}
	if (size == nil)
		return;
	*size = ((*size + 3) & ~3) + 8;
}


// ROM 0x000ce968 AlignBuffer__FPPvPUlUl
void
AlignBuffer(void** buf, ULong* size, ULong align)
{
	if (buf != nil)
		*buf = (void*) (((uintptr_t) *buf + align - 1) & ~(uintptr_t) (align - 1));
	if (size == nil)
		return;
	*size = (*size + align - 1) & ~(align - 1);
}


// ROM 0x000ce9b0 AlignForType__FPPvPUlRC6RefVar
// The boundary a type starts on: a struct's four, an array's its element's,
// a short's two, a byte's, boolean's or char's none, anything else four.
void
AlignForType(void** buf, ULong* size, RefArg type)
{
	if (IsArray(type))
	{
		long kind = TypeCode(RefVar(GetArraySlotRef(type, 0)));
		if (kind == kMarshalStruct)
			AlignBuffer(buf, size, 4);
		else if (kind == kMarshalArray)
			AlignForType(buf, size, RefVar(GetArraySlotRef(type, 1)));
		return;
	}
	long code = TypeCode(type);
	ULong align;
	if (code == kMarshalShort)
		align = 2;
	else if (code == kMarshalByte || code == kMarshalBoolean || code == kMarshalChar)
		return;
	else
		align = 4;
	if (buf != nil)
		*buf = (void*) (((uintptr_t) *buf + align - 1) & ~(uintptr_t) (align - 1));
	if (size != nil)
		*size = (*size + align - 1) & ~(align - 1);
}


// ROM 0x000ceaf0 MarshalCString__FRC6RefVarPPvT2PUlT4il
// A string: in the string area, with a pointer to it where it is due, when
// there is a string area; else in place, cut to max bytes if max says so.
// Encoding nought keeps it Unicode.
void
MarshalCString(RefArg value, void** buf, void** strBuf, ULong* size, ULong* strSize, int encoding, long max)
{
	if (strBuf != nil)
		StuffPtr(*strBuf, buf, size, 4);
	else if (strSize != nil)
		StuffPtr(nil, buf, size, 4);
	long length;
	if (encoding == 0)
		length = Length(value);
	else
		length = Umbstrlen(GetCString(value)) + 1;
	Boolean truncated = false;
	if (strSize == nil)
	{
		if (size != nil || buf != nil)
		{
			if (max > 0 && max < length)
			{
				length = max;
				truncated = true;
			}
			if (size != nil)
				*size += length;
		}
	}
	else
		*strSize += (length + 3) & ~3;
	const UniChar* chars = GetCString(value);
	if (strBuf == nil)
	{
		if (buf != nil)
		{
			if (!truncated)
			{
				if (encoding == 0)
					Ustrcpy((UniChar*) *buf, chars);
				else
					ConvertFromUnicode(chars, *buf, encoding, 0x7fffffff);
			}
			else
				ConvertUnicodeCharacters(chars, (char*) *buf, encoding, length);
			*buf = (char*) *buf + length;
		}
	}
	else
	{
		if (encoding == 0)
			Ustrcpy((UniChar*) *strBuf, chars);
		else
			ConvertFromUnicode(chars, *strBuf, encoding, 0x7fffffff);
		*strBuf = (char*) *strBuf + ((length + 3) & ~3);
	}
}


// ROM 0x000cece8 MarshalAggregrate__FRC6RefVarT1PPvT3PUlT5liT1
// A 'struct (its fields, padded to a word) or an 'array (its elements; a
// string into an array of bytes, chars or shorts is copied in, cut to the
// array's size).  What it answers for any other type is
// kNSErrNotAnAggregate, which DoMarshal takes as "a scalar".
long
MarshalAggregrate(RefArg value, RefArg type, void** buf, void** strBuf, ULong* size, ULong* strSize, long step, int encoding, RefArg locked)
{
	long kind = TypeCode(RefVar(GetArraySlotRef(type, 0)));
	if (kind == kMarshalStruct)
	{
		AlignBuffer(buf, size, 4);
		long err = Marshal1(value, type, buf, strBuf, nil, size, strSize, nil, 1, 0, 0x10000, step, encoding, locked);
		if (err != noErr)
			return err;
		if (size != nil)
			*size += (*size & 3) ? 4 - (*size & 3) : 0;
		if (buf == nil)
			return noErr;
		uintptr_t p = (uintptr_t) *buf;
		*buf = (void*) (p + ((p & 3) ? 4 - (p & 3) : 0));
		return noErr;
	}
	if (kind != kMarshalArray)
		return kNSErrNotAnAggregate;
	AlignForType(buf, size, type);
	if (!IsString(value) || !ISINT(GetArraySlotRef(type, 2)))
		return Marshal1(value, type, buf, strBuf, nil, size, strSize, nil, 1, 0, RINT(GetArraySlotRef(type, 2)), 0, encoding, locked);

	// a string into an array of characters
	long elementKind = TypeCode(RefVar(GetArraySlotRef(type, 1)));
	Boolean wide;
	long n;
	if (elementKind == kMarshalShort || elementKind == kMarshalUniChar)
	{
		wide = true;
		n = RINT(GetArraySlotRef(type, 2)) * 2;
	}
	else if (elementKind == kMarshalByte || elementKind == kMarshalChar)
	{
		wide = false;
		n = RINT(GetArraySlotRef(type, 2));
		if (n != 0)
			n = Umbstrnlen(GetCString(value), encoding, n);
	}
	else
		return kNSErrNotAnAggregate;
	char* end = (buf != nil) ? (char*) *buf + n : nil;
	ULong endSize = (size != nil) ? *size + n : 0;
	if (wide)
		encoding = 0;
	MarshalCString(value, buf, nil, size, nil, encoding, n);
	if (n == 0)
		AlignBuffer(buf, size, 4);
	else
	{
		if (buf != nil)
			*buf = end;
		if (size != nil)
			*size = endSize;
	}
	return noErr;
}


// ROM 0x000cf078 Marshal1__FRC6RefVarT1PPvN23PUlN26lN39iT1
// The values from valueIndex on, by the types from typeIndex on (step: how
// far the type moves with each value - one for an argument list or a
// struct's fields, nought for an array's elements), until either runs out
// or count values are done.  A scalar goes to the register area when there
// is one (regBuf, regSize), taking a whole word there.
long
Marshal1(RefArg value, RefArg type, void** buf, void** strBuf, void** regBuf, ULong* size, ULong* strSize, ULong* regSize,
		 long typeIndex, long valueIndex, long count, long step, int encoding, RefArg locked)
{
	long err = noErr;
	while (Length(type) > typeIndex && valueIndex < count)
	{
		RefVar t(GetArraySlotRef(type, typeIndex));
		RefVar v((valueIndex == 0x10000 || ISNIL((Ref) value)) ? (Ref) 0 : GetArraySlotRef(value, valueIndex));
		void** target = (regBuf != nil) ? regBuf : buf;
		ULong* targetSize = (regSize != nil) ? regSize : size;
		Boolean inRegisters = (regBuf != nil || regSize != nil);
		if (IsArray(t))
		{
			long kind = TypeCode(RefVar(GetArraySlotRef(t, 0)));
			if (kind == kMarshalCString)
				MarshalCString(v, target, strBuf, targetSize, strSize, RINT(GetArraySlotRef(t, 1)), 0);
			else
			{
				long aggregateErr = MarshalAggregrate(v, t, target, strBuf, targetSize, strSize, step, encoding, locked);
				err = noErr;
				if (aggregateErr != noErr)
					return aggregateErr;
			}
		}
		else
		{
			ULong word;
			switch (TypeCode(t))
			{
			case kMarshalLong:
			case kMarshalULong:
			case kMarshalHexLong:
			case kMarshalSplitLong:
			case kMarshalSplitByteLong:
				if ((err = RefToULong(v, &word)) == noErr)
					StuffScalar(word, target, targetSize, 4);
				break;

			case kMarshalShort:
			case kMarshalUniChar:
				if ((err = RefToULong(v, &word)) == noErr)
					StuffScalar(word, target, targetSize, inRegisters ? 4 : 2);
				break;

			case kMarshalChar:
				if (ISCHAR((Ref) v))
				{
					// ROM BUG (fixed): what is stuffed is the address of the
					// converted bytes, not the bytes (StuffScalar is handed
					// the stack pointer); the host stuffs its buffer's
					// address likewise.  The fix stuffs the converted
					// character: its byte, or its two bytes big-endian.
					UniChar c[2] = { (UniChar) RCHAR(v), 0 };
					char bytes[4] = { 0, 0, 0, 0 };
					long k = ConvertUnicodeChar(c, bytes, encoding);
					ULong n = inRegisters ? 4 : (k == 1 ? 1 : 2);
					if (RomBugFixed())
						StuffScalar(k == 1 ? (ULong) (UByte) bytes[0] : ((ULong) (UByte) bytes[0] << 8) | (UByte) bytes[1], target, targetSize, n);
					else
						StuffScalar((ULong) (uintptr_t) bytes, target, targetSize, n);
					break;
				}
				// not a character: a byte
				// fall through
			case kMarshalByte:
			case kMarshalBoolean:
				if ((err = RefToULong(v, &word)) == noErr)
					StuffScalar(word, target, targetSize, inRegisters ? 4 : 1);
				break;

			case kMarshalHighInt:
				err = RefToULong(v, &word);
				word <<= 2;
				if (err == noErr)
					StuffScalar(word, target, targetSize, 4);
				break;

			case kMarshalCString:
				MarshalCString(v, target, strBuf, targetSize, strSize, encoding, 0);
				break;

			case kMarshalUnicodeString:
				MarshalCString(v, target, strBuf, targetSize, strSize, 0, 0);
				break;

			case kMarshalBinary:
				// ROM QUIRK: a binary is locked (and kept on the list to be
				// unlocked) and then marshalled as a real, as 'real is
				if ((ObjectFlags(v) & 0x10) == 0)
				{
					LockRef(v);
					if (!ISNIL((Ref) locked))
						AddArraySlot(locked, v);
				}
				// fall through
			case kMarshalReal:
				{
					double d = CoerceToDouble(v);
					if (err == noErr)
						StuffDouble(d, target, targetSize);
				}
				break;

			default:
				return kNSErrBadMarshalType;
			}
		}
		valueIndex++;
		typeIndex += step;
		if (err != noErr)
			return err;
	}
	if (step == 0 && valueIndex < count)
	{
		AlignForType(buf, size, RefVar(GetArraySlotRef(type, typeIndex)));
		err = Marshal1(value, type, buf, strBuf, nil, size, strSize, nil, typeIndex, 0x10000, 0x10000, 0, encoding, locked);
	}
	return err;
}


// ROM 0x000cda08 DoMarshal__FRC6RefVarT1PPvN23PUlN26lN39i
// An aggregate if the template is one, else the argument list's values by
// the template's types.
long
DoMarshal(RefArg value, RefArg type, void** buf, void** strBuf, void** regBuf, ULong* size, ULong* strSize, ULong* regSize,
		  long typeIndex, long valueIndex, long count, long step, int encoding)
{
	RefVar locked;
	long err = MarshalAggregrate(value, type, buf, strBuf, size, strSize, step, encoding, locked);
	if (err == kNSErrNotAnAggregate)
	{
		RefVar locked2;
		err = Marshal1(value, type, buf, strBuf, regBuf, size, strSize, regSize, typeIndex, valueIndex, count, step, encoding, locked2);
	}
	return err;
}


// ROM 0x000ce38c AggregateSize__FRC6RefVarPUl
long
AggregateSize(RefArg type, ULong* size)
{
	long err = noErr;
	if (IsArray(type))
	{
		long kind = TypeCode(RefVar(GetArraySlotRef(type, 0)));
		if (kind == kMarshalStruct || kind == kMarshalArray)
		{
			RefVar locked;
			RefVar value;
			err = MarshalAggregrate(value, type, nil, nil, size, nil, 1, 2, locked);
		}
	}
	return err;
}


// ROM 0x000cf694 MarshalArgumentSize__FRC6RefVarT1PUli
// (The size is set whatever DoMarshal answers.)
long
MarshalArgumentSize(RefArg args, RefArg types, ULong* size, int encoding)
{
	ULong mainSize = 0, strSize = 0;
	long err = DoMarshal(args, types, nil, nil, nil, &mainSize, &strSize, nil, 0, 0, 0x10000, 1, encoding);
	*size = mainSize + strSize;
	return err;
}


// ROM 0x000cf700 MarshalArguments__FRC6RefVarT1PvUli
// Into buf: the values, then their strings.
long
MarshalArguments(RefArg args, RefArg types, void* buf, ULong size, int encoding)
{
	ULong mainSize = 0, strSize = 0;
	long err = DoMarshal(args, types, nil, nil, nil, &mainSize, &strSize, nil, 0, 0, 0x10000, 1, encoding);
	if (err != noErr)
		return err;
	void* p = buf;
	void* strings = (char*) buf + mainSize;
	return DoMarshal(args, types, &p, &strings, nil, nil, nil, nil, 0, 0, 0x10000, 1, encoding);
}


// ROM 0x000cd634 MarshalArguments__FRC6RefVarT1PPvi
// Into a block of its own (malloc'd; the caller frees it).
long
MarshalArguments(RefArg args, RefArg types, void** block, int encoding)
{
	*block = nil;
	ULong size;
	long err = MarshalArgumentSize(args, types, &size, encoding);
	void* p = nil;
	if (err == noErr)
	{
		p = malloc(size);
		if (p == nil)
			err = MemError();
	}
	if (err != noErr)
		return err;
	err = MarshalArguments(args, types, p, size, encoding);
	if (err == noErr)
		*block = p;
	else
		free(p);
	return err;
}
