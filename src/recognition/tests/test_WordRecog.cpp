// The handwriting engine's word recogniser (recognition/WordRecog.h):
// the block a piece of writing is read in, and its life.  The grammar
// and the engine's common info are handed in by this test, because the
// ROM's own (`ROMGrammar`, `RosCI`) are NOT YET.
#include "WordRecog.h"
#include "FixedGeometry.h"
#include "RosEngine.h"
#include "Segment.h"
#include "ROMDictionaryData.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// a whole number of pixels as a Fixed
static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// ---- what the engine would bring, and does not yet ----

static const BiGrammar	gGeneral	= { "General" };
static const BiGrammar	gNumbers	= { "Numbers" };
static const BiGrammar* const	gContexts[2] = { &gNumbers, &gGeneral };
static const BiGrammars		gGrammars	= { 2, gContexts };



// ---- what the readings are handed to ----

static long		gCallCount;
static char**	gGotWords;
static ULong	gGotStrokes;
static ULong	gGotCount;

static void
TestCheckWords(char** words, UniChar* /*scores*/, long* /*flags*/, ULong strokes, ULong count)
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

	// the engine's own trained numbers, out of the ROM
	EXPECT(CharInitialize(0) == 0x86);		// the classifier's output nodes
	EXPECT(RosCI != nil && RosCI != &rosCI);
	EXPECT(RosCI->fLegalUse == rosCharLegalUse);
	EXPECT(RosCI->fMinStrokeSize == F(4) + F(1) / 2);
	// a capital is nearly a whole cap height, a lower-case o about
	// half of one, and a full stop almost nothing
	EXPECT(RosCI->fCharParams[1][(UByte) 'A'] > RosCI->fCharParams[1][(UByte) 'o']);
	EXPECT(RosCI->fCharParams[1][(UByte) 'o'] > RosCI->fCharParams[1][(UByte) '.']);

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
	// (with a real classifier, because closing a word now reads it)
	BPNet* gNet = BPNetCreateNumOut(134);
	BPNetLoad(gNet, nil);
	WordRecog* wr = WordRecogCreate2(nil, nil, TestCheckWords, 10, &gGrammars, gNet, 1);
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
	EXPECT(wr->fWordSize == FixedMultiply(0x0012d999, FixedDivide(0x000151c4, 0x0000fcb9)));
	// ... and the second of each pair is Create2's
	EXPECT(wr->fWordLeft[0] == F(1) && wr->fWordRight[0] == F(-1));

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
		// "AAA" is three capitals, each 0.96 of a cap height, so
		// characters measuring twenty pixels imply a cap height of
		// about twenty-one
		wr->fWords[0] = (char*) "AAA";
		wr->fMeanCharHeight = F(20);
		Fixed was = wr->fRun[20];
		Fixed estimate = FixedDivide(F(20), RosCI->fCharParams[1][(UByte) 'A']);
		EXPECT(estimate > F(20) && estimate < F(22));
		WordRecogComputeCapHeight(wr);
		// an eighth of the new answer, seven eighths of the old
		EXPECT(wr->fRun[20] == FixedMultiply(0x0000e000, was)
							+ FixedMultiply(0x00002000, estimate));

		// a word it could not read teaches nothing
		wr->fWords[0] = (char*) "????";
		Fixed before = wr->fRun[20];
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);

		// ... nor does one implying more than two and a half times what
		// is there
		wr->fWords[0] = (char*) "AAA";
		wr->fMeanCharHeight = F(400);
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);

		// ... nor one under the smallest the engine will credit
		wr->fMeanCharHeight = F(4);
		WordRecogComputeCapHeight(wr);
		EXPECT(wr->fRun[20] == before);
		wr->fRun[20] = was;
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

	// ---- strokes in, and the run of measurements ----
	{
		// three strokes of a word, each ten across and twenty down
		FPoint p[2];
		RosStroke* s[3];
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(10);	p[1].y = F(20);
		s[0] = StrokeCreate(2, p);
		p[0].x = F(15);	p[0].y = F(5);	p[1].x = F(25);	p[1].y = F(25);
		s[1] = StrokeCreate(2, p);
		p[0].x = F(30);	p[0].y = F(0);	p[1].x = F(40);	p[1].y = F(20);
		s[2] = StrokeCreate(2, p);

		WordRecogReset(wr);
		wr->fField68 = 0;
		EXPECT(wr->fStrokeCount == 0 && wr->fField22 == 0);
		Fixed strokeSize = wr->fRun[0];
		Fixed withinGap = wr->fRun[2];

		// the first one: nothing in front of it, so no gap is measured,
		// but its size is
		WordRecogAddStroke2(wr, s[0], F(10), 0, 0, 0, 0);
		EXPECT(wr->fStrokeCount == 1 && wr->fField22 == 1);
		// twenty-one, measured inclusively, an eighth of the way in
		EXPECT(wr->fRun[0] == FixedMultiply(0x0000e000, strokeSize)
							+ FixedMultiply(0x00002000, F(21)));
		EXPECT(wr->fField60 == F(21) && wr->fField68 == F(21));
		EXPECT(wr->fRun[2] == withinGap);		// no gap in front of the first
		EXPECT(gLastStrokeRight == F(10));

		// the second: five across from where the first one ended, and
		// the caller is sure it is part of the same letter
		WordRecogAddStroke2(wr, s[1], F(25), 0, 0, 0, 0);
		EXPECT(wr->fStrokeCount == 2);
		EXPECT(wr->fRun[2] == FixedMultiply(0x0000e000, withinGap)
							+ FixedMultiply(0x00002000, F(5)));
		EXPECT(gLastStrokeRight == F(25));

		// ... and the third, with the caller unsure: between 0.4 and
		// 0.6 nothing at all is learnt from the gap
		Fixed before = wr->fRun[2];
		WordRecogAddStroke2(wr, s[2], F(40), 0, 0, 0, 0x8000);
		EXPECT(wr->fStrokeCount == 3);
		EXPECT(wr->fRun[2] == before);

		// the word closed: the baseline of what was written, as the
		// two Points RosettaGetBaseLine hands out
		WordRecogAddStroke2(wr, nil, 0, 0, 1, 0, 0);
		Fixed meanBottom = (F(20) + F(25) + F(20)) / 3;
		Fixed meanHeight = (F(21) + F(21) + F(21)) / 3;
		EXPECT(wr->fBaseline.left == 0);
		EXPECT(wr->fBaseline.right == F(40));
		EXPECT(wr->fBaseline.bottom == meanBottom);
		EXPECT(wr->fBaseline.top == meanBottom - meanHeight);
		// ... and the strokes given back, because the recogniser owns them
		EXPECT(wr->fStrokeCount == 0 && wr->fStrokes[0] == nil);
	}

	// ---- the gap between letters is measured but never learnt ----
	{
		WordRecogReset(wr);
		wr->fField68 = 0;
		FPoint p[2];
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(10);	p[1].y = F(20);
		RosStroke* first = StrokeCreate(2, p);
		p[0].x = F(35);	p[0].y = F(0);	p[1].x = F(45);	p[1].y = F(20);
		RosStroke* second = StrokeCreate(2, p);

		WordRecogAddStroke2(wr, first, F(10), 0, 0, 0, 0);
		Fixed betweenMean = wr->fRun[4];
		// the gap is twenty-five, which is inside the half-to-double
		// band round the trained mean of 23.1, so the ROM does its
		// work - and writes the mean straight back unchanged
		WordRecogAddStroke2(wr, second, F(45), 0, 0, 0, F(1));
		EXPECT(wr->fRun[4] == betweenMean);
		// the second moment is what it always was, to the code's own
		// rounding
		Fixed deviate = FixedMultiply(0x000a7851, FixedDivide(wr->fRun[4], 0x00171999));
		EXPECT(wr->fRun[5] == FixedMultiply(wr->fRun[4], wr->fRun[4])
							+ FixedMultiply(deviate, deviate));

		WordRecogClearStrokes(wr);
	}

	// ---- and there is room for a hundred and fifty strokes ----
	{
		WordRecogReset(wr);
		FPoint p[2];
		p[0].x = F(0);	p[0].y = F(0);	p[1].x = F(4);	p[1].y = F(8);
		RosStroke* extra = StrokeCreate(2, p);
		wr->fStrokeCount = (short) kWordRecogMaxStrokes;
		WordRecogAddStroke2(wr, extra, 0, 0, 0, 0, 0);
		// it is given back rather than stored, and the count stands
		EXPECT(wr->fStrokeCount == kWordRecogMaxStrokes);
		wr->fStrokeCount = 0;
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
		BiGrammars empty;
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

	RSfRcl();
	EXPECT(RosCI == nil);
	// ---- the classifier run over a piece of a word ----
	{
		CharInitialize(0);			// the common info again: RSfRcl gave it back
		// The word recogniser's own way into the net, which is the twin
		// of `CharBoxNetEvaluate` - the ROM has the 256-code mapping
		// written out twice.  Given the same writing it must answer the
		// same thing.
		BPNet* net = BPNetCreateNumOut(134);
		BPNetLoad(net, nil);
		WordRecog* wr = WordRecogNew();
		WordRecogAllocate(wr);
		wr->fNet = net;

		// an upright stroke crossed by a level one, as the CharBox test
		// writes it
		RosStroke* made[2];
		FPoint a[2], b[2];
		a[0].x = F(25);	a[0].y = F(22);
		a[1].x = F(25);	a[1].y = F(58);
		b[0].x = F(12);	b[0].y = F(40);
		b[1].x = F(38);	b[1].y = F(40);
		made[0] = StrokeCreate(2, a);
		made[1] = StrokeCreate(2, b);
		RosStrokeList* writing = SLCreate(2, made);

		Fixed probs[256];
		EXPECT(wr->fPatternizer == nil && wr->fPattern == nil);
		WordRecogNetEvaluate(wr, net, writing, F(58), F(36), F(36), F(58), F(36), 0,
						F(36), F(36), F(36), probs);
		// the patternizer and its pattern were made on the way through
		// and kept, because a word is read one candidate letter at a
		// time and there may be dozens
		EXPECT(wr->fPatternizer != nil && wr->fPattern != nil);
		NetPatternizer* first = wr->fPatternizer;

		// + , t and T, and nothing else out of 256 - the same answer
		// `test_CharBox` gets
		EXPECT(probs['+'] == 0xf100);
		EXPECT(probs['t'] == 0xe500);
		EXPECT(probs['T'] == 0x0100);
		long sure = 0;
		for (long code = 0; code < 256; code++)
		{
			EXPECT(probs[code] >= 0 && probs[code] <= 0xff00);
			if (probs[code] > 0)
				sure++;
		}
		EXPECT(sure == 3);

		// a second reading does not make them again
		WordRecogNetEvaluate(wr, net, writing, F(58), F(36), F(36), F(58), F(36), 0,
						F(36), F(36), F(36), probs);
		EXPECT(wr->fPatternizer == first);
		EXPECT(probs['+'] == 0xf100);

		SLDestroy(writing, 1);
		WordRecogDestroy(wr);
	}

	// ---- how big is this word? ----
	{
		// `CharGetAvgBoxBHW` is what the engine measures a word with,
		// and everything the classifier is told about the writing's
		// size comes out of it.
		CharInitialize(0);

		RosSegment* segs[3];
		for (long i = 0; i < 3; i++)
			segs[i] = SegmentCreate();
		// two letters side by side, twenty-one tall and eleven wide
		SetFixedRect(&segs[0]->fBounds, F(0), F(0), F(10), F(20));
		SetFixedRect(&segs[1]->fBounds, F(20), F(0), F(30), F(20));
		segs[0]->fCount = 1;	segs[1]->fCount = 1;
		// ... and the dot over an i, well above them
		SetFixedRect(&segs[2]->fBounds, F(5), -F(10), F(6), -F(9));
		segs[2]->fCount = 1;
		segs[2]->fHasDot = 1;

		Fixed least = SegmentMinStrokeSize();
		Fixed base, height, width, altBase, maxHeight, maxWidth;
		CharGetAvgBoxBHW(segs, 3, least, F(20), 0xcccc, F(1), 0,
					&base, &height, &width, &altBase, &maxHeight, &maxWidth);

		// the base is the mean of the *letters'* bottoms.  The dot is
		// left out of it entirely, which is the whole reason the
		// segment layer records `fHasDot`: a dot sits nowhere near the
		// line and would drag the baseline up by a third.
		EXPECT(base == F(20));
		EXPECT(altBase == base);
		// the mean height and width, measured inclusively
		EXPECT(height == F(21));
		EXPECT(width == F(11));
		// the widest letter, scaled by the net's own 0.8
		EXPECT(maxWidth == FixedMultiply(F(11), 0xcccc));
		// and the tallest, raised to what the word's shape suggests:
		// a quarter of its height above the baseline (the dot counts
		// here) plus the widest letter comes to more than the 21 any
		// letter actually measures
		Fixed fromShape = FixedMultiply((base + F(10)) + F(21), 0x4000) + maxWidth;
		EXPECT(maxHeight == fromShape);
		EXPECT(maxHeight > F(21));

		// a word of nothing but wide flat letters is still measured as
		// though something in it were tall - which is what stops a row
		// of o's being read as a row of full stops
		RosSegment* flat[2];
		for (long i = 0; i < 2; i++)
		{
			flat[i] = SegmentCreate();
			flat[i]->fCount = 1;
		}
		SetFixedRect(&flat[0]->fBounds, F(0), F(18), F(14), F(20));
		SetFixedRect(&flat[1]->fBounds, F(20), F(18), F(34), F(20));
		CharGetAvgBoxBHW(flat, 2, least, F(20), 0xcccc, F(1), 0,
					&base, &height, &width, &altBase, &maxHeight, &maxWidth);
		// three pixels tall is under the two fifths of the word's size
		// that counts as measurable, so nothing is measured at all and
		// the height falls back to twice the mean width
		EXPECT(height == F(30));
		// ... and the tallest is raised to 1.8 times the widest letter
		EXPECT(maxHeight == FixedMultiply(0x1cccc, FixedMultiply(F(15), 0xcccc)));

		// nothing big enough to measure at all: the fallbacks
		RosSegment* tiny[1];
		tiny[0] = SegmentCreate();
		tiny[0]->fCount = 1;
		SetFixedRect(&tiny[0]->fBounds, F(0), F(0), F(1), F(1));
		CharGetAvgBoxBHW(tiny, 1, least, F(20), 0, 0, 0,
					&base, &height, &width, &altBase, &maxHeight, &maxWidth);
		EXPECT(height == least * 3);			// three times the least stroke
		EXPECT(width == FixedMultiply(height, 0x8000));		// and half of that

		for (long i = 0; i < 3; i++)
			SegmentDestroy(segs[i]);
		SegmentDestroy(flat[0]);
		SegmentDestroy(flat[1]);
		SegmentDestroy(tiny[0]);
	}

	// ---- the hand measured from a word, and held in range ----
	{
		// `WordRecogAnalyzeWord` moves three of the four lengths an
		// eighth of the way towards what the word just read says - but
		// only when the word is within half to twice what it already
		// believed, so one badly written word cannot drag the measure
		// away, and then holds each to between half and twice its
		// nominal.
		BPNet* net = BPNetCreateNumOut(134);
		BPNetLoad(net, nil);
		// as `RosettaAwaken` makes it, because closing a word now
		// hands readings back through `fWords` and the rest
		WordRecog* word = WordRecogCreate2(nil, nil, TestCheckWords, 10,
								&gGrammars, net, 1);
		// the ROM's General grammar with its lexicons found, which is
		// the state `RosettaSetArea` leaves a grammar in and the only
		// one the search may be run in - until then a kind of word
		// names its lexicon by its place in `gROMDictionaryData`
		// rather than pointing at it.  No ROM image is imported here,
		// so every one of them comes back empty, which is a machine
		// whose lexicons could not be built: the search reads nothing
		// and this is left to measure the hand.
		BiGrammar* context = BiGrammarClone(ROMGrammar.fContexts[0]);
		for (long i = 0; i < context->fCount; i++)
		{
			BiGSlice* slice = (BiGSlice*) context->fSlices[i];
			slice->fDictionary = (ULong) gROMDictionaryData[slice->fDictionary];
		}
		word->fContext = context;
		word->fField60 = F(20);
		word->fStrokeCount = 1;
		word->fSegmentCount = 1;

		FPoint pts[2];
		pts[0].x = F(0);	pts[0].y = F(0);
		pts[1].x = F(10);	pts[1].y = F(20);
		RosStroke* one = StrokeCreate(2, pts);
		SegmentStrokeData(one, 0, 0, 0);
		one->fSegment = 0;
		word->fStrokes[0] = one;
		word->fStrokeCount = 1;

		RosSegment* seg = SegmentCreate();
		SegmentSetStrokes(seg, 1, word->fStrokes);
		seg->fFirstStroke = 0;
		seg->fCount = 1;
		seg->fRealCount = 1;		// as `SegmentMakeSegments` would leave it
		SegmentBoundsDotsEtc(seg);
		word->fSegments[0] = seg;

		Fixed nominal18 = FixedMultiply(0x0012d999,
						FixedDivide(0x000151c4, 0x000117d5));
		word->fRun[18] = nominal18;
		Fixed was20 = word->fRun[20];
		WordRecogAnalyzeWord(word);

		// the fourth length is not touched here
		EXPECT(word->fRun[20] == was20);
		// the one segment is 21 tall, which is within half to twice
		// the nominal 22.9, so an eighth of the difference is learnt
		EXPECT(word->fRun[18] == FixedMultiply(0xe000, nominal18)
							+ FixedMultiply(0x2000, F(21)));
		EXPECT(word->fRun[18] >= FixedMultiply(0x8000, nominal18));
		EXPECT(word->fRun[18] <= FixedMultiply(0x20000, nominal18));
		// the word's size was worked out and kept
		EXPECT(word->fWordSize > 0);
		EXPECT(word->fMeanCharHeight > 0);
		// the mean height of one segment is that segment's height
		EXPECT(word->fMeanCharHeight == F(21));

		// a word wildly bigger than the hand teaches it nothing.
		// (`fReturnedStrokes` is put back by hand: the engine closes a
		//  word between one analysis and the next, and this does not.)
		Fixed steady = word->fRun[18];
		SetFixedRect(&seg->fBounds, F(0), F(0), F(10), F(400));
		word->fField60 = F(20);
		word->fReturnedStrokes = 0;
		WordRecogAnalyzeWord(word);
		EXPECT(word->fRun[18] == steady);

		SegmentDestroy(seg);
		StrokeDestroy(one);
		word->fSegments[0] = nil;
		word->fStrokes[0] = nil;
		word->fStrokeCount = 0;
		WordRecogDestroy(word);
	}

	if (failures == 0)
		printf("test_WordRecog: all passed\n");
	else
		printf("test_WordRecog: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
