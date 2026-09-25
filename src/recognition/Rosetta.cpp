/*
	File:		recognition/Rosetta.cpp

	Contains:	The engine's side of the seam - see Rosetta.h.

	The engine's **life** is reconstructed: waking, quietening and
	sleeping, and the small calls that go with them.  There is one word
	recogniser (`gWordRecog`) and it is made when the engine wakes and
	destroyed when it sleeps; between those, `RosettaQuiesce` hands its
	arrays back without moving the block and `RosettaAwaken` is what
	makes it again.

	The engine **reads**.  `RosettaClassify` takes a stroke's points,
	cleans the stroke up and hands the pieces to the word recogniser
	(`WordRecogAddStroke`), which decides where the words are, cuts
	joined-up writing into letters, and - when a word is closed - cuts
	it into candidate letters, classifies each, and searches the
	lattice against the grammar and the ROM's own lexicons.  The
	readings come back through `RosettaCheckWords`, which turns their
	scores into how sure the engine is out of a thousand and hands them
	to the Newton.  `recognition/tests/test_Reading.cpp` writes words
	with a pen and reads them.

	The work below this file, in the order it wants doing, is in
	`docs/recognition/README.md` under "The Rosetta engine".

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "Rosetta.h"
#include "RosEngine.h"
#include "Segment.h"
#include "CharBox.h"
#include "Search.h"
#include "GeoContext.h"
#include "ROMDictionaryData.h"
#include "RosStrokes.h"
#include "WordRecog.h"
#include "FixedMath.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "Fragment.h"
#include "OSErrors.h"

#include <string.h>
#include <stdio.h>


Boolean
RosettaEngineIsReconstructed(void)
{
	return true;
}


// ROM 0x0c101960 (unnamed)
// The one word recogniser.  It sits in the ROM's data just past
// `gRosCallBack` and has no symbol of its own.
WordRecog*	gWordRecog = nil;
// ROM 0x0c101964 (unnamed)
// How sure the engine was of the last word it read, out of a thousand.
// The view system asks it: a word read badly enough leaves its hilite
// up so the writer can correct it (`TView::RemoveHilite`).
short		gRosLastConfidence = 0;

// ROM 0x0c101954 gRosResX
long		gRosResX = 0;
// ROM 0x0c101958 gRosResY
long		gRosResY = 0;
// ROM 0x0c10195c gRosCallBack
RosettaCheckWordsProc	gRosCallBack = nil;


#pragma mark -
/*--------------------------------------------------------------------
	Waking, quietening and sleeping.
--------------------------------------------------------------------*/

// ROM 0x001b810c RosettaInitialize
// The tablet's resolution and the callback written down, and the
// engine woken.  The recogniser calls this once.
NewtonErr
RosettaInitialize(long xScale, long yScale, RosettaCheckWordsProc proc)
{
	gWordRecog = nil;
	gRosResX = xScale;
	gRosResY = yScale;
	gRosCallBack = proc;
	RosettaAwaken();
	return noErr;
}


// ROM 0x001b819c RosettaAwaken
// Everything the engine needs, in order: the common info out of the
// ROM, a classifier with as many outputs as there are character
// classes, and the word recogniser over both - ten readings, the ROM's
// own grammar, and the strokes are the engine's to free.  A throw
// anywhere in that gives the classifier back before it goes on.
//
// Waking twice is nothing: the word recogniser standing is what says
// the engine is awake.
NewtonErr
RosettaAwaken(void)
{
	if (gWordRecog != nil)
		return noErr;

	long outputs = CharInitialize(0);
	BPNet* net = nil;
	newton_try
	{
		net = BPNetCreateNumOut(outputs);
		BPNetLearnEnable(net, 0);
		BPNetLoad(net, nil);
		gWordRecog = WordRecogCreate2(nil, nil, RosettaCheckWords, 10,
									kROMGrammars, net, 1);
		gWordRecog->fCallBack = (void*) gRosCallBack;
		gWordRecog->fCharBox = nil;
	}
	cleanup
	{
		BPNetDestroy(net);
	}
	end_try;

	gWordRecog->fResX = (short) (gRosResX >> 16);
	gWordRecog->fResY = (short) (gRosResY >> 16);
	gWordRecog->fClassifyMode = kRosettaClassifyNormally;
	gWordRecog->fCharBox = nil;
	gWordRecog->fCharBoxRect.top = -1;
	for (long i = 0; i < 6; i++)
		gWordRecog->fDicts[i] = nil;
	gWordRecog->fContextIndex = 0;
	RosettaInitializeValues();
	return noErr;
}


// ROM 0x001b8304 RosettaQuiesce
// Everything in hand given back, but the engine left standing: the
// word recogniser keeps its block, so everything pointing at it stays
// good, and `WordRecogResume` makes its arrays again when the next
// stroke comes down.
NewtonErr
RosettaQuiesce(void)
{
	RosettaInitializeValues();
	WordRecogSuspend(gWordRecog);
	LEquiesant();
	GeoCQuiesence();
	return noErr;
}


// ROM 0x001b8388 RosettaSleep
// ... and the engine taken down.  A grammar context the engine built
// for itself is named by a negative index, and goes back here; the
// ROM's own eight do not.
NewtonErr
RosettaSleep(void)
{
	if (gWordRecog == nil)
		return noErr;

	RosettaQuiesce();
	if (gWordRecog->fContextIndex < 0)
		BiGrammarDestroy(gWordRecog->fContext);
	gWordRecog->fContextIndex = 0;
	BPNetDestroy(gWordRecog->fNet);
	RSfRcl();
	WordRecogDestroy(gWordRecog);
	gWordRecog = nil;
	return noErr;
}


// ROM 0x001b83f4 RosettaInitializeValues
// The engine put back where a word starts: reading again, the sentence
// forgotten, the writing in hand cleared, and the boxed-character
// recogniser given back.  The run is only put back as it was when the
// segments have overflowed - nine hundred of them is a word gone very
// wrong, and what was learnt from it is not worth keeping.
NewtonErr
RosettaInitializeValues(void)
{
	gWordRecog->fClassifyMode = kRosettaClassifyNormally;
	SegmentIntegrated(1);
	RosettaClearSentence();
	// (the run is put back as it was saved when the last word was read
	//  confidently - over 899 out of a thousand - and left as it has
	//  drifted when it was not)
	WordRecogClear(gWordRecog, gRosLastConfidence > 899);
	gWordRecog->fCharBoxRect.top = -1;
	gWordRecog->fCharBoxStrokes = 0;
	if (gWordRecog->fCharBox != nil)
	{
		CharBoxDestroy(gWordRecog->fCharBox);
		gWordRecog->fCharBox = nil;
	}
	return noErr;
}


// ROM 0x001b8184 RosettaClearSentence
void
RosettaClearSentence(void)
{
	SearchDeallocateGlobals();
}


// ROM 0x001b8478 RosettaDontClassify
// What the engine is to stop doing.  The segment layer is told the
// other way round - it is "integrated" when the engine *is* reading -
// and the three bits taken out of the flags are what a classify would
// have set.
void
RosettaDontClassify(ULong what)
{
	gWordRecog->fClassifyMode = what;
	SegmentIntegrated(what == kRosettaClassifyNormally);
	gWordRecog->fFlags1f4 &= 0xffdfd7fe;
}


// ROM 0x001b84c4 RosettaGetBaseLine
// The box `WordRecogAddStroke2` worked out, as the two Points the
// recogniser wants: the top-left corner and the bottom-right one, each
// the whole-pixel part of a 16.16 number.
NewtonErr
RosettaGetBaseLine(Point* out)
{
	out[0].v = (short) (gWordRecog->fBaseline.top >> 16);
	out[0].h = (short) (gWordRecog->fBaseline.left >> 16);
	out[1].v = (short) (gWordRecog->fBaseline.bottom >> 16);
	out[1].h = (short) (gWordRecog->fBaseline.right >> 16);
	return noErr;
}


// ROM 0x001b8134 RosettaVerifyWordSymbols
// Whether every character of a reading is one the engine is allowed to
// answer at the moment - the 256 bits an area sets.
Boolean
RosettaVerifyWordSymbols(char* word)
{
	for (UByte* p = (UByte*) word; *p != 0; p++)
		if ((RosCI->fLegalUse[*p >> 5] & (1 << (*p & 0x1f))) == 0)
			return false;
	return true;
}


#pragma mark -
/*--------------------------------------------------------------------
	Reading.
--------------------------------------------------------------------*/

// ROM 0x001b7120 RosettaCheckWords
// What the word recogniser hands its readings to, and what hands them
// on to the Newton.  The scores are turned from the engine's own - the
// negative logarithm of a probability, times five hundred, summed over
// the word - into how sure the engine is out of a thousand: each is
// averaged over twice the length of the longest reading (plus three),
// turned back into a probability and taken from one.  The first of them
// is remembered for the view system (`gRosLastConfidence`), and 950
// when there are none.  Nothing is handed on when the engine was only
// asked for the baseline.
void
RosettaCheckWords(char** words, UniChar* scores, long* /*flags*/, ULong strokes, ULong count)
{
	if (gWordRecog->fClassifyMode == kRosettaBaselineOnly)
		return;

	long longest = 0;
	for (long i = 0; i < (long) count; i++)
	{
		long length = (long) strlen(words[i]) + 3;
		if (longest < length)
			longest = length;
	}
	for (long i = 0; i < (long) count; i++)
	{
		xpsvx = (Fixed) ((long) scores[i] / (longest * 2));
		Fixed p;
		if (xpsvx >= kArProbMaxScore)
			p = 0;
		else if (xpsvx < 1)
			p = 0x00010000;
		else
			p = ArProbDecodeLu[xpsvx >> 3];
		scores[i] = (UniChar) (((unsigned int) ((0x00010000 - p) * 1000)) >> 16);
	}
	gRosLastConfidence = (count == 0) ? 0x3b6 : (short) scores[0];
	RosettaCheckWordsProc proc = (RosettaCheckWordsProc) gWordRecog->fCallBack;
	proc(words, scores, strokes, count);
}


// ROM 0x001b7fc8 (unnamed)
// A stroke made of the points the recogniser copied out of the tablet,
// with the time it began and ended.
static RosStroke*
RosettaStrokeFromPoints(short count, const FPoint* points, ULong startTime, ULong endTime)
{
	RosStroke* stroke = StrokeCreate(count, points);
	stroke->fField20 = (long) endTime;
	stroke->fField1c = (long) startTime;
	return stroke;
}


// ROM 0x001b7724 RosICBX
// The box a boxed character is taken to be written in, worked out from
// where the field says the writing goes: the field's own box when it
// gave one, and otherwise a box around the baseline - two and a half
// small heights above it and one and a half below, and as wide as the
// field's box, or 1.1 small heights either side of its middle when the
// field does not space its boxes.  A field that does space them steps
// the box along, a spacing at a time, until the stroke's middle is in it.
void
RosICBX(RosStroke* stroke, FRect* box)
{
	WordRecog* wr = gWordRecog;
	FPoint centre;
	StrokeCentroid(stroke, &centre);

	Fixed left, top, right, bottom;
	if (wr->fXSpace != 0 || wr->fYSpace != 0)
	{
		ULong boxTop = (UShort) wr->fBoxTop;
		ULong boxBottom = 0;
		if (boxTop != 0)
			boxBottom = (UShort) wr->fBoxBottom;
		if (boxTop == 0 || boxBottom == 0)
		{
			ULong base = (UShort) wr->fBase;
			ULong small = wr->fSmallHeight;
			ULong half = (small + 1) >> 1;
			bottom = (Fixed) ((base + small + half) << 16);
			top = (Fixed) ((base - (small * 2 + half)) << 16);
		}
		else
		{
			bottom = (Fixed) (boxBottom << 16);
			top = (Fixed) (boxTop << 16);
		}
		right = (Fixed) ((ULong) (UShort) wr->fBoxRight << 16);
		left = (Fixed) ((ULong) (UShort) wr->fBoxLeft << 16);
	}
	else
	{
		Fixed middle = (Fixed) (int) (((unsigned int) (UShort) wr->fBoxLeft
								+ (unsigned int) (UShort) wr->fBoxRight) << 16) >> 1;
		Fixed reach = FixedMultiply(0x00011999, (Fixed) ((ULong) wr->fSmallHeight << 16));
		Fixed base = (Fixed) ((ULong) (UShort) wr->fBase << 16);
		bottom = base + (Fixed) (wr->fSmallHeight * 0x18000);
		top = base - (Fixed) (wr->fSmallHeight * 0x28000);
		right = middle + reach;
		left = middle - reach;
	}
	SetFixedRect(box, left, top, right, bottom);

	while (wr->fXSpace != 0 && box->right < centre.x)
	{
		box->right += (Fixed) ((ULong) wr->fXSpace << 16);
		box->left += (Fixed) ((ULong) wr->fXSpace << 16);
	}
	while (wr->fYSpace != 0 && box->bottom < centre.y)
	{
		box->top += (Fixed) ((ULong) wr->fYSpace << 16);
		box->bottom += (Fixed) ((ULong) wr->fYSpace << 16);
	}
}


// ROM 0x001b7b80 (unnamed)
// The boxed character read and handed on: up to five readings, each a
// single character, with how sure the engine is of it out of a hundred
// and twenty-five - the probability its score stands for, taken from
// one - and the box given back.
static void
RosettaCharBoxFinish(void)
{
	WordRecog* wr = gWordRecog;
	CharBoxChoice choices[5];
	short count = 5;
	CharBoxGetChars(wr->fCharBox, choices, &count);

	char letters[5][2];
	char* words[5];
	UniChar scores[5];
	for (long i = 0; i < count; i++)
	{
		words[i] = letters[i];
		letters[i][0] = (char) choices[i].fCode;
		letters[i][1] = 0;
		xpsvx = (UShort) choices[i].fScore;
		Fixed p;
		if (xpsvx >= kArProbMaxScore)
			p = 0;
		else if (xpsvx > 0)
			p = ArProbDecodeLu[(ULong) xpsvx >> 3];
		else
			p = 0x00010000;
		short thousandths = (short) (((0x00010000 - p) * 1000) >> 16);
		scores[i] = (UniChar) (short) (thousandths / 8);
	}

	RosettaCheckWordsProc proc = (RosettaCheckWordsProc) wr->fCallBack;
	proc(words, scores, (ULong) wr->fCharBoxStrokes, (ULong) count);
	CharBoxDestroy(wr->fCharBox);
	wr->fCharBox = nil;
	wr->fCharBoxStrokes = 0;
}


// ROM 0x001b78d0 RosettaClassifySetup
// The engine made ready to read: the word recogniser's arrays taken
// back, and - when the grammar in use is the one made for this field -
// the field's own dictionaries locked down and put into the six kinds
// of word kept for them (`~user` and `~null1`..`~null5`).  Each points
// four bytes before its data, where a lexicon's size word would be.
void
RosettaClassifySetup(void)
{
	WordRecog* wr = gWordRecog;
	const BiGrammar* context = wr->fContext;
	WordRecogResume(wr);
	if (wr->fContextIndex != -1)
		return;
	for (long k = 0; k < 6; k++)
	{
		Handle h = wr->fDicts[k];
		Ptr data;
		if (h == nil || GetHandleSize(h) < 3)
			data = nil;
		else
		{
			data = IsFakeHandle(h) ? *h : (Ptr) HLock(h);
			data -= 4;
		}
		((BiGSlice*) context->fSlices[7 + k])->fDictionary = (ULong) data;
	}
}


// ROM 0x001b7b10 RosettaClassifyCleanup
// ... and the dictionaries let go again.
void
RosettaClassifyCleanup(void)
{
	WordRecog* wr = gWordRecog;
	for (long k = 0; k < 6; k++)
	{
		Handle h = wr->fDicts[k];
		if (h != nil && !IsFakeHandle(h) && GetHandleSize(h) > 2)
			HUnlock(h);
	}
}


// ROM 0x001b7cc4 RosettaClassifyAnalyze
// One stroke given to the engine, or - with none - the writing so far
// closed.
//
// Ordinarily the stroke is cleaned up first (`StrokePreprocess`, with
// the numbers the classifier's own table carries for it) and each piece
// it comes out as is taken into the word (`WordRecogAddStroke`) - in a
// field of joined-up writing (`kRosAreaCursive`) as a word of its own.
// A field that says where its writing goes (`kRosAreaHasBaseInfo`)
// keeps a box to write in, starting a new word whenever a stroke's
// middle falls outside it; and a field of single letters reads each box
// as one character instead (`CharBox*`), handing the characters on as
// soon as a stroke falls outside the box they are in.
void
RosettaClassifyAnalyze(RosStroke* stroke)
{
	WordRecog* wr = gWordRecog;
	if (stroke == nil)
	{
		if ((wr->fFlags1f4 & kRosAreaHasBaseInfo) != 0 && wr->fCharBox != nil)
			RosettaCharBoxFinish();
		else
			WordRecogAddStroke(wr, nil,
						(short) ((wr->fFlags1f4 & kRosAreaCursive) != 0 ? 2 : 0), 0);
		return;
	}

	if ((wr->fFlags1f4 & kRosAreaHasBaseInfo) != 0)
	{
		ULong single = wr->fFlags1f4 & kRosAreaSingleLetters;
		if ((single != 0 && wr->fCharBox == nil)
			|| (single == 0 && wr->fCharBoxRect.top == -1))
		{
			RosICBX(stroke, &wr->fCharBoxRect);
			if ((wr->fFlags1f4 & kRosAreaSingleLetters) != 0)
				CharBoxIntialize(&wr->fCharBox, (long) ((ULong) wr->fSmallHeight << 16),
							&wr->fCharBoxRect, (long) ((ULong) (UShort) wr->fBase << 16),
							wr->fNet);
		}
	}

	if ((wr->fFlags1f4 & kRosAreaSingleLetters) == 0)
	{
		BPNet* net = wr->fNet;
		if ((wr->fFlags1f4 & kRosAreaHasBaseInfo) != 0)
		{
			// a stroke outside the box begins a new word, in a box of
			// its own
			FPoint centre;
			StrokeCentroid(stroke, &centre);
			Boolean outside =
				(wr->fXSpace != 0 && (centre.x < wr->fCharBoxRect.left
									|| wr->fCharBoxRect.right < centre.x))
				|| (wr->fYSpace != 0 && (centre.y < wr->fCharBoxRect.top
									|| wr->fCharBoxRect.bottom < centre.y));
			if (outside)
			{
				WordRecogAddStroke(wr, nil, 0, 0);
				RosICBX(stroke, &wr->fCharBoxRect);
			}
		}

		RosStrokeList* volatile pieces = nil;
		newton_try
		{
			const ULong* params = net->fArParams;
			pieces = StrokePreprocess(stroke, (Fixed) params[49], (Fixed) params[51],
							(Fixed) params[52], (short) params[53]);
			for (long i = 0; i < pieces->fCount; i++)
			{
				RosStroke* piece = pieces->fStrokes[i];
				pieces->fStrokes[i] = nil;
				WordRecogAddStroke(wr, piece,
							(short) ((wr->fFlags1f4 & kRosAreaCursive) != 0 ? 1 : 0), 0);
			}
		}
		newton_catch_all
		{
			SLDestroy(pieces, 1);
			rethrow;
		}
		end_try;
		SLDestroy(pieces, 1);
		return;
	}

	// single characters, one to a box
	if (wr->fCharBoxStrokes != 0 && !CharBoxStrokeInBox(wr->fCharBox, stroke))
	{
		RosettaCharBoxFinish();
		RosICBX(stroke, &wr->fCharBoxRect);
		CharBoxIntialize(&wr->fCharBox, (long) ((ULong) wr->fSmallHeight << 16),
					&wr->fCharBoxRect, (long) ((ULong) (UShort) wr->fBase << 16), wr->fNet);
	}
	CharBoxAddStroke(wr->fCharBox, stroke);
	wr->fCharBoxStrokes++;
}


// ROM 0x001b7ff4 RosettaClassify
// The recogniser's one way in: a stroke's points (which the engine takes
// over and gives back), or - with none - the word closed.  The engine
// is made ready, given the stroke and let go again every time; closing
// a word quiesces it as well, handing its arrays back until the next
// stroke.  A throw anywhere quiesces it and goes on.  It always answers
// that it succeeded.
NewtonErr
RosettaClassify(ULong count, FPoint* points, ULong startTime, ULong endTime)
{
	RosStroke* volatile stroke = nil;
	FPoint* volatile held = points;
	newton_try
	{
		newton_try
		{
			RosettaClassifySetup();
			if (count != 0)
			{
				stroke = RosettaStrokeFromPoints((short) count, points, startTime, endTime);
				DisposPtr((Ptr) points);
				held = nil;
			}
			RosettaClassifyAnalyze(stroke);
		}
		newton_catch_all
		{
			if (held != nil)
				DisposPtr((Ptr) held);
			StrokeDestroy(stroke);
			RosettaClassifyCleanup();
			rethrow;
		}
		end_try;
		if (held != nil)
			DisposPtr((Ptr) held);
		StrokeDestroy(stroke);
		RosettaClassifyCleanup();
	}
	newton_catch_all
	{
		RosettaQuiesce();
		rethrow;
	}
	end_try;
	if (count == 0)
		RosettaQuiesce();
	return noErr;
}


// ROM 0x001b7254 RosettaSetArea
// What a field's configuration becomes.  This is the engine being told
// where it is writing: which grammar to read against, which characters
// are allowed, which dictionaries to look in, and - when the field says
// so - where its baseline and its box are.
//
// The grammar comes first.  Eight of the `fFlags` bits pick one of the
// ROM's seven special grammars outright; anything else, and any field
// that names dictionaries of its own, gets the **General** grammar
// narrowed to the kinds of word the field expects.  The narrowing is a
// bitmask of slice indices built from the same flags, handed to
// `BiGrammarModifyContext` with nine tenths of the probability going to
// the kinds that are wanted.  Either way what the engine ends up
// holding is a copy it owns, which is what `fContextIndex < 0` records.
//
// Then every slice's `fDictionary` stops being an index into
// `gROMDictionaryData` and becomes the data itself - after any
// substitutions the field asked for through `fMap`.
NewtonErr
RosettaSetArea(RosettaAreaInfo* area)
{
	ULong mask = 0x1800000;
	const BiGrammar* previous = gWordRecog->fContext;
	ULong flags = area->fFlags & 0x000387bf;

	SegmentSetWordSpacing(9 - area->fLetterSpace);
	gWordRecog->fFlags1f4 = area->fFlags;

	// the grammar it was holding, if it was its own
	if (gWordRecog->fContextIndex < 0)
		BiGrammarDestroy(previous);

	// a field with dictionaries of its own always takes the General
	// grammar, however it is labelled
	if (area->fDictCount != 0)
		flags = 0;

	long index;
	if (flags == kRosAreaDate)					index = 5;
	else if (flags < kRosAreaDate + 1)
	{
		if (flags == kRosAreaNumbers)			index = 2;
		else if (flags == kRosAreaPunctuation)	index = 4;
		else if (flags == kRosAreaPhone)		index = 1;
		else									index = 0;
	}
	else if (flags == kRosAreaAddress)			index = 6;
	else if (flags == kRosAreaCustom1)			index = 7;
	else if (flags == kRosAreaCustom2)			index = 3;
	else										index = 0;
	gWordRecog->fContextIndex = index;

	const BiGrammar* context = gWordRecog->fGrammars->fContexts[index];
	gWordRecog->fContext = context;

	if (index == 0)
	{
		// the General grammar, narrowed to what this field expects.
		// The numbers are sets of slice indices, and they overlap: a
		// field that wants times gets a quite different set from one
		// that wants only letters.
		ULong f = gWordRecog->fFlags1f4;
		if ((f & kRosAreaNumbers) != 0)			mask = 0x190a000;
		if ((f & kRosAreaPunctuation) != 0)		mask |= 0x40000;
		if ((f & kRosAreaPhone) != 0)			mask |= 0x20021;
		if ((f & kRosAreaDate) != 0)			mask |= 0x10000;
		if ((f & kRosAreaTime) != 0)			mask = 0x1ffe0ff;
		if ((f & kRosAreaMoney) != 0)			mask |= 0x1800000;
		if ((f & (kRosAreaLetters | kRosAreaNames)) != 0)	mask |= 0x4040f1;
		if ((f & kRosAreaUpperCase) != 0)		mask |= 0x180a000;
		if ((f & 0x00000200) != 0)				mask |= 0x20000e;
		if ((f & kRosAreaNames) != 0)			mask |= 0x1802000;

		// and one slice per dictionary the field named, which are the
		// `~user` and `~null1`..`~null5` slots
		UByte named = area->fDictCount;
		if (named >= 1)	mask |= 0x100;
		if (named >= 2)	mask |= 0x200;
		if (named >= 3)	mask |= 0x400;
		if (named >= 4)	mask |= 0x800;
		if (named >= 5)	mask |= 0x1000;

		// the mask spread out into the list of indices
		// `BiGrammarModifyContext` walks, with -1 after the last so
		// that its cursor never runs off the end
		long wanted[32];
		long count = 0;
		for (long bit = 0; bit < 32; bit++)
			if ((mask & (1UL << bit)) != 0)
				wanted[count++] = bit;
		wanted[count] = -1;

		context = BiGrammarModifyContext(context, count, wanted, 0xe666);
		gWordRecog->fContext = context;
		gWordRecog->fContextIndex = -1;
		gWordRecog->fDicts[0] = area->fMainDict;
		for (long i = 0; i < 5; i++)
			gWordRecog->fDicts[i + 1] = area->fDicts[i];
	}
	else
	{
		// one of the seven, copied so the substitutions below do not
		// write into ROM
		context = BiGrammarClone(context);
		gWordRecog->fContextIndex = -(gWordRecog->fContextIndex + 1);
		gWordRecog->fContext = context;
		for (long i = 0; i < 6; i++)
			gWordRecog->fDicts[i] = nil;
	}

	// the dictionaries: any the field asked to have read as another,
	// and then the index turned into the data itself
	for (long i = 0; i < context->fCount; i++)
	{
		BiGSlice* slice = (BiGSlice*) context->fSlices[i];
		for (long k = 0; k < 5; k++)
			if (slice->fDictionary == (ULong) area->fMap[k][0])
			{
				slice->fDictionary = (ULong) area->fMap[k][1];
				break;
			}
		slice->fDictionary = (ULong) gROMDictionaryData[slice->fDictionary];
	}

	// where the writing goes, when the field knows
	if ((gWordRecog->fFlags1f4 & kRosAreaHasBaseInfo) != 0)
	{
		gWordRecog->fBase = area->fBase;
		// the smaller of the two heights, but never less than halfway
		// between it and eleven
		UByte small = area->fSmallHeight;
		UByte half = (UByte) ((small + 0x0b) / 2);
		gWordRecog->fSmallHeight = (half <= small) ? small : half;
		gWordRecog->fBoxLeft = area->fBoxLeft;
		gWordRecog->fBoxRight = area->fBoxRight;
		gWordRecog->fXSpace = area->fXSpace;
		gWordRecog->fBoxTop = area->fBoxTop;
		gWordRecog->fBoxBottom = area->fBoxBottom;
		gWordRecog->fYSpace = area->fYSpace;
	}

	gWordRecog->fCharBoxRect.top = -1;
	if (gWordRecog->fCharBox != nil)
	{
		CharBoxDestroy(gWordRecog->fCharBox);
		gWordRecog->fCharBox = nil;
	}

	// which characters the engine may answer here: the ROM's own set
	// narrowed by the field's, or the ROM's own when the field has
	// nothing to say
	if (RosCI->fLegalUse != rosCharLegalUse)
	{
		DisposPtr((Ptr) RosCI->fLegalUse);
		RosCI->fLegalUse = rosCharLegalUse;
	}
	if ((gWordRecog->fFlags1f4 & kRosAreaHasSymbolSet) != 0)
	{
		ULong* set = (ULong*) NewPtrClear(8 * (long) sizeof(ULong));
		if (set == nil)
			Throw(exOutOfStack, (void*) "", nil);
		SetPtrName((Ptr) set, kRosettaMemoryTag);
		RosCI->fLegalUse = set;
		for (long i = 0; i < 8; i++)
			set[i] = rosCharLegalUse[i] & area->fSymbolSet[i];
	}
	return noErr;
}
