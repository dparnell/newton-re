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


// ROM 0x0003de8c BiGrammarNew
// A grammar with room for `capacity` kinds of word.  The array of slice
// pointers lives **behind the struct in the same block**, which is why
// `fSlices` points at the byte after it and why the whole thing is one
// `DisposPtr`.  Nothing else in the engine allocates that way.
BiGrammar*
BiGrammarNew(short capacity)
{
	// DEVIATION: the ROM asks for `4 * capacity + 0x20` - its header is
	// 0x20 bytes and its pointers four - and points `fSlices` at the
	// byte after the header.  A host pointer is eight, so the size and
	// the place are worked out from the struct instead.
	BiGrammar* grammar = (BiGrammar*) RosAllocate(
					(long) (sizeof(BiGrammar) + capacity * sizeof(BiGSlice*)));
	grammar->fName = nil;
	grammar->fField04 = 0;
	grammar->fCount = 0;
	grammar->fCapacity = capacity;
	grammar->fSlices = (const BiGSlice* const*) (grammar + 1);
	// the ROM clears the eleven bytes from 0x14 to 0x1e one at a time;
	// here they are the fields they belong to
	grammar->fField14 = 0;
	grammar->fField15 = 0;
	grammar->fField16 = 0;
	grammar->fField17 = 0;
	grammar->fField18 = 0;
	grammar->fField1c = 0;
	return grammar;
}


// ROM 0x0003df2c BiGrammarCreate
// The same, taking a name - which it accepts and never stores: the
// register holding it is overwritten with the capacity before the call
// and `fName` is set to nil afterwards.  So a grammar the engine builds
// for a field has no name, and `BiGrammarClone` does not copy one
// either.  Nothing reads it, so nothing notices.
BiGrammar*
BiGrammarCreate(const char* /*name*/, short capacity)
{
	BiGrammar* grammar = BiGrammarNew(capacity);
	grammar->fName = nil;
	return grammar;
}


// ROM 0x0003dfa8 BiGSliceDestroy
void
BiGSliceDestroy(const BiGSlice* slice)
{
	DisposPtr((Ptr) slice);
}


// ROM 0x0003df4c BiGrammarDestroy
// Its slices and then itself.  Only ever called on a grammar the engine
// cloned for a field: `RosettaSleep` checks `fContextIndex < 0` first,
// and the ROM's own eight are in ROM.
void
BiGrammarDestroy(const BiGrammar* grammar)
{
	if (grammar == nil)
		return;
	if (grammar->fSlices != nil)
		for (long i = 0; i < grammar->fCount; i++)
			BiGSliceDestroy(grammar->fSlices[i]);
	DisposPtr((Ptr) grammar);
}


// ROM 0x0003dfb4 BiGSliceNew
// One kind of word, with room for `capacity` kinds that may follow it.
// Both of those arrays live behind the struct in the same block - first
// the pointers to the kinds, then a score for each - so a slice is one
// allocation of `6 * capacity + 0x30` bytes.  A slice with nowhere to
// go has nil for both rather than a pointer past its own end.
BiGSlice*
BiGSliceNew(short capacity)
{
	long n = capacity;
	// DEVIATION: the ROM asks for `6 * capacity + 0x30`, a 0x30-byte
	// header followed by `capacity` four-byte pointers and then as many
	// two-byte scores.  A host pointer is eight, so the size and the
	// two places are worked out from the struct.
	BiGSlice* slice = (BiGSlice*) RosAllocate(
					(long) (sizeof(BiGSlice) + n * (sizeof(BiGSlice*) + sizeof(short))));
	slice->fName = nil;
	slice->fNextCount = 0;
	slice->fNextCapacity = n;

	const BiGSlice** behind = (n != 0) ? (const BiGSlice**) (slice + 1) : nil;
	slice->fNext = behind;
	slice->fWeights = (n != 0) ? (const short*) (behind + n) : nil;
	slice->fField2c = 0xff;
	return slice;
}


// ROM 0x000ffd60 LEquiesant
void	LEquiesant(void)									{ }
// ROM 0x000d9ce4 GeoCQuiesence
void	GeoCQuiesence(void)									{ }
// ROM 0x001d02ec SearchDeallocateGlobals
void	SearchDeallocateGlobals(void)						{ }

// ROM 0x0011343c ListZap
void	ListZap(void)											{ }
