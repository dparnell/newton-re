// Marshalling test: a block of C structure bytes read back through a
// template.  The ROM's own `marshalTypes` frame is what the type symbols
// are looked up in, so the ROM's objects are imported for it.
//
// The block built here is the one the Extras drawer asks the system for -
// kGestalt_Ext_VolumeInfo, four flag bytes, a double and two longs - laid
// out the way the ARM lays it out, which is the whole point: the double
// sits on a four-byte boundary where a host compiler would want eight.

#include "Marshalling.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Marshalling: cannot import %s\n", NEWTON_ROM_BIN);
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

	printf("test_Marshalling: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
