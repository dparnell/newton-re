/*
	File:		assist/Heuristics.cpp

	Contains:	The Assistant's heuristics.  See Heuristics.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Heuristics.h"
#include "Assistant.h"
#include "AssistStrings.h"
#include "Lexicon.h"
#include "Soups.h"
#include "Cursors.h"
#include "Frames.h"
#include "REPTranslators.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "host/RomBugs.h"

#include <string.h>

Ref		gWhoHistory = NILREF;		// ROM 0x0c100b98 gWhoHistory
Ref		gWhatHistory = NILREF;		// ROM 0x0c100b9c gWhatHistory
Ref		gWhenHistory = NILREF;		// ROM 0x0c100ba0 gWhenHistory
Ref		gWhereHistory = NILREF;		// ROM 0x0c100ba4 gWhereHistory
Ref		gWhoObj = NILREF;			// ROM 0x0c100ba8 gWhoObj
Ref		gWhatObj = NILREF;			// ROM 0x0c100bac gWhatObj
Ref		gWhenObj = NILREF;			// ROM 0x0c100bb0 gWhenObj
Ref		gWhereObj = NILREF;			// ROM 0x0c100bb4 gWhereObj

// the short words a name or place is not made of ("the", "at", ...)
const Ref	kStopWords = MAKEMAGICPTR(270);
// the words that introduce a place ("at", "in", ...)
const Ref	kPlaceWords = MAKEMAGICPTR(545);
// the characters a word's end may carry that are not part of it
const Ref	kTrailingPunctuation = MAKEMAGICPTR(249);
// the words a date can be (Lexicon.cpp's kDateWords)
const Ref	kWackyDateWords = MAKEMAGICPTR(113);

// frames/StringNatives.cpp
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1
Ref		FFindStringInArray(RefArg rcvr, RefArg array, RefArg str);	// ROM 0x001fe3c8 FFindStringInArray__FRC6RefVarN21
Ref		FStrLen(RefArg rcvr, RefArg str);							// ROM 0x001fd0a8 FStrLen__FRC6RefVarT1
Ref		FStrConcat(RefArg rcvr, RefArg a, RefArg b);				// ROM 0x001fc970 FStrConcat__FRC6RefVarN21


/*------------------------------------------------------------------------------
	T h e   h i s t o r i e s
------------------------------------------------------------------------------*/

// ROM 0x0007ebb4 ShuffleHistory__FRC6RefVarT1
// The item made the newest of the three: the other two moved down one and
// the oldest dropped.  ==> nil when there is no history.
Ref
ShuffleHistory(RefArg history, RefArg item)
{
	if (ISNIL(history))
		return NILREF;
	for (long slot = 2; slot != 0; slot--)
		SetArraySlotRef(history, slot, GetArraySlotRef(history, slot - 1));
	SetArraySlotRef(history, 0, item);
	return TRUEREF;
}


// ROM 0x0007ec48 SelectHistList__FRC6RefVar
// The history a thing belongs in, by what it is a kind of.
Ref
SelectHistList(RefArg thing)
{
	if (NOTNIL(ISATest(RefVar(), thing, RefVar(gWhoObj))))
		return gWhoHistory;
	if (NOTNIL(ISATest(RefVar(), thing, RefVar(gWhatObj))))
		return gWhatHistory;
	if (NOTNIL(ISATest(RefVar(), thing, RefVar(gWhenObj))))
		return gWhenHistory;
	if (NOTNIL(ISATest(RefVar(), thing, RefVar(gWhereObj))))
		return gWhereHistory;
	return NILREF;
}


// ROM 0x0007fca8 RecordHistory__FRC6RefVar
// The thing remembered in its history, unless it is there already.
// ==> TRUE when it went in.
Ref
RecordHistory(RefArg thing)
{
	RefVar history(SelectHistList(thing));
	if (ISNIL(member_p(history, thing)))
	{
		ShuffleHistory(history, thing);
		return TRUEREF;
	}
	return NILREF;
}


// ROM 0x0007fd18 History__FRC6RefVarT1
// Hist(thing): the history of its kind.
Ref
History(RefArg /*rcvr*/, RefArg thing)
{
	return SelectHistList(thing);
}


// ROM 0x0007fd20 AddHistory__FRC6RefVarT1
// AddHist(thing)
Ref
AddHistory(RefArg /*rcvr*/, RefArg thing)
{
	return RecordHistory(thing);
}


// ROM 0x0007fd28 InitDSHeuristics__FRC6RefVarT1
// The four histories, three long, and the four classes that choose
// between them.
Ref
InitDSHeuristics(RefArg /*rcvr*/, RefArg /*arg*/)
{
	gWhoHistory = AllocateArray(RSSYMhistory, 3);
	AddGCRoot(gWhoHistory);
	gWhatHistory = AllocateArray(RSSYMhistory, 3);
	AddGCRoot(gWhatHistory);
	gWhenHistory = AllocateArray(RSSYMhistory, 3);
	AddGCRoot(gWhenHistory);
	gWhereHistory = AllocateArray(RSSYMhistory, 3);
	AddGCRoot(gWhereHistory);
	gWhoObj = GetFrameSlotRef(kAssistantFrame, RSSYMwho_obj);
	AddGCRoot(gWhoObj);
	gWhatObj = GetFrameSlotRef(kAssistantFrame, RSSYMwhat_obj);
	// ROM BUG (fixed): gWhoObj is made a root a second time and gWhatObj
	// never is (harmless: the class lives in the ROM, which never moves).
	// The fix makes gWhatObj the root.
	if (RomBugFixed())
		AddGCRoot(gWhatObj);
	else
		AddGCRoot(gWhoObj);
	gWhenObj = GetFrameSlotRef(kAssistantFrame, RSSYMwhen_obj);
	AddGCRoot(gWhenObj);
	gWhereObj = GetFrameSlotRef(kAssistantFrame, RSSYMwhere_obj);
	AddGCRoot(gWhereObj);
	return TRUEREF;
}


/*------------------------------------------------------------------------------
	T h e   t a s k   s c r i p t s '   h e l p e r s
------------------------------------------------------------------------------*/

// The first word of the Lexicon of a thing (a symbol names its frame).
static Ref
LexiconWord(RefArg thing)
{
	RefVar frame(IsSymbol(thing) ? MapSymToFrame(RefVar(), thing) : (Ref) thing);
	RefVar lexicon(GetFrameSlotRef(frame, RSSYMlexicon));
	RefVar first(GetArraySlotRef(lexicon, 0));
	return GetArraySlotRef(first, 0);
}


// ROM 0x0007edc0 DSConstructSubjectLine__FRC6RefVarT1
// ConstructSubjectLine(phrases): the word a meeting's subject is made of
// - a meal's ("lunch") when the sentence names one, else the scheduling
// word's ("meet") - the first of its Lexicon, the last of each kind
// found winning.
Ref
DSConstructSubjectLine(RefArg /*rcvr*/, RefArg phrases)
{
	RefVar phrase;
	RefVar item;
	RefVar schedule;
	RefVar meal;
	ULong count = (ULong) Length(phrases);
	for (ULong i = 0; i < count; i++)
	{
		phrase = GetArraySlotRef(phrases, i);
		ULong n = (ULong) Length(phrase);
		for (ULong j = 0; j < n; j++)
		{
			item = GetArraySlotRef(phrase, j);
			if (ISNIL(ISATest(RefVar(), item, RSSYMschedule_act)))
			{
				if (NOTNIL(ISATest(RefVar(), item, RSSYMmeal_act)))
					meal = LexiconWord(item);
			}
			else
				schedule = LexiconWord(item);
		}
	}
	return NOTNIL(meal) ? (Ref) meal : (Ref) schedule;
}


// ROM 0x0007f15c DSFindPossibleName__FRC6RefVarN21
// FindPossibleName(words, all): the capitalised words (the stop words
// taken out) that stand next to each other in `all`, glued back together
// - one to four of them; nil otherwise.
Ref
DSFindPossibleName(RefArg /*rcvr*/, RefArg words, RefArg all)
{
	RefVar filtered(PhraseFilter(RefVar(), words));
	ULong count;
	if (ISNIL(filtered) || (count = (ULong) Length(filtered)) == 0)
		return NILREF;
	RefVar word;
	RefVar index;
	RefVar name(AllocateArray(RSSYMarray, 0));
	long last = -1;
	for (ULong i = 0; i < count; i++)
	{
		word = GetArraySlotRef(filtered, i);
		UniChar first = *(const UniChar*) BinaryData(word);
		if (first > 0x40 && first < 0x5b)
		{
			index = FFindStringInArray(RefVar(), all, word);
			if (NOTNIL(index))
			{
				if (last >= 0 && RINT(index) != last + 1)
					break;
				last = RINT(index);
				AddArraySlot(name, word);
			}
		}
	}
	ULong n = (ULong) Length(name);
	if (n != 0 && n < 5)
		return GlueStrings(RefVar(), name);
	return NILREF;
}


// ROM 0x0007f3b0 DSFindPossibleLocation__FRC6RefVarN21
// FindPossibleLocation(unmatched, words): the words after the first place
// word ("at", "in") that has any, up to the first that is not unmatched
// or is a stop word, glued together; nil when there are none.
Ref
DSFindPossibleLocation(RefArg /*rcvr*/, RefArg unmatched, RefArg words)
{
	RefVar placeWords(kPlaceWords);
	ULong places = (ULong) Length(kPlaceWords);
	if (places == 0)
		return NILREF;
	RefVar found;
	RefVar stopWords(kStopWords);
	RefVar location(AllocateArray(RSSYMarray, 0));
	ULong count = (ULong) Length(words);
	for (ULong slot = 0; ; slot++)
	{
		if (slot >= places || Length(location) != 0)
		{
			if (Length(location) < 1)
				return NILREF;
			return GlueStrings(RefVar(), location);
		}
		found = FFindStringInArray(RefVar(), words, RefVar(GetArraySlotRef(placeWords, slot)));
		if (ISNIL(found))
			continue;
		for (ULong i = (ULong) RINT(found) + 1; i < count; i++)
		{
			found = GetArraySlotRef(words, i);
			if (ISNIL(FFindStringInArray(RefVar(), unmatched, found)))
				break;
			if (NOTNIL(FFindStringInArray(RefVar(), stopWords, found)))
				break;
			Append(RefVar(), location, found);
		}
	}
}


// ROM 0x0007f678 DSFilterStringsAux__FRC6RefVarN21
// FilterStringsAux(word, words): the words without the first that is the
// word (case aside).
Ref
DSFilterStringsAux(RefArg /*rcvr*/, RefArg word, RefArg words)
{
	Boolean found = false;
	RefVar kept(AllocateArray(RSSYMarray, 0));
	RefVar item;
	ULong count = (ULong) Length(words);
	for (ULong i = 0; i < count; i++)
	{
		item = GetArraySlotRef(words, i);
		if (!found)
		{
			if (ISNIL(DSStringEQ(RefVar(), item, word)))
				Append(RefVar(), kept, item);
			else
				found = true;
		}
		else
			Append(RefVar(), kept, item);
	}
	return kept;
}


// ROM 0x0007f818 DSFilterStrings__FRC6RefVarN21
// FilterStrings(str, words): the words without the string's own words.
Ref
DSFilterStrings(RefArg /*rcvr*/, RefArg str, RefArg words)
{
	RefVar kept(words);
	RefVar parts(SplitString(RefVar(), str));
	ULong count = (ULong) Length(parts);
	for (ULong i = 0; i < count; i++)
		kept = DSFilterStringsAux(RefVar(), RefVar(GetArraySlotRef(parts, i)), kept);
	return kept;
}


// ROM 0x0007f918 DSScanForWackyName__FRC6RefVarN21
// ScanForWackyName(phrases, strings): the first phrase read as a date
// whose words are one of the date words ("may", "june" - which could be
// names as well); nil for none.
Ref
DSScanForWackyName(RefArg /*rcvr*/, RefArg phrases, RefArg strings)
{
	ULong count = (ULong) Length(phrases);
	for (ULong i = 0; i < count; i++)
	{
		ULong n = (ULong) Length(RefVar(GetArraySlotRef(phrases, i)));
		for (ULong j = 0; j < n; j++)
		{
			RefVar phrase(GetArraySlotRef(phrases, i));
			if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(phrase, j)), RSSYMdate))
			 && NOTNIL(FFindStringInArray(RefVar(), RefVar(kWackyDateWords), RefVar(GetArraySlotRef(strings, i)))))
				return GetArraySlotRef(strings, i);
		}
	}
	return NILREF;
}


// ROM 0x0007fae0 DSGetMatchedEntries__FRC6RefVarN21
// GetMatchedEntries(kind, entries): the people ('person), the places
// ('places) or both ('allEntries) the parse recorded, as one list.
Ref
DSGetMatchedEntries(RefArg /*rcvr*/, RefArg kind, RefArg entries)
{
	RefVar result;
	RefVar found;
	if (EQRef(kind, RSSYMperson) || EQRef(kind, RSSYMallentries))
	{
		found = GetFrameSlotRef(entries, RSSYMperson);
		if (NOTNIL(found))
			result = FSetUnion(RefVar(), result, found, RefVar(TRUEREF));
	}
	if (EQRef(kind, RSSYMplaces) || EQRef(kind, RSSYMallentries))
	{
		found = GetFrameSlotRef(entries, RSSYMplaces);
		if (NOTNIL(found))
			result = FSetUnion(RefVar(), result, found, RefVar(TRUEREF));
	}
	return result;
}


// ROM 0x0007fe74 SuffixP__FRC6RefVarT1
// SuffixP(word): the word without its last character when that is one of
// the trailing punctuation marks (magic pointer 249) - "Bob," is "Bob";
// nil when it is not, or the word is that character alone, or it is 51
// characters or more.
Ref
SuffixP(RefArg /*rcvr*/, RefArg word)
{
	char buffer[54];
	char ascii[54];
	RefVar marks;
	RefVar last;
	if (ISNIL(word))
		return NILREF;
	marks = kTrailingPunctuation;
	ULong chars = (ULong) Length(word) / sizeof(UniChar);		// the nul among them
	if ((long) (chars - 1) >= 0x33)
		return NILREF;
	ConvertFromUnicode(GetCString(word), ascii + 2, kMacRomanEncoding, 0x7fffffff);
	buffer[0] = ascii[chars];
	buffer[1] = 0;
	last = MakeString(buffer);
	long count = Length(marks);
	for (long i = 0; i < count; i++)
	{
		if (ISNIL(DSStringEQ(RefVar(), last, RefVar(GetArraySlotRef(marks, i)))))
			continue;
		if (chars == 2)
			return NILREF;
		for (long j = 0; j < (long) (chars - 2); j++)
			buffer[j + 4] = ascii[j + 2];
		buffer[chars + 2] = 0;
		return MakeString(buffer + 4);
	}
	return NILREF;
}


// ROM 0x00080060 PrefixP__FRC6RefVarT1
// PrefixP(word): whether the word is a salutation ("Mr", "Dr").
Ref
PrefixP(RefArg /*rcvr*/, RefArg word)
{
	RefVar meanings(FastStringLookup(RefVar(), word));
	if (NOTNIL(meanings))
	{
		long count = Length(meanings);
		for (long i = 0; i < count; i++)
			if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(meanings, i)), RSSYMsalutationprefix)))
				return TRUEREF;
	}
	return NILREF;
}


// ROM 0x00080174 GuessAddressee__FRC6RefVarT1
// GuessAddressee(str): the cards of the person a letter's opening lines
// are to - "Dear Mr Smith," looks up "Smith": the first seven words,
// tidied; the first that is not a salutation and ends in a punctuation
// mark, or the one or two words after a salutation; else all of them.
Ref
GuessAddressee(RefArg /*rcvr*/, RefArg str)
{
	RefVar words;
	RefVar word;
	RefVar second;
	RefVar third;
	RefVar suffixed;
	if (ISNIL(str))
		return NILREF;
	words = TrimBlanksAndPunct(RefVar(), str);
	if (ISNIL(words))
		return NILREF;
	RefVar diced(StringDicer(words, 7));
	ULong count = (ULong) Length(diced);
	words = AllocateArray(RSSYMarray, count);
	ULong kept = 0;
	for (ULong i = 0; i < count; i++)
	{
		word = TrimBlanksAndPunct(RefVar(), RefVar(GetArraySlotRef(diced, i)));
		if (NOTNIL(word))
			SetArraySlotRef(words, kept++, word);
	}
	SetLength(words, kept);
	for (ULong i = 0; i < kept; i++)
	{
		word = GetArraySlotRef(words, i);
		if (ISNIL(PrefixP(RefVar(), word)))
		{
			suffixed = SuffixP(RefVar(), word);
			if (NOTNIL(suffixed))
			{
				words = AllocateArray(RSSYMarray, 1);
				SetArraySlotRef(words, 0, suffixed);
				return IASmarterCFLookup(RefVar(), words);
			}
		}
		else if (i + 1 < kept)
		{
			second = GetArraySlotRef(words, i + 1);
			suffixed = SuffixP(RefVar(), second);
			if (ISNIL(suffixed))
			{
				if (i + 2 < kept)
				{
					third = GetArraySlotRef(words, i + 2);
					suffixed = SuffixP(RefVar(), third);
					words = AllocateArray(RSSYMarray, 2);
					SetArraySlotRef(words, 0, second);
					SetArraySlotRef(words, 1, ISNIL(suffixed) ? (Ref) third : (Ref) suffixed);
					return IASmarterCFLookup(RefVar(), words);
				}
				words = AllocateArray(RSSYMarray, 1);
				SetArraySlotRef(words, 0, second);
				return IASmarterCFLookup(RefVar(), words);
			}
			words = AllocateArray(RSSYMarray, 1);
			SetArraySlotRef(words, 0, suffixed);
			return IASmarterCFLookup(RefVar(), words);
		}
	}
	return IASmarterCFLookup(RefVar(), words);
}


// ROM 0x00080710 DSFindPossiblePhone__FRC6RefVarT1
// FindPossiblePhone(phrases): the index of the first phrase read as a
// phone number; nil for none.
Ref
DSFindPossiblePhone(RefArg /*rcvr*/, RefArg phrases)
{
	RefVar phrase;
	ULong count = (ULong) Length(phrases);
	for (ULong i = 0; i < count; i++)
	{
		phrase = GetArraySlotRef(phrases, i);
		ULong n = (ULong) Length(phrase);
		for (ULong j = 0; j < n; j++)
			if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(phrase, j)), RSSYMparsed_phone)))
				return MAKEINT(i);
	}
	return NILREF;
}


// ROM 0x0008105c GetCardFileSoup__Fv
// The Names union soup (the ROM has GetUnionSoup 0x0035ff60 inline).
Ref
GetCardFileSoup(void)
{
	return GetUnionSoup(RefVar(Rcardfilesoupname));
}


/*------------------------------------------------------------------------------
	T h e   N a m e s   f i l e
------------------------------------------------------------------------------*/

// ROM 0x00081068 IASmartCFLookup__FRC6RefVarT1
// SmartCFQuery(str): the cards that match the string (remembered as the
// last looked for).
Ref
IASmartCFLookup(RefArg /*rcvr*/, RefArg str)
{
	gLastLookupString = str;
	return StringToFrameMapper(RefVar(), str);
}


// ROM 0x000810bc StringToFrameMapper__FRC6RefVarT1
// StringToFrame(str): up to fifteen Names cards the string's words are
// found in (the Assistant's dsQuery with the words); nil when there are
// none, the string has more than four words or any of them is a single
// character.
Ref
StringToFrameMapper(RefArg /*rcvr*/, RefArg str)
{
	RefVar found(AllocateArray(RSSYMarray, 0));
	RefVar words(SplitString(RefVar(), str));
	long count = Length(words);
	if (count > 4)
		return NILREF;
	for (long i = 0; i < count; i++)
		if (RINT(FStrLen(RefVar(), RefVar(GetArraySlotRef(words, i)))) == 1)
			return NILREF;
	RefVar query(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMdsquery))));
	SetFrameSlot(query, RSSYMwords, words);
	RefVar soup(GetCardFileSoup());
	RefVar cursor(Query(soup, query));
	long n = 0;
	RefVar entry(CursorEntry(cursor));
	while (NOTNIL(entry))
	{
		if (++n > 15)
			break;
		Append(RefVar(), found, entry);
		CursorNext(cursor);
		entry = CursorEntry(cursor);
	}
	if (Length(found) < 1)
		return NILREF;
	return found;
}


// ROM 0x00081ddc TagStringHelper__FRC6RefVar6RefVar
// Whether a name frame's first and last names (either alone when the
// other is missing or rich) hold every one of the words: ['person], or
// nil.
Ref
TagStringHelper(RefArg name, RefVar words)
{
	RefVar first(GetFrameSlotRef(name, RSSYMfirst));
	RefVar last(GetFrameSlotRef(name, RSSYMlast));
	RefVar full;
	if (ISNIL(first) || IsRichString(first))
		full = last;
	else if (ISNIL(last) || IsRichString(last))
		full = first;
	else
	{
		full = FStrConcat(RefVar(), first, RefVar(MakeString(" ")));
		full = FStrConcat(RefVar(), full, last);
	}
	if (ISNIL(full))
		return NILREF;
	RefVar kinds(AllocateArray(RSSYMarray, 0));
	RefVar parts(SplitString(RefVar(), full));
	if (NOTNIL(DSPartialStrMatch(RefVar(), words, parts)))
		return UniqueAppendItem(RefVar(), kinds, RSSYMperson);
	return NILREF;
}


// ROM 0x000813ec DSTagString__FRC6RefVarN31
// TagString(entries, str, info): the cards looked over, slot by slot, for
// one that holds every word of the string - a name (or one of the names
// of the people a card lists), a company, a custom field, a group or a
// title - each hit recorded in `info` (AddEntry, through the card's
// alias) under what it matched.  ==> the phrase as a person
// (TagPhraseFrame over what was matched), or nil.
Ref
DSTagString(RefArg /*rcvr*/, RefArg entries, RefArg str, RefArg info)
{
	RefVar entry;
	RefVar alias;
	RefVar matched;
	RefVar value;
	RefVar kinds;
	RefVar valueClass;
	RefVar words;
	RefVar parts;
	if (ISNIL(entries))
		return NILREF;
	matched = AllocateArray(RSSYMarray, 0);
	long count = Length(entries);
	words = SplitString(RefVar(), str);
	for (long i = 0; i < count; i++)
	{
		entry = GetArraySlotRef(entries, i);
		alias = MakeEntryAlias(entry);
		SetFrameSlot(info, RSSYMpersonadded, RefVar(NILREF));
		SetFrameSlot(info, RSSYMplaceadded, RefVar(NILREF));
		TObjectIterator iter(entry);
		for ( ; !iter.Done(); iter.Next())
		{
			value = iter.Value();
			if (ISNIL(value))
				continue;
			RefVar tag(iter.Tag());
			valueClass = ClassOf(value);
			long kind;
			if (!IsString(value))
			{
				if (EQRef(tag, RSSYMname))
					kind = 0;
				else if (EQRef(tag, RSSYMnames))
					kind = 2;
				else
					kind = -1;
			}
			else if (EQRef(tag, RSSYMgroup))
				kind = 4;
			else if (EQRef(valueClass, RSSYMcompany))
				kind = 1;
			else if (EQRef(valueClass, RSSYMstring_2Ecustom))
				kind = 3;
			else if (EQRef(tag, RSSYMtitle))
				kind = 5;
			else
				kind = -1;
			if (EQRef(tag, RSSYMsorton))
				continue;
			switch (kind)
			{
			case 0:
				kinds = TagStringHelper(value, words);
				if (NOTNIL(kinds))
				{
					UniqueAppendListGen(RefVar(), matched, kinds);
					AddEntry(RefVar(), RSSYMperson, RefVar(MAKEINT(0)), alias, info);
				}
				break;
			case 1:
				parts = SplitString(RefVar(), value);
				if (NOTNIL(DSPartialStrMatch(RefVar(), words, parts)))
				{
					AddEntry(RefVar(), RSSYMcompany, RefVar(MAKEINT(0)), alias, info);
					UniqueAppendItem(RefVar(), matched, RSSYMperson);
				}
				break;
			case 2:
			{
				RefVar name;
				long names = Length(value);
				for (long j = 0; j < names; j++)
				{
					name = GetArraySlotRef(value, j);
					kinds = TagStringHelper(name, words);
					if (NOTNIL(kinds))
					{
						UniqueAppendListGen(RefVar(), matched, kinds);
						AddEntry(RefVar(), RSSYMaffiliate, RefVar(MAKEINT(j)), alias, info);
					}
				}
				break;
			}
			case 3:
				parts = SplitString(RefVar(), value);
				if (NOTNIL(DSPartialStrMatch(RefVar(), words, parts)))
				{
					AddEntry(RefVar(), RSSYMcustom, tag, alias, info);
					UniqueAppendItem(RefVar(), matched, RSSYMperson);
				}
				break;
			case 4:
				parts = SplitString(RefVar(), value);
				if (NOTNIL(DSPartialStrMatch(RefVar(), words, parts)))
				{
					AddEntry(RefVar(), RSSYMgroup, RefVar(MAKEINT(0)), alias, info);
					UniqueAppendItem(RefVar(), matched, RSSYMperson);
				}
				// ROM BUG (fixed): a group falls through into the title's
				// case, so a group that matches is recorded as a title too.
				// The fix stops at the group, with the break the other
				// cases have.
				if (RomBugFixed())
					break;
			case 5:
				parts = SplitString(RefVar(), value);
				if (NOTNIL(DSPartialStrMatch(RefVar(), words, parts)))
				{
					AddEntry(RefVar(), RSSYMtitle, RefVar(MAKEINT(0)), alias, info);
					UniqueAppendItem(RefVar(), matched, RSSYMperson);
				}
				break;
			}
		}
	}
	if (Length(matched) > 0)
		return TagPhraseFrame(RefVar(), matched, str);
	return NILREF;
}


// ROM 0x000820ec DSResolveString__FRC6RefVarN21
// ResolveString(str, info): a phrase no lexicon knows read as a person
// out of the Names file: the cards the whole phrase finds, else those
// its first word finds; nil when it has a stop word in it or nothing is
// found.
Ref
DSResolveString(RefArg /*rcvr*/, RefArg str, RefArg info)
{
	RefVar words(SplitString(RefVar(), str));
	if (NOTNIL(DSHasStopString(RefVar(), words)))
		return NILREF;
	RefVar result(DSTagString(RefVar(), RefVar(StringToFrameMapper(RefVar(), str)), str, info));
	if (ISNIL(result))
	{
		result = IASmartCFLookup(RefVar(), str);
		if (ISNIL(result))
			return NILREF;
		return DSTagString(RefVar(), result, RefVar(GetArraySlotRef(words, 0)), info);
	}
	return result;
}


// ROM 0x000822f4 PhraseFilter__FRC6RefVarT1
// PhraseFilter(words): the words without the short ones (two to four
// characters) on the stop list; nil when none is left.
Ref
PhraseFilter(RefArg /*rcvr*/, RefArg words)
{
	if (ISNIL(words))
		return NILREF;
	RefVar stopWords(kStopWords);
	RefVar kept(AllocateArray(RSSYMarray, 0));
	RefVar word;
	ULong count = (ULong) Length(words);
	for (ULong i = 0; i < count; i++)
	{
		word = GetArraySlotRef(words, i);
		ULong chars = (ULong) Length(word) / sizeof(UniChar) - 1;
		if (chars < 2 || chars > 4)
			Append(RefVar(), kept, word);
		else if (ISNIL(FFindStringInArray(RefVar(), stopWords, word)))
			Append(RefVar(), kept, word);
	}
	if (Length(kept) > 0)
		return kept;
	return NILREF;
}


// ROM 0x000824bc IASmarterCFLookup__FRC6RefVarT1
// The cards all the words find (each word's trailing punctuation taken
// off): the first word's cards, narrowed by each of the others' that
// finds any.
Ref
IASmarterCFLookup(RefArg /*rcvr*/, RefArg words)
{
	RefVar list(words);
	if (ISNIL(list))
		return NILREF;
	list = PhraseFilter(RefVar(), list);
	if (ISNIL(list))
		return NILREF;
	RefVar word;
	if (Length(list) < 2)
	{
		word = SuffixP(RefVar(), RefVar(GetArraySlotRef(list, 0)));
		if (ISNIL(word))
			word = GetArraySlotRef(list, 0);
		return StringToFrameMapper(RefVar(), word);
	}
	word = SuffixP(RefVar(), RefVar(GetArraySlotRef(list, 0)));
	if (ISNIL(word))
		word = GetArraySlotRef(list, 0);
	RefVar cards(StringToFrameMapper(RefVar(), word));
	RefVar more;
	long count = Length(list);
	for (long i = 1; i < count; i++)
	{
		word = SuffixP(RefVar(), RefVar(GetArraySlotRef(list, i)));
		if (ISNIL(word))
			word = GetArraySlotRef(list, i);
		more = StringToFrameMapper(RefVar(), word);
		if (ISNIL(cards))
		{
			if (ISNIL(more))
				continue;
			// (the ROM then intersects the list with itself)
			cards = more;
		}
		if (NOTNIL(more) && NOTNIL(cards))
			cards = CheezyIntersect(RefVar(), cards, more);
	}
	if (ISNIL(cards) || Length(cards) < 1)
		return NILREF;
	return cards;
}


/*------------------------------------------------------------------------------
	P h o n e   n u m b e r s
------------------------------------------------------------------------------*/

// vars.PhoneTypes.phoneText: the names of the kinds of phone number
static Ref
PhoneTexts(void)
{
	RefVar types(GetFrameSlotRef(gVarFrame, RSSYMphonetypes));
	return GetFrameSlotRef(types, RSSYMphonetext);
}


// the numbers that are neither nil nor empty
static Ref
RealPhones(RefArg phones)
{
	RefVar real(AllocateArray(RSSYMarray, 0));
	RefVar phone;
	long count = Length(phones);
	for (long i = 0; i < count; i++)
	{
		phone = GetArraySlotRef(phones, i);
		if (NOTNIL(phone) && ISNIL(FStrEqual(RefVar(), phone, RefVar(MakeString("")))))
			Append(RefVar(), real, phone);
	}
	return real;
}


// ROM 0x000828b0 GenPhoneTypeList__FRC6RefVarT1
// GenPhoneTypes(phones): each number labelled with its kind - "work
// (555 1234)"; nil when there are none.
Ref
GenPhoneTypeList(RefArg /*rcvr*/, RefArg phones)
{
	RefVar list(AllocateArray(RSSYMarray, 0));
	RefVar phone;
	RefVar label;
	long count = Length(phones);
	for (long i = 0; i < count; i++)
	{
		label = PhoneSymToString(RefVar(), RefVar(ClassOf(RefVar(GetArraySlotRef(phones, i)))));
		phone = GetArraySlotRef(phones, i);
		if (NOTNIL(phone) && NOTNIL(label)
		 && ISNIL(FStrEqual(RefVar(), phone, RefVar(MakeString("")))))
		{
			label = StringAnnotate(RefVar(), label, phone);
			Append(RefVar(), list, label);
		}
	}
	if (Length(list) < 1)
		return NILREF;
	return list;
}


// ROM 0x00082ad8 PhoneSymToString__FRC6RefVarT1
// The name of a kind of phone number (vars.PhoneTypes.phoneText).
Ref
PhoneSymToString(RefArg /*rcvr*/, RefArg sym)
{
	RefVar texts(PhoneTexts());
	if (NOTNIL(sym))
		return GetFrameSlotRef(texts, sym);
	return NILREF;
}


// ROM 0x00082b94 PhoneSymToIndex__FRC6RefVarN21
// PhoneSymToIndex(kind, phones): the index, among the numbers that are
// there, of the first of that kind (named as the user reads it).
Ref
PhoneSymToIndex(RefArg /*rcvr*/, RefArg str, RefArg phones)
{
	if (ISNIL(str) || ISNIL(phones))
		return NILREF;
	RefVar sym(PhoneStringToSym(str));
	RefVar real(RealPhones(phones));
	long count = Length(real);
	for (long i = 0; i < count; i++)
		if (EQRef(sym, ClassOf(RefVar(GetArraySlotRef(real, i)))))
			return MAKEINT(i);
	return NILREF;
}


// ROM 0x00082dd4 PhoneStringToSym__FRC6RefVar
// The kind of phone number whose name this is.
Ref
PhoneStringToSym(RefArg str)
{
	RefVar texts(PhoneTexts());
	for (TObjectIterator iter(texts); !iter.Done(); iter.Next())
		if (NOTNIL(FStrEqual(RefVar(), str, RefVar(iter.Value()))))
			return iter.Tag();
	return NILREF;
}


// ROM 0x00082f20 PhoneStringToValue__FRC6RefVarN21
// PhoneStringToValue(kind, phones): the `value` of the first number of
// that kind.
Ref
PhoneStringToValue(RefArg /*rcvr*/, RefArg str, RefArg phones)
{
	RefVar sym(PhoneStringToSym(str));
	long count = Length(phones);
	for (long i = 0; i < count; i++)
		if (EQRef(sym, ClassOf(RefVar(GetArraySlotRef(phones, i)))))
			return GetFrameSlotRef(RefVar(GetArraySlotRef(phones, i)), RSSYMvalue);
	return NILREF;
}


// ROM 0x0008302c PhoneIndexToValue__FRC6RefVarN21
// PhoneIndexToValue(index, phones): the index'th of the numbers that are
// there.
Ref
PhoneIndexToValue(RefArg /*rcvr*/, RefArg index, RefArg phones)
{
	RefVar real(RealPhones(phones));
	if (Length(real) < 1)
		return NILREF;
	return GetArraySlotRef(real, RINT(index));
}


void
RegisterHeuristicsNatives(void)
{
	RegisterNativeFunction("History__FRC6RefVarT1", (void*) History, 1);
	RegisterNativeFunction("AddHistory__FRC6RefVarT1", (void*) AddHistory, 1);
	RegisterNativeFunction("DSConstructSubjectLine__FRC6RefVarT1", (void*) DSConstructSubjectLine, 1);
	RegisterNativeFunction("DSFindPossibleName__FRC6RefVarN21", (void*) DSFindPossibleName, 2);
	RegisterNativeFunction("DSFindPossibleLocation__FRC6RefVarN21", (void*) DSFindPossibleLocation, 2);
	RegisterNativeFunction("DSFilterStringsAux__FRC6RefVarN21", (void*) DSFilterStringsAux, 2);
	RegisterNativeFunction("DSFilterStrings__FRC6RefVarN21", (void*) DSFilterStrings, 2);
	RegisterNativeFunction("DSScanForWackyName__FRC6RefVarN21", (void*) DSScanForWackyName, 2);
	RegisterNativeFunction("DSGetMatchedEntries__FRC6RefVarN21", (void*) DSGetMatchedEntries, 2);
	RegisterNativeFunction("SuffixP__FRC6RefVarT1", (void*) SuffixP, 1);
	RegisterNativeFunction("PrefixP__FRC6RefVarT1", (void*) PrefixP, 1);
	RegisterNativeFunction("GuessAddressee__FRC6RefVarT1", (void*) GuessAddressee, 1);
	RegisterNativeFunction("DSFindPossiblePhone__FRC6RefVarT1", (void*) DSFindPossiblePhone, 1);
	RegisterNativeFunction("IASmartCFLookup__FRC6RefVarT1", (void*) IASmartCFLookup, 1);
	RegisterNativeFunction("StringToFrameMapper__FRC6RefVarT1", (void*) StringToFrameMapper, 1);
	RegisterNativeFunction("DSTagString__FRC6RefVarN31", (void*) DSTagString, 3);
	RegisterNativeFunction("DSResolveString__FRC6RefVarN21", (void*) DSResolveString, 2);
	RegisterNativeFunction("PhraseFilter__FRC6RefVarT1", (void*) PhraseFilter, 1);
	RegisterNativeFunction("GenPhoneTypeList__FRC6RefVarT1", (void*) GenPhoneTypeList, 1);
	RegisterNativeFunction("PhoneSymToString__FRC6RefVarT1", (void*) PhoneSymToString, 1);
	RegisterNativeFunction("PhoneSymToIndex__FRC6RefVarN21", (void*) PhoneSymToIndex, 2);
	RegisterNativeFunction("PhoneStringToValue__FRC6RefVarN21", (void*) PhoneStringToValue, 2);
	RegisterNativeFunction("PhoneIndexToValue__FRC6RefVarN21", (void*) PhoneIndexToValue, 2);
	RegisterNativeFunction("StringAssoc__FRC6RefVarN21", (void*) StringAssoc, 2);
	RegisterNativeFunction("StringShorten__FRC6RefVarT1", (void*) StringShorten, 1);
	RegisterNativeFunction("StringAnnotate__FRC6RefVarN21", (void*) StringAnnotate, 2);
	RegisterNativeFunction("DSPrevSubStr__FRC6RefVarN21", (void*) DSPrevSubStr, 2);
}
