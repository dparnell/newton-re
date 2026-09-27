/*
	File:		assist/Phrases.h

	Contains:	The Assistant's phrase generator: every run of consecutive
				words of the sentence, longest first and left to right, for
				the lexicon to try - and, when a run is found, every run
				that overlaps it is struck off, so that "call Daniel Parnell"
				tries "call Daniel Parnell", then "call Daniel", "Daniel
				Parnell" and so on, but once "Daniel Parnell" is a person
				neither "Daniel" nor "Parnell" is tried alone.

				The state is a 16 x 16 grid of bytes, one per (length,
				start) - 4 not yet tried, 1 a hit, 3 struck off by a hit -
				with the length and start being generated and the last run
				handed out; the sentence is fifteen words at most.

	Reconstructed from the MP2x00 US ROM (0x00080830-0x0008105c); each
	function cites its origin.
*/

#ifndef __PHRASES_H
#define __PHRASES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

// what the grid holds for a run
enum
{
	kPhraseHit = 1,			// the lexicon knew it
	kPhraseCovered = 3,		// it overlaps one that was known
	kPhraseUntried = 4
};

struct PhrasalGlobals		// 0x114 bytes
{
	UByte		fGrid[16][16];		// +0x000  [length][start], both from 1
	long		fLength;			// +0x100  the length being generated
	long		fStart;				// +0x104  the next start
	long		fHitLength;			// +0x108  the run last handed out
	long		fHitStart;			// +0x10c
	RefStruct*	fWords;				// +0x110  the sentence's words (a handle that lives as long as the Assistant)
};
extern PhrasalGlobals*	gPhrasalGlobals;	// ROM 0x0c100bbc gPhrasalGlobals
extern Ref				gUtterGenFrame;		// ROM 0x0c100bc0 gUtterGenFrame

Ref		InitDSPhraseSupport(RefArg rcvr, RefArg arg);				// ROM 0x00080924 InitDSPhraseSupport__FRC6RefVarT1
void	setPhraseElem(long length, long start, long value);			// ROM 0x00080830 setPhraseElem__FiN21
long	getPhraseElem(long length, long start);						// ROM 0x00080848 getPhraseElem__FiT1
void	IPhraseGenerator(RefArg sentence);							// ROM 0x00080da0 IPhraseGenerator__FRC6RefVar
Ref		NextPhrase(void);											// ROM 0x00080e94 NextPhrase__Fv - nil when there are no more
long	PeekValidPhrase(void);										// ROM 0x00080f7c PeekValidPhrase__Fv - 4 when an untried run is left
void	PhraseHit(long length, long start);							// ROM 0x00080cd4 PhraseHit__FiT1
void	PhraseHitExt(void);											// ROM 0x0008090c PhraseHitExt__Fv - the run last handed out was known
long	interval_intersection_p(long length1, long start1, long length2, long start2);	// ROM 0x00080c94 interval_intersection_p__FiN31
Ref		PartialGlueString(RefArg words, ULong start, ULong length);	// ROM 0x00080a88 PartialGlueString__FRC6RefVarUlT2
Ref		UnmatchedWords(RefArg rcvr);								// ROM 0x00080974 UnmatchedWords__FRC6RefVar - the words neither known nor covered
Ref		OrigPhrase(RefArg rcvr);									// ROM 0x00080a70 OrigPhrase__FRC6RefVar
Ref		GeneratePhrases(RefArg rcvr, RefArg sentence);				// ROM 0x00080860 GeneratePhrases__FRC6RefVarT1 - (a test) the runs printed
void	PrintGeneratorState(void);									// ROM 0x00080b9c PrintGeneratorState__Fv

void	RegisterPhraseNatives(void);

#endif	/* __PHRASES_H */
