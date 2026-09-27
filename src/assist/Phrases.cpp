/*
	File:		assist/Phrases.cpp

	Contains:	The Assistant's phrase generator.  See Phrases.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Phrases.h"
#include "Assistant.h"
#include "AssistStrings.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "RSSymbols.h"

#include <stdio.h>

PhrasalGlobals*	gPhrasalGlobals = nil;		// ROM 0x0c100bbc gPhrasalGlobals
Ref				gUtterGenFrame = NILREF;	// ROM 0x0c100bc0 gUtterGenFrame

// frames/StringNatives.cpp
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1


// the sentence's words
static inline Ref
Words(void)
{
	return *gPhrasalGlobals->fWords;
}


// ROM 0x00080830 setPhraseElem__FiN21
// (the grid addressed as the ROM does, length * 16 + start from the
// state's own start: a start past a row's end is the next row's)
void
setPhraseElem(long length, long start, long value)
{
	((UByte*) gPhrasalGlobals)[length * 16 + start] = (UByte) value;
}


// ROM 0x00080848 getPhraseElem__FiT1
long
getPhraseElem(long length, long start)
{
	return ((UByte*) gPhrasalGlobals)[length * 16 + start];
}


// ROM 0x00080860 GeneratePhrases__FRC6RefVarT1
// GenPhrases(sentence): a test of the generator - the grid printed, the
// run of three words from the third struck as a hit, the grid printed
// again, and every run it then hands out printed.
Ref
GeneratePhrases(RefArg /*rcvr*/, RefArg sentence)
{
	RefVar phrase;
	IPhraseGenerator(sentence);
	PrintGeneratorState();
	PhraseHit(3, 3);
	PrintGeneratorState();
	phrase = NextPhrase();
	while (NOTNIL(phrase))
	{
		char text[256];
		ConvertFromUnicode(GetCString(phrase), text, kMacRomanEncoding, sizeof(text) - 1);
		text[sizeof(text) - 1] = 0;
		printf("\r%s", text);
		phrase = NextPhrase();
	}
	printf("\r");
	return TRUEREF;
}


// ROM 0x0008090c PhraseHitExt__Fv
// The run NextPhrase last handed out was known: every untried run that
// overlaps it is struck off, and it is marked a hit.  (As PhraseHit, and
// with its quirk: the first row looked over starts at the start before
// the run's, the rest at the first, and each goes on for as many runs as
// the row has from there, past the row's end.)
void
PhraseHitExt(void)
{
	long hitLength = gPhrasalGlobals->fHitLength;
	long hitStart = gPhrasalGlobals->fHitStart;
	long count = Length(Words());
	long start = hitStart - 1;
	if (start == 0)
		start = 1;
	for (long length = hitLength; length > 0; length--)
	{
		if (count - length != -1)
		{
			ULong n = 1;
			do
			{
				if (getPhraseElem(length, start) == kPhraseUntried
				 && interval_intersection_p(hitLength, hitStart, length, start))
					setPhraseElem(length, start, kPhraseCovered);
				n++;
				start++;
			}
			while (n <= (ULong) (count - length) + 1);
		}
		start = 1;
	}
	setPhraseElem(hitLength, hitStart, kPhraseHit);
}


// ROM 0x00080924 InitDSPhraseSupport__FRC6RefVarT1
// The generator's state, made once.  DEVIATION: the ROM's operator new
// leaves the grid as the heap had it; the host's starts at nought.
Ref
InitDSPhraseSupport(RefArg /*rcvr*/, RefArg /*arg*/)
{
	AddGCRoot(gUtterGenFrame);
	PhrasalGlobals* globals = new PhrasalGlobals();
	if (globals != nil)
		globals->fWords = new RefStruct(NILREF);
	gPhrasalGlobals = globals;
	return TRUEREF;
}


// ROM 0x00080974 UnmatchedWords__FRC6RefVar
// The words that were neither known on their own nor part of a run that
// was.
Ref
UnmatchedWords(RefArg /*rcvr*/)
{
	RefVar unmatched(AllocateArray(RSSYMarray, 0));
	long count = Length(Words());
	for (long start = 1; start <= count; start++)
	{
		long state = getPhraseElem(1, start);
		if (state != kPhraseHit && state != kPhraseCovered)
			Append(RefVar(), unmatched, RefVar(PartialGlueString(RefVar(Words()), start, 1)));
	}
	return unmatched;
}


// ROM 0x00080a70 OrigPhrase__FRC6RefVar
// The sentence's words.
Ref
OrigPhrase(RefArg /*rcvr*/)
{
	return Words();
}


// ROM 0x00080a88 PartialGlueString__FRC6RefVarUlT2
// The run of `length` words from the start'th (from 1), a space between
// each.
Ref
PartialGlueString(RefArg words, ULong start, ULong length)
{
	RefVar glued(MakeString(""));
	ULong end = start + length - 1;
	for ( ; start <= end; start++)
	{
		NStringCat(RefVar(), glued, RefVar(GetArraySlotRef(words, start - 1)));
		if (start < end)
			NStringCat(RefVar(), glued, RefVar(MakeString(" ")));
	}
	return glued;
}


// ROM 0x00080b9c PrintGeneratorState__Fv
// The grid printed, a run a line, longest first.
void
PrintGeneratorState(void)
{
	RefVar phrase;
	ULong count = (ULong) Length(Words());
	for (ULong length = count; (long) length > 0; length--)
	{
		ULong start = 1;
		ULong n = 1;
		if (count - length != (ULong) -1)
		{
			do
			{
				phrase = PartialGlueString(RefVar(Words()), start, length);
				char text[256];
				ConvertFromUnicode(GetCString(phrase), text, kMacRomanEncoding, sizeof(text) - 1);
				text[sizeof(text) - 1] = 0;
				printf("\r[%lu,%lu] : %s = %ld", (unsigned long) start, (unsigned long) length, text, getPhraseElem(length, start));
				n++;
				start++;
			}
			while (n <= count - length + 1);
		}
	}
	printf("\r");
}


// ROM 0x00080c94 interval_intersection_p__FiN31
// Whether the two runs of words overlap.
long
interval_intersection_p(long length1, long start1, long length2, long start2)
{
	long end1 = start1 + length1;
	long end2 = start2 + length2;
	if (start1 == start2 && end1 == end2)
		return 1;
	if (start1 < end2 && start2 < end1)
		return 1;
	if (start2 < end1 && start1 < end2)
		return 1;
	return 0;
}


// ROM 0x00080cd4 PhraseHit__FiT1
// A run was known: as PhraseHitExt, for the run given.
void
PhraseHit(long hitLength, long hitStart)
{
	long count = Length(Words());
	long start = hitStart - 1;
	if (start == 0)
		start = 1;
	for (long length = hitLength; length > 0; length--)
	{
		if (count - length != -1)
		{
			ULong n = 1;
			do
			{
				if (getPhraseElem(length, start) == kPhraseUntried
				 && interval_intersection_p(hitLength, hitStart, length, start))
					setPhraseElem(length, start, kPhraseCovered);
				n++;
				start++;
			}
			while (n <= (ULong) (count - length) + 1);
		}
		start = 1;
	}
	setPhraseElem(hitLength, hitStart, kPhraseHit);
}


// ROM 0x00080da0 IPhraseGenerator__FRC6RefVar
// A sentence to generate the runs of: its words, every run of them (of
// the first fifteen) untried, and the longest to come first.
void
IPhraseGenerator(RefArg sentence)
{
	RefVar words(SplitString(RefVar(), sentence));
	*gPhrasalGlobals->fWords = words;
	long count = Length(Words());
	if (count > 15)
		count = 15;
	for (long length = 1; length <= count; length++)
		for (long start = 1; start <= count - length + 1; start++)
			setPhraseElem(length, start, kPhraseUntried);
	gPhrasalGlobals->fLength = count;
	gPhrasalGlobals->fStart = 1;
	gPhrasalGlobals->fHitLength = 0;
	gPhrasalGlobals->fHitStart = 0;
}


// ROM 0x00080e94 NextPhrase__Fv
// The next untried run, as a string (the start moved on past it); nil
// when there are none left.
Ref
NextPhrase(void)
{
	long count = Length(Words());
	if (gPhrasalGlobals->fLength == 0 || PeekValidPhrase() != kPhraseUntried)
		return NILREF;
	ULong length = (ULong) gPhrasalGlobals->fLength;
	ULong start = (ULong) gPhrasalGlobals->fStart;
	if ((long) ((count + 1) - length) < (long) start)
	{
		gPhrasalGlobals->fStart = 1;
		gPhrasalGlobals->fLength--;
		if (gPhrasalGlobals->fLength == 0 || PeekValidPhrase() != kPhraseUntried)
			return NILREF;
		start = (ULong) gPhrasalGlobals->fStart;
		length = (ULong) gPhrasalGlobals->fLength;
	}
	RefVar phrase(PartialGlueString(RefVar(Words()), start, length));
	gPhrasalGlobals->fStart++;
	return phrase;
}


// ROM 0x00080f7c PeekValidPhrase__Fv
// The generator moved to the next untried run - this length from the
// current start, then each shorter one from the first - which becomes
// the one handed out.  ==> 4, or 0 (everything cleared) when none is
// left.  ROM QUIRK, kept: a row is looked at from the current start for
// as many runs as the whole row has, so it can read past the row's end.
long
PeekValidPhrase(void)
{
	long count = Length(Words());
	long length = gPhrasalGlobals->fLength;
	for (;;)
	{
		if (length < 1)
		{
			gPhrasalGlobals->fLength = 0;
			gPhrasalGlobals->fStart = 0;
			gPhrasalGlobals->fHitLength = 0;
			gPhrasalGlobals->fHitStart = 0;
			return 0;
		}
		long start = gPhrasalGlobals->fStart;
		long runs = count - length + 1;
		for (long n = 1; n <= runs; n++, start++)
		{
			if (getPhraseElem(length, start) == kPhraseUntried)
			{
				gPhrasalGlobals->fLength = length;
				gPhrasalGlobals->fStart = start;
				gPhrasalGlobals->fHitLength = length;
				gPhrasalGlobals->fHitStart = start;
				return kPhraseUntried;
			}
		}
		length--;
		gPhrasalGlobals->fStart = 1;
	}
}


void
RegisterPhraseNatives(void)
{
	RegisterNativeFunction("GeneratePhrases__FRC6RefVarT1", (void*) GeneratePhrases, 1);
	RegisterNativeFunction("UnmatchedWords__FRC6RefVar", (void*) UnmatchedWords, 0);
	RegisterNativeFunction("OrigPhrase__FRC6RefVar", (void*) OrigPhrase, 0);
}
