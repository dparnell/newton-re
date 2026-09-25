/*
	File:		recognition/Rosetta.cpp

	Contains:	The engine's side of the seam - see Rosetta.h.

	The engine's **life** is reconstructed: waking, quietening and
	sleeping, and the small calls that go with them.  There is one word
	recogniser (`gWordRecog`) and it is made when the engine wakes and
	destroyed when it sleeps; between those, `RosettaQuiesce` hands its
	arrays back without moving the block and `RosettaAwaken` is what
	makes it again.

	The engine wakes: the common info and the bigram grammar are the
	ROM's own, so `RosettaAwaken` makes a word recogniser that knows
	the eight grammars a field may ask for and the 166 characters it
	may answer.  What it cannot do yet is *read*: the classifier is
	NOT YET (`BPNetCreateNumOut` answers nil), and so are the three
	passes a classify is made of.

	NOT YET: `RosettaSetArea`, which reads an area block into the
	engine, and `RosettaClassify` with its setup/analyze/cleanup.  A
	call that fails answers `kRosettaFailed`, `TRosRecognizer` turns
	that into `evt.ex.abt` - which is what the ROM's own does when its
	engine fails - and the recognition system puts it to sleep
	(`TWRecDomain::SignalMemoryError`).  Nothing installs it; the
	engine the host installs is `TInkOnlyRecognizer`.

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
#include "OSErrors.h"

#include <string.h>


// The engine's own error: the ROM's calls answer 0 for done and
// anything else for not.  (It has no symbol for this; the callers only
// ever test against nought.)
const NewtonErr	kRosettaFailed	= kError_Call_Not_Implemented;


Boolean
RosettaEngineIsReconstructed(void)
{
	return false;
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
	gWordRecog->fField1e0 = -1;
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
	gWordRecog->fField1e0 = -1;
	gWordRecog->fField202 = 0;
	gWordRecog->fField203 = 0;
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
	NOT YET.
--------------------------------------------------------------------*/

// ROM 0x001b7ff4 RosettaClassify
NewtonErr	RosettaClassify(ULong /*count*/, FPoint* /*points*/, ULong /*startTime*/, ULong /*endTime*/)	{ return kRosettaFailed; }
// ROM 0x001b78d0 RosettaClassifySetup
NewtonErr	RosettaClassifySetup(void)								{ return kRosettaFailed; }
// ROM 0x001b7cc4 RosettaClassifyAnalyze
NewtonErr	RosettaClassifyAnalyze(void)							{ return kRosettaFailed; }
// ROM 0x001b7b10 RosettaClassifyCleanup
NewtonErr	RosettaClassifyCleanup(void)							{ return kRosettaFailed; }
// ROM 0x001b7120 RosettaCheckWords
void		RosettaCheckWords(char** /*words*/, UniChar* /*scores*/, long* /*flags*/,
							ULong /*strokes*/, ULong /*count*/)		{ }


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

	gWordRecog->fField1e0 = -1;
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
