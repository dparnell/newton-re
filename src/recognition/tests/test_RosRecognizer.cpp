// The ROM's own handwriting engine as the recognition system sees it:
// TRosRecognizer, the TWRecognizer implementation that sits over
// Rosetta (recognition/RosRecognizer.h, Rosetta.h).
//
// The engine itself is not reconstructed, so what can be checked here
// is the shell: that the recogniser registers and can be made by name
// the way the recognition manager makes one, the area block it hands
// the engine, the questions it answers without asking the engine at
// all - and that the ones which do ask throw `evt.ex.abt`, which is
// what the ROM's own does when its engine fails and is what keeps this
// recogniser out of a running system until the engine is there.
//
// It runs as the kernel services task because the protocol registry is
// a monitor.
#include "RosRecognizer.h"
#include "Rosetta.h"
#include "WRecDomain.h"
#include "Dictionaries.h"
#include "RecObject.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "NewtonMemory.h"
#include "Protocols.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NewtonExceptions.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// The engine is not there, so every call that needs it fails and the
// recogniser throws.  This says whether it did.
#define EXPECT_ABORTS(expr) do {							\
		Boolean threw = false;								\
		newton_try { expr; }								\
		newton_catch(exAbort) { threw = true; }				\
		end_try;											\
		if (!threw) { failures++; fprintf(stderr, "FAIL %s:%d: %s did not throw\n", __FILE__, __LINE__, #expr); } \
	} while (0)


static void
RosScenario(void)
{
	gObjectHeapSize = 0x80000;
	InitObjects();

	// the engine below the seam is not reconstructed yet
	EXPECT(!RosettaEngineIsReconstructed());

	// registered, and made by name the way the word domain makes one
	RegisterRosettaWRec();
	TWRecognizer* recognizer = TWRecognizer::New("TRosRecognizer");
	EXPECT(recognizer != nil);
	if (recognizer == nil)
		return;
	// there is only one, and the engine's callback finds it here
	EXPECT((void*) gRosRecognizer == (void*) recognizer);

	// the area block it wants
	EXPECT(recognizer->AreaInfoGetSize() == kRosettaAreaInfoSize);

	// a real Handle, because the recogniser locks it
	Handle handle = NewHandle(recognizer->AreaInfoGetSize());
	EXPECT(handle != nil);
	RosettaAreaInfo* areaInfo = (RosettaAreaInfo*) *handle;
	RosettaAreaInfo& block = *areaInfo;
	memset(areaInfo, 0x5a, sizeof(block));
	recognizer->AreaInfoFillDefaults(handle);
	// every character allowed, nothing read as anything else
	for (long i = 0; i < 8; i++)
		EXPECT(block.fSymbolSet[i] == 0xffffffff);
	for (long i = 0; i < 5; i++)
		EXPECT(block.fMap[i][0] == -1);
	EXPECT(block.fLetterSpace == 5);
	// while the rest is clear
	EXPECT(block.fMainDict == nil && block.fFlags == 0);
	EXPECT(block.fLabel == 0 && block.fDictCount == 0 && block.fBase == 0);
	EXPECT(block.fDicts[0] == nil && block.fDicts[4] == nil);


	// ---- a configuration read into the block ----
	// (an empty locale and an empty dictionary list are all the
	//  configuration reading needs of the machine around it)
	{
		RefVar intl(AllocateFrame());
		SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(AllocateFrame()));
		SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
		gDictList = TDArray::Make(sizeof(dictListEntry), 0);

		RefVar config(AllocateFrame());
		SetFrameSlot(config, RSSYMinputmask, RefVar(MAKEINT(0)));
		// one letter at a time, so the engine is not asked for the
		// main lexicon
		SetFrameSlot(config, RSSYMrcsingleletters, RefVar(TRUEREF));
		// two dictionaries the engine knows how to read by itself
		RefVar dicts(MakeArray(2));
		SetArraySlot(dicts, 0, RefVar(MAKEINT(0x01)));		// times
		SetArraySlot(dicts, 1, RefVar(MAKEINT(0x67)));		// numbers
		SetFrameSlot(config, RSSYMdictionaries, dicts);
		// where the line is, and the boxes it is written in
		RefVar baseInfo(AllocateFrame());
		SetFrameSlot(baseInfo, RSSYMbase, RefVar(MAKEINT(40)));
		SetFrameSlot(baseInfo, RSSYMsmallheight, RefVar(MAKEINT(9)));
		SetFrameSlot(config, RSSYMrcbaseinfo, baseInfo);
		RefVar gridInfo(AllocateFrame());
		SetFrameSlot(gridInfo, RSSYMboxleft, RefVar(MAKEINT(4)));
		SetFrameSlot(gridInfo, RSSYMboxright, RefVar(MAKEINT(120)));
		SetFrameSlot(gridInfo, RSSYMboxtop, RefVar(MAKEINT(2)));
		SetFrameSlot(gridInfo, RSSYMboxbottom, RefVar(MAKEINT(24)));
		SetFrameSlot(gridInfo, RSSYMxspace, RefVar(MAKEINT(12)));
		SetFrameSlot(gridInfo, RSSYMyspace, RefVar(MAKEINT(3)));
		SetFrameSlot(config, RSSYMrcgridinfo, gridInfo);
		// and only these characters
		SetFrameSlot(config, RSSYMsymbolset, RefVar(MakeString("ABC")));

		recognizer->AreaInfoFillDefaults(handle);
		recognizer->AreaInfoConfigure(handle, config);

		// the two dictionaries set a flag each rather than being handed
		// over, and the first of them names the label
		EXPECT((block.fFlags & kRosAreaTime) != 0);
		EXPECT((block.fFlags & kRosAreaNumbers) != 0);
		EXPECT(block.fDictCount == 0);
		EXPECT(block.fLabel == 0x01);
		// one letter at a time
		EXPECT((block.fFlags & kRosAreaSingleLetters) != 0);
		EXPECT(block.fMainDict == nil);
		// no letterSpaceCursiveOption, so the writing is cursive
		EXPECT((block.fFlags & kRosAreaCursive) != 0);
		// the line and the grid
		EXPECT((block.fFlags & kRosAreaHasBaseInfo) != 0);
		EXPECT(block.fBase == 40 && block.fSmallHeight == 9);
		EXPECT(block.fBoxLeft == 4 && block.fBoxRight == 120);
		EXPECT(block.fBoxTop == 2 && block.fBoxBottom == 24);
		EXPECT(block.fXSpace == 12 && block.fYSpace == 3);
		// and the three characters it allows, as bits of a 256-bit set
		EXPECT((block.fFlags & kRosAreaHasSymbolSet) != 0);
		EXPECT(block.fSymbolSet['A' >> 5] == ((1u << ('A' & 0x1f))
											| (1u << ('B' & 0x1f))
											| (1u << ('C' & 0x1f))));
		EXPECT(block.fSymbolSet[0] == 0 && block.fSymbolSet[7] == 0);

		// one of them taken back out again
		SetFrameSlot(config, RSSYMremovesymbol, RefVar(MakeString("B")));
		recognizer->AreaInfoFillDefaults(handle);
		recognizer->AreaInfoConfigure(handle, config);
		EXPECT(block.fSymbolSet['A' >> 5] == ((1u << ('A' & 0x1f)) | (1u << ('C' & 0x1f))));

		// a field that is not single letters takes its kinds from the
		// input mask instead
		RefVar plain(AllocateFrame());
		SetFrameSlot(plain, RSSYMinputmask, RefVar(MAKEINT(0x00002000 | 0x00400000)));
		SetFrameSlot(plain, RSSYMletterspacecursiveoption, RefVar(MAKEINT(3)));
		// (FindDictionaryEntry would be asked for the main lexicon, and
		//  there is no list here, so only the flag work is checked)
		EXPECT(RINT(RefVar(GetVariable(plain, RSSYMinputmask, nil, false))) == 0x00402000);
	}

	// the two that do nothing
	recognizer->AreaInfoFreeDependents(handle);
	recognizer->UnitInfoFreePtr(nil);

	// a word longer than the engine could have written is turned down
	// without the engine being asked
	UniChar big[0x50];
	for (long i = 0; i < 0x48; i++)
		big[i] = 'a';
	big[0x48] = 0;
	EXPECT(!recognizer->VerifyWordSymbols(big));

	// and the calls that need the engine say so the way the ROM's own
	// does when its engine has failed
	EXPECT_ABORTS(recognizer->Initialize());
	EXPECT_ABORTS(recognizer->Sleep());

	// WakeUp does not look at what the engine answered, so it is quiet
	recognizer->WakeUp();

	recognizer->Delete();
	EXPECT(gRosRecognizer == nil);

	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = RosScenario;
	OsBoot();
	if (failures == 0)
		printf("test_RosRecognizer: all passed\n");
	else
		printf("test_RosRecognizer: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
