/*
	File:		recognition/WordRecog.cpp

	Contains:	The handwriting engine's word recogniser - see
				WordRecog.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "WordRecog.h"
#include "NetPattern.h"
#include "RosEngine.h"
#include "Search.h"
#include "Segment.h"
#include "RosList.h"
#include "Fragment.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"

#include "host/RomBugs.h"
#include <string.h>
#include <stdio.h>


// The engine's own exception, which it throws when it cannot be made
// at all.  (It is declared where the rest of the names are, in
// os600/user/ExceptionNames.cpp; only this file wants it.)
extern const ExceptionName exRosetta;	// ROM 0x003774f8 exRosetta


// ROM 0x0037acdc FailureString
// Four question marks: what a reading is when there is none.
static const char	kFailureString[] = "????";		// ROM 0x00272720 (unnamed)
const char* const	FailureString = kFailureString;


// ROM 0x0c104f94 (unnamed)
RosStroke*	gXGapStroke = nil;
// ROM 0x0c104f8c (unnamed)
Fixed		gXGapMidX = 0;
// ROM 0x0c104f98 (unnamed)
RosStroke*	gPrevXGapStroke = nil;
// ROM 0x0c104f90 (unnamed)
Fixed		gPrevXGapMidX = 0;


// The engine's own free, which is a branch straight to `DisposPtr`.
static void
RosFree(void* p)
{
	if (p != nil)
		DisposPtr((Ptr) p);
}


#pragma mark -
/*--------------------------------------------------------------------
	Making it, and its life.
--------------------------------------------------------------------*/

// ROM 0x00274970 WordRecogNew
// The block, and nothing in it but the ten pointers a deallocate gives
// back.  Everything else is whatever was in the heap until
// `WordRecogCreate2` writes it, which is safe only because those ten
// are exactly what is freed - so a throw part way through making one
// gives back what was got and no more.
WordRecog*
WordRecogNew(void)
{
	// (the ROM asks for kWordRecogStateSize; ours is bigger, because a
	//  host pointer is eight bytes)
	WordRecog* wr = (WordRecog*) RosAllocate((long) sizeof(WordRecog));
	if (wr == nil)
		return nil;

	wr->fWordCount = 0;
	wr->fStrokes = nil;
	wr->fSegments = nil;
	wr->fBuffer48 = nil;
	wr->fWords = nil;
	wr->fScores = nil;
	wr->fWordFlags = nil;
	wr->fField1c = nil;
	wr->fGrammars = nil;
	wr->fPatternizer = nil;
	wr->fPattern = nil;
	wr->fBuffer4c = nil;
	return wr;
}


// ROM 0x00274c08 WordRecogDeallocate
// Everything hung off the block given back and nilled, so that it can
// be done twice - which it is, once by the exception handler in
// `WordRecogAllocate` and again by whoever catches the throw.
void
WordRecogDeallocate(WordRecog* wr)
{
	ListZap();
	RosFree(wr->fStrokes);
	RosFree(wr->fSegments);
	RosFree(wr->fWords);
	RosFree(wr->fScores);
	RosFree(wr->fWordFlags);
	RosFree(wr->fField1c);
	RosFree(wr->fBuffer48);
	RosFree(wr->fBuffer4c);
	NetPatternDestroy(wr->fPattern);
	NetPatternizerDestroy(wr->fPatternizer);
	wr->fStrokes = nil;
	wr->fSegments = nil;
	wr->fWords = nil;
	wr->fScores = nil;
	wr->fWordFlags = nil;
	wr->fField1c = nil;
	wr->fBuffer48 = nil;
	wr->fBuffer4c = nil;
	wr->fPattern = nil;
	wr->fPatternizer = nil;
}


// ROM 0x00274a08 WordRecogDestroy
void
WordRecogDestroy(WordRecog* wr)
{
	if (wr == nil)
		return;
	SegmentQuiesce();
	WordRecogDeallocate(wr);
	RosFree(wr);
}


// ROM 0x00274a38 WordRecogAllocate
// The arrays, sized from `fWordCount` and from the most strokes and
// segments one word may be made of.  Any of them failing throws, and
// the handler gives the rest back before the throw goes on.
void
WordRecogAllocate(WordRecog* wr)
{
	if (wr == nil)
		return;

	newton_try
	{
		// DEVIATION: the three arrays of pointers are sized by `sizeof`
		// on the host, where the ROM has four bytes apiece
		wr->fStrokes = (RosStroke**) RosAllocate(kWordRecogMaxStrokes * (long) sizeof(RosStroke*));
		wr->fSegments = (RosSegment**) RosAllocate(kWordRecogMaxSegments * (long) sizeof(RosSegment*));
		wr->fWords = (char**) RosAllocate(wr->fWordCount * (long) sizeof(char*));
		wr->fScores = (UniChar*) RosAllocate(wr->fWordCount * 2);
		wr->fWordFlags = (long*) RosAllocate(wr->fWordCount * (long) sizeof(long));
		wr->fBuffer48 = RosAllocate(kWordRecogBufferSize);
		wr->fBuffer4c = RosAllocate(kWordRecogBufferSize);
	}
	newton_catch_all
	{
		WordRecogDeallocate(wr);
		rethrow;
	}
	end_try;
}


// ROM 0x00275940 WordRecogCreate2
// A whole recogniser.  Nothing in the ROM calls this - `RosettaAwaken`
// reaches it through the patchable jump table - and what it passes is
// `WordRecogCreate2(nil, nil, RosettaCheckWords, 10, ROMGrammar,
// theNet, 1)`: ten readings, the engine's own grammar, and the strokes
// are the engine's to free.
WordRecog*
WordRecogCreate2(void* field00, void* field04,
				WordRecogCheckWordsProc checkWords, long wordCount,
				const BiGrammars* grammars, BPNet* net, short ownsStrokes)
{
	if (grammars == nil)
		Throw(exRosetta, (void*) -1, nil);

	WordRecog* wr = WordRecogNew();
	if (wr == nil)
		return nil;

	wr->fWordCount = wordCount;
	if (checkWords == nil || wordCount < 1)
		// with nobody to hand them to, or a silly number asked for,
		// one reading
		wr->fWordCount = 1;

	newton_try
	{
		WordRecogAllocate(wr);
		wr->fGrammars = BiGrammarsLoad(grammars);
		if (wr->fGrammars == nil || wr->fGrammars->fCount == 0)
			Throw(exRosetta, (void*) 1, nil);
	}
	newton_catch_all
	{
		WordRecogDestroy(wr);
		rethrow;
	}
	end_try;

	wr->fSuspended = 0;
	wr->fField00 = field00;
	wr->fField04 = field04;
	wr->fCheckWords = checkWords;
	wr->fOwnsStrokes = ownsStrokes;
	wr->fNet = net;
	wr->fStrokeCount = 0;
	wr->fSegmentCount = 0;
	wr->fPendingStroke = nil;
	// eight pairs, of which only the second word of each is set here;
	// what the first word of the first pair is, `WordRecogReset` works
	// out
	// the word's box and middle, empty: the minima start at one and
	// the maxima at minus one, so the first stroke sets them all
	wr->fWordLeft[0] = 0x00010000;
	wr->fWordRight[0] = (Fixed) 0xffff0000;
	wr->fWordTop[0] = 0x00010000;
	wr->fWordBottom[0] = (Fixed) 0xffff0000;
	wr->fWordCentroidX[0] = (Fixed) 0xffff0000;
	wr->fWordCentroidY[0] = (Fixed) 0xffff0000;
	wr->fWordBodyTop[0] = 0;
	wr->fWordBodyBottom[0] = 0;
	WordRecogReset(wr);
	return wr;
}


// ROM 0x00274cbc WordRecogSuspend
// The writing forgotten and the arrays given back, but the block left
// where it is: everything that points at the recogniser goes on
// pointing at it while the engine sleeps.
void
WordRecogSuspend(WordRecog* wr)
{
	if (wr == nil)
		return;
	WordRecogClear(wr, false);
	WordRecogDeallocate(wr);
	wr->fSuspended = 1;
}


// ROM 0x002758d0 WordRecogResume
// ... and made again.  It asks five of the arrays rather than the flag
// alone, so a recogniser that still has its memory is left alone even
// if it thinks it is suspended.
long
WordRecogResume(WordRecog* wr)
{
	if (wr == nil)
		return kWordRecogNoRecognizer;

	if (wr->fStrokes == nil && wr->fSegments == nil && wr->fWords == nil
		&& wr->fScores == nil && wr->fWordFlags == nil && wr->fSuspended != 0)
	{
		WordRecogAllocate(wr);
		wr->fSuspended = 0;
		return kWordRecogOk;
	}
	return kWordRecogNothingToDo;
}


#pragma mark -
/*--------------------------------------------------------------------
	The starting values.

	Every one of them is the engine's nominal cap height times a ratio,
	and the five at the end say so out loud: they are the nominal ratio
	divided by a number of its own.  The numbers themselves are
	ParaGraph's, trained rather than reasoned, and are transcribed as
	the ROM has them.
--------------------------------------------------------------------*/

// 18.85 pixels, and 1.3231.
static const Fixed	kNominalHeight	= 0x0012d999;
static const Fixed	kNominalRatio	= 0x000151c4;


// ROM 0x00275b14 WordRecogReset
void
WordRecogReset(WordRecog* wr)
{
	if (wr == nil)
		return;

	// the tablet's resolution is not known until the engine is woken
	wr->fResX = -1;
	wr->fResY = -1;
	wr->fField2c = 0x00010000;
	wr->fField30 = 0x00010000;
	wr->fField34 = FixedMultiply(kNominalHeight, kNominalRatio);

	wr->fSavedRun[0]  = kNominalHeight;
	wr->fSavedRun[1]  = 0x01a4fae1;
	wr->fSavedRun[2]  = 0x00063851;
	wr->fSavedRun[3]  = 0x003647ae;
	wr->fSavedRun[4]  = 0x00171999;
	wr->fSavedRun[5]  = 0x0283451e;
	wr->fSavedRun[6]  = 0x000eb0a3;
	wr->fSavedRun[7]  = 0x011b11eb;
	wr->fSavedRun[8]  = 0x0022d1eb;
	wr->fSavedRun[9]  = 0x05408f5c;
	wr->fSavedRun[10] = 0x000057ce;
	wr->fSavedRun[11] = 0x00002a09;
	wr->fSavedRun[12] = 0x00015212;
	wr->fSavedRun[13] = 0x00022339;
	wr->fSavedRun[14] = 0x0000c9db;
	// (the ROM makes this one by subtracting 0x1d8 from the one before)
	wr->fSavedRun[15] = 0x0000c9db - 0x1d8;
	wr->fSavedRun[16] = 0x0001f5e3;
	wr->fSavedRun[17] = 0x00043ed2;
	wr->fSavedRun[18] = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x000117d5));
	wr->fSavedRun[19] = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x00006edf));
	wr->fSavedRun[20] = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x0000d36e));
	wr->fSavedRun[21] = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x00011c08));
	// ... and one more in the same style, which the run itself does not
	// hold: it is the first of the eight pairs `WordRecogCreate2` sets
	wr->fWordSize = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x0000fcb9));

	wr->fField44 = 0;
	wr->fContextIndex = 0;
	wr->fContext = wr->fGrammars->fContexts[0];
	WordRecogSetContext(wr, "General");
	WordRecogClear(wr, true);
}


// ROM 0x00275d28 WordRecogClear
void
WordRecogClear(WordRecog* wr, Boolean invalRun)
{
	if (wr == nil)
		return;
	if (invalRun)
		WordRecogInvalRun(wr);

	wr->fField60 = wr->fRun[0];
	if (RomBugFixed())
		wr->fField68 = wr->fRun[18];	// (the ROM bug in WordRecogIsStrokeTooWide)
	wr->fWordBreak = 0;

	RosStroke* pending = wr->fPendingStroke;
	if (pending != nil
		&& (wr->fOwnsStrokes != 0 || pending->fFragment != 0 || pending->fJoinsNext != 0))
		StrokeDestroy(pending);
	wr->fPendingStroke = nil;

	WordRecogClearStrokes(wr);
}


// ROM 0x00275f04 WordRecogClearStrokes
// The segments always go back; a stroke only goes back if the
// recogniser owns them all or the engine made this one itself, because
// the rest belong to whoever handed them over.
void
WordRecogClearStrokes(WordRecog* wr)
{
	if (wr == nil)
		return;

	if (wr->fSegments != nil)
		for (short i = 0; i < wr->fSegmentCount; i++)
		{
			SegmentDestroy(wr->fSegments[i]);
			wr->fSegments[i] = nil;
		}
	wr->fSegmentCount = 0;

	if (wr->fStrokes != nil)
		for (short i = 0; i < wr->fStrokeCount; i++)
		{
			RosStroke* stroke = wr->fStrokes[i];
			if (wr->fOwnsStrokes == 0
				&& (stroke == nil || (stroke->fFragment == 0 && stroke->fJoinsNext == 0)))
				continue;
			StrokeDestroy(stroke);
			// and the gap cache forgets it, because the next stroke to
			// be allocated may well land on the same address
			if (stroke == gPrevXGapStroke)
			{
				gPrevXGapMidX = 0;
				gPrevXGapStroke = nil;
			}
			else if (stroke == gXGapStroke)
			{
				gXGapMidX = 0;
				gXGapStroke = nil;
			}
			wr->fStrokes[i] = nil;
		}
	wr->fStrokeCount = 0;
	wr->fField22 = 0;
	wr->fWordStrokes = 0;
	wr->fReturnedStrokes = 0;
	if (wr->fField1c != nil)
		wr->fField1c[0] = 0;
}


// ROM 0x00275d94 WordRecogInvalRun
void
WordRecogInvalRun(WordRecog* wr)
{
	for (long i = 0; i < kWordRecogRunLength; i++)
		wr->fRun[i] = wr->fSavedRun[i];
}


// ROM 0x00275e48 WordRecogSaveRun
void
WordRecogSaveRun(WordRecog* wr)
{
	if (wr == nil)
		return;
	for (long i = 0; i < kWordRecogRunLength; i++)
		wr->fSavedRun[i] = wr->fRun[i];
}


// ROM 0x00276050 WordRecogSetContext
// The grammar contexts are found by name rather than by number, so a
// field asking to be read as a date or a telephone number names one.
long
WordRecogSetContext(WordRecog* wr, const char* name)
{
	for (long i = 0; i < wr->fGrammars->fCount; i++)
		if (strcmp(wr->fGrammars->fContexts[i]->fName, name) == 0)
		{
			wr->fContextIndex = i;
			wr->fContext = wr->fGrammars->fContexts[i];
			return kWordRecogOk;
		}
	return 1;
}


#pragma mark -
/*--------------------------------------------------------------------
	Measuring, and handing the readings back.
--------------------------------------------------------------------*/

// ROM 0x002760d0 WordRecogDetermineMaxHeight
// The tallest stroke, measured inclusively - a stroke drawn on one
// scan line is one pixel tall, not none - and with nothing written
// yet, the height the run says a word has.
Fixed
WordRecogDetermineMaxHeight(WordRecog* wr)
{
	if (wr->fStrokeCount == 0)
		return wr->fRun[18];

	RosStroke* stroke = wr->fStrokes[0];
	Fixed tallest = (stroke->fBounds.bottom - stroke->fBounds.top) + 0x00010000;
	for (short i = 1; i < wr->fStrokeCount; i++)
	{
		stroke = wr->fStrokes[i];
		Fixed height = (stroke->fBounds.bottom - stroke->fBounds.top) + 0x00010000;
		if (height >= tallest)
			tallest = height;
	}
	return tallest;
}


// ROM 0x00274818 WordRecogComputeCapHeight
// The word just read says how tall a capital is.  Every character code
// has a nominal height in the common info - a fraction of the cap
// height, and the table reads like one: 0.96 for `A`, 0.92 for `l`,
// 0.45 for `o`, 0.08 for a full stop - so the height the characters
// actually took, divided by the average of the fractions the engine
// decided it was reading, is what the hand's cap height must be.
//
// It is not believed outright: an eighth of the answer, and the cap
// height that was there is the other seven.  And it is thrown away
// entirely if it is smaller than the engine will credit anything or
// more than two and a half times what it had.
void
WordRecogComputeCapHeight(WordRecog* wr)
{
	const char* word = wr->fWords[0];
	if (strcmp(word, FailureString) == 0)
		return;
	if (strlen(word) == 0)
		return;

	Fixed sum = 0;
	ULong i = 0;
	// (the ROM asks `strlen` again every time round the loop, and once
	//  more to start it: the word is a C string and nothing is caching
	//  its length)
	while (i < strlen(word))
	{
		// table 1 is how tall a character is, as a fraction of the
		// cap height
		sum += RosCI->fCharParams[1][(UByte) word[i]];
		i++;
	}

	// (the average is an ordinary integer divide: the fractions are
	//  16.16 and the count is a count, so what comes out is 16.16)
	Fixed estimate = FixedDivide(wr->fMeanCharHeight, sum / (Fixed) i);
	if (RosCI->fMinStrokeSize >= estimate)
		return;
	if (FixedMultiply(0x00028000, wr->fRun[20]) <= estimate)
		return;
	wr->fRun[20] = FixedMultiply(0x0000e000, wr->fRun[20])
				 + FixedMultiply(0x00002000, estimate);
}


// ROM 0x00274910 WordRecogDotIsHigh
// `range` is a pair of shorts - the first stroke and how many - which
// is how the layers above hand a run of them about.  A dot counts as
// high if the middle of it is above a quarter of the way down from
// `y`; the loop does not stop at the first one it finds.
Boolean
WordRecogDotIsHigh(WordRecog* wr, const short* range, Fixed y, Fixed height)
{
	Boolean high = false;
	short first = range[0];
	short end = (short) (first + range[1]);
	if (end <= first)
		return high;

	Fixed limit = y + (height >> 2);
	for (short i = first; i < end; i++)
	{
		RosStroke* stroke = wr->fStrokes[i];
		if (stroke->fIsDot != 0
			&& limit < ((stroke->fBounds.top + stroke->fBounds.bottom) >> 1))
			high = true;
	}
	return high;
}


// ROM 0x002746f0 WordRecogEndWord
void
WordRecogEndWord(WordRecog* wr, char** words, UniChar* scores, long* flags,
				long strokes, long count)
{
	if (count > 0)
		WordRecogComputeCapHeight(wr);
	WordRecogReturnWords(wr, words, scores, flags, strokes, count);
}


// ROM 0x00274744 WordRecogReturnWords
// The readings handed to whoever asked for them.  With no readings at
// all the recogniser puts "????" in its own first slot with the worst
// score there is and says the whole of the writing is covered by it,
// so that the caller always gets an answer.
//
// The stroke count it passes on is not the one it was given: strokes
// the engine cut for itself do not count, because the layers above
// know nothing of them, and at least one is always claimed.
void
WordRecogReturnWords(WordRecog* wr, char** words, UniChar* scores, long* flags,
					long strokes, long count)
{
	if (count < 1)
	{
		wr->fWords[0] = (char*) FailureString;
		wr->fScores[0] = 0x7ffe;
		wr->fWordFlags[0] = 0;
		count = 1;
		strokes = wr->fStrokeCount;
	}

	long i = wr->fReturnedStrokes;
	long end = i + strokes;
	for (; i < end; i++)
		if (wr->fStrokes[i]->fFragment != 0)
			strokes--;
	wr->fReturnedStrokes = (short) i;

	if (strokes < 1)
		strokes = 1;
	if (wr->fCheckWords == nil)
		return;
	wr->fCheckWords(words, scores, flags, (ULong) strokes, (ULong) count);
}


#pragma mark -
/*--------------------------------------------------------------------
	What the recogniser makes of one stroke.
--------------------------------------------------------------------*/

// ROM 0x0c104f88 MinFragmentWidthMultiple
// 0.45 of what a letter of the hand being read should measure.  It is
// in the ROM's initialised data and nothing ever writes it.
Fixed	MinFragmentWidthMultiple = 0x00007333;


// ROM 0x002765ac WordRecogStrokeType
// Which way a stroke goes, if it goes any way at all.  A stroke whose
// longer side is under the smallest the engine credits has no shape
// worth talking about and is neither.
//
// The horizontal test has two parts, and the second is the interesting
// one: a stroke is only horizontal if it is both much wider than it is
// tall *and* short in its own right - no taller than a quarter of the
// engine's small height.  A long shallow arc drawn large is therefore
// not a horizontal stroke, because at that size a quarter of it is
// still a letter's worth of ink.
long
WordRecogStrokeType(WordRecog* wr, const RosStroke* stroke)
{
	Fixed width = stroke->fBounds.right - stroke->fBounds.left;
	Fixed height = stroke->fBounds.bottom - stroke->fBounds.top;
	Fixed longer = (width < height) ? height : width;

	if (SegmentMinStrokeSize() > longer)
		return kWordRecogStrokeNeither;
	if (width < (height >> 2))
		return kWordRecogStrokeVertical;
	if (height < (width >> 2) && height < (wr->fWordSize >> 2))
		return kWordRecogStrokeHorizontal;
	return kWordRecogStrokeNeither;
}


// ROM 0x00276618 WordRecogIsStrokeTooWide
// Too wide for one letter of the hand being read.  What a letter
// should measure is `fRun[21]`, and it is scaled up when the writing
// has turned out bigger than the run expected: three parts of what the
// word has measured so far and one of the tallest stroke in it,
// against the small height the run holds.
//
// A stroke the engine cut for itself is never too wide, whatever it
// measures - the pieces are not cut again.
Boolean
WordRecogIsStrokeTooWide(WordRecog* wr, RosStroke* stroke, Fixed multiple)
{
	// ROM BUG (fixed): fField68 is never set before its running mean first
	// reads it (+0x68 is written only in WordRecogAddStroke2), so it starts
	// as whatever the heap held and can be large enough for this and the
	// mean to overflow; the ARM wraps, so the sums are worked in 32-bit
	// unsigned arithmetic here (the host traps a signed overflow) and the
	// shift is the ARM's arithmetic one.  The fix starts it, with the rest
	// of a run's measures in WordRecogClear, at the small height the run
	// holds (fRun[18]), the measure it is set against here.
	Fixed measured = (Fixed) (int32_t) ((uint32_t) wr->fField68 * 3u + (uint32_t) WordRecogDetermineMaxHeight(wr)) >> 2;
	Fixed scale = (wr->fRun[18] < measured)
				? FixedDivide(measured, wr->fRun[18])
				: 0x00010000;
	Fixed limit = FixedMultiply(multiple, FixedMultiply(scale, wr->fRun[21]));

	FRect bounds;
	StrokeFindBounds(stroke, &bounds);
	FPoint size;
	FixedRectSize(&size, &bounds);

	if (size.x + 0x00010000 >= limit
		&& stroke->fFragment == 0 && stroke->fJoinsNext == 0)
		return true;
	return false;
}


// Does an end of `other` run down through `stroke`?  Two thirds of
// its points from one end, then two thirds from the other: what
// matters is whether an *end* of it is vertical, because the middle of
// a letter can go anywhere.  A piece counts if it is vertical, if
// `middle` falls inside it top to bottom, and if the piece's own
// middle width falls inside `left`..`right`.
//
// (The ROM has this written out twice, once for each loop below.)
static Boolean
StrokeEndRunsThrough(WordRecog* wr, const RosStroke* other,
					Fixed left, Fixed right, Fixed middle)
{
	// two thirds of its points, rounded up.  (The ROM divides with
	// `__rt_sdiv`, which takes the divisor first.)
	short n = (short) ((other->fCount * 2 + 1) / 3);
	Boolean through = false;
	RosStroke* piece = nil;
	for (short end = 0; end < 2; end++)
	{
		if (end != 0)
			StrokeDestroy(piece);
		const FPoint* points = (end == 0)
							? other->fPoints
							: other->fPoints + other->fCount - n;
		piece = StrokeCreate(n, points);
		if (piece != nil && WordRecogStrokeType(wr, piece) == kWordRecogStrokeVertical)
		{
			Fixed across = (piece->fBounds.left + piece->fBounds.right) >> 1;
			if (middle > piece->fBounds.top && middle < piece->fBounds.bottom
				&& across > left && across < right)
			{
				through = true;
				break;
			}
		}
	}
	StrokeDestroy(piece);
	return through;
}


// ROM 0x00276374 WordRecogStrokeIntersectsTwoVerticalStrokes
// The question a long horizontal stroke is put: does it run through
// two letters?  A stroke that does is the cross of a double-struck t,
// or a line drawn under a word, and has to be cut; one that runs
// through only one is part of that letter.
Boolean
WordRecogStrokeIntersectsTwoVerticalStrokes(WordRecog* wr, const RosStroke* stroke)
{
	long found = 0;
	Fixed left = stroke->fBounds.left;
	Fixed right = stroke->fBounds.right;
	Fixed middle = (stroke->fBounds.top + stroke->fBounds.bottom) >> 1;

	for (long i = 0; i < wr->fWordStrokes; i++)
		if (StrokeEndRunsThrough(wr, wr->fStrokes[i], left, right, middle))
			found++;

	// ... and the stroke that has not been taken in yet is asked in
	// exactly the same way
	if (wr->fPendingStroke != nil
		&& StrokeEndRunsThrough(wr, wr->fPendingStroke, left, right, middle))
		found++;

	return found > 1;
}


// ROM 0x002762f4 WordRecogStrokeNeedsFragmenting
// Whether a stroke is to be cut in two before the engine reads it.  It
// has to be too wide first; then a stroke with no shape of its own is
// cut, a vertical one never is - one letter can be as tall as it likes
// - and a horizontal one is only cut if it runs through two letters.
Boolean
WordRecogStrokeNeedsFragmenting(WordRecog* wr, RosStroke* stroke)
{
	if (!WordRecogIsStrokeTooWide(wr, stroke, MinFragmentWidthMultiple))
		return false;

	long type = WordRecogStrokeType(wr, stroke);
	if (type == kWordRecogStrokeNeither)
		return true;
	if (type == kWordRecogStrokeVertical)
		return false;
	if (type == kWordRecogStrokeHorizontal)
		return WordRecogStrokeIntersectsTwoVerticalStrokes(wr, stroke);
	return true;
}


#pragma mark -
/*--------------------------------------------------------------------
	Strokes in, and the run of measurements.

	`fRun` is the engine's model of the hand it is reading, and it is
	nine Gaussians and four lengths.  Each Gaussian is a pair: the mean
	of what has been measured, and the mean of its square - which, for
	a distribution whose spread grows with its mean, is all the
	classifier needs to score a measurement against it.  The four
	things measured are the gap in front of a stroke and that gap as a
	fraction of the writing's size, each in both directions; and each
	of those has *two* distributions, one for a gap inside a letter and
	one for a gap between letters, which is what `separation` chooses
	between.  Pair 0 is the size of a stroke itself.

	ParaGraph trained the nine, and their starting values say so: every
	second number is its own mean squared plus a standard deviation
	that is a fixed fraction of the mean.  `WordRecogReset` writes them
	out and the code below learns away from them, an eighth at a time,
	never letting a mean stray more than a quarter from what was
	trained (or, for the stroke size, more than double or less than
	half).
--------------------------------------------------------------------*/

// ROM 0x0c104f84 FragmentLigatures
ULong	FragmentLigatures = 1;

// ROM 0x0c104fa0 (unnamed)
Fixed	gLastStrokeRight = 0;
// ROM 0x0c104fa4 (unnamed)
Fixed	gLastStrokeAdvance = 0;
// ROM 0x0c104fa8 (unnamed)
UByte	gLastStrokeWasCut = 0;


// One of the nine learnt from: the mean nudged an eighth of the way
// towards what was just measured, held within `low` and `high` of what
// ParaGraph trained, and the second moment worked out from it again.
static void
LearnRunPair(Fixed* pair, Fixed value, Fixed nominal, Fixed deviation, Fixed low, Fixed high)
{
	pair[0] = FixedMultiply(0x0000e000, pair[0]) + FixedMultiply(0x00002000, value);
	if (pair[0] < FixedMultiply(low, nominal))
		pair[0] = FixedMultiply(low, nominal);
	else if (FixedMultiply(high, nominal) < pair[0])
		pair[0] = FixedMultiply(high, nominal);
	Fixed deviate = FixedMultiply(deviation, FixedDivide(pair[0], nominal));
	pair[1] = FixedMultiply(pair[0], pair[0]) + FixedMultiply(deviate, deviate);
}


// ... and the same again with the mean left alone.
//
// ROM BUG (fixed): this works the second moment out from a mean it does
// not change, so after the first stroke it writes back the number that
// was already there.  The first time it does have an effect - it replaces
// ParaGraph's trained second moment with what the code's own rounding
// makes of the same formula - but nothing is learnt.  The four
// distributions it is used on (the between-letter ones) therefore
// never move at all, while their four within-letter counterparts do.
// The shape of the call says what was meant: it is the other half of
// `LearnRunPair` with the first two lines dropped.  The fix puts the
// first line back - the mean nudged an eighth of the way towards the
// value, inside the gate the ROM already has (half to twice the mean),
// which stands in for LearnRunPair's limits.
static void
RelearnRunSpread(Fixed* pair, Fixed value, Fixed nominal, Fixed deviation)
{
	if (FixedDivide(pair[0], 0x00020000) < value && value < FixedMultiply(pair[0], 0x00020000))
	{
		if (RomBugFixed())
			pair[0] = FixedMultiply(0x0000e000, pair[0]) + FixedMultiply(0x00002000, value);
		Fixed deviate = FixedMultiply(deviation, FixedDivide(pair[0], nominal));
		pair[1] = FixedMultiply(pair[0], pair[0]) + FixedMultiply(deviate, deviate);
	}
}


// A stroke given back if it is the recogniser's to give back: the
// engine's own pieces and, when it owns them all, everything.
static void
DestroyStrokeIfOurs(WordRecog* wr, RosStroke* stroke)
{
	if (wr->fOwnsStrokes != 0
		|| (stroke != nil && (stroke->fFragment != 0 || stroke->fJoinsNext != 0)))
		StrokeDestroy(stroke);
}




// ROM 0x00274cf0 WordRecogAddStroke2
void
WordRecogAddStroke2(WordRecog* wr, RosStroke* stroke, Fixed advance, Fixed /*field04*/,
					long endWord, short how, Fixed separation)
{
	if (endWord != 0)
	{
		newton_try
		{
			/*----------------------------------------------------------
				The baseline of what has been written.
			----------------------------------------------------------*/
			// the mean height and the mean foot of the strokes, and the
			// box round the lot.  (Nothing guards `fStrokeCount` being
			// nought here, and the divides below would trap: the ROM is
			// never called that way.)
			RosStroke** strokes = wr->fStrokes;
			Fixed sumHeight = (strokes[0]->fBounds.bottom - strokes[0]->fBounds.top) + 0x00010000;
			Fixed sumBottom = strokes[0]->fBounds.bottom;
			Fixed top = strokes[0]->fBounds.top;
			Fixed left = strokes[0]->fBounds.left;
			Fixed right = strokes[0]->fBounds.right;
			short count = wr->fStrokeCount;
			for (short i = 1; i < count; i++)
			{
				RosStroke* s = strokes[i];
				sumHeight += (s->fBounds.bottom - s->fBounds.top) + 0x00010000;
				sumBottom += s->fBounds.bottom;
				if (s->fBounds.top <= top)
					top = s->fBounds.top;
				if (s->fBounds.left <= left)
					left = s->fBounds.left;
				if (right < s->fBounds.right)
					right = s->fBounds.right;
			}
			Fixed meanHeight = sumHeight / count;
			Fixed meanBottom = sumBottom / count;

			// the box is answered relative to its own top-left corner,
			// with the foot of the writing as its bottom and one mean
			// height above that as its top - which is what
			// `RosettaGetBaseLine` hands out as two Points.
			SetFixedRect(&wr->fBaseline, 0, meanBottom - meanHeight - top,
						right - left, meanBottom - top);

			// ... in seventy-seconds of an inch, if the tablet's
			// resolution is known and is not already that
			if (wr->fResX > 0 && wr->fResY > 0
				&& !(wr->fResX == 0x48 && wr->fResY == 0x48))
				XYFixedScaleFixedRect(&wr->fBaseline,
					FixedDivide((Fixed) (int) ((unsigned int) wr->fResX << 16), 0x00480000),
					FixedDivide((Fixed) (int) ((unsigned int) wr->fResY << 16), 0x00480000));
			else if (RomBugFixed())
			{
				// (the fix: each axis by its own scale, when either is not one)
				if (wr->fField2c != 0x00010000 || wr->fField30 != 0x00010000)
					XYFixedScaleFixedRect(&wr->fBaseline, wr->fField2c, wr->fField30);
			}
			else if (wr->fField2c != 0x00010000)
				// ROM BUG (fixed): it asks whether the horizontal scale is
				// one and then scales *both* axes by the vertical one.
				// `fField2c` is never read anywhere else, so nothing
				// notices.  The fix scales x by fField2c and y by fField30.
				XYFixedScaleFixedRect(&wr->fBaseline, wr->fField30, wr->fField30);

			/*----------------------------------------------------------
				... and the word read.
			----------------------------------------------------------*/
			if (wr->fClassifyMode != 0)
			{
				// the engine has been told to group but not to read, so
				// it answers a word of its own saying just that
				wr->fWords[0] = (char*) "gROSsegOnly";	// ROM 0x00274f44 (unnamed)
				wr->fScores[0] = 0x7ffe;
				wr->fWordFlags[0] = 0;
				WordRecogReturnWords(wr, wr->fWords, wr->fScores, wr->fWordFlags,
									wr->fStrokeCount, 1);
			}
			else
			{
				if (how == 0)
				{
					if (FragmentLigatures != 0)
						StrokeSortFrags(wr->fStrokes, wr->fStrokeCount);
					else
						StrokeSort(wr->fStrokes, wr->fStrokeCount);
				}
				wr->fSegmentCount = SegmentChars(wr->fStrokeCount, wr->fStrokes, wr->fField60,
												wr->fSegments, wr->fField44, wr->fNet);
				WordRecogAnalyzeWord(wr);
			}
			WordRecogClearStrokes(wr);
		}
		cleanup
		{
			DestroyStrokeIfOurs(wr, stroke);
		}
		end_try;
	}

	if (stroke == nil)
		return;

	// there is room for kWordRecogMaxStrokes of them and no more
	if (wr->fStrokeCount > kWordRecogMaxStrokes - 1)
	{
		DestroyStrokeIfOurs(wr, stroke);
		return;
	}

	newton_try
	{
		SegmentStrokeData(stroke, (UByte) how, wr->fStrokeCount, separation);
		wr->fStrokes[wr->fStrokeCount] = stroke;

		FRect bounds;
		StrokeFindBounds(stroke, &bounds);
		if (wr->fStrokeCount == 0 && wr->fWordBreak != 1)
		{
			// the first stroke of a word has nothing in front of it, so
			// the gap is measured from its own left edge and comes out
			// nought
			gLastStrokeRight = bounds.left;
			gLastStrokeAdvance = advance;
			wr->fWordBreak = 0;
			gLastStrokeWasCut = 0;
		}

		/*--------------------------------------------------------------
			How big the writing is.
		--------------------------------------------------------------*/
		// a dot, and a piece the engine cut for itself, say nothing
		// about the size of the hand
		if (stroke->fIsDot == 0 && stroke->fFragment == 0 && stroke->fJoinsNext == 0)
		{
			wr->fField22 = (short) (wr->fField22 + 1);

			FPoint size;
			FixedRectSize(&size, &bounds);
			Fixed larger = ((size.y <= size.x) ? size.x : size.y) + 0x00010000;
			Fixed height = size.y + 0x00010000;
			long seen = wr->fField22;

			// the running mean of the larger side, over the strokes
			// narrow enough to be a letter: three letters' width at the
			// scale the writing has turned out to be
			Fixed scale = (wr->fRun[18] < height)
						? FixedDivide(height, wr->fRun[18])
						: 0x00010000;
			Fixed wide = FixedMultiply(0x00030000, FixedMultiply(scale, wr->fRun[21]));
			if (size.x + 0x00010000 < wide)
				wr->fField60 = (wr->fField60 * (seen - 1) + larger) / seen;

			// ... and of the height, over the strokes tall enough to be
			// worth counting
			if (FixedMultiply(0x00004000, wr->fRun[18]) < height)
				// (fField68 starts as heap rubbish in the ROM - see the ROM
				//  bug in WordRecogIsStrokeTooWide; the product wraps as the
				//  ARM's does)
				wr->fField68 = (Fixed) ((int32_t) ((uint32_t) wr->fField68 * (uint32_t) (seen - 1) + (uint32_t) height) / (int32_t) seen);

			// and the first of the nine: how big a stroke is.  Anything
			// more than twice what is expected is left out of it.
			if (larger < FixedMultiply(0x00020000, wr->fRun[0]))
				LearnRunPair(&wr->fRun[0], larger, kNominalHeight, 0x00081c28,
							0x00008000, 0x00020000);
		}

		/*--------------------------------------------------------------
			... and how it is spaced.
		--------------------------------------------------------------*/
		// the gap in front of this stroke, in both directions, and the
		// same as a fraction of how big a stroke is
		Fixed gap = bounds.left - gLastStrokeRight;
		if (gap < 1)
			gap = 0;
		Fixed gapAlong = advance - gLastStrokeAdvance;
		if (gapAlong < 1)
			gapAlong = 0;
		Fixed gapRatio = FixedDivide(gap, wr->fRun[0]);
		Fixed gapAlongRatio = FixedDivide(gapAlong, wr->fRun[0]);

		// a gap either side of a piece the engine cut is not a gap the
		// writer made
		if (stroke->fFragment == 0 && stroke->fJoinsNext == 0 && gLastStrokeWasCut == 0)
		{
			Boolean betweenLetters = (wr->fWordBreak == 1);
			Boolean neither = false;
			if (!betweenLetters)
			{
				if (separation > 0x00009999)
					betweenLetters = true;
				else if (separation >= 0x00006666)
					neither = true;		// neither one thing nor the other
			}

			if (neither)
				;
			else if (betweenLetters)
			{
				// the four between-letter distributions (which, for the
				// reason in `RelearnRunSpread`, never actually move)
				RelearnRunSpread(&wr->fRun[4], gap, 0x00171999, 0x000a7851);
				RelearnRunSpread(&wr->fRun[12], gapRatio, 0x00015212, 0x0000a09d);
				RelearnRunSpread(&wr->fRun[8], gapAlong, 0x0022d1eb, 0x000b8000);
				RelearnRunSpread(&wr->fRun[16], gapAlongRatio, 0x0001f5e3, 0x0000a24d);
			}
			else
			{
				// ... and the four within-letter ones, which do
				if (gap > 0 && gap < FixedMultiply(wr->fRun[2], 0x00020000))
					LearnRunPair(&wr->fRun[2], gap, 0x00063851, 0x0003f333,
								0x0000c000, 0x00014000);
				if (gapRatio > 0 && gapRatio < FixedMultiply(wr->fRun[10], 0x00020000))
					LearnRunPair(&wr->fRun[10], gapRatio, 0x000057ce, 0x00003738,
								0x0000c000, 0x00014000);
				if (gapAlong > 0 && gapAlong < FixedMultiply(wr->fRun[6], 0x00020000))
					LearnRunPair(&wr->fRun[6], gapAlong, 0x000eb0a3, 0x000835c2,
								0x0000c000, 0x00014000);
				if (gapAlongRatio > 0 && gapAlongRatio < FixedMultiply(wr->fRun[14], 0x00020000))
					LearnRunPair(&wr->fRun[14], gapAlongRatio, 0x0000c9db, 0x0000663f,
								0x0000c000, 0x00014000);
			}
		}

		// where this stroke reached, for the next one to measure from
		if (gLastStrokeRight < bounds.right)
			gLastStrokeRight = bounds.right;
		if (gLastStrokeAdvance < advance)
			gLastStrokeAdvance = advance;
		gLastStrokeWasCut = (UByte) (stroke->fFragment | stroke->fJoinsNext);
	}
	cleanup
	{
		DestroyStrokeIfOurs(wr, stroke);
	}
	end_try;

	wr->fStrokeCount = (short) (wr->fStrokeCount + 1);
}


// ROM 0x0027627c WordRecogNetSetInputs
// The writing measured into the pattern, and the pattern written into
// the net's inputs.  All twelve geometry numbers go straight through -
// unlike `CharBoxNetSetInputs`, which passes two of its own twice
// because a box has no separate second baseline to offer.
void
WordRecogNetSetInputs(NetPattern* pattern, BPNet* net, RosStrokeList* strokes,
				Fixed base, Fixed height, Fixed arg6, Fixed altBase, Fixed altHeight,
				Fixed arg9, Fixed arg10, Fixed arg11, Fixed capHeight)
{
	const NetPatternizerType* type = pattern->fPatternizer->fType;
	type->fSLToPat(net, strokes, pattern, base, height, arg6, altBase,
				altHeight, arg9, arg10, arg11, capHeight);
	type->fSetInput(pattern);
}


// ROM 0x00276134 WordRecogNetEvaluate
// The classifier run over a piece of a word, and what it thinks of each
// of the 256 character codes left in `out`.
//
// The patternizer and its pattern are made the first time they are
// wanted and kept on the word recogniser afterwards, because a word is
// read one candidate letter at a time and there may be dozens of them.
//
// The mapping from the net's 134 outputs to the 256 codes is the same
// as `CharBoxNetEvaluate`'s, and the ROM has it written out twice: a
// code the area will not have scores nothing, a code standing for one
// shape takes its node's output widened by a shift of eight, and a code
// that is really two characters takes the product of its two parts'
// outputs - or, when both parts map to the same node, that node's
// output on its own.
void
WordRecogNetEvaluate(WordRecog* wr, BPNet* net, RosStrokeList* strokes,
				Fixed base, Fixed height, Fixed arg6, Fixed altBase, Fixed altHeight,
				Fixed arg9, Fixed arg10, Fixed arg11, Fixed capHeight,
				Fixed* out)
{
	if (wr->fPatternizer == nil)
		wr->fPatternizer = NetPatternizerCreateFromBP(net);
	if (wr->fPattern == nil)
		wr->fPattern = NetPatternCreate(wr->fPatternizer);

	WordRecogNetSetInputs(wr->fPattern, net, strokes, base, height, arg6,
					altBase, altHeight, arg9, arg10, arg11, capHeight);
	BPNetEvaluate(net);

	const UByte* outputs = net->fOutputs;
	for (long code = 0; code < 256; code++)
	{
		if ((RosCI->fLegalUse[code >> 5] & (1UL << (code & 31))) == 0)
		{
			out[code] = 0;
			continue;
		}
		UByte part1 = RosCI->fCompoundPart1[code];
		UByte node;
		if (part1 == 0)
			node = RosCI->fCharToNetNode[code];
		else
		{
			node = RosCI->fCharToNetNode[part1];
			UByte node2 = RosCI->fCharToNetNode[RosCI->fCompoundPart2[code]];
			if (node != node2)
			{
				out[code] = FixedMultiply((Fixed) ((ULong) outputs[node] << 8),
								(Fixed) ((ULong) outputs[node2] << 8));
				continue;
			}
		}
		out[code] = (Fixed) ((ULong) outputs[node] << 8);
	}
}



// One of the four lengths the engine keeps about the hand, moved an
// eighth of the way towards what the word just read says - but only
// when the word is within half to twice what it already believed, so
// that one badly written word cannot drag the whole measure away.
static void
LearnLength(Fixed* run, Fixed value)
{
	if (value < FixedMultiply(0x20000, *run)
		&& FixedMultiply(0x8000, *run) < value)
		*run = FixedMultiply(0xe000, *run) + FixedMultiply(0x2000, value);
}

// ... and then held to between half and twice its nominal, whatever it
// has learnt.
static Fixed
ClampLength(Fixed run, Fixed nominal)
{
	Fixed least = FixedMultiply(0x8000, nominal);
	Fixed most = FixedMultiply(0x20000, nominal);
	if (most < run)
		return most;
	return (least < run) ? run : least;
}


// ROM 0x002766c0 WordRecogAnalyzeWord
// A word read: the strokes are already cut into a lattice of candidate
// letters, and this is where the classifier is asked about each one and
// the search is told what it said.
//
// First the word is measured (`CharGetAvgBoxBHW`), and three of the
// four lengths the engine keeps about the writer's hand are moved an
// eighth of the way towards it and then held to between half and twice
// their nominal.  The fourth is not touched here.  Then the five
// lengths are combined - each scaled by the nominal ratio it was
// measured against - into one number for how big this word is, which
// goes to the classifier as its cap height.
//
// Then every segment in the lattice in turn: the classifier is run over
// it (`WordRecogNetEvaluate` into `fBuffer48`), `CharModifyProbs` leans
// on what it said with where the piece sits, and the result goes to the
// search with a **confidence** - the mean of how much of the line each
// of the segment's strokes shares with the one before it, capped at a
// half, so a letter written in strokes that lie on each other is
// trusted more than one written in strokes that merely follow.
void
WordRecogAnalyzeWord(WordRecog* wr)
{
	Fixed* probs = (Fixed*) wr->fBuffer48;
	Fixed* scratch = (Fixed*) wr->fBuffer4c;
	if (wr->fStrokeCount == 0)
		return;

	const ULong* params = wr->fNet->fArParams;
	Fixed base, height, width, altBase, maxHeight, maxWidth;
	CharGetAvgBoxBHW(wr->fSegments, wr->fSegmentCount, SegmentMinStrokeSize(),
					wr->fField60, (Fixed) params[0x48 / 4], (Fixed) params[0x4c / 4],
					(short) params[0x50 / 4],
					&base, &height, &width, &altBase, &maxHeight, &maxWidth);
	wr->fMeanCharHeight = height;

	LearnLength(&wr->fRun[18], height);
	LearnLength(&wr->fRun[19], maxHeight);
	LearnLength(&wr->fRun[21], width);

	wr->fRun[18] = ClampLength(wr->fRun[18],
					FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x000117d5)));
	wr->fRun[19] = ClampLength(wr->fRun[19],
					FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x00006edf)));
	wr->fRun[21] = ClampLength(wr->fRun[21],
					FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x00011c08)));

	// the five lengths brought back to one scale and averaged: 0xfcb9
	// times a fifth is what each of them is worth
	Fixed total = FixedMultiply(wr->fRun[20], 0xd36e)
				+ FixedMultiply(wr->fRun[0], kNominalRatio)
				+ FixedMultiply(wr->fRun[18], 0x000117d5)
				+ FixedMultiply(wr->fRun[19], 0x00006edf)
				+ FixedMultiply(wr->fRun[21], 0x00011c08);
	wr->fWordSize = FixedMultiply(FixedMultiply(0xfcb9, 0x3333), total);

	SearchBeginWord(wr->fContext);
	wr->fField64 = base;

	for (long i = 0; i < wr->fSegmentCount; i++)
	{
		RosSegment* seg = wr->fSegments[i];
		// how far the search may jump from here: the last grouping the
		// stroke this one ends on belongs to, counted from this one
		RosStroke* last = wr->fStrokes[seg->fFirstStroke + seg->fCount - 1];
		seg->fField04 = (short) (last->fSegment - i);

		WordRecogNetEvaluate(wr, wr->fNet, seg->fStrokes, base, height, width,
						altBase, maxHeight, maxWidth,
						wr->fRun[18], wr->fRun[19], wr->fWordSize, probs);
		CharModifyProbs(&seg->fBounds, seg->fStrokes->fCount, seg->fHasDot,
						seg->fFragment, seg->fJoinsNext,
						wr->fRun[0], wr->fRun[18], wr->fRun[19], wr->fRun[20],
						wr->fRun[21], wr->fField60, wr->fWordSize, scratch,
						altBase, maxHeight, maxWidth, probs);

		// how much of the line the segment's strokes share with each
		// other, capped at a half apiece
		long strokes = seg->fStrokes->fCount;
		Fixed confidence;
		if (strokes < 2)
			confidence = 0x00010000;
		else
		{
			Fixed shared = 0;
			for (long k = 1; k < strokes; k++)
			{
				Fixed overlap = seg->fStrokes->fStrokes[k]->fOverlap;
				if (overlap > 0x8000)
					overlap = 0x8000;
				shared += overlap;
			}
			confidence = FixedDivide(shared,
						(Fixed) (int) ((unsigned int) (strokes - 1) << 16));
		}

		SearchProcessSegment(wr->fContext, probs, scratch, i, seg, confidence,
						(Boolean) (seg->fFirstStroke + seg->fCount == wr->fStrokeCount),
						wr->fField1c);
	}

	SearchEndWord(wr->fContext, wr->fStrokeCount, WordRecogEndWord, wr,
				wr->fWords, wr->fScores, wr->fWordFlags, wr->fWordCount);
}


// Where a stroke's horizontal range had its middle before the engine
// cut it up.
Fixed
WordRecogStrokeMidX(const RosStroke* stroke)
{
	if (stroke == gXGapStroke)
		return gXGapMidX;
	if (stroke == gPrevXGapStroke)
		return gPrevXGapMidX;
	return stroke->fMidX;
}


// The weight a stroke's own number is given against what has been
// gathered so far: its place in the word, but never less than one or
// more than four - so the first few strokes pull hard and the rest a
// quarter at a time.
static long
WRSegWeight(long i)
{
	if (i < 4)
		return (i < 1) ? 1 : i;
	return 4;
}


// (i x base + w x value) / (w + i), with the sum wrapping as the ARM's
// thirty-two bits do before the signed divide.
static Fixed
WRSegMean(Fixed base, long i, Fixed value, long w)
{
	unsigned int sum = (unsigned int) i * (unsigned int) base
					+ (unsigned int) w * (unsigned int) value;
	return (Fixed) ((int) sum / (int) (w + i));
}


// ROM 0x00274244 WRSegWordXGap
// The gap before a stroke, measured against the strokes of the word
// that come before it along the line.
//
// The reference is built stroke by stroke out of every stroke whose
// middle lies to the left of this one's: the box round them, the
// rightmost middle, a running middle height and a **body band** - the
// top and the bottom of the writing without its ascenders and
// descenders - each pulled a little further towards each stroke in
// turn (`WRSegWeight`).  A stroke too short to say anything and too
// far from the running middle is left out of the middle altogether,
// which is how a dot or a crossing is kept from moving it.
//
// ROM BUG (fixed): the reference's top and bottom are not gathered at
// all.  Each stroke sets them afresh, as the smaller of its own top and
// the *leftmost x so far* and the greater of its own bottom and the
// *rightmost x so far* - an x compared with a y, both being pixels on
// the one tablet - so what reaches `SegmentWordXGap` is the last
// stroke's, bent by where the word begins and ends.  Ported as it
// stands; the fix gathers them, the smallest top and the greatest bottom.
Boolean
WRSegWordXGap(RosStroke* stroke, const SegWordInk* ink, WordRecog* wr, Fixed* strength)
{
	Fixed mid = WordRecogStrokeMidX(stroke);
	long nearest = -1;

	FPoint centre;
	StrokeCentroid(wr->fStrokes[0], &centre);
	const FRect* first = &wr->fStrokes[0]->fBounds;
	Fixed bodyTop = first->top;
	Fixed right = first->right;
	Fixed left = first->left;
	Fixed bodyBottom = first->bottom;
	Fixed rightmostX = centre.x;
	Fixed top = bodyTop;
	Fixed bottom = bodyBottom;
	Fixed middleY = centre.y;

	for (long i = 0; i < wr->fWordStrokes; i++)
	{
		RosStroke* s = wr->fStrokes[i];
		if (!(WordRecogStrokeMidX(s) < mid))
			continue;

		// the nearest stroke before this one along the line
		if (nearest < 0
			|| WordRecogStrokeMidX(wr->fStrokes[nearest]) < WordRecogStrokeMidX(s))
			nearest = i;

		StrokeCentroid(s, &centre);
		const FRect* b = &wr->fStrokes[i]->fBounds;
		if (b->left <= left)
			left = b->left;
		if (right < b->right)
			right = b->right;
		if (RomBugFixed())
		{
			top = (top < b->top) ? top : b->top;
			bottom = (bottom < b->bottom) ? b->bottom : bottom;
		}
		else
		{
			top = (left < b->top) ? left : b->top;
			bottom = (right < b->bottom) ? b->bottom : right;
		}
		if (rightmostX < centre.x)
			rightmostX = centre.x;

		Fixed height = b->bottom - b->top;
		Fixed quarter = (bodyBottom - bodyTop) >> 2;
		Fixed gate = quarter;
		if (quarter <= FixedMultiply(0x00018000, SegmentMinStrokeSize()))
			gate = FixedMultiply(0x00018000, SegmentMinStrokeSize());

		// the body band's top, pulled towards this stroke's
		if (gate <= height || wr->fStrokes[i]->fBounds.top <= middleY - quarter)
		{
			Fixed base = (middleY < bodyTop) ? middleY : bodyTop;
			long w = WRSegWeight(i);
			bodyTop = WRSegMean(base, i, wr->fStrokes[i]->fBounds.top, w);
		}

		gate = quarter;
		if (quarter <= FixedMultiply(0x00018000, SegmentMinStrokeSize()))
			gate = FixedMultiply(0x00018000, SegmentMinStrokeSize());
		// ... and its bottom
		if (gate <= height || middleY + quarter <= wr->fStrokes[i]->fBounds.bottom)
		{
			Fixed base = (bodyBottom < middleY) ? middleY : bodyBottom;
			long w = WRSegWeight(i);
			bodyBottom = WRSegMean(base, i, wr->fStrokes[i]->fBounds.bottom, w);
		}

		gate = quarter;
		if (quarter <= FixedMultiply(0x00018000, SegmentMinStrokeSize()))
			gate = FixedMultiply(0x00018000, SegmentMinStrokeSize());
		// a short stroke far from the middle - a dot, a crossing - is
		// left out of the middle
		if (height < gate)
		{
			Fixed away = centre.y - middleY;
			abs_temp = away;
			if (away < 0)
				away = -away;
			if (quarter < away)
				continue;
		}
		long w = WRSegWeight(i);
		middleY = WRSegMean(middleY, i, centre.y, w);
	}

	if (nearest < 0)
	{
		// nothing before it on the line: a new word
		*strength = 0x00010000;
		return true;
	}

	// the reference, whose size is the nearest stroke's
	const FRect* n = &wr->fStrokes[nearest]->fBounds;
	Fixed width = n->right - n->left;
	Fixed height = n->bottom - n->top;
	Fixed size = (width < height) ? height : width;

	SegWordRef ref;
	ref.fInk.fLeft = left;
	ref.fInk.fRight = right;
	ref.fInk.fTop = top;
	ref.fInk.fBottom = bottom;
	ref.fInk.fCentroidX = rightmostX;
	ref.fInk.fCentroidY = middleY;
	ref.fInk.fHeight = height + 0x00010000;
	ref.fInk.fSizeMax = size + 0x00010000;
	ref.fBodyTop = bodyTop;
	ref.fBodyBottom = bodyBottom;
	ref.fStrokes = wr->fWordStrokes;
	return SegmentWordXGap(ink, &ref, wr->fField60, wr->fWordSize, wr->fRun, strength);
}


/*--------------------------------------------------------------------
	The strokes in.
--------------------------------------------------------------------*/

// The eight numbers the word spacing judges a stroke by.
static void
WRInkOf(SegWordInk* ink, const RosStroke* stroke, const FPoint* centre)
{
	ink->fLeft = stroke->fBounds.left;
	ink->fRight = stroke->fBounds.right;
	ink->fTop = stroke->fBounds.top;
	ink->fBottom = stroke->fBounds.bottom;
	ink->fCentroidX = centre->x;
	ink->fCentroidY = centre->y;
	Fixed width = ink->fRight - ink->fLeft;
	Fixed height = ink->fBottom - ink->fTop;
	ink->fSizeMax = ((width < height) ? height : width) + 0x00010000;
	ink->fHeight = height + 0x00010000;
}


// The word so far as a reference - its box, its middle and its body
// band - and the word as it was before its last stroke (the second of
// each pair, which is kept for exactly this).
static void
WRWordRef(SegWordRef* ref, const WordRecog* wr, Boolean saved)
{
	long k = saved ? 1 : 0;
	ref->fInk.fLeft = wr->fWordLeft[k];
	ref->fInk.fRight = wr->fWordRight[k];
	ref->fInk.fTop = wr->fWordTop[k];
	ref->fInk.fBottom = wr->fWordBottom[k];
	ref->fInk.fCentroidX = wr->fWordCentroidX[k];
	ref->fInk.fCentroidY = wr->fWordCentroidY[k];
	ref->fInk.fHeight = wr->fWordHeight;
	ref->fInk.fSizeMax = wr->fWordSizeMax;
	ref->fBodyTop = wr->fWordBodyTop[k];
	ref->fBodyBottom = wr->fWordBodyBottom[k];
	ref->fStrokes = saved ? wr->fWordStrokes - 1 : wr->fWordStrokes;
}


// ... and the stroke taken in last, whose body band is its own top and
// bottom.
static void
WRLastRef(SegWordRef* ref, const WordRecog* wr)
{
	ref->fInk.fLeft = wr->fLastLeft;
	ref->fInk.fRight = wr->fLastRight;
	ref->fInk.fTop = wr->fLastTop;
	ref->fInk.fBottom = wr->fLastBottom;
	ref->fInk.fCentroidX = wr->fLastCentroidX;
	ref->fInk.fCentroidY = wr->fLastCentroidY;
	ref->fInk.fHeight = wr->fLastHeight;
	ref->fInk.fSizeMax = wr->fLastSizeMax;
	ref->fBodyTop = wr->fLastTop;
	ref->fBodyBottom = wr->fLastBottom;
	ref->fStrokes = 1;
}


// The word started afresh from the last stroke, or from the stroke in
// hand; the sizes go with the first but not always with the second, as
// the ROM has it.
static void
WRWordFromLast(WordRecog* wr)
{
	wr->fWordLeft[0] = wr->fLastLeft;
	wr->fWordRight[0] = wr->fLastRight;
	wr->fWordTop[0] = wr->fLastTop;
	wr->fWordBottom[0] = wr->fLastBottom;
	wr->fWordBodyBottom[0] = wr->fLastBottom;
	wr->fWordBodyTop[0] = wr->fLastTop;
	wr->fWordCentroidX[0] = wr->fLastCentroidX;
	wr->fWordCentroidY[0] = wr->fLastCentroidY;
	wr->fWordSizeMax = wr->fLastSizeMax;
	wr->fWordHeight = wr->fLastHeight;
}

static void
WRWordFromInk(WordRecog* wr, const SegWordInk* ink, Boolean sizesToo)
{
	wr->fWordLeft[0] = ink->fLeft;
	wr->fWordRight[0] = ink->fRight;
	wr->fWordBottom[0] = ink->fBottom;
	wr->fWordTop[0] = ink->fTop;
	wr->fWordCentroidX[0] = ink->fCentroidX;
	wr->fWordBodyBottom[0] = ink->fBottom;
	wr->fWordBodyTop[0] = ink->fTop;
	wr->fWordCentroidY[0] = ink->fCentroidY;
	if (sizesToo)
	{
		wr->fWordSizeMax = ink->fSizeMax;
		wr->fWordHeight = ink->fHeight;
	}
}


// The word as it stands kept as it was, for the next stroke to be
// judged against if that one goes back.
static void
WRSaveWord(WordRecog* wr)
{
	wr->fWordLeft[1] = wr->fWordLeft[0];
	wr->fWordRight[1] = wr->fWordRight[0];
	wr->fWordTop[1] = wr->fWordTop[0];
	wr->fWordBottom[1] = wr->fWordBottom[0];
	wr->fWordCentroidX[1] = wr->fWordCentroidX[0];
	wr->fWordBodyTop[1] = wr->fWordBodyTop[0];
	wr->fWordBodyBottom[1] = wr->fWordBodyBottom[0];
	wr->fWordCentroidY[1] = wr->fWordCentroidY[0];
}


// A stroke folded into the word's body band and middle height - the same
// weighted means `WRSegWordXGap` uses, each pulled a little further
// towards the stroke the more strokes the word already has, and each
// left alone when the stroke is short and lies outside the band.  The
// band's height is taken once, before anything moves.
static void
WRFoldBand(const WordRecog* wr, Fixed top, Fixed bottom, Fixed middleOfStroke,
				Fixed height, Fixed* bodyTop, Fixed* bodyBottom, Fixed* middle)
{
	Fixed band = *bodyBottom - *bodyTop;
	Fixed quarter = band >> 2;
	long n = wr->fWordStrokes;
	long w = WRSegWeight(n);

	Fixed least = FixedMultiply(0x00018000, SegmentMinStrokeSize());
	Fixed gate = (least < quarter) ? quarter : least;
	Fixed newTop;
	if (gate > height && *middle - quarter < top)
		newTop = *bodyTop;
	else
		newTop = WRSegMean((*middle >= *bodyTop) ? *bodyTop : *middle, n, top, w);

	least = FixedMultiply(0x00018000, SegmentMinStrokeSize());
	gate = (least < quarter) ? quarter : least;
	Fixed newBottom;
	if (gate > height && *middle + quarter > bottom)
		newBottom = *bodyBottom;
	else
		newBottom = WRSegMean((*middle > *bodyBottom) ? *middle : *bodyBottom, n, bottom, w);

	least = FixedMultiply(0x00018000, SegmentMinStrokeSize());
	gate = (least < quarter) ? quarter : least;
	Fixed newMiddle;
	Boolean keep = false;
	if (gate > height)
	{
		Fixed away = middleOfStroke - *middle;
		abs_temp = away;
		if (away < 0)
			away = -away;
		keep = (away > quarter);
	}
	newMiddle = keep ? *middle : WRSegMean(*middle, n, middleOfStroke, w);

	*bodyTop = newTop;
	*bodyBottom = newBottom;
	*middle = newMiddle;
}


// ... and the box grown round it.
static void
WRGrowWord(WordRecog* wr, Fixed left, Fixed right, Fixed top, Fixed bottom, Fixed cx)
{
	if (left <= wr->fWordLeft[0])
		wr->fWordLeft[0] = left;
	if (wr->fWordRight[0] < right)
		wr->fWordRight[0] = right;
	if (top <= wr->fWordTop[0])
		wr->fWordTop[0] = top;
	if (wr->fWordBottom[0] < bottom)
		wr->fWordBottom[0] = bottom;
	if (wr->fWordCentroidX[0] < cx)
		wr->fWordCentroidX[0] = cx;
}


// The strokes before this one that lie further right than it does, or
// that run on into the next, walked back over: each has its separation
// cleared, because the gap before it no longer means what it did.
// Answers the last one it passed over - or, when `track` is given, the
// one whose middle lies furthest left of those, starting from `track`
// itself - and `stop` when it passed over none.
static long
WRWalkBack(WordRecog* wr, RosStroke* stroke, long start, Fixed* track, long stop)
{
	Fixed mid = WordRecogStrokeMidX(stroke);
	for (long i = start; i >= 0; i--)
	{
		RosStroke* s = wr->fStrokes[i];
		if (WordRecogStrokeMidX(s) <= mid && s->fJoinsNext == 0)
			break;
		if (track == nil)
			stop = i;
		else if (WordRecogStrokeMidX(s) < *track)
		{
			*track = WordRecogStrokeMidX(s);
			stop = i;
		}
		s->fSeparation = 0;
	}
	return stop;
}


// ROM 0x00272728 WordRecogAddStroke
// A stroke taken into the word being gathered - or the word closed,
// when there is no stroke or `endWord` is more than one.
//
// This is where the engine decides, stroke by stroke, where one word
// ends and the next begins, and it is careful about it.  A stroke that
// the spacing says *may* begin a new word (`SegmentWord` answering 1,
// the gap was wide) is not acted on at once: it is held back as
// `fPendingStroke`, with the separation and the `how` it will be taken
// in with, and the *next* stroke decides - if it belongs with the one
// held back, the two join the word together; if it does not, the word
// is closed before the held stroke.  A stroke that went back or down a
// line (2 or 3) closes the word straight away.
//
// A stroke that lies to the left of the last one - a dot, a crossing,
// a letter corrected - walks back over the strokes it now comes before,
// clearing their separations, and is measured against the word as it
// stood before them (`WRSegWordXGap`).
//
// Joined-up writing is cut into letters first (`FragmentStroke`, when
// `FragmentLigatures` is set and the stroke is wide enough), and each
// piece is taken in by calling this again; a stroke is scaled to
// seventy-two dots to the inch before anything is measured, unless it
// is a piece the engine made itself.  The recogniser is closed and a new
// word begun when a hundred and fifty strokes have gathered.
//
// `endWord` and `how` are passed down to `WordRecogAddStroke2`; a 2 in
// either is turned into a 1 once it has done its work.
void
WordRecogAddStroke(WordRecog* wr, RosStroke* stroke, short endWord, short how)
{
	// DEVIATION: the ROM reads `fOwnsStrokes` before it checks for a
	// nil recogniser - harmless on the Newton, where address 0x38 is
	// readable, and a fault on a host.
	if (wr == nil)
		return;

	// the stroke to give back if anything below throws
	RosStroke* volatile owned = nil;
	if (wr->fOwnsStrokes != 0
		|| (stroke != nil && (stroke->fFragment != 0 || stroke->fJoinsNext != 0)))
		owned = stroke;

	if (wr->fSuspended != 0)
	{
		newton_try
		{
			WordRecogResume(wr);
		}
		newton_catch_all
		{
			StrokeDestroy(owned);
			rethrow;
		}
		end_try;
	}

	SegOnly = wr->fClassifyMode;
	const Fixed* run = wr->fRun;

	SegWordInk ink;
	memset(&ink, 0, sizeof(ink));
	if (stroke != nil)
	{
		// too many strokes: close the word first
		long room = 0x96 - ((wr->fPendingStroke != nil) ? 1 : 0);
		if (wr->fWordStrokes >= room)
		{
			newton_try
			{
				WordRecogAddStroke(wr, nil, (short) (endWord != 0 ? 2 : 0),
								(short) (how != 0 ? 2 : 0));
			}
			newton_catch_all
			{
				StrokeDestroy(owned);
				rethrow;
			}
			end_try;
		}

		// seventy-two dots to the inch, unless the engine made the
		// stroke itself
		if (stroke->fFragment == 0 && stroke->fJoinsNext == 0)
		{
			Fixed xScale, yScale;
			Boolean scale = true;
			if (wr->fResX > 0 && wr->fResY > 0 && !(wr->fResX == 0x48 && wr->fResY == 0x48))
			{
				xScale = FixedDivide(0x00480000, (Fixed) (int) ((unsigned int) wr->fResX << 16));
				yScale = FixedDivide(0x00480000, (Fixed) (int) ((unsigned int) wr->fResY << 16));
			}
			else
			{
				xScale = yScale = wr->fField2c;
				scale = (wr->fField2c != 0x00010000);
			}
			if (scale)
				StrokeScale(stroke, xScale, yScale);
		}

		FPoint centre;
		StrokeCentroid(stroke, &centre);
		WRInkOf(&ink, stroke, &centre);

		// joined-up writing cut into letters, each taken in in turn
		if (stroke->fFragment == 0 && stroke->fJoinsNext == 0)
		{
			volatile Boolean handled = false;
			newton_try
			{
				if (FragmentLigatures != 0 && WordRecogStrokeNeedsFragmenting(wr, stroke))
				{
					RosStrokeList* pieces = FragmentStroke(stroke, run);
					newton_try
					{
						if (pieces->fCount > 1)
						{
							if (gXGapStroke != nil)
								gPrevXGapMidX = gXGapMidX;
							else
								gPrevXGapMidX = 0;
							gPrevXGapStroke = gXGapStroke;
							gXGapMidX = stroke->fMidX;
							StrokeDestroy(owned);
							owned = nil;
							for (long i = 0; i < pieces->fCount; i++)
							{
								gXGapStroke = pieces->fStrokes[i];
								pieces->fStrokes[i] = nil;
								WordRecogAddStroke(wr, gXGapStroke, endWord, how);
								if (endWord == 2)
									endWord = 1;
								if (how == 2)
									how = 1;
							}
							handled = true;
						}
					}
					newton_catch_all
					{
						SLDestroy(pieces, 1);
						rethrow;
					}
					end_try;
					SLDestroy(pieces, 1);
				}
			}
			newton_catch_all
			{
				StrokeDestroy(owned);
				rethrow;
			}
			end_try;
			if (handled)
				return;
		}
	}

	/*----------------------------------------------------------------
		The first stroke of a word.
	----------------------------------------------------------------*/
	if (wr->fWordStrokes == 0)
	{
		WordRecogSaveRun(wr);
		wr->fWordBreak = 0;
		if (stroke != nil)
		{
			WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY, 0, how, 0);
			WRWordFromInk(wr, &ink, true);
			wr->fLastMidX = WordRecogStrokeMidX(stroke);
			wr->fPendingStroke = nil;
			wr->fWordStrokes = 1;
		}
		return;
	}

	/*----------------------------------------------------------------
		The word closed.
	----------------------------------------------------------------*/
	if (stroke == nil || endWord > 1)
	{
		if (wr->fPendingStroke != nil)
		{
			newton_try
			{
				RosStroke* held = wr->fPendingStroke;
				wr->fPendingStroke = nil;
				WordRecogAddStroke2(wr, held, wr->fLastCentroidX, wr->fLastCentroidY,
								1, wr->fPendingHow, wr->fPendingSeparation);
			}
			newton_catch_all
			{
				StrokeDestroy(owned);
				rethrow;
			}
			end_try;
		}
		wr->fWordBreak = -1;
		WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY, 2, how, 0x00010000);
		if (stroke == nil)
			wr->fWordStrokes = 0;
		else
		{
			WRWordFromInk(wr, &ink, true);
			wr->fLastMidX = WordRecogStrokeMidX(stroke);
			wr->fWordStrokes = 1;
			wr->fWordBreak = 0;
		}
		wr->fPendingStroke = nil;
		return;
	}

	/*----------------------------------------------------------------
		A stroke added to the word, or beginning another.
	----------------------------------------------------------------*/
	newton_try
	{
		Boolean defer = false;		// hold this stroke back
		Boolean walked = false;		// the saved word is not to be refreshed
		Fixed strength = 0;			// what the spacing said (S1)
		Fixed strength2 = 0;		// ... and the other test (S2)
		Boolean takeIn = false;		// taken in as part of the word
		SegWordRef ref;

		if (wr->fPendingStroke != nil)
		{
			// A stroke is held back; this one decides.
			long answer = 0;
			Boolean againstWord = true;
			if (stroke->fFragment == 0)
			{
				WRLastRef(&ref, wr);
				answer = SegmentWord(&ink, &ref, wr->fField60, wr->fWordSize, run, &strength);
				againstWord = (answer == 0);
			}
			else
				strength = 0;

			if (!againstWord)
			{
				// the held stroke did begin a word of its own
				RosStroke* held = wr->fPendingStroke;
				wr->fPendingStroke = nil;
				WordRecogAddStroke2(wr, held, wr->fLastCentroidX, wr->fLastCentroidY,
								1, wr->fPendingHow, wr->fPendingSeparation);
				wr->fWordBreak = answer;
				wr->fWordStrokes = 1;
				if (answer == kSegWordWentBack || answer == kSegWordWentDown)
				{
					// ... and so does this one
					owned = nil;
					WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY,
									1, how, 0x00010000);
					WRWordFromInk(wr, &ink, true);
					wr->fWordStrokes = 0;
				}
				else
				{
					defer = true;
					WRWordFromLast(wr);
				}
				WRSaveWord(wr);
			}
			else
			{
				// measured against the whole word instead
				WRWordRef(&ref, wr, false);
				answer = SegmentWord(&ink, &ref, wr->fField60, wr->fWordSize, run, &strength2);
				if (answer != 0)
				{
					// the held stroke began a word, and this one is in it
					if (WordRecogStrokeMidX(stroke) < WordRecogStrokeMidX(wr->fPendingStroke))
					{
						strength = strength2;
						wr->fPendingSeparation = 0;
					}
					RosStroke* held = wr->fPendingStroke;
					wr->fPendingStroke = nil;
					Fixed sep = wr->fPendingSeparation;
					if (sep > strength2)
						sep = strength2;
					WordRecogAddStroke2(wr, held, wr->fLastCentroidX, wr->fLastCentroidY,
									1, wr->fPendingHow, sep);
					WRWordFromLast(wr);
					wr->fWordStrokes = 1;
					wr->fWordBreak = 0;
					takeIn = true;
				}
				else
				{
					wr->fWordBreak = 0;
					if (WordRecogStrokeMidX(stroke) < WordRecogStrokeMidX(wr->fPendingStroke))
					{
						// this one went back over the word
						Fixed track = WordRecogStrokeMidX(wr->fPendingStroke);
						walked = true;
						wr->fPendingSeparation = 0;
						long stop = WRWalkBack(wr, stroke, wr->fWordStrokes - 1, &track,
										wr->fWordStrokes);
						if (wr->fWordStrokes == stop)
							strength = strength2;
						else
							WRSegWordXGap(stroke, &ink, wr, &strength);
					}
					else
					{
						// the held stroke and this one measured
						// together against the word
						wr->fPendingSeparation = strength2;
						SegWordRef both;
						both.fInk.fLeft = (wr->fWordLeft[0] >= wr->fLastLeft)
									? wr->fLastLeft : wr->fWordLeft[0];
						both.fInk.fRight = (wr->fWordRight[0] < wr->fLastRight)
									? wr->fLastRight : wr->fWordRight[0];
						both.fInk.fTop = (wr->fWordTop[0] >= wr->fLastTop)
									? wr->fLastTop : wr->fWordTop[0];
						both.fInk.fBottom = (wr->fWordBottom[0] < wr->fLastBottom)
									? wr->fLastBottom : wr->fWordBottom[0];
						both.fInk.fCentroidX = (wr->fWordCentroidX[0] >= wr->fLastCentroidX)
									? wr->fWordCentroidX[0] : wr->fLastCentroidX;
						both.fBodyTop = wr->fWordBodyTop[0];
						both.fBodyBottom = wr->fWordBodyBottom[0];
						both.fInk.fCentroidY = wr->fWordCentroidY[0];
						WRFoldBand(wr, wr->fLastTop, wr->fLastBottom, wr->fLastCentroidY,
									wr->fLastBottom - wr->fLastTop,
									&both.fBodyTop, &both.fBodyBottom, &both.fInk.fCentroidY);
						both.fInk.fHeight = wr->fWordHeight;
						both.fInk.fSizeMax = wr->fWordSizeMax;
						both.fStrokes = wr->fWordStrokes + 1;
						if (stroke->fFragment == 0)
							SegmentWord(&ink, &both, wr->fField60, wr->fWordSize, run, &strength);
						else
							strength = 0;
					}

					// the held stroke joins the word
					RosStroke* held = wr->fPendingStroke;
					wr->fPendingStroke = nil;
					WordRecogAddStroke2(wr, held, wr->fLastCentroidX, wr->fLastCentroidY,
									0, wr->fPendingHow, wr->fPendingSeparation);
					WRGrowWord(wr, wr->fLastLeft, wr->fLastRight, wr->fLastTop,
								wr->fLastBottom, wr->fLastCentroidX);
					WRFoldBand(wr, wr->fLastTop, wr->fLastBottom, wr->fLastCentroidY,
								wr->fLastBottom - wr->fLastTop,
								&wr->fWordBodyTop[0], &wr->fWordBodyBottom[0],
								&wr->fWordCentroidY[0]);
					wr->fWordStrokes++;
					takeIn = true;
				}
			}
			if (takeIn)
			{
				owned = nil;
				WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY, 0, how, strength);
			}
		}
		else
		{
			// Nothing held back.
			Boolean decided = false;	// fWordBreak is to be acted on
			if (endWord != 0)
			{
				wr->fWordBreak = 0;
				if (WordRecogStrokeMidX(stroke) >= wr->fLastMidX)
				{
					strength = 0;
					decided = true;
				}
				else
				{
					walked = true;
					if (wr->fWordStrokes <= 0)
						decided = false;
					else
					{
						long last = WRWalkBack(wr, stroke, wr->fWordStrokes - 1, nil,
										wr->fWordStrokes - 1);
						strength = (last == 0) ? 0x00010000 : 0;
						decided = true;
					}
				}
			}
			else
			{
				if (stroke->fFragment == 0)
				{
					WRWordRef(&ref, wr, false);
					wr->fWordBreak = SegmentWordBkVt(&ink, &ref, wr->fField60,
									wr->fWordSize, run, &strength);
				}
				else
				{
					wr->fWordBreak = 0;
					strength = 0;
				}

				if (wr->fWordBreak != 0)
					decided = true;
				else if (WordRecogStrokeMidX(stroke) < wr->fLastMidX)
				{
					// this one went back over the word
					long n = wr->fWordStrokes;
					Fixed track = WordRecogStrokeMidX(wr->fStrokes[n - 1]) + 0x00010000;
					walked = true;
					long stop = WRWalkBack(wr, stroke, n - 1, &track, n - 1);
					if (n == 1)
						strength = 0;
					else
					{
						WRWordRef(&ref, wr, true);
						SegmentWordXGap(&ink, &ref, wr->fField60, wr->fWordSize, run, &strength);
						if (n - 1 != stop)
							WRSegWordXGap(stroke, &ink, wr, &strength);
					}
					wr->fWordBreak = 0;
					decided = false;
				}
				else
				{
					long n = wr->fWordStrokes;
					if (n > 1 && WordRecogStrokeMidX(wr->fStrokes[n - 1])
									> WordRecogStrokeMidX(wr->fStrokes[n - 2]))
					{
						// the last stroke measured again against the
						// word before it, and its separation lowered to
						// match if that says less
						WRWordRef(&ref, wr, true);
						SegmentWordXGap(&ink, &ref, wr->fField60, wr->fWordSize, run, &strength);
						if (wr->fStrokes[n - 1]->fSeparation > strength)
							wr->fStrokes[n - 1]->fSeparation = strength;
					}
					if (stroke->fFragment != 0)
					{
						wr->fWordBreak = 0;
						strength = 0;
					}
					else
					{
						WRWordRef(&ref, wr, false);
						wr->fWordBreak = SegmentWordXGap(&ink, &ref, wr->fField60,
										wr->fWordSize, run, &strength) ? kSegWordWideGap : kSegWordSame;
					}
					decided = true;
				}
			}

			if (decided && wr->fWordBreak != 0)
			{
				if (wr->fWordBreak == kSegWordWideGap)
					defer = true;
				else
				{
					// went back or down: the word ends before this one
					owned = nil;
					WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY,
									1, how, strength);
					wr->fPendingStroke = nil;
					wr->fWordStrokes = 0;
					WRWordFromInk(wr, &ink, false);
				}
			}
			else
			{
				owned = nil;
				WordRecogAddStroke2(wr, stroke, ink.fCentroidX, ink.fCentroidY, 0, how, strength);
			}
		}

		/*------------------------------------------------------------
			The stroke becomes the last one.
		------------------------------------------------------------*/
		wr->fLastLeft = ink.fLeft;
		wr->fLastRight = ink.fRight;
		wr->fLastTop = ink.fTop;
		wr->fLastBottom = ink.fBottom;
		wr->fLastCentroidX = ink.fCentroidX;
		wr->fLastCentroidY = ink.fCentroidY;
		wr->fLastSizeMax = ink.fSizeMax;
		wr->fLastHeight = ink.fHeight;
		wr->fLastMidX = WordRecogStrokeMidX(stroke);

		if (defer)
		{
			// held back for the next stroke to decide about
			wr->fPendingHow = how;
			wr->fPendingSeparation = strength;
			wr->fPendingStroke = stroke;
		}
		else
		{
			if (!walked)
				WRSaveWord(wr);
			WRGrowWord(wr, ink.fLeft, ink.fRight, ink.fTop, ink.fBottom, ink.fCentroidX);
			WRFoldBand(wr, ink.fTop, ink.fBottom, ink.fCentroidY, ink.fBottom - ink.fTop,
						&wr->fWordBodyTop[0], &wr->fWordBodyBottom[0], &wr->fWordCentroidY[0]);
			wr->fWordSizeMax = ink.fSizeMax;
			wr->fWordHeight = ink.fHeight;
			wr->fPendingStroke = nil;
			wr->fWordStrokes++;
		}
	}
	newton_catch_all
	{
		StrokeDestroy(owned);
		rethrow;
	}
	end_try;
}
