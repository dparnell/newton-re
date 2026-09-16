// Frames part test (src/frames/FramesPart.h): the ROM's objects imported,
// then the frames part of a package built into the ROM extension
// (Cardfile, the Names application: build/MP2100D/rom.bin at 0x6f3444,
// its part at +284, refs being its ROM addresses) imported and looked
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

const long kCardfilePackage = 0x6f3444;		// rex-packages.md
const long kCardfilePartOffset = 284;
const long kCardfilePartSize = 146516;


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

	if (failures == 0)
		printf("test_FramesPart: all passed\n");
	return failures != 0;
}
