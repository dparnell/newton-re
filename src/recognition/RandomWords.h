/*
	File:		recognition/RandomWords.h

	Contains:	Random words out of the dictionaries, which is what
				Handwriting Practice gives the writer to copy
				(GetRandomDictionaryWord) and what GetRandomWord answers
				a script.

				A word is grown a letter at a time down the dictionary's
				trie: at each step the letters that may come next are
				asked for (the engine's NextSet), one is picked at random,
				weighted by how often it is written (charWeights - a
				capital only allowed after the first letter), and the word
				so far verified; a node that ends a word stops it there
				always when nothing can follow and one time in five when
				something could.  A word outside the lengths asked for, or
				one that ran into an apostrophe, is started again (up to a
				thousand times).

				GetRandomDictionaryWord goes further: out of up to
				twenty-two such words it keeps the one with the most pairs
				of letters it has not handed out before (letterPairs, a
				26 x 26 count halved when one of them reaches 255), so
				that a practice session works through as many different
				joins as it can.

	Reconstructed from the MP2x00 US ROM (0x0013e624-0x0013ec74, with the
	natives at 0x0019fbe4 and 0x001a0890); each function cites its origin.
*/

#ifndef __RANDOMWORDS_H
#define __RANDOMWORDS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

void	InitRandomWords(void);								// ROM 0x0013e624 InitRandomWords__Fv - the generator seeded from the clock, the letter pairs cleared
void	RandomCommonWord(UniChar* word, ULong minLength, ULong maxLength);	// ROM 0x0013e640 RandomCommonWord__FPUsUlT2 - out of the common words (dictionary 0), spread over the letter pairs
void	CommonWord(UniChar* word, ULong minLength, ULong maxLength);		// ROM 0x0013e6a8 CommonWord__FPUsUlT2 - out of dictionary 6
long	RangeRand(long low, long high);						// ROM 0x0013e6e0 RangeRand__FiT1 - low .. high
UByte	GetCharWeight(char c, ULong position, Boolean capitalsAllowed);	// ROM 0x0013e71c GetCharWeight__FcUlUc
ULong	ChooseWeightedChar(char* chars, ULong position, Boolean capitalsAllowed);	// ROM 0x0013e77c ChooseWeightedChar__FPcUlUc
void	GetRandomWord(Handle dictionary, UniChar* word, ULong minLength, ULong maxLength);	// ROM 0x0013e82c GetRandomWord__FPP15AirusAParmBlockPUsUlT3
char	ShiftLetter(char c);								// ROM 0x0013e9c4 ShiftLetter__Fc - a letter's place in the alphabet, 26 for anything else
void	InitLetterPairs(void);								// ROM 0x0013ea00 InitLetterPairs__Fv
void	HalveLetterPairs(void);								// ROM 0x0013ea40 HalveLetterPairs__Fv
void	AddLetterPairScore(char* word);						// ROM 0x0013ea88 AddLetterPairScore__FPc
long	GetLetterPairScore(char* word);						// ROM 0x0013eb1c GetLetterPairScore__FPc - how many of its pairs have not been seen
long	GetDistributedWord(Handle dictionary, UniChar* word, ULong maxLength, ULong minLength);	// ROM 0x0013eba8 GetDistributedWord__FPP15AirusAParmBlockPUsUlT3

Ref		FGetRandomDictionaryWord(RefArg rcvr, RefArg minLength, RefArg maxLength);	// ROM 0x0019fbe4 FGetRandomDictionaryWord__FRC6RefVarN21
Ref		FGetRandomWord(RefArg rcvr, RefArg minLength, RefArg maxLength);			// ROM 0x001a0890 FGetRandomWord__FRC6RefVarN21
void	RegisterRandomWordNatives(void);

extern const unsigned char	charWeights[26];			// ROM 0x0c101634 charWeights (RandomWordTables.cpp, generated)

#endif	/* __RANDOMWORDS_H */
