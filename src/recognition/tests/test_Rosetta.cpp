// The handwriting engine's life (recognition/Rosetta.h): waking,
// quietening and sleeping, and the small calls that go with them.
// This is level 2, the join between the Newton's recogniser and
// ParaGraph's engine.
#include "Rosetta.h"
#include "RosEngine.h"
#include "RosStrokes.h"
#include "WordRecog.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// A grammar of our own, because the ROM's `ROMGrammar` is NOT YET.
static RosGrammarContext	gGeneral	= { "General" };
static RosGrammarContext*	gContexts[1] = { &gGeneral };
static RosGrammars			gGrammars	= { 1, gContexts };

static long		gWordsHandedBack;

static void
TestCheckWords(char** /*words*/, UniChar* /*scores*/, ULong /*strokes*/, ULong /*count*/)
{
	gWordsHandedBack++;
}


int
main()
{
	InitHostStandaloneHeap();

	// ---- the engine cannot start, and says so where it fails ----
	{
		Boolean threw = false;
		newton_try
		{
			RosettaInitialize(F(72), F(72), TestCheckWords);
		}
		newton_catch_all
		{
			threw = true;
		}
		end_try;
		// `BiGrammarsLoad` has no `ROMGrammar` to answer with, so the
		// word recogniser cannot be made
		EXPECT(threw);
		EXPECT(gWordRecog == nil);
		EXPECT(!RosettaEngineIsReconstructed());
		// ... but what it was told on the way in stands
		EXPECT(gRosResX == F(72) && gRosResY == F(72));
		EXPECT(gRosCallBack == TestCheckWords);
		// and sleeping with nothing awake is nothing
		EXPECT(RosettaSleep() == noErr);
	}

	// ---- the engine's own character set ----
	{
		EXPECT(CharInitialize(0) == 0x86);
		EXPECT(RosettaVerifyWordSymbols((char*) "hello") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "Hello,") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "$1,000.") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "") == true);
		// a control character is not one the engine may answer
		char bad[3];
		bad[0] = 'a';	bad[1] = 0x01;	bad[2] = 0;
		EXPECT(RosettaVerifyWordSymbols(bad) == false);
		// ... and neither is a space: the engine reads one word at a
		// time and a space is never a character of one
		EXPECT(RosettaVerifyWordSymbols((char*) "two words") == false);
	}

	// ---- everything above the grammar, over a recogniser of our own ----
	{
		gWordRecog = WordRecogCreate2(nil, nil, nil, 10, &gGrammars, nil, 1);
		EXPECT(gWordRecog != nil);
		gWordRecog->fResX = -1;
		gWordRecog->fResY = -1;

		// what the engine is to stop doing.  The three bits taken out
		// of the flags are what a classify would have set.
		gWordRecog->fFlags1f4 = 0xffffffff;
		RosettaDontClassify(kRosettaBaselineOnly);
		EXPECT(gWordRecog->fClassifyMode == kRosettaBaselineOnly);
		EXPECT(gWordRecog->fFlags1f4 == 0xffdfd7fe);

		// the baseline, as the two Points the recogniser wants: the
		// box's top-left corner and its bottom-right one
		SetFixedRect(&gWordRecog->fBaseline, F(3), F(5), F(40), F(26));
		Point ends[2];
		EXPECT(RosettaGetBaseLine(ends) == noErr);
		EXPECT(ends[0].h == 3 && ends[0].v == 5);
		EXPECT(ends[1].h == 40 && ends[1].v == 26);

		// the working values put back: reading again, and the boxed
		// character recogniser given back
		gWordRecog->fCharBox = (void*) NewPtr(4);
		gWordRecog->fField202 = 1;
		gWordRecog->fField203 = 1;
		gWordRecog->fField1e0 = 0;
		EXPECT(RosettaInitializeValues() == noErr);
		EXPECT(gWordRecog->fClassifyMode == kRosettaClassifyNormally);
		EXPECT(gWordRecog->fCharBox == nil);
		EXPECT(gWordRecog->fField202 == 0 && gWordRecog->fField203 == 0);
		EXPECT(gWordRecog->fField1e0 == -1);

		// ... and a word read well enough puts the run back as it was
		// saved, where one read badly leaves it as it has drifted
		gRosLastConfidence = 1000;
		gWordRecog->fSavedRun[0] = F(11);
		gWordRecog->fRun[0] = F(99);
		RosettaInitializeValues();
		EXPECT(gWordRecog->fRun[0] == F(11));
		gRosLastConfidence = 500;
		gWordRecog->fRun[0] = F(99);
		RosettaInitializeValues();
		EXPECT(gWordRecog->fRun[0] == F(99));

		// quietened: the arrays given back, the block left standing
		EXPECT(RosettaQuiesce() == noErr);
		EXPECT(gWordRecog != nil && gWordRecog->fSuspended == 1);
		EXPECT(gWordRecog->fStrokes == nil && gWordRecog->fWords == nil);
		// ... and made again by the word recogniser's own resume
		EXPECT(WordRecogResume(gWordRecog) == kWordRecogOk);
		EXPECT(gWordRecog->fStrokes != nil);

		// and taken down
		EXPECT(RosettaSleep() == noErr);
		EXPECT(gWordRecog == nil);
		// sleeping gives the common info back as well
		EXPECT(RosCI == nil);
	}

	EXPECT(gWordsHandedBack == 0);		// nothing was ever read
	if (failures == 0)
		printf("test_Rosetta: all passed\n");
	else
		printf("test_Rosetta: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
