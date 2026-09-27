// ROM import test: the ROM image's object area read into the
// host (ROMImport.h), then the object system started over it: the ROM's
// symbol table serves Intern, the magic pointer table and the constants
// resolve to ROM objects, strings and reals read as the host's, the
// built-in functions frame has its functions, and heap objects referring
// to ROM objects survive a collection.

#include "NewtQD.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a ROM object of the given class among the first n, for a look at its data
static Ref
FindROMObjectOfClass(Ref theClass, Boolean slotted, long minLength)
{
	for (long i = 0; i < gMagicPointerTableCounts[0]; i++)
	{
		RefVar obj(gMagicPointerTables[0][i]);
		if (!ISPTR(obj))
			continue;
		if (slotted != ((ObjectFlags(obj) & kObjSlotted) != 0))
			continue;
		if (slotted && (ObjectFlags(obj) & kObjFrame) != 0)
		{
			// a frame: look at its slots
			long length = Length(obj);
			for (long j = 0; j < length; j++)
			{
				Ref slot = ObjArraySlots(OBJ(obj))[j];
				if (ISPTR(slot) && (ObjectFlags(slot) & kObjSlotted) == 0 && EQRef(ObjClass(OBJ(slot)), theClass) && Length(slot) >= minLength)
					return slot;
			}
		}
	}
	return NILREF;
}


int
main()
{
	InitHostStandaloneHeap();
	NewtonErr err = ImportROMObjectsFromFile(NEWTON_ROM_IMAGE);
	if (err != noErr)
	{
		printf("test_ROMImport: cannot import %s (%ld)\n", NEWTON_ROM_IMAGE, (long) err);
		return 1;
	}
	EXPECT(ROMObjectsImported() && ROMObjectCount() == 46538);
	gObjectHeapSize = 0x80000;
	InitObjects();

	// the ROM's symbol table serves Intern; its symbols are the constants'
	EXPECT(gROMSymbolTableSize == 32768 && gROMSymbolTableHashShift == 17);
	EXPECT(InROMObjectArea(RSSYM_proto) && strcmp(SymbolName(RSSYM_proto), "_proto") == 0);
	EXPECT(Intern((char*) "_proto") == RSSYM_proto && Intern((char*) "_PROTO") == RSSYM_proto);
	EXPECT(Intern((char*) "frozenEntry") != NILREF && InROMObjectArea(Intern((char*) "frozenEntry")));
	EXPECT(SymbolHash(RSSYMstring) == SymbolHashFunction("string"));
	Ref fresh = Intern((char*) "notInTheROMAtAll");
	EXPECT(gHeap->InHeap(fresh) && Intern((char*) "notInTheROMAtAll") == fresh);
	EXPECT(gNumSymbols == 1);
	EXPECT(EQ(RSSYMstring, Intern((char*) "string")) && !EQ(RSSYMstring, RSSYMsymbol));

	// the built-in functions frame and its map
	EXPECT(IsFrame(Rbuiltinfunctions) && gROMBuiltinFunctions == Rbuiltinfunctions && InROMObjectArea(Rbuiltinfunctions));
	EXPECT(Length(Rbuiltinfunctions) > 500);
	RefVar length(GetFrameSlot(Rbuiltinfunctions, Intern((char*) "Length")));
	EXPECT(IsFrame(length) && FrameHasSlot(length, Intern((char*) "funcPtr")));
	EXPECT(FrameHasSlot(Rbuiltinfunctions, Intern((char*) "SetLength")) && !FrameHasSlot(Rbuiltinfunctions, fresh));
	EXPECT(EQ(ClassOf(length), Intern((char*) "CFunction")) || EQ(ClassOf(length), RSSYM_function_2Enative));
	EXPECT(FrameHasPath(Rbuiltinfunctions, Intern((char*) "Length")));
	// the frame's map is a ROM map, sorted
	Ref map = ObjClass(OBJ(Rbuiltinfunctions));
	EXPECT(InROMObjectArea(map) && (MapFlags(OBJ(map)) & kMapSorted) != 0);
	// a slot of the frame may be found through GetFrameSlotRef's fast path
	EXPECT(GetFrameSlotRef(Rbuiltinfunctions, Intern((char*) "Length")) == (Ref) length);

	// magic pointers and the R constants
	EXPECT(gMagicPointerTableCounts[0] == 873);
	Ref mp0 = MAKEMAGICPTR(0);
	EXPECT(ObjectPtr(mp0) == ObjectPtr(gMagicPointerTables[0][0]));
	EXPECT(ISPTR(gMagicPointerTables[0][0]) && InROMObjectArea(gMagicPointerTables[0][0]));
	EXPECT(RTAG(Rupbitmap) == kTagMagicPtr);
	EXPECT(IsBinary(Rupbitmap) || IsFrame(Rupbitmap));
	EXPECT(EQ(ClassOf(MAKEMAGICPTR(1 << 12 | 2)), ClassOf(Rbuiltinfunctions)));
	long badMagic = 0;
	newton_try
	{
		ObjectPtr(MAKEMAGICPTR(1000));
	}
	newton_catch((ExceptionName) "evt.ex.fr")
	{
		badMagic = 1;
	}
	end_try;
	EXPECT(badMagic == 1);

	// strings and reals read as the host's
	RefVar str(FindROMObjectOfClass(RSSYMstring, true, 4));
	EXPECT(IsString(str));
	if (IsString(str))
	{
		UniChar* s = GetCString(str);
		Boolean printable = true;
		for (long i = 0; s[i] != 0; i++)
			if (s[i] < 0x20 && s[i] != '\t' && s[i] != '\n' && s[i] != '\r')
				printable = false;
		EXPECT(printable && s[0] < 0x100);
		EXPECT(s[Length(str) / 2 - 1] == 0);
	}
	RefVar real(FindROMObjectOfClass(RSSYMreal, true, 8));
	EXPECT(ISREAL(real));
	if (ISREAL(real))
	{
		double d = CDouble(real);
		EXPECT(d == d && fabs(d) < 1e30);
	}
	// a shape's rectangle is read as a Rect: its halfwords come across in
	// the host's order (the first in the ROM is a 17 by 15 box at 3, 7)
	RefVar box(GetFrameSlotRef(RefVar(TranslateROMRef(0x004db6cd)), RSSYMbounds));	// (a frame of the MP2x00 US ROM)
	EXPECT(NOTNIL(box));
	if (NOTNIL(box))
	{
		const Rect* r = (const Rect*) BinaryData(box);
		EXPECT(r->top == 7 && r->left == 3 && r->bottom == 22 && r->right == 20);
	}

	// ROM objects are read-only, never in the heap, and refs to them are
	// kept across a collection
	EXPECT((ObjectFlags(Rbuiltinfunctions) & kObjReadOnly) != 0);
	long code = 0;
	newton_try
	{
		SetFrameSlot(Rbuiltinfunctions, fresh, NILREF);
	}
	newton_catch((ExceptionName) "evt.ex.fr")
	{
		RefStruct* data = (RefStruct*) _info.exception.data;
		code = RINT(GetFrameSlot(*data, RSSYMerrorcode));
	}
	end_try;
	EXPECT(code == kNSErrObjectReadOnly);
	RefVar holder(AllocateFrame());
	SetFrameSlot(holder, RSSYMvalue, Rbuiltinfunctions);
	SetFrameSlot(holder, RSSYMsymbol, RSSYM_proto);
	for (int i = 0; i < 300; i++)
		AllocateBinary(RSSYMstring, 500);
	GC();
	EXPECT(GetFrameSlot(holder, RSSYMvalue) == Rbuiltinfunctions && GetFrameSlot(holder, RSSYMsymbol) == RSSYM_proto);
	EXPECT(Intern((char*) "_proto") == RSSYM_proto);
	// a clone of a ROM frame is a heap frame sharing the ROM map; TotalClone
	// makes everything the heap's
	RefVar copy(Clone(length));
	EXPECT(gHeap->InHeap(copy) && ObjClass(OBJ(copy)) == ObjClass(OBJ(length)));
	SetFrameSlot(copy, fresh, MAKEINT(1));
	EXPECT(FrameHasSlot(copy, fresh) && !FrameHasSlot(length, fresh) && ObjClass(OBJ(copy)) != ObjClass(OBJ(length)));
	RefVar total(TotalClone(length));
	EXPECT(gHeap->InHeap(total) && gHeap->InHeap(ObjClass(OBJ(total))) && FrameHasSlot(total, Intern((char*) "funcPtr")));

	if (failures == 0)
		printf("test_ROMImport: all passed\n");
	else
		printf("test_ROMImport: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
