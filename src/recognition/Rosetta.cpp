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
// ROM 0x001b7254 RosettaSetArea
NewtonErr	RosettaSetArea(RosettaAreaInfo* /*areaInfo*/)			{ return kRosettaFailed; }
// ROM 0x001b78d0 RosettaClassifySetup
NewtonErr	RosettaClassifySetup(void)								{ return kRosettaFailed; }
// ROM 0x001b7cc4 RosettaClassifyAnalyze
NewtonErr	RosettaClassifyAnalyze(void)							{ return kRosettaFailed; }
// ROM 0x001b7b10 RosettaClassifyCleanup
NewtonErr	RosettaClassifyCleanup(void)							{ return kRosettaFailed; }
// ROM 0x001b7120 RosettaCheckWords
void		RosettaCheckWords(char** /*words*/, UniChar* /*scores*/, long* /*flags*/,
							ULong /*strokes*/, ULong /*count*/)		{ }
