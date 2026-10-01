/*
	File:		recognition/RandomWords.cpp

	Contains:	Random words out of the dictionaries.  See RandomWords.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RandomWords.h"
#include "Airus.h"
#include "Dictionaries.h"
#include "Spelling.h"			// StringLength, CopyCString
#include "RecObject.h"		// GetTicks
#include "Random.h"
#include "Unicode.h"
#include "NativeFunctions.h"
#include <string.h>

// ROM 0x0c106528 letterPairs - how often each pair of letters has been
// handed out (a byte each, halved when one reaches 255)
static UByte	letterPairs[26][26];

// ROM 0x0c101630 inited - InitRandomWords has run
static Boolean	inited = false;



// ROM 0x0013e624 InitRandomWords__Fv
void
InitRandomWords(void)
{
	NewtonSrand(GetTicks());
	for (ULong i = 0; i < 26; i++)
		for (ULong j = 0; j < 26; j++)
			letterPairs[i][j] = 0;
}


// ROM 0x0013e640 RandomCommonWord__FPUsUlT2
// A word out of the common words, the one of a handful that brings the
// most letter pairs not yet handed out; when none brings any new pair,
// the counts start again.
void
RandomCommonWord(UniChar* word, ULong minLength, ULong maxLength)
{
	Handle dictionary = FindDictionaryEntry(0)->fDictionary;
	if (!inited)
	{
		InitRandomWords();
		inited = true;
	}
	if (GetDistributedWord(dictionary, word, maxLength, minLength) != 0)
		return;
	for (ULong i = 0; i < 26; i++)
		for (ULong j = 0; j < 26; j++)
			letterPairs[i][j] = 0;
}


// ROM 0x0013e6a8 CommonWord__FPUsUlT2
void
CommonWord(UniChar* word, ULong minLength, ULong maxLength)
{
	GetRandomWord(FindDictionaryEntry(6)->fDictionary, word, minLength, maxLength);
}


// ROM 0x0013e6e0 RangeRand__FiT1
// low .. high, from the C library's rand() (the remainder of the ROM's
// signed division); low when the range is back to front.
long
RangeRand(long low, long high)
{
	if (low > high)
		return low;
	return NewtonRand() % (high - low + 1) + low;
}


// ROM 0x0013e71c GetCharWeight__FcUlUc
// How likely a letter is to be picked: its weight in charWeights, a
// capital's only where capitals are allowed (nought elsewhere), nought
// for an apostrophe and two for anything else.
UByte
GetCharWeight(char c, ULong /*position*/, Boolean capitalsAllowed)
{
	UByte ch = (UByte) c;
	if (ch >= 'A' && ch <= 'Z')
	{
		if (capitalsAllowed)
			return charWeights[ch - 'A'];
		return 0;
	}
	if (ch >= 'a' && ch <= 'z')
		return charWeights[ch - 'a'];
	if (ch != '\'')
		return 2;
	return 0;
}


// ROM 0x0013e77c ChooseWeightedChar__FPcUlUc
// One of the characters at random, in proportion to their weights.  ==>
// its index; nought when nothing has any weight.
//
// ROM BUG, kept: the draw is 1 .. the total and a character is taken
// while the draw is *below* the running total, so a draw of the total
// itself takes none of them and answers the index of the terminator.
ULong
ChooseWeightedChar(char* chars, ULong position, Boolean capitalsAllowed)
{
	ULong n = (ULong) StringLength(chars);
	ULong total = 0;
	for (ULong i = 0; i < n; i++)
		total += GetCharWeight(chars[i], position, capitalsAllowed);
	if (n == 0 || total == 0)
		return 0;
	ULong draw = (ULong) RangeRand(1, (long) total);
	ULong running = 0;
	ULong i = 0;
	for ( ; i < n; i++)
	{
		running += GetCharWeight(chars[i], position, capitalsAllowed);
		if (draw < running)
			return i;
	}
	return i;
}


// ROM 0x0013e82c GetRandomWord__FPP15AirusAParmBlockPUsUlT3
// A word grown a letter at a time down the dictionary: the letters that
// may come next asked for, one picked by weight, the word verified; a
// word that ends where nothing can follow is taken, one where something
// could is taken one time in five.  A word of the wrong length, or one
// that met an apostrophe or a dead end, is started again - a thousand
// times at most, the word left empty if none will do.  A word whose
// attribute says so gets a capital.
void
GetRandomWord(Handle dictionary, UniChar* word, ULong minLength, ULong maxLength)
{
	char nextChars[100];
	char letters[24];
	memset(nextChars, 0, sizeof(nextChars));
	memset(letters, 0, sizeof(letters));
	if (maxLength > 20)
		maxLength = 20;
	ULong tries = 0;
	word[0] = 0;
	for (;;)
	{
		tries++;
		AirusAParmBlock* parms = (AirusAParmBlock*) *dictionary;
		parms->fResult = 0;
		parms->fNode = 0;
		parms->fIndex = 0;
		for (;;)
		{
			parms->fWord = (UByte*) nextChars;
			CallAirusA(dictionary, kAirusNextSet);
			parms = (AirusAParmBlock*) *dictionary;
			ULong available = (ULong) StringLength(nextChars);
			ULong which = ChooseWeightedChar(nextChars, (ULong) parms->fIndex, parms->fIndex != 0);
			if (nextChars[which] == '\'')
				break;
			letters[parms->fIndex] = nextChars[which];
			parms->fWord = (UByte*) letters;
			CallAirusA(dictionary, kAirusVerify);
			parms = (AirusAParmBlock*) *dictionary;
			if (parms->fResult == 2 || (parms->fResult == 1 && RangeRand(0, 4) == 2))
			{
				if (available != 0)
				{
					letters[parms->fIndex] = 0;
					ULong length = (ULong) StringLength(letters);
					if (minLength <= length && length <= maxLength)
					{
						ConvertToUnicode(letters, word, kMacRomanEncoding, 20);
						if ((parms->fAttribute & 0x80) != 0)
							UppercaseText(word, 1);
						return;
					}
				}
				break;
			}
			if (parms->fResult == 3 || (ULong) parms->fIndex == maxLength)
				break;
		}
		if (tries > 1000)
			return;
	}
}


// ROM 0x0013e9c4 ShiftLetter__Fc
char
ShiftLetter(char c)
{
	UByte ch = (UByte) c;
	if (ch >= 'a' && ch <= 'z')
		return (char) (ch - 'a');
	if (ch >= 'A' && ch <= 'Z')
		return (char) (ch - 'A');
	return 26;
}


// ROM 0x0013ea00 InitLetterPairs__Fv
void
InitLetterPairs(void)
{
	for (ULong i = 0; i < 26; i++)
		for (ULong j = 0; j < 26; j++)
			letterPairs[i][j] = 0;
}


// ROM 0x0013ea40 HalveLetterPairs__Fv
void
HalveLetterPairs(void)
{
	for (ULong i = 0; i < 26; i++)
		for (ULong j = 0; j < 26; j++)
			letterPairs[i][j] = (UByte) (letterPairs[i][j] >> 1);
}


// ROM 0x0013ea88 AddLetterPairScore__FPc
// Each pair of letters in the word counted once more; when one of the
// counts reaches 255 every count is halved.
void
AddLetterPairScore(char* word)
{
	ULong length = (ULong) StringLength(word);
	if (length < 2)
		return;
	long previous = ShiftLetter(word[0]);
	for (ULong i = 1; i < length; i++)
	{
		long letter = ShiftLetter(word[i]);
		if (previous != 26 && letter != 26)
		{
			letterPairs[previous][letter]++;
			if (letterPairs[previous][letter] == 0xff)
				HalveLetterPairs();
		}
		previous = letter;
	}
}


// ROM 0x0013eb1c GetLetterPairScore__FPc
// How many of the word's pairs of letters have never been handed out.
long
GetLetterPairScore(char* word)
{
	long score = 0;
	ULong length = (ULong) StringLength(word);
	if (length < 2)
		return 0;
	long previous = ShiftLetter(word[0]);
	for (ULong i = 1; i < length; i++)
	{
		long letter = ShiftLetter(word[i]);
		if (previous != 26 && letter != 26 && letterPairs[previous][letter] == 0)
			score++;
		previous = letter;
	}
	return score;
}


// ROM 0x0013eba8 GetDistributedWord__FPP15AirusAParmBlockPUsUlT3
// Of up to twenty-two random words of no more than eleven letters, the
// one with the most new letter pairs (the shorter of two that tie),
// stopping early at one with five; its pairs are then counted.  ==> its
// score.
long
GetDistributedWord(Handle dictionary, UniChar* word, ULong maxLength, ULong minLength)
{
	char best[12];
	char tried[12];
	UniChar candidate[12];
	memset(best, 0, sizeof(best));
	long bestScore = 0;
	ULong limit = maxLength > 11 ? 11 : maxLength;
	ULong bestLength = maxLength;
	for (ULong tries = 0; ; tries++)
	{
		GetRandomWord(dictionary, candidate, minLength, limit);
		ConvertFromUnicode(candidate, tried, kMacRomanEncoding, 11);
		ULong length = (ULong) StringLength(tried);
		long score = GetLetterPairScore(tried);
		if (bestScore < score || (score == bestScore && length < bestLength))
		{
			CopyCString(best, tried);
			bestScore = score;
			bestLength = length;
		}
		if (bestScore > 4 || tries > 20)
			break;
	}
	AddLetterPairScore(best);
	ConvertToUnicode(best, word, kMacRomanEncoding, 11);
	return bestScore;
}


// ROM 0x0019fbe4 FGetRandomDictionaryWord__FRC6RefVarN21
// GetRandomDictionaryWord(minLength, maxLength): a common word to
// practise, twenty letters at most.
Ref
FGetRandomDictionaryWord(RefArg /*rcvr*/, RefArg minLength, RefArg maxLength)
{
	UniChar word[22];
	ULong low = (ULong) RINT(minLength);
	long high = RINT(maxLength);
	if (high > 20)
		high = 20;
	RandomCommonWord(word, low, (ULong) high);
	return MakeString(word);
}


// ROM 0x001a0890 FGetRandomWord__FRC6RefVarN21
// GetRandomWord(minLength, maxLength): a word out of dictionary 6.
Ref
FGetRandomWord(RefArg /*rcvr*/, RefArg minLength, RefArg maxLength)
{
	UniChar word[22];
	ULong low = (ULong) RINT(minLength);
	long high = RINT(maxLength);
	if (high > 20)
		high = 20;
	CommonWord(word, low, (ULong) high);
	return MakeString(word);
}


void
RegisterRandomWordNatives(void)
{
	RegisterNativeFunction("FGetRandomDictionaryWord__FRC6RefVarN21", (void*) FGetRandomDictionaryWord, 2);
	RegisterNativeFunction("FGetRandomWord__FRC6RefVarN21", (void*) FGetRandomWord, 2);
}
