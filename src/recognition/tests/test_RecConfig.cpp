// RecConfig test: the configuration frame a view's writing is recognised
// under.  The ROM's objects are imported for protoRecConfig, rcPrefsConfig
// and rcNoRecog, which are what the configurations are built out of; the
// user's handwriting preferences are made here rather than booted, so that
// the settings a configuration answers are known.
//
// What is checked is the shape of the chain PrepRecConfig builds (nothing
// is copied: the configuration becomes a _proto and the preferences a
// _parent, so a preference changed afterwards is seen through it), the
// input mask BuildInputMask makes out of the settings, and that
// BuildRecConfig picks the right starting point for a view that allows
// everything and for one that allows only some things.
//
// No view is made: every function here takes nil for one, which is the
// path a configuration built for the machine rather than for a field
// takes.

#include "RecConfig.h"
#include "ViewFlags.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// the handwriting preferences as the boot would leave them, with only the
// settings these tests read
static void
MakeUserConfiguration(Boolean text, Boolean words, Boolean letters,
					  Boolean numbers, Boolean punctuation, Boolean shapes)
{
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RSSYMdotextrecognition, RefVar(MAKEBOOLEAN(text)));
	SetFrameSlot(config, RSSYMwordscursiveoption, RefVar(MAKEBOOLEAN(words)));
	SetFrameSlot(config, RSSYMletterscursiveoption, RefVar(MAKEBOOLEAN(letters)));
	SetFrameSlot(config, RSSYMnumberscursiveoption, RefVar(MAKEBOOLEAN(numbers)));
	SetFrameSlot(config, RSSYMpunctuationcursiveoption, RefVar(MAKEBOOLEAN(punctuation)));
	SetFrameSlot(config, RSSYMdoshaperecognition, RefVar(MAKEBOOLEAN(shapes)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_RecConfig: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));

	MakeUserConfiguration(true, true, true, true, true, true);

	// ---- BuildInputMask ----
	// a configuration that allows nothing is not asked its settings at all
	// unless it is forced
	RefVar plain(AllocateFrame());
	SetFrameSlot(plain, RSSYMdotextrecognition, RefVar(TRUEREF));
	SetFrameSlot(plain, RSSYMwordscursiveoption, RefVar(TRUEREF));
	EXPECT(BuildInputMask(plain, 0xa00, false) == 0xa00);
	EXPECT(BuildInputMask(plain, 0xa00, true) == (0xa00 | 0x1000));

	// letters bring two bits, numbers three more, and punctuation only
	// counts when one of the others is on
	RefVar all(AllocateFrame());
	SetFrameSlot(all, RSSYMdotextrecognition, RefVar(TRUEREF));
	SetFrameSlot(all, RSSYMletterscursiveoption, RefVar(TRUEREF));
	SetFrameSlot(all, RSSYMnumberscursiveoption, RefVar(TRUEREF));
	SetFrameSlot(all, RSSYMpunctuationcursiveoption, RefVar(TRUEREF));
	EXPECT(BuildInputMask(all, 0, true) == (0x6000 | 0x100000 | 0xc2000 | 0x8000));

	RefVar punctuationOnly(AllocateFrame());
	SetFrameSlot(punctuationOnly, RSSYMdotextrecognition, RefVar(TRUEREF));
	SetFrameSlot(punctuationOnly, RSSYMpunctuationcursiveoption, RefVar(TRUEREF));
	EXPECT(BuildInputMask(punctuationOnly, 0, true) == 0);		// nothing to punctuate

	// text turned off keeps every cursive option out of the mask, but the
	// shapes are asked for separately
	RefVar shapesOnly(AllocateFrame());
	SetFrameSlot(shapesOnly, RSSYMwordscursiveoption, RefVar(TRUEREF));
	SetFrameSlot(shapesOnly, RSSYMdoshaperecognition, RefVar(TRUEREF));
	EXPECT(BuildInputMask(shapesOnly, 0, true) == 0x10000);

	// ---- PrepRecConfig ----
	RefVar mine(AllocateFrame());
	SetFrameSlot(mine, RSSYMbaseinputmask, RefVar(MAKEINT(0x55)));
	RefVar prepped(PrepRecConfig(nil, mine));
	EXPECT(EQRef(GetFrameSlotRef(prepped, RSSYM_proto), mine));
	EXPECT(EQRef(GetFrameSlotRef(prepped, RSSYM_parent),
				 GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration)));
	// nothing was copied: what the configuration says is read through it
	EXPECT(RINT(GetProtoVariable(prepped, RSSYMbaseinputmask, nil)) == 0x55);
	// a preference changed afterwards is seen through the chain
	SetFrameSlot(RefVar(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration)),
				 RSSYMdotextrecognition, RefVar(NILREF));
	EXPECT(ISNIL(GetProtoVariable(prepped, RSSYMdotextrecognition, nil)));
	SetFrameSlot(RefVar(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration)),
				 RSSYMdotextrecognition, RefVar(TRUEREF));

	// one that has been prepared already is given back as it is
	EXPECT(EQRef(PrepRecConfig(nil, prepped), prepped));

	// ---- BuildRCProto ----
	// the mask is built from the base when there is no mask of its own
	RefVar built(BuildRCProto(nil, mine));
	EXPECT(RINT(GetFrameSlotRef(built, RSSYMinputmask))
		   == (long) BuildInputMask(built, 0x55, false));
	// a mask it already has is kept
	RefVar hasMask(AllocateFrame());
	SetFrameSlot(hasMask, RSSYMinputmask, RefVar(MAKEINT(0x1234)));
	EXPECT(RINT(GetFrameSlotRef(RefVar(BuildRCProto(nil, hasMask)), RSSYMinputmask)) == 0x1234);
	// unless it asks for one to be built
	SetFrameSlot(hasMask, RSSYMbuildinputmask, RefVar(TRUEREF));
	SetFrameSlot(hasMask, RSSYMbaseinputmask, RefVar(MAKEINT(0)));
	EXPECT(RINT(GetFrameSlotRef(RefVar(BuildRCProto(nil, hasMask)), RSSYMinputmask)) != 0x1234);

	// ---- BuildRecConfig ----
	// a view that allows everything is read against the user's own
	// preferences, and its mask is built from them rather than from its
	// flags
	RefVar anything(BuildRecConfig(nil, vAnythingAllowed));
	EXPECT(EQRef(GetFrameSlotRef(anything, RSSYM_proto), RefVar(Rrcprefsconfig)));
	EXPECT(RINT(GetFrameSlotRef(anything, RSSYMinputmask))
		   == (long) BuildInputMask(anything, vClickable | vGesturesAllowed, true));

	// a view that allows only some things starts from rcNoRecog and keeps
	// the mask its own flags make
	ULong dateOnly = vClickable | vGesturesAllowed | vDateField;
	RefVar only(BuildRecConfig(nil, dateOnly));
	EXPECT(EQRef(GetFrameSlotRef(only, RSSYM_proto), RefVar(Rrcnorecog)));
	EXPECT(RINT(GetFrameSlotRef(only, RSSYMinputmask)) == (long) dateOnly);

	// testConfig, which the handwriting test screens set, is what a view
	// that allows everything is read against instead when there is one
	RefVar testConfig(AllocateFrame());
	SetFrameSlot(testConfig, RSSYMinputmask, RefVar(MAKEINT(0x4321)));
	SetFrameSlot(RefVar(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration)),
				 RSSYMtestconfig, testConfig);
	RefVar tested(BuildRecConfig(nil, vAnythingAllowed));
	EXPECT(RINT(GetFrameSlotRef(tested, RSSYMinputmask)) == 0x4321);

	printf("test_RecConfig: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
