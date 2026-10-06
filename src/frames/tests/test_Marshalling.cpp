// Marshalling test: a block of C structure bytes read back through a
// template.  The ROM's own `marshalTypes` frame is what the type symbols
// are looked up in, so the ROM's objects are imported for it.
//
// The block built here is the one the Extras drawer asks the system for -
// kGestalt_Ext_VolumeInfo, four flag bytes, a double and two longs - laid
// out the way the ARM lays it out, which is the whole point: the double
// sits on a four-byte boundary where a host compiler would want eight.

#include "Marshalling.h"
#include "Unicode.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "memory/host/KernelHeap.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a template array: ['struct, ...types]
static Ref
Template(const char* const* types, long count)
{
	RefVar t(MakeArray(count));
	for (long i = 0; i < count; i++)
		SetArraySlot(t, i, RefVar(Intern((char*) types[i])));
	return t;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Marshalling: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	// the type symbols come out of the ROM's own table
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(Intern((char*) "long"))) == kMarshalLong);
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(Intern((char*) "boolean"))) == kMarshalBoolean);
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(Intern((char*) "struct"))) == kMarshalStruct);
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(Intern((char*) "Real"))) == kMarshalReal);
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(MAKEINT(7))) == kMarshalHexLong);
	EXPECT(TranslateTypeMarshalingSymbol(RefVar(Intern((char*) "notAType"))) == 0);

	// ---- the volume information block ----
	{
		unsigned char block[20];
		memset(block, 0, sizeof(block));
		block[0] = 0;
		block[1] = 1;
		block[2] = 1;				// the server answers the volume
		block[3] = 0;
		double decibels = -18.0618;
		memcpy(block + 4, &decibels, sizeof(decibels));		// on a four-byte boundary
		int32_t highest = 4;
		memcpy(block + 12, &highest, sizeof(highest));
		int32_t spare = -7;
		memcpy(block + 16, &spare, sizeof(spare));

		static const char* const kTypes[] =
			{ "struct", "boolean", "boolean", "boolean", "boolean", "Real", "long", "long" };
		RefVar type(Template(kTypes, 8));
		long failed = 0;
		RefVar fields(ConstructReturnValue(block, type, &failed, 0));
		EXPECT(failed == 0);
		EXPECT(IsArray(fields) && Length(fields) == 7);
		EXPECT(ISNIL(GetArraySlotRef(fields, 0)));
		EXPECT(NOTNIL(GetArraySlotRef(fields, 1)));
		EXPECT(NOTNIL(GetArraySlotRef(fields, 2)));
		EXPECT(ISNIL(GetArraySlotRef(fields, 3)));
		double back = CDouble(RefVar(GetArraySlotRef(fields, 4)));
		EXPECT(back < -18.06 && back > -18.07);
		EXPECT(RINT(GetArraySlotRef(fields, 5)) == 4);
		EXPECT(RINT(GetArraySlotRef(fields, 6)) == -7);
	}

	// ---- the small types, packed and in a register ----
	{
		unsigned char block[16];
		memset(block, 0, sizeof(block));
		block[0] = 0x41;					// a byte
		block[1] = 0;
		*(int16_t*) (block + 2) = -3;		// a short, on its own boundary
		*(int32_t*) (block + 4) = 0x1234;	// a long
		*(int32_t*) (block + 8) = 40;		// a high int: the low two bits are not part of it
		*(int32_t*) (block + 12) = (int32_t) 0xdeadbeef;

		static const char* const kTypes[] =
			{ "struct", "byte", "byte", "short", "long", "highint", "hexlong" };
		RefVar type(Template(kTypes, 7));
		long failed = 0;
		RefVar fields(ConstructReturnValue(block, type, &failed, 0));
		EXPECT(failed == 0 && Length(fields) == 6);
		EXPECT(RINT(GetArraySlotRef(fields, 0)) == 0x41);
		EXPECT(RINT(GetArraySlotRef(fields, 1)) == 0);
		EXPECT(RINT(GetArraySlotRef(fields, 2)) == -3);
		EXPECT(RINT(GetArraySlotRef(fields, 3)) == 0x1234);
		EXPECT(RINT(GetArraySlotRef(fields, 4)) == 10);			// 40 / 4
		RefVar hex(GetArraySlotRef(fields, 5));
		EXPECT(NOTNIL(hex) && Length(hex) == (long) ((8 + 1) * sizeof(UniChar)));
	}

	// a type this cannot read says so and answers nil
	{
		unsigned char block[8];
		memset(block, 0, sizeof(block));
		static const char* const kTypes[] = { "struct", "cstring" };
		RefVar type(Template(kTypes, 2));
		long failed = 0;
		RefVar fields(ConstructReturnValue(block, type, &failed, 0));
		EXPECT(Length(fields) == 1 && ISNIL(GetArraySlotRef(fields, 0)));
	}

	// ---- marshalling out: the NIE's remote socket option, as a script
	// writes it - ['struct, ['array, 'byte, 4], 'short] - big-endian and the
	// struct rounded up to a word ----
	{
		RefVar address(MakeArray(4));
		SetArraySlot(address, 0, MAKEINT(127));
		SetArraySlot(address, 1, MAKEINT(0));
		SetArraySlot(address, 2, MAKEINT(0));
		SetArraySlot(address, 3, MAKEINT(1));
		RefVar args(MakeArray(2));
		SetArraySlot(args, 0, address);
		SetArraySlot(args, 1, MAKEINT(0x1234));
		RefVar bytes(MakeArray(3));
		SetArraySlot(bytes, 0, RefVar(Intern((char*) "array")));
		SetArraySlot(bytes, 1, RefVar(Intern((char*) "byte")));
		SetArraySlot(bytes, 2, MAKEINT(4));
		RefVar type(MakeArray(3));
		SetArraySlot(type, 0, RefVar(Intern((char*) "struct")));
		SetArraySlot(type, 1, bytes);
		SetArraySlot(type, 2, RefVar(Intern((char*) "short")));

		ULong size = 0;
		EXPECT(MarshalArgumentSize(args, type, &size, 0) == noErr && size == 8);
		unsigned char block[8];
		memset(block, 0xee, sizeof(block));
		EXPECT(MarshalArguments(args, type, block, sizeof(block), 0) == noErr);
		static const unsigned char kExpected[6] = { 0x7f, 0, 0, 1, 0x12, 0x34 };
		EXPECT(memcmp(block, kExpected, 6) == 0);

		// and back, in the device's order: the array field an array again
		long failed = 0;
		RefVar back(ConstructReturnValueFromDevice(block, type, &failed, 0));
		EXPECT(failed == 0 && IsArray(back) && Length(back) == 2);
		RefVar a(GetArraySlotRef(back, 0));
		EXPECT(IsArray(a) && Length(a) == 4 && RINT(GetArraySlotRef(a, 0)) == 127 && RINT(GetArraySlotRef(a, 3)) == 1);
		EXPECT(RINT(GetArraySlotRef(back, 1)) == 0x1234);
	}

	// a character marshalled out: the ROM stuffs the address of its
	// converted bytes (a ROM bug); fixed, the byte itself
	{
		static const char* const kTypes[] = { "struct", "char", "byte" };
		RefVar type(Template(kTypes, 3));
		RefVar args(MakeArray(2));
		SetArraySlot(args, 0, MAKECHAR('A'));
		SetArraySlot(args, 1, MAKEINT(0x42));
		unsigned char block[8];
		memset(block, 0xee, sizeof(block));
		EXPECT(MarshalArguments(args, type, block, sizeof(block), kMacRomanEncoding) == noErr);
		EXPECT(block[0] == 'A' && block[1] == 0x42);
		SetRomBugFixed(false);
		memset(block, 0xee, sizeof(block));
		EXPECT(MarshalArguments(args, type, block, sizeof(block), kMacRomanEncoding) == noErr);
		EXPECT(block[1] == 0x42);		// (block[0]: the low byte of a stack address)
		SetRomBugFixed(true);
	}

	// device-order words and an array of characters read as a string
	{
		alignas(4) static const unsigned char kBlock[12] = { 0xff, 0xff, 0xff, 0xfe, 'a', 'b', 'c', 0, 0x12, 0x34, 0, 0 };
		RefVar chars(MakeArray(3));
		SetArraySlot(chars, 0, RefVar(Intern((char*) "array")));
		SetArraySlot(chars, 1, RefVar(Intern((char*) "char")));
		SetArraySlot(chars, 2, MAKEINT(4));
		RefVar type(MakeArray(4));
		SetArraySlot(type, 0, RefVar(Intern((char*) "struct")));
		SetArraySlot(type, 1, RefVar(Intern((char*) "long")));
		SetArraySlot(type, 2, chars);
		SetArraySlot(type, 3, RefVar(Intern((char*) "short")));
		long failed = 0;
		RefVar back(ConstructReturnValueFromDevice((void*) kBlock, type, &failed, kMacRomanEncoding));
		EXPECT(failed == 0 && Length(back) == 3);
		EXPECT(RINT(GetArraySlotRef(back, 0)) == -2);
		RefVar s(GetArraySlotRef(back, 1));
		EXPECT(IsString(s) && GetCString(s)[0] == 'a' && GetCString(s)[2] == 'c' && GetCString(s)[3] == 0);
		EXPECT(RINT(GetArraySlotRef(back, 2)) == 0x1234);

		// an aggregate that is neither a struct nor an array
		RefVar bad(MakeArray(1));
		SetArraySlot(bad, 0, RefVar(Intern((char*) "long")));
		ConstructReturnValueFromDevice((void*) kBlock, bad, &failed, 0);
		EXPECT(failed == kNSErrBadMarshalType);
	}

	// a long and a short packed, the long's word big-endian
	{
		static const char* const kTypes[] = { "struct", "long", "short", "byte" };
		RefVar type(Template(kTypes, 4));
		RefVar args(MakeArray(3));
		SetArraySlot(args, 0, MAKEINT(0x01020304));
		SetArraySlot(args, 1, MAKEINT(-2));
		SetArraySlot(args, 2, MAKEINT(0x41));
		void* block = nil;
		EXPECT(MarshalArguments(args, type, &block, 0) == noErr && block != nil);
		if (block != nil)
		{
			static const unsigned char kExpected[7] = { 1, 2, 3, 4, 0xff, 0xfe, 0x41 };
			EXPECT(memcmp(block, kExpected, 7) == 0);
			free(block);
		}
		// a value the template cannot take
		SetArraySlot(args, 0, RefVar(AllocateFrame()));			// a frame is no word
		ULong size;
		EXPECT(MarshalArgumentSize(args, type, &size, 0) != noErr);
	}

	printf("test_Marshalling: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
