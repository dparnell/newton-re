// The handwriting engine's life (recognition/Rosetta.h): waking,
// quietening and sleeping, and the small calls that go with them.
// This is level 2, the join between the Newton's recogniser and
// ParaGraph's engine, over the bigram grammar and the common info the
// ROM brings with it.
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

	// ---- the grammar the ROM brings ----
	{
		EXPECT(ROMGrammar.fCount == 8);
		static const char* const kNames[8] = {
			"General", "Date", "Numbers&Money", "Numbers",
			"Phone", "Time", "Money", "PostalCode"
		};
		for (long i = 0; i < 8; i++)
			EXPECT(strcmp(ROMGrammar.fContexts[i]->fName, kNames[i]) == 0);

		// a grammar is a list of *kinds of word*, each a lexicon out of
		// gROMDictionaryData with a score of its own
		const BiGrammar* phone = ROMGrammar.fContexts[4];
		EXPECT(phone->fCount == 5 && phone->fCapacity == 5);
		EXPECT(strcmp(phone->fSlices[0]->fName, "PhoneR_US_C") == 0);
		EXPECT(phone->fSlices[0]->fDictionary == 15);	// gLex8phone
		EXPECT(strcmp(phone->fSlices[1]->fName, "hyphen") == 0);
		EXPECT(phone->fSlices[1]->fDictionary == 113);	// gLex8hyphen
		// ... and a score for each kind that may follow it: a telephone
		// number may be followed by a hyphen and by nothing else
		EXPECT(phone->fSlices[0]->fCount == 1);
		EXPECT(phone->fSlices[0]->fNext[0] == phone->fSlices[1]);
		EXPECT(phone->fSlices[0]->fWeights[0] == 458);
		// and after the hyphen, three things, of which going back to
		// the number costs nothing at all
		EXPECT(phone->fSlices[1]->fCount == 3);
		EXPECT(phone->fSlices[1]->fNext[0] == phone->fSlices[0]);
		EXPECT(phone->fSlices[1]->fWeights[0] == 0);

		// a kind of word that can never start one has the worst score
		// there is
		const BiGrammar* general = ROMGrammar.fContexts[0];
		EXPECT(general->fCount == 25);
		EXPECT(strcmp(general->fSlices[1]->fName, "endpunct") == 0);
		EXPECT(general->fSlices[1]->fScore == 0x7ffe);
		// the six an area fills in with dictionaries of its own
		EXPECT(strcmp(general->fSlices[7]->fName, "~user") == 0);
		EXPECT(strcmp(general->fSlices[12]->fName, "~null5") == 0);
	}

	// ---- the engine wakes ----
	{
		EXPECT(RosettaInitialize(F(72), F(72), TestCheckWords) == noErr);
		EXPECT(gWordRecog != nil);
		EXPECT(gRosResX == F(72) && gRosResY == F(72));
		EXPECT(gRosCallBack == TestCheckWords);
		// the common info out of the ROM
		EXPECT(RosCI != nil && RosCI->fMinStrokeSize == F(4) + F(1) / 2);
		// ten readings, the ROM's grammar, and its first context found
		// by the name "General"
		EXPECT(gWordRecog->fWordCount == 10);
		EXPECT(gWordRecog->fGrammars == &ROMGrammar);
		EXPECT(gWordRecog->fContextIndex == 0);
		EXPECT(strcmp(gWordRecog->fContext->fName, "General") == 0);
		// the tablet's resolution, in whole dots to the inch
		EXPECT(gWordRecog->fResX == 72 && gWordRecog->fResY == 72);
		EXPECT(gWordRecog->fClassifyMode == kRosettaClassifyNormally);
		EXPECT(gWordRecog->fOwnsStrokes == 1);
		// waking again is nothing
		EXPECT(RosettaAwaken() == noErr);

		// a field asks for a grammar by name
		EXPECT(WordRecogSetContext(gWordRecog, "Phone") == kWordRecogOk);
		EXPECT(gWordRecog->fContextIndex == 4);
		EXPECT(WordRecogSetContext(gWordRecog, "Esperanto") == 1);
		EXPECT(gWordRecog->fContextIndex == 4);
	}

	// ---- the engine's own character set ----
	{
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

	// ---- what it is told to stop doing ----
	{
		gWordRecog->fFlags1f4 = 0xffffffff;
		RosettaDontClassify(kRosettaBaselineOnly);
		EXPECT(gWordRecog->fClassifyMode == kRosettaBaselineOnly);
		EXPECT(gWordRecog->fFlags1f4 == 0xffdfd7fe);
	}

	// ---- the baseline, as the two Points the recogniser wants ----
	{
		SetFixedRect(&gWordRecog->fBaseline, F(3), F(5), F(40), F(26));
		Point ends[2];
		EXPECT(RosettaGetBaseLine(ends) == noErr);
		EXPECT(ends[0].h == 3 && ends[0].v == 5);
		EXPECT(ends[1].h == 40 && ends[1].v == 26);
	}

	// ---- the working values put back ----
	{
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
	}

	// ---- quiet, and asleep ----
	{
		// quietened: the arrays given back, the block left standing
		EXPECT(RosettaQuiesce() == noErr);
		EXPECT(gWordRecog != nil && gWordRecog->fSuspended == 1);
		EXPECT(gWordRecog->fStrokes == nil && gWordRecog->fWords == nil);
		// ... and made again by the word recogniser's own resume
		EXPECT(WordRecogResume(gWordRecog) == kWordRecogOk);
		EXPECT(gWordRecog->fStrokes != nil);

		// and taken down, the common info with it
		EXPECT(RosettaSleep() == noErr);
		EXPECT(gWordRecog == nil);
		EXPECT(RosCI == nil);
		// sleeping again is nothing
		EXPECT(RosettaSleep() == noErr);
	}

	EXPECT(gWordsHandedBack == 0);		// nothing was ever read
	if (failures == 0)
		printf("test_Rosetta: all passed\n");
	else
		printf("test_Rosetta: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
