/*
	File:		recognition/RosEngine.cpp

	Contains:	The engine's other layers - see RosEngine.h.

	Two of them are real: `CharInitialize`/`RSfRcl`, which make and
	give back the common info out of the ROM's own template, and
	`BiGrammarsLoad`, which answers the ROM's own bigram grammar.  The
	rest are NOT YET; the destroyers do nothing, which is safe because
	nothing makes the objects they would give back.

	The work below this file, in the order it wants doing, is in
	`docs/recognition/README.md` under "The Rosetta engine".
*/

#include "RosEngine.h"
#include "RosStrokes.h"
#include "NewtonMemory.h"


// ROM 0x0c100b08 RosCI
RosCommonInfo*	RosCI = nil;


// ROM 0x00057074 CharInitialize
// The common info made: the ROM's template copied into a block of its
// own, because an area may put a character set of its own in it.  What
// comes back is how many output nodes the classifier has, which is the
// one thing the caller wants and which the ROM simply writes down.
long
CharInitialize(long /*unused*/)
{
	RosCI = (RosCommonInfo*) RosAllocate((long) sizeof(RosCommonInfo));
	*RosCI = rosCI;
	return 0x86;
}


// ROM 0x001b8334 RSfRcl
// ... and given back.  A character set the engine made for an area goes
// first, which is what the test against the ROM's own is for.
void
RSfRcl(void)
{
	if (RosCI->fLegalUse != rosCharLegalUse)
	{
		DisposPtr((Ptr) RosCI->fLegalUse);
		RosCI->fLegalUse = rosCharLegalUse;
	}
	DisposPtr((Ptr) RosCI);
	RosCI = nil;
}


Boolean
RosEngineLayersAreReconstructed(void)
{
	return false;
}


// ROM 0x0003e0b4 BiGrammarsLoad
// The ROM's is one instruction: it answers `ROMGrammar` and never
// looks at what it is asked for.  Ours does the same when it is asked
// for the ROM's (`kROMGrammars`, the constant the engine passes) and
// otherwise answers what it is handed, so a caller may bring a grammar
// of its own.
const BiGrammars*
BiGrammarsLoad(const BiGrammars* source)
{
	if (source == kROMGrammars)
		return &ROMGrammar;
	return source;
}


// ROM 0x0003df4c BiGrammarDestroy
void	BiGrammarDestroy(const BiGrammar* /*grammar*/)			{ }

// ROM 0x001d48a4 SegmentChars
short	SegmentChars(short /*count*/, RosStroke** /*strokes*/, Fixed /*meanSize*/,
					RosSegment** /*segments*/, UByte /*how*/, void* /*net*/)	{ return 0; }
// ROM 0x001d2224 SegmentStrokeData
void	SegmentStrokeData(RosStroke* /*stroke*/, UByte /*how*/, short /*index*/, Fixed /*separation*/)	{ }

// ROM 0x001d0e68 SegmentCreate
RosSegment*	SegmentCreate(void)								{ return nil; }
// ROM 0x001d1cac SegmentDestroy
void	SegmentDestroy(RosSegment* /*segment*/)					{ }
// ROM 0x001d0f3c SegmentQuiesce
void	SegmentQuiesce(void)									{ }
// ROM 0x001d4cbc SegmentIntegrated
void	SegmentIntegrated(long /*how*/)							{ }

// ROM 0x001d1890 SegmentMinStrokeSize
// One field of the common info, under another name.
Fixed
SegmentMinStrokeSize(void)
{
	return RosCI->fMinStrokeSize;
}


// ROM 0x000ffd60 LEquiesant
void	LEquiesant(void)									{ }
// ROM 0x000d9ce4 GeoCQuiesence
void	GeoCQuiesence(void)									{ }
// ROM 0x001d02ec SearchDeallocateGlobals
void	SearchDeallocateGlobals(void)						{ }

// ROM 0x0011343c ListZap
void	ListZap(void)											{ }
