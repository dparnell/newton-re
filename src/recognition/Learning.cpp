/*
	File:		recognition/Learning.cpp

	Contains:	The writer's own words and the expansions - Learning.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Learning.h"
#include "Dictionaries.h"
#include "Words.h"
#include "Airus.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Interpreter.h"	// NSSend
#include "Locale.h"		// GetPreference
#include "RootView.h"

#include <string.h>


// ROM 0x001aa8ec Capitalized__FPUs
// Whether the word starts with a capital.
//
// The ROM works it out by reading the first two characters as one word,
// lowercasing the first, and comparing the top halfword of what it read
// with the top halfword of what is there now - which is the first
// character, the machine being big-endian - and then putting the two
// bytes of the original back.  Here it is written out as what it does:
// the first character saved, lowered, compared and restored.
Boolean
Capitalized(UniChar* word)
{
	UniChar was = word[0];
	LowercaseText(word, 1);
	if (word[0] != was)
	{
		word[0] = was;
		return true;
	}
	return false;
}


// ROM 0x001aa680 CollectPunctSymbols__FPUsPPUsT2
// The punctuation at the two ends of the word taken off and handed back
// separately, so that what was written around an abbreviation can be
// written around its expansion.
//
// The word is moved down over its leading punctuation in place, and the
// two runs come back as pointers of their own (nil when there is no
// punctuation at that end).  A word that is nothing but punctuation
// comes back as the leading run and an empty word.
void
CollectPunctSymbols(UniChar* word, UniChar** leading, UniChar** trailing)
{
	long length = Ustrlen(word);
	long first = 0;
	long last = length - 1;
	*leading = nil;
	*trailing = nil;
	while (IsPunctSymbol(word, first))
		first++;
	while (last >= 0 && IsPunctSymbol(word, last))
		last--;

	if (first > 0)
	{
		Size size = (Size) (first + 1) * sizeof(UniChar);
		*leading = (UniChar*) NewPtr(size);
		if (*leading != nil)
		{
			BlockMove(word, *leading, size);
			(*leading)[first] = 0;
		}
	}
	if (last < length - 1 && first <= last)
	{
		Size size = (Size) (length - last) * sizeof(UniChar);
		*trailing = (UniChar*) NewPtr(size);
		if (*trailing != nil)
			BlockMove(word + last + 1, *trailing, size);
	}

	long middle = last - first + 1;
	if (middle < 1)
		middle = 0;
	else
		BlockMove(word + first, word, middle * sizeof(UniChar));
	word[middle] = 0;
}


// ROM 0x001aa600 GetExpandIndex__FPUsPUl
// The expand dictionary asked about a word.  What it stores beside a word
// is not the expansion but where the expansion is: the index of the slot
// of the dictionary frame's `list` that holds it.
Boolean
GetExpandIndex(const UniChar* word, ULong* index)
{
	UByte bytes[32];
	ConvertFromUnicode(word, bytes, 1, 0x1f);
	dictListEntry* entry = FindDictionaryEntry(kExpandDictionary);
	ULong* attribute = nil;
	VerifyString(entry->fDictionary, bytes, nil, &attribute, nil);
	if (airusResult == kAirusNotAWord || airusResult == kAirusIsPrefix)
	{
		*index = 0;
		return false;
	}
	*index = *attribute;
	return true;
}


// ROM 0x001aa930 ExpandWord__FPUs
// An abbreviation written out in full.  What the writer wrote around it
// is kept - the punctuation at either end goes back where it was - and a
// capital is carried over: an abbreviation written with a capital letter
// expands into a word with one, unless the expansion already begins with
// one of its own.
//
// ==> a Handle of UniChars the caller disposes of, nil when there is
// nothing to expand.
Handle
ExpandWord(UniChar* word)
{
	Handle result = nil;
	Size size = (Size) (Ustrlen(word) + 1) * sizeof(UniChar);
	UniChar* copy = (UniChar*) NewPtr(size);
	if (copy == nil)
		return nil;
	BlockMove(word, copy, size);

	UniChar* leading;
	UniChar* trailing;
	CollectPunctSymbols(copy, &leading, &trailing);
	long leadLength = leading != nil ? Ustrlen(leading) : 0;
	long trailLength = trailing != nil ? Ustrlen(trailing) : 0;

	Boolean capitalized = Capitalized(copy);
	LowercaseText(copy, 1);

	ULong index;
	if (Ustrlen(copy) != 0 && GetExpandIndex(copy, &index))
	{
		RefVar frame(FindDictionaryFrame(kExpandDictionary));
		RefVar list(GetFrameSlotRef(frame, RSSYMlist));
		RefVar expansion(GetArraySlotRef(list, (long) index));
		if (NOTNIL(expansion))
		{
			ULong length = (ULong) (Length(expansion) - sizeof(UniChar)) / sizeof(UniChar);
			if (length != 0)
			{
				long total = leadLength + (long) length + trailLength;
				result = NewHandle((Size) (total + 1) * sizeof(UniChar));
				if (result != nil)
				{
					UniChar* out = (UniChar*) *result;
					if (leading != nil)
						BlockMove(leading, out, leadLength * sizeof(UniChar));
					UniChar* text = CString(expansion);
					BlockMove(text, out + leadLength, length * sizeof(UniChar));
					if (capitalized && !Capitalized(text))
						UppercaseText(out + leadLength, 1);
					if (trailing != nil)
						BlockMove(trailing, out + leadLength + length,
								  trailLength * sizeof(UniChar));
					out = (UniChar*) *result;
					out[total] = 0;
				}
			}
		}
	}

	if (leading != nil)
		DisposePtr((Ptr) leading);
	if (trailing != nil)
		DisposePtr((Ptr) trailing);
	DisposePtr((Ptr) copy);
	return result;
}


// ROM 0x001aab74 AddWordWithCount__FlPUcUl
// A word added to one of the writer's own dictionaries, with the count
// the frame keeps put up by one.  A dictionary with a `limit` is not
// allowed past it: airusResult comes back -15, which is what the Prefs
// slip turns into "the dictionary is full".
//
// ==> the count as it was before the word was added.
long
AddWordWithCount(long id, UByte* word, ULong attribute)
{
	Boolean counted = false;
	long count = 0;
	dictListEntry* entry = FindDictionaryEntry((ULong) id);
	RefVar frame(GetArraySlotRef(RefVar(Dictionaries()), entry->fIndex));
	RefVar value(GetProtoVariable(frame, RSSYMcount, nil));
	if (NOTNIL(value))
	{
		counted = true;
		count = RINT(value);
		value = GetProtoVariable(frame, RSSYMlimit, nil);
		if (NOTNIL(value) && RINT(value) <= count)
		{
			airusResult = kAirusDictionaryFull;
			return count;
		}
	}

	AddWord(entry->fDictionary, 0, word, attribute);
	if (airusResult == 0 && counted)
	{
		long now = RINT(RefVar(GetProtoVariable(frame, RSSYMcount, nil))) + 1;
		SetFrameSlot(frame, RSSYMcount, RefVar(MAKEINT(now)));
	}
	return count;
}


// ROM 0x001aae08 LastWordSame__FRC6RefVar
// Whether this is the word the auto-add dictionary was offered last
// time.  The word is kept in the dictionary frame's `last` slot, and a
// word that is not the one there takes its place - so the same word
// written twice running is only added once, and a different word in
// between lets it be offered again.
Boolean
LastWordSame(RefArg word)
{
	RefVar frame(FindDictionaryFrame(kAutoAddDictionary));
	RefVar last(GetFrameSlotRef(frame, RSSYMlast));
	RefVar same;
	if (NOTNIL(last))
		same = FStrEqual(RefVar(NILREF), last, word);
	if (NOTNIL(same))
		return true;
	SetFrameSlot(frame, RSSYMlast, word);
	return false;
}


// ROM 0x001aacdc DeleteWordWithCount__FlPUc
// A word taken out of one of the writer's own dictionaries, with the
// count the frame keeps put down by one.  ==> airusResult.
//
// A frame whose count has fallen to nothing is counted again by walking
// the whole dictionary, so that a count that has drifted is put right
// rather than going negative.
long
DeleteWordWithCount(long id, UByte* word)
{
	Boolean counted = false;
	long count = 0;
	dictListEntry* entry = FindDictionaryEntry((ULong) id);
	RefVar frame(GetArraySlotRef(RefVar(Dictionaries()), entry->fIndex));
	RefVar value(GetProtoVariable(frame, RSSYMcount, nil));
	if (NOTNIL(value))
	{
		counted = true;
		count = RINT(value);
	}

	DeleteWord(entry->fDictionary, word);
	if (airusResult == 0 && counted)
	{
		if (count < 1)
			count = WalkDictionary(GetScriptDictRef(frame), (const UByte*) "", nil, nil) + 1;
		SetFrameSlot(frame, RSSYMcount, RefVar(MAKEINT(count - 1)));
	}
	return airusResult;
}


// ROM 0x001aaee4 AddAutoAdd__FPUs
// A word the machine adds to the writer's dictionary on their behalf.
//
// It is only done when the writer has asked for it (`doAutoAdd`) and the
// word came from the handwriting recogniser rather than from the
// keyboard or a script; a word the auto-add dictionary was offered last
// time is skipped, so the same word twice running is only added once;
// and only a well-formed word the dictionaries do not already have is
// added.
//
// The word goes into two dictionaries: the auto-add one plain, which is
// the machine's record of what it did, and the user dictionary encoded,
// which is what the recogniser reads against.  Every twentieth word the
// `autoAdd` view is told, which is what puts up the slip offering to
// show the writer what has been learnt.
//
// (BUG, kept: the answer is set to true before the second add is
//  checked, so a word whose user-dictionary entry failed - and which is
//  therefore taken out of the auto-add dictionary again - is still
//  reported as added.)
Boolean
AddAutoAdd(UniChar* word)
{
	Boolean added = false;
	if (ISNIL(RefVar(GetPreference(RSSYMdoautoadd))) || gWordID != 'WREC')
		return false;

	RefVar str(MakeString(word));
	if (LastWordSame(str))
		return false;
	str = FLookupWord(RefVar(NILREF), str);
	if (ISNIL(str))
		return false;

	UniChar* chars = CString(str);
	Size size = (Size) Ustrlen(chars) + 1;
	if (size > 0x1f)
		return false;
	UByte* bytes = (UByte*) NewPtr(size);
	if (bytes == nil)
		return false;

	ConvertFromUnicode(chars, bytes, 1, size);
	long count = AddWordWithCount(kAutoAddDictionary, bytes, 0);
	if (airusResult == 0)
	{
		Boolean undo = true;
		UniChar* copy = (UniChar*) NewPtr(size * sizeof(UniChar));
		if (copy != nil)
		{
			Ustrcpy(copy, chars);
			ULong attribute = EncodeRecognitionWord(copy);
			ConvertFromUnicode(copy, bytes, 1, size);
			AddWordWithCount(kUserDictionary, bytes, attribute);
			// DEVIATION: the ROM has a root view by the time anything is
			// written; a host may run the recogniser without a view system,
			// and then there is nobody to tell.
			if (count % 20 == 0 && gRootView != nil)
				NSSend(RefVar(gRootView->GetVar(RSSYMautoadd)), RSSYMaddnotification);
			added = true;
			DisposePtr((Ptr) copy);
			if (airusResult == 0)
				undo = false;
		}
		if (undo)
		{
			// the auto-add entry taken back out, so that the two
			// dictionaries say the same thing
			ConvertFromUnicode(chars, bytes, 1, size);
			DeleteWordWithCount(kAutoAddDictionary, bytes);
		}
	}
	DisposePtr((Ptr) bytes);
	return added;
}


// ROM 0x001ab0f8 RemoveAutoAdd__FPUs
// A word the machine added taken back out of both dictionaries - the
// auto-add one plain and the user dictionary encoded - which is what
// happens when the entry the word was learnt from goes.
void
RemoveAutoAdd(UniChar* word)
{
	Size size = (Size) Ustrlen(word) + 1;
	UByte* bytes = (UByte*) NewPtr(size);
	UniChar* copy = (UniChar*) NewPtr(size * sizeof(UniChar));
	if (bytes != nil && copy != nil)
	{
		Ustrcpy(copy, word);
		StripRecognitionWordDiacritsOK(copy);
		ConvertFromUnicode(copy, bytes, 1, size);
		DeleteWordWithCount(kAutoAddDictionary, bytes);
		if (airusResult == 0)
		{
			EncodeRecognitionWord(copy);
			ConvertFromUnicode(copy, bytes, 1, size);
			DeleteWordWithCount(kUserDictionary, bytes);
		}
	}
	if (bytes != nil)
		DisposePtr((Ptr) bytes);
	if (copy != nil)
		DisposePtr((Ptr) copy);
}
