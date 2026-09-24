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

	RosettaAreaInfo block;
	memset(&block, 0x5a, sizeof(block));
	char* area = (char*) &block;
	recognizer->AreaInfoFillDefaults(&area);
	// every lexicon, every stroke shape: the eight words of flags and
	// the five shape entries are all ones
	for (long i = 0; i < 8; i++)
		EXPECT(block.fLexicons[i] == -1);
	for (long i = 0; i < 5; i++)
		EXPECT(block.fShapes[i][0] == 0xff && block.fShapes[i][1] == 0xff);
	// ... and five more strokes expected
	EXPECT(block.fStrokesExpected == 5);
	// while the rest is clear
	EXPECT(block.fField04 == 0 && block.fFlags == 0);
	EXPECT(block.fLabel == 0 && block.fField61 == 0 && block.fField2c[0] == 0);
	EXPECT(block.fField38 == 0 && block.fField48 == 0);
	// the block is exactly the size the engine reads
	EXPECT(sizeof(RosettaAreaInfo) == (size_t) kRosettaAreaInfoSize);

	// NOT YET: a configuration is read as the defaults, so every area
	// is every word list
	memset(&block, 0x5a, sizeof(block));
	area = (char*) &block;
	recognizer->AreaInfoConfigure(&area, RefVar(NILREF));
	EXPECT(block.fLexicons[0] == -1 && block.fStrokesExpected == 5);

	// the two that do nothing
	recognizer->AreaInfoFreeDependents(&area);
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
