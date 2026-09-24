// The handwriting engine's word recogniser (recognition/WordRecog.h):
// the block a piece of writing is read in, and its life.  The grammar
// and the engine's common info are handed in by this test, because the
// ROM's own (`ROMGrammar`, `RosCI`) are NOT YET.
#include "WordRecog.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a whole number of pixels as a Fixed
static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// ---- what the engine would bring, and does not yet ----

static RosGrammarContext	gGeneral	= { "General" };
static RosGrammarContext	gNumbers	= { "Numbers" };
static RosGrammarContext*	gContexts[2] = { &gNumbers, &gGeneral };
static RosGrammars			gGrammars	= { 2, gContexts };

static Fixed		gWidths[256];
static RosCharInfo	gCharInfo;
static RosCommonInfo	gCommonInfo;


// ---- what the readings are handed to ----

static long		gCallCount;
static char**	gGotWords;
static ULong	gGotStrokes;
static ULong	gGotCount;

static void
TestCheckWords(char** words, UniChar* /*scores*/, ULong /*unused*/, ULong strokes, ULong count)
{
	gCallCount++;
	gGotWords = words;
	gGotStrokes = strokes;
	gGotCount = count;
}


int
main()
{
	InitHostStandaloneHeap();

	// every character a pixel wide but 'W', which is four
	for (long i = 0; i < 256; i++)
		gWidths[i] = F(1);
	gWidths[(UByte) 'W'] = F(4);
	gCharInfo.fField00 = 0;
	gCharInfo.fWidths = gWidths;
	memset(&gCommonInfo, 0, sizeof(gCommonInfo));
	gCommonInfo.fCharInfo = &gCharInfo;
	gCommonInfo.fMinStrokeSize = F(2);
	RosCI = &gCommonInfo;

	// ---- the empty block ----
	{
		WordRecog* bare = WordRecogNew();
		EXPECT(bare != nil);
		// exactly the pointers a deallocate gives back, and nothing else
		EXPECT(bare->fWordCount == 0);
		EXPECT(bare->fStrokes == nil && bare->fSegments == nil);
		EXPECT(bare->fWords == nil && bare->fScores == nil && bare->fWordFlags == nil);
		EXPECT(bare->fBuffer48 == nil && bare->fBuffer4c == nil);
		EXPECT(bare->fGrammars == nil);
		EXPECT(bare->fPattern == nil && bare->fPatternizer == nil);
		// ... so it can be given back although nothing else was set
		WordRecogDestroy(bare);
		// and nil is not a mistake
		WordRecogDestroy(nil);
	}

	// ---- a whole one ----
	// as `RosettaAwaken` makes it: ten readings and the strokes are the
	// engine's to free
	WordRecog* wr = WordRecogCreate2(nil, nil, TestCheckWords, 10, &gGrammars, nil, 1);
	EXPECT(wr != nil);
	EXPECT(wr->fWordCount == 10);
	EXPECT(wr->fCheckWords == TestCheckWords);
	EXPECT(wr->fOwnsStrokes == 1);
	EXPECT(wr->fSuspended == 0);
	EXPECT(wr->fStrokes != nil && wr->fSegments != nil);
	EXPECT(wr->fWords != nil && wr->fScores != nil && wr->fWordFlags != nil);
	EXPECT(wr->fBuffer48 != nil && wr->fBuffer4c != nil);
	EXPECT(wr->fStrokeCount == 0 && wr->fSegmentCount == 0);
	EXPECT(wr->fPendingStroke == nil);

	// the grammar, and the context found by name rather than by number:
	// "General" is the second of ours, so nothing but the name could
	// have found it
	EXPECT(wr->fGrammars == &gGrammars);
	EXPECT(wr->fContextIndex == 1 && wr->fContext == &gGeneral);
	EXPECT(WordRecogSetContext(wr, "Numbers") == kWordRecogOk);
	EXPECT(wr->fContextIndex == 0 && wr->fContext == &gNumbers);
	EXPECT(WordRecogSetContext(wr, "Esperanto") == 1);
	EXPECT(wr->fContext == &gNumbers);		// and the old one stands

	// the resolution is not known until the engine is woken
	EXPECT(wr->fResX == -1 && wr->fResY == -1);

	// ---- the starting values ----
	// every one of them is the nominal cap height times a ratio, and
	// the run starts as the saved copy
	EXPECT(wr->fSavedRun[0] == 0x0012d999);
	EXPECT(wr->fField34 == FixedMultiply(0x0012d999, 0x000151c4));
	for (long i = 0; i < 22; i++)
		EXPECT(wr->fRun[i] == wr->fSavedRun[i]);
	// the last five are worked out rather than written down
	EXPECT(wr->fSavedRun[18] == FixedMultiply(0x0012d999, FixedDivide(0x000151c4, 0x000117d5)));
	EXPECT(wr->fField120[0] == FixedMultiply(0x0012d999, FixedDivide(0x000151c4, 0x0000fcb9)));
	// ... and the second of each pair is Create2's
	EXPECT(wr->fField120[1] == F(1) && wr->fField120[3] == F(-1));

	// the run is learnt from and put back
	Fixed capHeight = wr->fRun[20];
	wr->fRun[20] = F(99);
	WordRecogInvalRun(wr);
	EXPECT(wr->fRun[20] == capHeight);		// back to the saved copy
	wr->fRun[20] = F(99);
	WordRecogSaveRun(wr);
	WordRecogInvalRun(wr);
	EXPECT(wr->fRun[20] == F(99));			// ... which is now this
	wr->fSavedRun[20] = capHeight;
	wr->fRun[20] = capHeight;

	// ---- the strokes ----
	{
		FPoint a[2], b[2];
		a[0].x = F(0);	a[0].y = F(10);	a[1].x = F(4);	a[1].y = F(30);
		b[0].x = F(8);	b[0].y = F(4);	b[1].x = F(9);	b[1].y = F(5);
		wr->fStrokes[0] = StrokeCreate(2, a);
		wr->fStrokes[1] = StrokeCreate(2, b);
		wr->fStrokes[1]->fIsDot = 1;
		wr->fStrokeCount = 2;

		// the tallest stroke, measured inclusively
		EXPECT(WordRecogDetermineMaxHeight(wr) == F(21));

		// a dot is high if its middle is above a quarter of the way
		// down from where the line is said to be.  The dot's middle is
		// at y = 4.5.
		short range[2];
		range[0] = 0;
		range[1] = 2;
		EXPECT(WordRecogDotIsHigh(wr, range, F(0), F(8)) == true);	// limit 2
		EXPECT(WordRecogDotIsHigh(wr, range, F(4), F(8)) == false);	// limit 6
		// ... and only strokes in the range are asked
		range[0] = 0;
		range[1] = 1;
		EXPECT(WordRecogDotIsHigh(wr, range, F(0), F(8)) == false);

		// the gap cache forgets a stroke it is about to lose
		gXGapStroke = wr->fStrokes[1];
		gXGapMidX = F(123);
		gPrevXGapStroke = wr->fStrokes[0];
		gPrevXGapMidX = F(45);

		// the recogniser owns them, so they all go
		WordRecogClearStrokes(wr);
		EXPECT(wr->fStrokeCount == 0 && wr->fReturnedStrokes == 0);
		EXPECT(wr->fStrokes[0] == nil && wr->fStrokes[1] == nil);
		EXPECT(gXGapStroke == nil && gXGapMidX == 0);
		EXPECT(gPrevXGapStroke == nil && gPrevXGapMidX == 0);

		// with nothing written at all, the height the run says a word has
		EXPECT(WordRecogDetermineMaxHeight(wr) == wr->fRun[18]);
	}

	// ... and a recogniser that does not own them leaves alone the ones
	// it did not make itself
	{
		FPoint p[2];
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(1);	p[1].y = F(1);
		RosStroke* lent = StrokeCreate(2, p);
		RosStroke* ours = StrokeCreate(2, p);
		ours->fFragment = 1;			// the engine cut this one itself
		wr->fStrokes[0] = lent;
		wr->fStrokes[1] = ours;
		wr->fStrokeCount = 2;
		wr->fOwnsStrokes = 0;
		WordRecogClearStrokes(wr);
		EXPECT(wr->fStrokeCount == 0);
		// the one it made is gone and nilled; the borrowed one is left
		// in place, which is the only way to tell from outside
		EXPECT(wr->fStrokes[0] == lent);
		EXPECT(wr->fStrokes[1] == nil);
		StrokeDestroy(lent);
		wr->fStrokes[0] = nil;
		wr->fOwnsStrokes = 1;
	}

	// ---- the readings handed back ----
	{
		char* words[2];
		UniChar scores[2];
		words[0] = (char*) "hello";
		words[1] = (char*) "hallo";
		scores[0] = 100;
		scores[1] = 200;

		// three strokes, of which the middle one is a piece the engine
		// cut for itself: the layers above never saw it, so it does not
		// count towards what the readings cover
		FPoint p[2];
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(1);	p[1].y = F(1);
		for (short i = 0; i < 3; i++)
			wr->fStrokes[i] = StrokeCreate(2, p);
		wr->fStrokes[1]->fFragment = 1;
		wr->fStrokeCount = 3;

		gCallCount = 0;
		WordRecogReturnWords(wr, words, scores, 0, 3, 2);
		EXPECT(gCallCount == 1);
		EXPECT(gGotWords == words && gGotCount == 2);
		EXPECT(gGotStrokes == 2);				// three offered, one of them the engine's
		EXPECT(wr->fReturnedStrokes == 3);		// ... but all three are done with
		WordRecogClearStrokes(wr);

		// with no readings at all the recogniser answers for itself:
		// "????" with the worst score there is, covering everything
		// written so far
		for (short i = 0; i < 5; i++)
			wr->fStrokes[i] = StrokeCreate(2, p);
		wr->fReturnedStrokes = 0;
		wr->fStrokeCount = 5;
		gCallCount = 0;
		WordRecogReturnWords(wr, words, scores, 0, 0, 0);
		EXPECT(gCallCount == 1);
		EXPECT(strcmp(wr->fWords[0], "????") == 0);
		EXPECT(wr->fScores[0] == 0x7ffe);
		EXPECT(wr->fWordFlags[0] == 0);
		EXPECT(gGotCount == 1 && gGotStrokes == 5);
		WordRecogClearStrokes(wr);
	}

	// ---- the cap height learnt from a word ----
	{
		// "WWW" is twelve nominal units wide, so a word measured 24
		// pixels across implies a cap height of two
		wr->fWords[0] = (char*) "WWW";
		wr->fWordWidth = F(24);
		wr->fRun[20] = F(4);
		WordRecogComputeCapHeight(wr);
		// an eighth of the new answer and seven eighths of the old:
		// 0.875 * 4 + 0.125 * 6 = 4.25  (24 / (12/3) = 6)
		EXPECT(wr->fRun[20] == F(4) + F(2) / 8);

		// a word it could not read teaches nothing
		wr->fWords[0] = (char*) "????";
		Fixed before = wr->fRun[20];
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);

		// ... nor does one implying more than two and a half times what
		// is there
		wr->fWords[0] = (char*) "WWW";
		wr->fWordWidth = F(400);
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);

		// ... nor one under the smallest the engine will credit
		wr->fWordWidth = F(4);			// 4/4 = 1, under the two we set
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);
	}

	// ---- what it makes of one stroke ----
	{
		// a stroke is only said to go one way or the other if its
		// longer side is at least the smallest the engine credits
		FPoint p[6];
		p[0].x = F(5);	p[0].y = F(0);	p[1].x = F(6);	p[1].y = F(20);
		RosStroke* tall = StrokeCreate(2, p);
		EXPECT(WordRecogStrokeType(wr, tall) == kWordRecogStrokeVertical);

		p[0].x = F(0);	p[0].y = F(10);	p[1].x = F(40);	p[1].y = F(10);
		RosStroke* flat = StrokeCreate(2, p);
		EXPECT(WordRecogStrokeType(wr, flat) == kWordRecogStrokeHorizontal);

		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(5);	p[1].y = F(5);
		RosStroke* blob = StrokeCreate(2, p);
		EXPECT(WordRecogStrokeType(wr, blob) == kWordRecogStrokeNeither);

		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(1);	p[1].y = F(1);
		RosStroke* speck = StrokeCreate(2, p);
		EXPECT(WordRecogStrokeType(wr, speck) == kWordRecogStrokeNeither);
		StrokeDestroy(speck);
		StrokeDestroy(blob);

		// too wide for a letter of this hand.  With nothing written yet
		// the limit is 0.45 of what the run says a letter measures.
		wr->fStrokeCount = 0;
		wr->fField68 = 0;
		Fixed letter = wr->fRun[21];
		EXPECT(WordRecogIsStrokeTooWide(wr, flat, MinFragmentWidthMultiple) == true);
		EXPECT(WordRecogIsStrokeTooWide(wr, tall, MinFragmentWidthMultiple) == false);
		// ... and asking for a multiple nothing can reach says no
		EXPECT(WordRecogIsStrokeTooWide(wr, flat, F(100)) == false);

		// the writing having turned out bigger than the run expected
		// scales the limit up with it, so the same stroke is no longer
		// too wide
		wr->fField68 = F(200);
		EXPECT(WordRecogIsStrokeTooWide(wr, flat, MinFragmentWidthMultiple) == false);
		wr->fField68 = 0;

		// a piece the engine cut for itself is never too wide
		flat->fFragment = 1;
		EXPECT(WordRecogIsStrokeTooWide(wr, flat, MinFragmentWidthMultiple) == false);
		flat->fFragment = 0;
		EXPECT(letter == wr->fRun[21]);		// nothing above moved the run

		// ---- does it run through two letters? ----
		// two uprights, at x = 10 and x = 30, under the flat stroke's
		// middle height
		FPoint up[6];
		for (long i = 0; i < 6; i++)
		{
			up[i].x = F(10);
			up[i].y = F(4 * i);
		}
		RosStroke* first = StrokeCreate(6, up);
		for (long i = 0; i < 6; i++)
			up[i].x = F(30);
		RosStroke* second = StrokeCreate(6, up);

		wr->fStrokes[0] = first;
		wr->fField1ac = 1;
		EXPECT(WordRecogStrokeIntersectsTwoVerticalStrokes(wr, flat) == false);
		wr->fStrokes[1] = second;
		wr->fField1ac = 2;
		EXPECT(WordRecogStrokeIntersectsTwoVerticalStrokes(wr, flat) == true);

		// one of them moved out from under it counts for nothing
		StrokeScale(second, F(4), F(1));		// now at x = 120
		EXPECT(WordRecogStrokeIntersectsTwoVerticalStrokes(wr, flat) == false);
		StrokeScale(second, 0x4000, F(1));		// and back

		// so the flat stroke is one to cut in two, and an upright is
		// not - a letter may be as tall as it likes
		EXPECT(WordRecogStrokeNeedsFragmenting(wr, flat) == true);
		EXPECT(WordRecogStrokeNeedsFragmenting(wr, tall) == false);
		// ... and a wide stroke with no shape of its own is cut without
		// anything else being asked
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(40);	p[1].y = F(20);
		RosStroke* sprawl = StrokeCreate(2, p);
		EXPECT(WordRecogStrokeType(wr, sprawl) == kWordRecogStrokeNeither);
		EXPECT(WordRecogStrokeNeedsFragmenting(wr, sprawl) == true);
		StrokeDestroy(sprawl);

		wr->fField1ac = 0;
		wr->fStrokes[0] = nil;
		wr->fStrokes[1] = nil;
		StrokeDestroy(first);
		StrokeDestroy(second);
		StrokeDestroy(flat);
		StrokeDestroy(tall);
	}

	// ---- asleep and awake ----
	{
		WordRecogSuspend(wr);
		EXPECT(wr->fSuspended == 1);
		EXPECT(wr->fStrokes == nil && wr->fSegments == nil && wr->fWords == nil);
		EXPECT(wr->fScores == nil && wr->fWordFlags == nil);
		// the block itself has not moved, so what it was told stands
		EXPECT(wr->fWordCount == 10 && wr->fGrammars == &gGrammars);

		EXPECT(WordRecogResume(wr) == kWordRecogOk);
		EXPECT(wr->fSuspended == 0);
		EXPECT(wr->fStrokes != nil && wr->fWords != nil);
		// ... and again is nothing to do
		EXPECT(WordRecogResume(wr) == kWordRecogNothingToDo);
		EXPECT(WordRecogResume(nil) == kWordRecogNoRecognizer);
	}

	// ---- reset ----
	{
		wr->fRun[20] = F(99);
		WordRecogSaveRun(wr);
		WordRecogReset(wr);
		// the engine's own numbers again, not the ones just learnt
		EXPECT(wr->fSavedRun[20] == FixedMultiply(0x0012d999, FixedDivide(0x000151c4, 0x0000d36e)));
		EXPECT(wr->fRun[20] == wr->fSavedRun[20]);
		// and "General" again, whatever the context had been set to
		EXPECT(wr->fContext == &gGeneral);
	}

	WordRecogDestroy(wr);

	// ---- and what it refuses ----
	{
		// no grammar at all: the engine cannot be made
		Boolean threw = false;
		newton_try
		{
			WordRecogCreate2(nil, nil, TestCheckWords, 10, nil, nil, 1);
		}
		newton_catch_all
		{
			threw = true;
		}
		end_try;
		EXPECT(threw);

		// an empty one is refused just as firmly, and the block it had
		// already made is given back
		RosGrammars empty;
		empty.fCount = 0;
		empty.fContexts = nil;
		threw = false;
		newton_try
		{
			WordRecogCreate2(nil, nil, TestCheckWords, 10, &empty, nil, 1);
		}
		newton_catch_all
		{
			threw = true;
		}
		end_try;
		EXPECT(threw);
	}

	// with nobody to hand the readings to, one reading is made anyway
	{
		WordRecog* quiet = WordRecogCreate2(nil, nil, nil, 10, &gGrammars, nil, 1);
		EXPECT(quiet->fWordCount == 1);
		// ... and handing them back is simply not done
		gCallCount = 0;
		WordRecogReturnWords(quiet, nil, nil, 0, 0, 0);
		EXPECT(gCallCount == 0);
		EXPECT(strcmp(quiet->fWords[0], "????") == 0);
		WordRecogDestroy(quiet);
	}

	RosCI = nil;
	if (failures == 0)
		printf("test_WordRecog: all passed\n");
	else
		printf("test_WordRecog: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
