/*
	File:		recognition/Rosetta.cpp

	Contains:	The engine's side of the seam - see Rosetta.h.

	NOT YET RECONSTRUCTED.  Each call answers the value the ROM's own
	engine answers when it cannot do what was asked, so a TRosRecognizer
	built on this behaves exactly as the ROM's does with a broken engine:
	it throws `evt.ex.abt`, and the recognition system puts it to sleep
	(`TWRecDomain::SignalMemoryError`).  Nothing installs it; the engine
	the host installs is `TInkOnlyRecognizer`.

	The work below this file, in the order it wants doing, is in
	`docs/recognition/README.md` under "The Rosetta engine".
*/

#include "Rosetta.h"
#include "OSErrors.h"


// The engine's own error: the ROM's calls answer 0 for done and
// anything else for not.  (It has no symbol for this; the callers only
// ever test against nought.)
const NewtonErr	kRosettaFailed	= kError_Call_Not_Implemented;


Boolean
RosettaEngineIsReconstructed(void)
{
	return false;
}


// ROM 0x001b810c RosettaInitialize
NewtonErr	RosettaInitialize(long /*xScale*/, long /*yScale*/, RosettaCheckWordsProc /*proc*/)	{ return kRosettaFailed; }
// ROM 0x001b7ff4 RosettaClassify
NewtonErr	RosettaClassify(ULong /*count*/, FPoint* /*points*/, ULong /*startTime*/, ULong /*endTime*/)	{ return kRosettaFailed; }
// ROM 0x001b83f4 RosettaInitializeValues
NewtonErr	RosettaInitializeValues(void)							{ return kRosettaFailed; }
// ROM 0x001b8478 RosettaDontClassify
void		RosettaDontClassify(ULong /*what*/)						{ }
// ROM 0x001b7254 RosettaSetArea
NewtonErr	RosettaSetArea(RosettaAreaInfo* /*areaInfo*/)			{ return kRosettaFailed; }
// ROM 0x001b84c4 RosettaGetBaseLine
NewtonErr	RosettaGetBaseLine(Point* /*out*/)						{ return kRosettaFailed; }
// ROM 0x001b8134 RosettaVerifyWordSymbols
Boolean		RosettaVerifyWordSymbols(char* /*word*/)				{ return false; }
// ROM 0x001b8184 RosettaClearSentence
void		RosettaClearSentence(void)								{ }
// ROM 0x001b819c RosettaAwaken
NewtonErr	RosettaAwaken(void)										{ return kRosettaFailed; }
// ROM 0x001b8304 RosettaQuiesce
NewtonErr	RosettaQuiesce(void)									{ return kRosettaFailed; }
// ROM 0x001b8388 RosettaSleep
NewtonErr	RosettaSleep(void)										{ return kRosettaFailed; }
// ROM 0x001b78d0 RosettaClassifySetup
NewtonErr	RosettaClassifySetup(void)								{ return kRosettaFailed; }
// ROM 0x001b7cc4 RosettaClassifyAnalyze
NewtonErr	RosettaClassifyAnalyze(void)							{ return kRosettaFailed; }
// ROM 0x001b7b10 RosettaClassifyCleanup
NewtonErr	RosettaClassifyCleanup(void)							{ return kRosettaFailed; }
// ROM 0x001b7120 RosettaCheckWords
void		RosettaCheckWords(void)									{ }
