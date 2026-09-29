/*
	File:		frames/Marshalling.cpp

	Contains:	Unmarshalling (Marshalling.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Marshalling.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "Frames.h"
#include "NewtonExceptions.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "toolbox/ByteOrder.h"


// ROM 0x000cd6cc TranslateTypeMarshalingSymbol__FRC6RefVar
// The code for a type: an integer is its own code, and a symbol is
// looked up in the ROM's marshalTypes frame - a symbol that is not there
// answers 0, which is a type nothing knows how to read.
long
TranslateTypeMarshalingSymbol(RefArg type)
{
	if (ISINT((Ref) type))
		return RINT(type);
	RefVar code(GetFrameSlotRef(RefVar(Rmarshaltypes), type));
	return ISNIL(code) ? 0 : RINT(code);
}


// The pointer moved on to the next boundary.
static inline char*
AlignedTo(void* p, long align)
{
	return (char*) (((intptr_t) p + align - 1) & ~(intptr_t) (align - 1));
}


// DEVIATION: the byte order the block is read in.  On the MessagePad
// every block is its own (big-endian); on the host a system call's
// parameter block is the host's while bytes that came from outside - a
// comm tool, a wire - are the device's, so ConstructReturnValueFromDevice
// says which for the length of one read.
static bool sDeviceOrder = false;

static inline uint32_t
ReadWord(const char* p)
{
	uint32_t w;
	if (sDeviceOrder)
		return GetBigEndianWord(p);
	memcpy(&w, p, 4);
	return w;
}

static inline uint16_t
ReadHalf(const char* p)
{
	uint16_t h;
	if (sDeviceOrder)
		return GetBigEndianHalf(p);
	memcpy(&h, p, 2);
	return h;
}


// ROM 0x000cdaf4 UnmarshalArray__FPPvRC6RefVarPli
// An ['array, type, count] template: count values of the type, as an
// array (the walk stops at one that cannot be read).
Ref
UnmarshalArray(void** bytes, RefArg type, long* failed, int encoding)
{
	RefVar element(GetArraySlotRef(type, 1));
	long count = RINT(GetArraySlotRef(type, 2));
	RefVar result(MakeArray(count));
	for (long i = 0; i < count; i++)
	{
		RefVar value(UnmarshalValue(bytes, element, 0, failed, encoding));
		SetArraySlot(result, i, value);
		if (*failed != 0)
			break;
	}
	return result;
}


// ROM 0x000cdcb4 UnmarshalValue__FPPvRC6RefVariPlT3
// One value out of the block.  A template - an array - is an aggregate,
// and its first element says which kind; anything else is a scalar, read
// where its own alignment puts it.
//
// `inRegister` is what makes the small types awkward: a byte or a short
// that has been passed in a register sits in the low end of a whole word
// and the pointer steps four, and the same type packed in a structure
// sits where its own alignment puts it and the pointer steps one or two.
//
// An 'array of 'char is a string of the bytes in place and an 'array of
// 'unichar one of the UniChars (a count of nought: up to the terminator);
// any other 'array is its elements (UnmarshalArray); an aggregate that is
// neither is kNSErrBadMarshalType.
// NOT YET RECONSTRUCTED: the types that are pointers into the block
// rather than values in it - 'cstring, 'unicode, 'binary, 'ref and the
// split longs.  Those are what calling a C function from NewtonScript
// needs; a system call's parameter block is scalars and structures of
// them.  A scalar type this does not read answers nil.
Ref
UnmarshalValue(void** bytes, RefArg type, int inRegister, long* failed, int encoding)
{
	*failed = 0;
	if (IsArray(type))
	{
		long kind = TranslateTypeMarshalingSymbol(RefVar(GetArraySlotRef(type, 0)));
		if (kind == kMarshalStruct)
		{
			*bytes = AlignedTo(*bytes, 4);
			return UnmarshalStruct(bytes, type, failed, encoding);
		}
		if (kind != kMarshalArray)
		{
			*failed = kNSErrBadMarshalType;
			return NILREF;
		}
		long elementKind = TranslateTypeMarshalingSymbol(RefVar(GetArraySlotRef(type, 1)));
		if (elementKind == kMarshalChar)
		{
			// characters in place: count of them, or (a count of nought)
			// up to the terminator, the pointer stepping a word past it
			long n = RINT(GetArraySlotRef(type, 2));
			Boolean counted = (n != 0);
			if (!counted)
				n = (strlen((const char*) *bytes) + 4) & ~3;
			const char* str = (const char*) *bytes;
			*bytes = (char*) *bytes + n;
			RefVar result;
			if (counted)
				result = AllocateBinary(RSSYMstring, n * 2 + 2);
			else
			{
				result = AllocateBinary(RSSYMstring, strlen(str) * 2 + 2);
				n = 0x7fffffff;
			}
			ConvertToUnicode(str, (UniChar*) BinaryData(result), encoding, n);
			return result;
		}
		if (elementKind == kMarshalUniChar)
		{
			// UniChars in place, likewise (a halfword step past the terminator)
			long n = RINT(GetArraySlotRef(type, 2));
			const UniChar* str = (const UniChar*) *bytes;
			if (n == 0)
			{
				long length = 0;
				while (ReadHalf((const char*) (str + length)) != 0)
					length++;
				n = (length + 2) & ~1;
			}
			*bytes = (char*) *bytes + n * 2;
			if (!sDeviceOrder)
				return MakeString(str);
			// DEVIATION: the device's UniChars swapped into the host's
			// (MakeString reads to the terminator, as the ROM's does)
			long length = 0;
			while (ReadHalf((const char*) (str + length)) != 0)
				length++;
			UniChar* host = (UniChar*) malloc((length + 1) * sizeof(UniChar));
			for (long i = 0; i <= length; i++)
				host[i] = ReadHalf((const char*) (str + i));
			RefVar result(MakeString(host));
			free(host);
			return result;
		}
		AlignForType(bytes, nil, type);
		return UnmarshalArray(bytes, type, failed, encoding);
	}

	RefVar result;
	char* p = (char*) *bytes;
	switch (TranslateTypeMarshalingSymbol(type))
	{
	case kMarshalLong:
	case kMarshalULong:
		p = AlignedTo(p, 4);
		result = MAKEINT((int32_t) ReadWord(p));
		p += 4;
		break;

	case kMarshalShort:
		if (inRegister)
		{
			result = MAKEINT(ReadWord(p) & 0xffff);
			p += 4;
		}
		else
		{
			p = AlignedTo(p, 2);
			result = MAKEINT((int16_t) ReadHalf(p));
			p += 2;
		}
		break;

	case kMarshalByte:
		if (inRegister)
		{
			result = MAKEINT(ReadWord(p) & 0xff);
			p += 4;
		}
		else
		{
			result = MAKEINT(*(unsigned char*) p);
			p += 1;
		}
		break;

	case kMarshalBoolean:
		{
			long value;
			if (inRegister)
			{
				value = ReadWord(p) & 0xff;
				p += 4;
			}
			else
			{
				value = *(unsigned char*) p;
				p += 1;
			}
			result = MAKEBOOLEAN(value != 0);
		}
		break;

	case kMarshalHighInt:
		// the low two bits are not part of the number
		{
			p = AlignedTo(p, 4);
			long value = (int32_t) ReadWord(p);
			if (value < 0)
				value += 3;
			result = MAKEINT(value >> 2);
			p += 4;
		}
		break;

	case kMarshalHexLong:
		// eight hexadecimal characters, most significant first
		{
			p = AlignedTo(p, 4);
			uint32_t value = ReadWord(p);
			p += 4;
			char digits[9];
			for (long i = 0; i < 8; i++)
			{
				long nibble = (long) ((value >> (i * 4)) & 0xf);
				digits[7 - i] = (char) (nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
			}
			digits[8] = 0;
			result = MakeString(digits);
		}
		break;

	case kMarshalChar:
		{
			char ch;
			if (inRegister)
			{
				ch = (char) ReadWord(p);
				p += 4;
			}
			else
			{
				ch = *p;
				p += 1;
			}
			UniChar wide = 0;
			ConvertToUnicode(&ch, &wide, encoding, 1);
			result = MAKECHAR(wide);
		}
		break;

	case kMarshalReal:
		// a double sits on a four-byte boundary in an ARM structure, which
		// is not where the host would want to read one from, so its bytes
		// are taken rather than dereferenced
		{
			p = AlignedTo(p, 4);
			double value;
			if (sDeviceOrder)
			{
				// the device's double: its bytes most significant first
				uint64_t bits = 0;
				for (int i = 0; i < 8; i++)
					bits = (bits << 8) | (unsigned char) p[i];
				memcpy(&value, &bits, sizeof(value));
			}
			else
				memcpy(&value, p, sizeof(value));
			result = MakeReal(value);
			p += 8;
		}
		break;

	default:
		// a type this does not read: nil, and the pointer left where it was
		break;
	}
	*bytes = p;
	return result;
}


// ROM 0x000cdbdc UnmarshalStruct__FPPvRC6RefVarPli
// The template's fields, in order, as an array one shorter than the
// template - the 'struct at its head is not a field.  The walk stops at
// the first field that cannot be read, leaving the rest nil.
Ref
UnmarshalStruct(void** bytes, RefArg type, long* failed, int encoding)
{
	long count = Length(type);
	RefVar result(MakeArray(count - 1));
	for (long i = 1; i < count; i++)
	{
		RefVar field(GetArraySlotRef(type, i));
		RefVar value(UnmarshalValue(bytes, field, 0, failed, encoding));
		SetArraySlot(result, i - 1, value);
		if (*failed != 0)
			break;
	}
	return result;
}


// ROM 0x000cf7c0 ConstructReturnValue__FPvRC6RefVarPli
// What a parameter block says, by its template.  The block is a return
// value rather than an argument list, so the top of it is read as though
// it had come back in a register.
Ref
ConstructReturnValue(void* bytes, RefArg type, long* failed, int encoding)
{
	void* p = bytes;
	return UnmarshalValue(&p, type, 1, failed, encoding);
}


// DEVIATION: ConstructReturnValue over bytes in the MessagePad's order -
// what a comm tool or a wire hands back - where the ROM's own reads its
// own order, which on the device is the same thing.
Ref
ConstructReturnValueFromDevice(void* bytes, RefArg type, long* failed, int encoding)
{
	bool saved = sDeviceOrder;
	sDeviceOrder = true;
	RefVar result;
	newton_try
	{
		result = ConstructReturnValue(bytes, type, failed, encoding);
	}
	newton_catch_all
	{
		sDeviceOrder = saved;
		rethrow;
	}
	end_try;
	sDeviceOrder = saved;
	return result;
}
