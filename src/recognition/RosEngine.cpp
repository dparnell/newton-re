/*
	File:		recognition/RosEngine.cpp

	Contains:	The engine's other layers - see RosEngine.h.

	NOT YET RECONSTRUCTED.  The destroyers do nothing, which is safe
	because nothing makes the objects they would give back; `RosCI` is
	nil until the engine's trained numbers are extracted, and
	`BiGrammarsLoad` answers the grammar it is handed so that the word
	recogniser can be driven before `ROMGrammar` is.

	The work below this file, in the order it wants doing, is in
	`docs/recognition/README.md` under "The Rosetta engine".
*/

#include "RosEngine.h"
#include "RosStrokes.h"


// ROM 0x0c100b08 RosCI
RosCommonInfo*	RosCI = nil;


Boolean
RosEngineLayersAreReconstructed(void)
{
	return false;
}


// ROM 0x0003e0b4 BiGrammarsLoad
// NOT YET: the ROM's is one instruction - it answers `ROMGrammar`, the
// list of eight contexts built into the ROM, and never looks at what
// it is asked for.  Until that table is extracted this one answers
// what it is handed, which is the smallest change that lets a caller
// bring a grammar of its own.
RosGrammars*
BiGrammarsLoad(RosGrammars* source)
{
	return source;
}


// ROM 0x0003df4c BiGrammarDestroy
void	BiGrammarDestroy(RosGrammarContext* /*context*/)			{ }

// ROM 0x001d48a4 SegmentChars
short	SegmentChars(short /*count*/, RosStroke** /*strokes*/, Fixed /*meanSize*/,
					RosSegment** /*segments*/, UByte /*how*/, void* /*net*/)	{ return 0; }
// ROM 0x001d2224 SegmentStrokeData
void	SegmentStrokeData(RosStroke* /*stroke*/, UByte /*how*/, short /*index*/, Fixed /*separation*/)	{ }

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


// ROM 0x00133a08 NetPatternDestroy
void	NetPatternDestroy(RosNetPattern* /*pattern*/)			{ }
// ROM 0x00133a58 NetPatternizerDestroy
void	NetPatternizerDestroy(RosNetPatternizer* /*patternizer*/)	{ }

// ROM 0x0011343c ListZap
void	ListZap(void)											{ }
