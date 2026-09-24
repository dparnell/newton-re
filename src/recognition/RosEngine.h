/*
	File:		recognition/RosEngine.h

	Contains:	The parts of the handwriting engine the word recogniser
				leans on that are not reconstructed yet.

				`recognition/Rosetta.h` draws the seam between the
				Newton and ParaGraph's engine; this file draws the
				smaller seams *inside* the engine, so that one layer of
				it can be reconstructed at a time.  Everything declared
				here is called by `WordRecog.h` and answers nothing
				useful until it is written.

				- the **common info** (`RosCI`) is the block of trained
				  numbers the whole engine measures against.  Two of its
				  fields are known, because the word recogniser reads
				  them: a nominal width for every character code, and
				  the smallest cap height it will believe.

				- a **grammar** is the finite-state machine the engine
				  reads a word against.  The ROM has eight of them built
				  in (`ROMGrammar`, 0x00366e0c): General, Date,
				  Numbers&Money, Numbers, Phone, Time, Money and
				  PostalCode.  `WordRecogSetContext` picks one by name.

				- a **segment** is a piece of a stroke the engine has
				  decided is one character, or part of one.

				- the **net** is the back-propagation classifier, and a
				  **patternizer** is what turns a segment into the
				  inputs it takes.

	NOT YET RECONSTRUCTED.  Each function does as little as it can
	without lying: the destroyers do nothing, and `BiGrammarsLoad`
	answers the grammar it is handed rather than the ROM's own trained
	one, so that a caller (or a test) can drive the word recogniser with
	a grammar of its own until `ROMGrammar` is extracted.

	Reconstructed from the MP2x00 US ROM; each declaration cites its
	origin.
*/

#ifndef __ROSENGINE_H
#define __ROSENGINE_H

#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

#ifndef __FIXEDGEOMETRY_H
#include "FixedGeometry.h"
#endif


/*--------------------------------------------------------------------
	The engine's common info.
--------------------------------------------------------------------*/

// What `RosCI->fCharInfo` points at.  Only the width table is read so
// far: it is indexed by the character's own code, so the engine's
// characters are bytes.  The widths are 16.16 - the word recogniser
// adds them up and divides by the number of characters with an
// ordinary integer divide, and then uses the answer as a Fixed.
struct RosCharInfo
{
	long	fField00;		// +0x00
	Fixed*	fWidths;		// +0x04  a nominal width per character code
};

// The block the whole engine measures against.  The ROM's is much
// larger than this; the fields below are the ones that have been read.
struct RosCommonInfo
{
	UByte			fField00[0x0c];		// +0x00
	RosCharInfo*	fCharInfo;			// +0x0c
	UByte			fField10[0x3c];		// +0x10
	Fixed			fMinCapHeight;		// +0x4c  smaller than this is not believed
};

// ROM 0x0c100b08 RosCI
// There is one, and it is nil until the engine is started.
extern RosCommonInfo*	RosCI;


/*--------------------------------------------------------------------
	The grammars.
--------------------------------------------------------------------*/

// One context: the name it is asked for by, and the finite-state
// grammar itself, which is NOT YET.
struct RosGrammarContext
{
	const char*	fName;		// +0x00
};

// The list of them, as `BiGrammarsLoad` answers it.
struct RosGrammars
{
	long				fCount;			// +0x00
	RosGrammarContext**	fContexts;		// +0x04
};

// NOT YET: the ROM's answers `ROMGrammar` (0x00366e0c), its eight
// built-in contexts, whatever it is asked for.  Ours answers what it
// is handed, so that the word recogniser can be driven before that
// table is extracted.
RosGrammars*	BiGrammarsLoad(RosGrammars* source);		// ROM 0x0003e0b4 BiGrammarsLoad
// A context the engine built for itself, given back.
void			BiGrammarDestroy(RosGrammarContext* context);	// ROM 0x0003df4c BiGrammarDestroy


/*--------------------------------------------------------------------
	The segments, and the classifier.
--------------------------------------------------------------------*/

// A piece of writing the engine has decided is a character, or part of
// one.  What is in it is NOT YET.
struct RosSegment;
// The classifier's pattern, and what fills it in.
struct RosNetPattern;
struct RosNetPatternizer;

void	SegmentDestroy(RosSegment* segment);				// ROM 0x001d1cac SegmentDestroy
// Everything the segment layer is holding on to, given back.
void	SegmentQuiesce(void);								// ROM 0x001d0f3c SegmentQuiesce
void	SegmentIntegrated(long how);						// ROM 0x001d4cbc SegmentIntegrated

void	NetPatternDestroy(RosNetPattern* pattern);			// ROM 0x00133a08 NetPatternDestroy
void	NetPatternizerDestroy(RosNetPatternizer* patternizer);	// ROM 0x00133a58 NetPatternizerDestroy

// The engine's own free list, emptied.
void	ListZap(void);										// ROM 0x0011343c ListZap

// True once the layers above are real.
Boolean	RosEngineLayersAreReconstructed(void);

#endif	/* __ROSENGINE_H */
