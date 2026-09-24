/*
	File:		recognition/WordRecog.cpp

	Contains:	The handwriting engine's word recogniser - see
				WordRecog.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "WordRecog.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"

#include <string.h>


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
		wr->fStrokes = (RosStroke**) RosAllocate(kWordRecogMaxStrokes * 4);
		wr->fSegments = (RosSegment**) RosAllocate(kWordRecogMaxSegments * 4);
		wr->fWords = (char**) RosAllocate(wr->fWordCount * 4);
		wr->fScores = (UniChar*) RosAllocate(wr->fWordCount * 2);
		wr->fWordFlags = (long*) RosAllocate(wr->fWordCount * 4);
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
				RosGrammars* grammars, void* net, short ownsStrokes)
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
	wr->fField120[1] = 0x00010000;
	wr->fField120[3] = (Fixed) 0xffff0000;
	wr->fField120[5] = 0x00010000;
	wr->fField120[7] = (Fixed) 0xffff0000;
	wr->fField120[9] = (Fixed) 0xffff0000;
	wr->fField120[11] = (Fixed) 0xffff0000;
	wr->fField120[13] = 0;
	wr->fField120[15] = 0;
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
	wr->fField120[0] = FixedMultiply(kNominalHeight, FixedDivide(kNominalRatio, 0x0000fcb9));

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
	wr->fField1a4 = 0;

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
	wr->fField1ac = 0;
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
// The word just read says how tall a capital is: the characters it is
// made of have nominal widths of their own, so the width the writing
// actually took, divided by the average of them, is what the hand's
// cap height must be.  It is not believed outright - it is an eighth
// of the answer and the cap height that was there is the other seven -
// and it is thrown away entirely if it is smaller than the engine will
// credit or more than two and a half times what it had.
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
		sum += RosCI->fCharInfo->fWidths[(UByte) word[i]];
		i++;
	}

	// (the average is an ordinary integer divide: the widths are 16.16
	//  and the count is a count, so what comes out is 16.16 again)
	Fixed estimate = FixedDivide(wr->fWordWidth, sum / (Fixed) i);
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
WordRecogEndWord(WordRecog* wr, char** words, UniChar* scores, ULong unused,
				long strokes, long count)
{
	if (count > 0)
		WordRecogComputeCapHeight(wr);
	WordRecogReturnWords(wr, words, scores, unused, strokes, count);
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
WordRecogReturnWords(WordRecog* wr, char** words, UniChar* scores, ULong unused,
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
	wr->fCheckWords(words, scores, unused, (ULong) strokes, (ULong) count);
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
	if (height < (width >> 2) && height < (wr->fField120[0] >> 2))
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
	Fixed measured = (wr->fField68 * 3 + WordRecogDetermineMaxHeight(wr)) >> 2;
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

	for (long i = 0; i < wr->fField1ac; i++)
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
