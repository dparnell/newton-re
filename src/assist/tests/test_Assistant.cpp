// Intelligent Assistant test: the list operations the Assistant is
// written in, and GenFullCommands over the ROM's own task templates - the
// words the "Please ..." slip offers.  The ROM's objects are imported for
// the Assistant's frame (magic pointer 8).
#include "Assistant.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref NIL() { return NILREF; }

// the array's strings, as an ASCII list, for looking at what came back
static Boolean
Has(RefArg list, const char* text)
{
	long count = ISNIL(list) ? 0 : Length(list);
	for (long i = 0; i < count; i++)
	{
		RefVar item(GetArraySlotRef(list, i));
		if (!IsString(item))
			continue;
		char buffer[64];
		ConvertFromUnicode(GetCString(item), buffer, kMacRomanEncoding, sizeof(buffer) - 1);
		if (strcmp(buffer, text) == 0)
			return true;
	}
	return false;
}


static void
TestLists()
{
	// Append makes the array when there is none
	RefVar list(Append(RefVar(NIL()), RefVar(NIL()), RefVar(MAKEINT(1))));
	EXPECT(IsArray(list) && Length(list) == 1);
	EXPECT(Append(RefVar(NIL()), list, RefVar(MAKEINT(2))) == (Ref) list && Length(list) == 2);

	// member_p answers the element, not the position
	EXPECT(RINT(member_p(list, RefVar(MAKEINT(2)))) == 2);
	EXPECT(ISNIL(RefVar(member_p(list, RefVar(MAKEINT(3))))));
	EXPECT(ISNIL(RefVar(member_p(RefVar(NIL()), RefVar(MAKEINT(1))))));
	EXPECT(ISNIL(RefVar(member_p(list, RefVar(NIL())))));

	// UniqueAppendItem does not add what is there already
	UniqueAppendItem(RefVar(NIL()), list, RefVar(MAKEINT(2)));
	EXPECT(Length(list) == 2);
	UniqueAppendItem(RefVar(NIL()), list, RefVar(MAKEINT(3)));
	EXPECT(Length(list) == 3);

	// strings are compared as text, so two equal strings are one item
	RefVar strings(MakeArray(0));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("cat")));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("cat")));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("dog")));
	EXPECT(Length(strings) == 2 && Has(strings, "cat") && Has(strings, "dog"));

	RefVar more(MakeArray(0));
	AddArraySlot(more, RefVar(MakeString("dog")));
	AddArraySlot(more, RefVar(MakeString("emu")));
	UniqueAppendList(RefVar(NIL()), strings, more);
	EXPECT(Length(strings) == 3 && Has(strings, "emu"));

	// MashLists: nil either side is the other side, and the items of the
	// second that are not in the first are appended to it
	RefVar a(MakeArray(0));
	AddArraySlot(a, RefVar(MAKEINT(1)));
	RefVar b(MakeArray(0));
	AddArraySlot(b, RefVar(MAKEINT(1)));
	AddArraySlot(b, RefVar(MAKEINT(2)));
	EXPECT(UniqueAppendListGen(RefVar(NIL()), a, RefVar(NIL())) == (Ref) a);
	EXPECT(UniqueAppendListGen(RefVar(NIL()), RefVar(NIL()), b) == (Ref) b);
	RefVar mashed(UniqueAppendListGen(RefVar(NIL()), a, b));
	EXPECT(mashed == (Ref) a && Length(a) == 2);			// appended in place
	EXPECT(RINT(GetArraySlotRef(a, 1)) == 2);

	// a read-only list is cloned rather than added to: the ROM's own
	// objects are read-only, so one of them will do
	RefVar romList(GetFrameSlotRef(kAssistantFrame, RSSYMtask_list));
	EXPECT(IsArray(romList) && IsReadOnly(romList));
	long was = Length(romList);
	RefVar grown(UniqueAppendListGen(RefVar(NIL()), romList, b));
	EXPECT(grown != (Ref) romList && Length(romList) == was);
}


static void
TestMapSymToFrame()
{
	// a global variable
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "aTestFrame")), RefVar(AllocateFrame()));
	EXPECT(IsFrame(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(Intern((char*) "aTestFrame"))))));
	// else the Assistant's own frame
	EXPECT(IsArray(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(RSSYMtask_list)))));
	EXPECT(ISNIL(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(Intern((char*) "noSuchSlotAnywhere"))))));
}


static void
TestGenFullCommands()
{
	RefVar commands(GenFullCommands(RefVar(NIL())));
	EXPECT(IsArray(commands) && Length(commands) > 0);
	// every one of them is a string, and none of them is there twice
	long count = ISNIL(commands) ? 0 : Length(commands);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(commands, i));
		EXPECT(IsString(word));
	}
	printf("  the Assistant's commands (%ld):", count);
	for (long i = 0; i < count && i < 30; i++)
	{
		RefVar word(GetArraySlotRef(commands, i));
		char buffer[64];
		ConvertFromUnicode(GetCString(word), buffer, kMacRomanEncoding, sizeof(buffer) - 1);
		printf(" %s", buffer);
	}
	printf("\n");
	// the words the ROM's own templates start with
	EXPECT(Has(commands, "print"));
	EXPECT(Has(commands, "find"));

	// asked twice, the same list comes back
	RefVar again(GenFullCommands(RefVar(NIL())));
	EXPECT(Length(again) == count);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Assistant: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	TestLists();
	TestMapSymToFrame();
	TestGenFullCommands();

	printf("test_Assistant: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
