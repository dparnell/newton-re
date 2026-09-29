// Frames part test (src/frames/FramesPart.h): the ROM's objects imported,
// then the frames part of a package built into the ROM extension
// (Cardfile, the Names application: build/MP2x00US/rom.bin at 0x7201f4,
// its part at +296, refs being its ROM addresses) imported and looked
// at: the top-level frame's slots (an 'auto part: installScript,
// removeScript, partData), the part's own symbols, a NewtonScript
// expression over it, its objects read-only, and the part removed with
// the refs to it declawed.

#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "FramesPart.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

const long kCardfilePackage = 0x7201f4;		// rex-packages.md
const long kCardfilePartOffset = 296;
const long kCardfilePartSize = 144308;


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static Boolean
StringIs(RefArg str, const char* text)
{
	if (!IsString(str))
		return false;
	UniChar* chars = GetCString(str);
	for (; *text != 0; text++, chars++)
		if (*chars != (UniChar) (unsigned char) *text)
			return false;
	return *chars == 0;
}


int
main()
{
	InitHostStandaloneHeap();
	FILE* f = fopen(NEWTON_ROM_BIN, "rb");
	if (f == nil)
	{
		printf("test_FramesPart: cannot read %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long romSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char* rom = (unsigned char*) malloc(romSize);
	if (fread(rom, 1, romSize, f) != (size_t) romSize)
	{
		printf("test_FramesPart: cannot read %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	fclose(f);
	EXPECT(ImportROMObjects(rom, romSize) == noErr);
	gObjectHeapSize = 0x100000;
	InitObjects();

	// the part imported
	const unsigned char* part = rom + kCardfilePackage + kCardfilePartOffset;
	EXPECT(GetBigEndianWord(part) == 0x00001041);
	TImportedObjectArea* area = ImportFramesPart(part, kCardfilePartSize, kCardfilePackage + kCardfilePartOffset);
	EXPECT(area != nil);
	if (area == nil)
		return 1;
	EXPECT(area->fCount > 1000);
	RefVar top(FramePartToplevelFrame(area->fArea));
	EXPECT(IsFrame(top) && InFramesPartArea(top));
	EXPECT((ObjectFlags(top) & kObjReadOnly) != 0);
	// NTK's part frame: app, text, icon, theForm, ... and the export/import tables
	EXPECT(FrameHasSlot(top, RSSYM_exporttable) || FrameHasSlot(top, RSSYM_importtable) || Length(top) >= 4);
	// an NTK 'auto part: installScript, removeScript, partData, and the export/import tables
	EXPECT(Length(top) == 5);
	EXPECT(FrameHasSlot(top, RSSYM_exporttable) && FrameHasSlot(top, RSSYM_importtable));
	RefVar installScript(GetFrameSlotRef(top, SYMBOL("installScript")));
	EXPECT(IsFunction(installScript) && InFramesPartArea(installScript));
	RefVar partData(GetFrameSlotRef(top, SYMBOL("partData")));
	EXPECT(IsFrame(partData) && InFramesPartArea(partData));
	// the part's symbols are its own objects, equal to the heap's by name
	RefVar tag(GetTag(RefVar(ObjClass(OBJ((Ref) top))), 2, nil));
	EXPECT(IsSymbol(tag) && InFramesPartArea(tag) && (Ref) tag != SYMBOL("partData") && EQRef(tag, SYMBOL("partData")));
	EXPECT(strcmp(SymbolName(tag), "partData") == 0);
	// the function's literals and code are the part's; a ROM function proto by magic pointer
	EXPECT(EQRef(ClassOf(installScript), RSSYMcodeblock) || ObjClass(OBJ((Ref) installScript)) == kFuncClass || IsFunction(installScript));
	// NewtonScript over the part
	SetFrameSlot(RefVar(gVarFrame), RefVar(SYMBOL("part")), top);
	EXPECT(EQRef(Eval("part.installScript"), installScript));
	EXPECT(RINT(Eval("Length(part)")) == 5);
	EXPECT(IsFrame(RefVar(Eval("part.partData"))));
	EXPECT(Eval("ClassOf(part.partData)") == RSSYMframe);
	EXPECT(Eval("HasSlot(part, 'removeScript)") == TRUEREF);
	// a read-only object cannot be changed
	Boolean threw = false;
	newton_try
	{
		Eval("part.partData := nil");
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	EXPECT(threw);
	// removed: the heap's refs to it are declawed
	RefVar keeper(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(keeper, 0, top);
	RemoveFramesPart(area);
	EXPECT(GetArraySlotRef(keeper, 0) == kDeclawedRef);
	EXPECT(!InFramesPartArea(top));

	// A provisional import (only looked at) goes with its package's bytes;
	// one that is not provisional (installed: its handler's) stays, and so
	// does one whose bytes are elsewhere.  Outside a collection it goes at
	// once; inside one it is declawed by that collection and given back
	// after it.
	{
		TImportedObjectArea* lookedAt = ImportFramesPart(part, kCardfilePartSize, kCardfilePackage + kCardfilePartOffset);
		EXPECT(lookedAt != nil && FindFramesPart(part) == lookedAt);
		SetFramesPartProvisional(lookedAt, true);
		RefVar lookedAtTop(FramePartToplevelFrame(lookedAt->fArea));
		SetArraySlotRef(keeper, 0, lookedAtTop);
		RemoveProvisionalFramesParts(part + 1, part + kCardfilePartSize);		// (not its first byte)
		EXPECT(FindFramesPart(part) == lookedAt);
		SetFramesPartProvisional(lookedAt, false);
		RemoveProvisionalFramesParts(part, part + kCardfilePartSize);
		EXPECT(FindFramesPart(part) == lookedAt && IsFrame(RefVar(GetArraySlotRef(keeper, 0))));
		SetFramesPartProvisional(lookedAt, true);
		RemoveProvisionalFramesParts(part, part + kCardfilePartSize);
		EXPECT(FindFramesPart(part) == nil && GetArraySlotRef(keeper, 0) == kDeclawedRef);
	}

	// Classes are named as the object system compares them - whatever the
	// case, and a dotted subclass of 'string is a string: a part whose real
	// is of class 'Real and whose string is a 'String.foo has both brought
	// into the host's byte order.
	{
		static unsigned char p[0x70];
		const ULong32 base = 0x10000000;
		memset(p, 0, sizeof(p));
		PutBigEndianWord(p + 0x00, 0x00001441);		// the array the part begins with, two slots
		PutBigEndianWord(p + 0x08, NILREF);
		PutBigEndianWord(p + 0x0c, base + 0x2c + 1);
		PutBigEndianWord(p + 0x10, base + 0x5c + 1);
		PutBigEndianWord(p + 0x14, (21 << 8) | kObjReadOnly);	// 'Real
		PutBigEndianWord(p + 0x1c, (ULong32) kSymbolClass);
		PutBigEndianWord(p + 0x20, SymbolHashFunction("Real"));
		memcpy(p + 0x24, "Real", 5);
		PutBigEndianWord(p + 0x2c, (20 << 8) | kObjReadOnly);	// a real of class 'Real: 1.5
		PutBigEndianWord(p + 0x34, base + 0x14 + 1);
		PutBigEndianWord(p + 0x38, 0x3ff80000);
		PutBigEndianWord(p + 0x40, (27 << 8) | kObjReadOnly);	// 'String.foo
		PutBigEndianWord(p + 0x48, (ULong32) kSymbolClass);
		PutBigEndianWord(p + 0x4c, SymbolHashFunction("String.foo"));
		memcpy(p + 0x50, "String.foo", 11);
		PutBigEndianWord(p + 0x5c, (18 << 8) | kObjReadOnly);	// a 'String.foo: "hi"
		PutBigEndianWord(p + 0x64, base + 0x40 + 1);
		PutBigEndianHalf(p + 0x68, 'h');
		PutBigEndianHalf(p + 0x6a, 'i');
		TImportedObjectArea* classes = ImportFramesPart(p, sizeof(p), base);
		EXPECT(classes != nil);
		if (classes != nil)
		{
			RefVar array(MAKEPTR(classes->fArea));
			RefVar real(GetArraySlotRef(array, 0));
			EXPECT(IsReal(real) && CDouble(real) == 1.5);
			RefVar string(GetArraySlotRef(array, 1));
			EXPECT(StringIs(string, "hi"));
			RemoveFramesPart(classes);
		}
	}

	if (failures == 0)
		printf("test_FramesPart: all passed\n");
	return failures != 0;
}
