/*
	File:		recognition/ParseString.cpp

	Contains:	Text read through a lexical dictionary (intl/LexParse.h):
				ParseString walks the string's characters through the
				dictionary one at a time, gathering them into parse buffers
				as each character's attribute says and converting each
				buffer into a field of whatever is being filled in (a
				TDate, a TNumberParser) when it is finished.

				How much of the string is read at all is FindLongestWord's:
				the longest run of whole words (by the locale's line-break
				table) that the dictionary takes as a word, found by
				dropping one word off the end at a time.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Airus.h"
#include "Dictionaries.h"
#include "LexParse.h"
#include "Locale.h"
#include "Text.h"			// FindWordBreaks
#include "RSSymbols.h"
#include "Unicode.h"
#include "ObjectHeap.h"


// ROM 0x001819b0 FindLongestWord__FPP15AirusAParmBlockPUcPUl
// Without the Unicode text there are no word breaks to go by: nothing.
long
FindLongestWord(Handle /*dictionary*/, UByte* /*word*/, ULong* /*length*/)
{
	return -1;
}


// ROM 0x001819b8 FindLongestWord__FPP15AirusAParmBlockPUcPCUsUlPUl
// The longest beginning of the text, cut at a word break, that the
// dictionary (any of its chain) takes as a word; *longest its length (0
// for none).  `word` is the same text in the dictionary's 8-bit encoding.
// ==> the id of the dictionary that took it, -1 for none.
long
FindLongestWord(Handle dictionary, UByte* word, const UniChar* text, ULong length, ULong* longest)
{
	UByte* terminal;
	ULong* position = nil;
	ULong* attribute = nil;
	ULong extra = 0;
	long result = -1;
	RefVar breakTable(GetLocaleSlot(RSSYMlinebreaktable));
	for (;;)
	{
		ULong wordStart, wordEnd;
		FindWordBreaks(text, length, length - 1, true, breakTable, &wordStart, &wordEnd);
		UByte saved = word[wordEnd];
		word[wordEnd] = 0;
		VerifyStart(dictionary);
		VerifyWord(dictionary, word, &terminal, &position, &attribute, true, &extra);
		word[wordEnd] = saved;
		if (airusResult == kAirusIsPrefixAndWord || airusResult == kAirusIsWord)
		{
			result = ((AirusAParmBlock*) *PositionToHandle(dictionary, *position))->fDictID;
			break;
		}
		length = wordStart;
		if (wordStart == 0)
			break;
	}
	*longest = length;
	return result;
}


// ROM 0x0018176c ParseString__FPP15AirusAParmBlockPvPCUsPUlUl
// The first `length` characters of `str` (at most) read through the
// dictionary into `into`: the longest run of words the dictionary knows,
// walked a character at a time, each character's attribute saying what
// it does - 0x40 gather it, 0x80 convert what is gathered, 0xc0 convert
// and start again with it - and naming the converter the buffer goes
// through (ConvertBuffer).  *consumed the characters used (0 on failure).
// ==> the id of the dictionary that took it, -1 for none or when a
// converter refused its value.
static long
ParseString(Handle dictionary, void* into, const UniChar* str, ULong* consumed, ULong length)
{
	if (dictionary == nil)
	{
		*consumed = 0;
		return -1;
	}
	ULong count = Ustrlen(str);
	if (length < count)
		count = length;
	UByte* text = (UByte*) NewPtr(count + 1);
	if (text == nil)
	{
		*consumed = 0;
		return -1;
	}
	ConvertFromUnicode(str, text, kMacRomanEncoding, count);
	ULong longest;
	long result = FindLongestWord(dictionary, text, str, count, &longest);
	if (longest != 0)
	{
		UByte* terminal;
		ULong* position = nil;
		ULong* attribute = nil;
		ULong extra = 0;
		TParseBuffer buffer;
		InitParseBuffer(&buffer);
		for (ULong i = 0; i < longest; i++)
		{
			UByte saved = text[i + 1];
			text[i + 1] = 0;
			VerifyStart(dictionary);
			VerifyWord(dictionary, text, &terminal, &position, &attribute, true, &extra);
			text[i + 1] = saved;
			if (attribute == nil)
			{
				result = -1;
				break;
			}
			ULong action = *attribute & 0xc0;
			ULong converter = *attribute & 0x3f;
			if (action == 0x80 || action == 0xc0)
			{
				if (action == 0x80 && converter != 0)
					buffer.fConverter = converter;
				if (ConvertBuffer(&buffer, into) == 0)
				{
					result = -1;
					break;
				}
				InitParseBuffer(&buffer);
			}
			if (action == 0x40 || action == 0xc0)
			{
				if (buffer.fCount < 0x3f)
				{
					buffer.fChars[buffer.fCount] = text[i];
					buffer.fChars[(UByte) (buffer.fCount + 1)] = 0;
					buffer.fCount++;
				}
				else
				{
					// too long for a buffer: failed, but the walk goes on
					// (with an empty buffer and this character's converter
					// left unset) as the ROM's does
					buffer.fCount = 0;
					result = -1;
					continue;
				}
			}
			if (action != 0x80 && converter != 0)
				buffer.fConverter = converter;
		}
		if (buffer.fCount != 0 && ConvertBuffer(&buffer, into) == 0)
			result = -1;
	}
	DisposePtr((Ptr) text);
	if (result == -1)
		longest = 0;
	*consumed = longest;
	return result;
}


// ROM 0x000ece50 ReplaceDictionaryHandle__FPPPcRC6RefVar
// The lexicon in the current locale's `slot` opened over its binary and
// put in *dictionary, the one there before disposed of; nothing changes
// when the locale has none.
static void
ReplaceDictionaryHandle(Handle* dictionary, RefArg slot)
{
	RefVar words(GetLocaleSlot(slot));
	if (ISNIL(words))
		return;
	Handle replacement = ReadRefDictionary(words);
	if (replacement != nil && *replacement != nil)
	{
		((AirusAParmBlock*) *replacement)->fField4c = 1;
		if (*dictionary != nil)
			DisposDictionary(dictionary);
		*dictionary = replacement;
	}
}


// Handed to intl, which reads dates and numbers through them and
// replaces the lexicons when the locale changes (LexParse.h).
void
InstallParseString(void)
{
	gParseStringProc = ParseString;
	gReplaceDictionaryHandleProc = ReplaceDictionaryHandle;
}
