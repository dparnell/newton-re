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

#include <string.h>
#include <stdint.h>


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
// NOT YET RECONSTRUCTED: the aggregates other than 'struct
// (UnmarshalArray 0x000cdaf4), and the types that are pointers into the
// block rather than values in it - 'cstring, 'unicode, 'binary, 'ref and
// the split longs.  Those are what calling a C function from NewtonScript
// needs; a system call's parameter block is scalars and structures of
// them.  A template asking for one of them answers nil and says so.
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
		// (kMarshalArray, and the aggregates whose second element is
		//  another type, are NOT YET: UnmarshalArray 0x000cdaf4)
		*failed = 1;
		return NILREF;
	}

	RefVar result;
	char* p = (char*) *bytes;
	switch (TranslateTypeMarshalingSymbol(type))
	{
	case kMarshalLong:
	case kMarshalULong:
		p = AlignedTo(p, 4);
		result = MAKEINT(*(int32_t*) p);
		p += 4;
		break;

	case kMarshalShort:
		if (inRegister)
		{
			result = MAKEINT(*(int32_t*) p & 0xffff);
			p += 4;
		}
		else
		{
			p = AlignedTo(p, 2);
			result = MAKEINT(*(int16_t*) p);
			p += 2;
		}
		break;

	case kMarshalByte:
		if (inRegister)
		{
			result = MAKEINT(*(int32_t*) p & 0xff);
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
				value = *(int32_t*) p & 0xff;
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
			long value = *(int32_t*) p;
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
			uint32_t value = *(uint32_t*) p;
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
				ch = (char) *(int32_t*) p;
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
