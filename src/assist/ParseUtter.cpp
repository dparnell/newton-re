/*
	File:		assist/ParseUtter.cpp

	Contains:	The Assistant's parse.  See ParseUtter.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParseUtter.h"
#include "Assistant.h"
#include "AssistStrings.h"
#include "Lexicon.h"
#include "Heuristics.h"
#include "Phrases.h"
#include "RootView.h"
#include "View.h"
#include "Frames.h"
#include "REPTranslators.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "Unicode.h"
#include "RSSymbols.h"

// the characters a phrase's end may carry that are not part of it
const Ref	kTrailingMarks = MAKEMAGICPTR(249);
// the words a date can be (their count sizes the parse's `exception` array)
const Ref	kDateWordList = MAKEMAGICPTR(113);

// frames/StringNatives.cpp
Ref		SplitString(RefArg rcvr, RefArg str);						// ROM 0x000833e4 SplitString__FRC6RefVarT1
Ref		FFindStringInArray(RefArg rcvr, RefArg array, RefArg str);	// ROM 0x001fe3c8 FFindStringInArray__FRC6RefVarN21


// ROM 0x00089a8c InitDarkStar__FRC6RefVarT1
// The Assistant ("DarkStar") started: the phrase generator, the lexicon,
// the histories and the task templates' classes.
Ref
InitDarkStar(RefArg rcvr, RefArg arg)
{
	InitDSPhraseSupport(rcvr, arg);
	InitDSDictionary(rcvr, arg);
	InitDSHeuristics(rcvr, arg);
	InitDSTaskTemplates(rcvr, arg);
	return TRUEREF;
}


// the Assistant's slip: the root's `assistant`
static Ref
AssistantSlip(void)
{
	return GetFrameSlotRef(RefVar(gRootView->fContext), RSSYMassistant);
}


// ROM 0x000e7048 parseUtter__FRC6RefVarT1
// ParseUtter(sentence): the sentence parsed and its task's slip opened.
// ==> the task template, filled in; nil when nothing could be made of it
// (the slip says so: `duh`), and TRUE for writing that is still ink (the
// slip is asked to have it read first: `deferredRec`).
Ref
parseUtter(RefArg rcvr, RefArg sentence)
{
	RefVar result;
	RefVar assistant;
	RefVar line;
	RefVar thing;
	RefVar chosen;
	RefVar parse;
	RefVar classes;
	gRootView->fDirtyFlag = true;
	assistant = AssistantSlip();
	FOpenX(assistant);
	line = GetFrameSlotRef(assistant, RSSYMassistline);
	line = GetFrameSlotRef(line, RSSYMentryline);
	FSetValue(assistant, line, RSSYMtext, sentence);
	gRootView->Update(nil);
	if (IsRichString(sentence))
	{
		DoMessage(assistant, RSSYMdeferredrec, RefVar());
		return TRUEREF;
	}
	DoMessage(assistant, RSSYMthinking, RefVar());
	if (IsString(sentence) && Ustrlen(GetCString(sentence)) == 0)
	{
		result = AllocateArray(RSSYMarray, 1);
		DoMessage(assistant, RSSYMduh, result);
		return NILREF;
	}
	SetFrameSlot(assistant, RSSYMmatchstring, sentence);
	if (NOTNIL(IAInputErrors(RefVar(), assistant)))
		result = NSCall(RefVar(Rstartiaprogress));
	long score = 0;		// DEVIATION: the ROM's is whatever its register held when a primary act is found
	if (NOTNIL(result))
	{
		if (Length(result) == 0)
		{
			NSCall(RefVar(Riacancelalert));
			return NILREF;
		}
		RefVar templates(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMtask_list))));
		RefVar dynamic(GetFrameSlotRef(gVarFrame, RSSYMdynatemplates));
		if (IsArray(dynamic))
			templates = UniqueAppendListGen(RefVar(), dynamic, templates);
		classes = GetArraySlotRef(result, 2);
		ULong count = (ULong) Length(classes);
		ULong index = 0;
		// the first action among the classes whose template makes it the
		// primary act
		for (TObjectIterator iter(classes); !iter.Done(); iter.Next())
		{
			thing = iter.Value();
			if (NOTNIL(IsAction(thing)))
			{
				for (ULong slot = index; slot < count; slot++)
				{
					RefVar candidate(GetArraySlotRef(classes, slot));
					if (IsSymbol(candidate))
						candidate = MapSymToFrame(RefVar(), candidate);
					if (NOTNIL(GetFrameSlotRef(candidate, RSSYMmeta_level)))
					{
						thing = candidate;
						break;
					}
				}
				for (TObjectIterator t(templates); !t.Done(); t.Next())
				{
					if (NOTNIL(IsPrimaryAct(thing, RefVar(t.Value()))))
					{
						chosen = t.Value();
						parse = GetFrameSlotRef(chosen, RSSYMsignature);
						parse = CheezySubsumption(RefVar(), classes, parse);
						break;
					}
				}
			}
			if (NOTNIL(chosen))
				break;
			index++;
		}
		// else the template whose signature the classes fit best
		if (ISNIL(chosen))
		{
			score = 0;
			long best = -10000;
			for (TObjectIterator t(templates); !t.Done(); t.Next())
			{
				RefVar signature(GetFrameSlotRef(RefVar(t.Value()), RSSYMsignature));
				long needed = Length(signature);
				signature = CheezySubsumption(RefVar(), classes, signature);
				long covered = ISNIL(signature) ? 0 : Length(signature);
				if (needed != 0 && covered != 0)
				{
					// ROM QUIRK, kept: the score the template is given is
					// the last one worked out, not necessarily the best
					score = ((covered * 2 - (long) count) * 1000) / needed;
					if (best < score)
					{
						chosen = t.Value();
						parse = signature;
						best = score;
					}
				}
			}
		}
	}
	if (NOTNIL(chosen))
	{
		chosen = Clone(chosen);
		SetFrameSlot(chosen, RSSYMparse, parse);
		SetFrameSlot(chosen, RSSYMinput, classes);
		SetFrameSlot(chosen, RSSYMraw, RefVar(GetArraySlotRef(result, 0)));
		SetFrameSlot(chosen, RSSYMscore, RefVar(MAKEINT(score)));
		SetFrameSlot(chosen, RSSYMphrases, RefVar(GetArraySlotRef(result, 1)));
		SetFrameSlot(chosen, RSSYMnoisewords, RefVar(UnmatchedWords(RefVar())));
		SetFrameSlot(chosen, RSSYMorigphrase, RefVar(OrigPhrase(RefVar())));
		SetFrameSlot(chosen, RSSYMentries, RefVar(GetArraySlotRef(result, 3)));
		FillPreconditions(RefVar(), chosen);
		DriveTaskSlip(rcvr, chosen);
		return chosen;
	}
	result = AllocateArray(RSSYMarray, 1);
	DoMessage(assistant, RSSYMduh, result);
	return NILREF;
}


// ROM 0x000e7a0c FIaAtWork
// What the progress box runs: every run of words of the slip's sentence
// looked up (MatchString, each run's trailing punctuation taken off, the
// box tickled each time), the runs that were known kept and their
// meanings remembered in the histories.  ==> [the runs' meanings, the
// runs, their classes, the people and places found]; nil when nothing
// was known, and [] when the parse was stopped (an evt.ex exception).
Ref
FIaAtWork(RefArg /*rcvr*/, RefArg progress)
{
	RefVar result;
	RefVar phrase;
	RefVar found;
	newton_try
	{
		RefVar assistant(AssistantSlip());
		RefVar sentence(GetFrameSlotRef(assistant, RSSYMmatchstring));
		RefVar matches(AllocateArray(RSSYMarray, 0));
		RefVar phrases(AllocateArray(RSSYMarray, 0));
		IPhraseGenerator(sentence);
		phrase = NextPhrase();
		RefVar info(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMentries))));
		SetFrameSlot(info, RSSYMexception, RefVar(AllocateArray(RSSYMarray, Length(kDateWordList))));
		while (NOTNIL(phrase))
		{
			RefVar trimmed(RemoveTrailingPunct(RefVar(), phrase));
			NSCall(RefVar(Rtickleiaprogress), progress);
			if (NOTNIL(trimmed) && (long) ((ULong) Length(trimmed) / sizeof(UniChar) - 1) > 0)
			{
				char* ascii = NewASCIIString(trimmed);
				found = MatchString(gTrie, ascii, info);
				DisposPtr(ascii);
				if (NOTNIL(found))
				{
					PhraseHitExt();
					Append(RefVar(), matches, found);
					Append(RefVar(), phrases, phrase);
					long count = Length(found);
					for (long i = 0; i < count; i++)
						RecordHistory(RefVar(GetArraySlotRef(found, i)));
				}
			}
			phrase = NextPhrase();
		}
		if (Length(matches) > 0)
		{
			found = Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMmatched)));
			SetFrameSlot(found, RSSYMperson, RefVar(GetFrameSlotRef(info, RSSYMperson)));
			SetFrameSlot(found, RSSYMplaces, RefVar(GetFrameSlotRef(info, RSSYMplaces)));
			result = AllocateArray(RSSYMarray, 4);
			SetArraySlotRef(result, 0, matches);
			SetArraySlotRef(result, 1, phrases);
			SetArraySlotRef(result, 2, GetClasses(matches));
			SetArraySlotRef(result, 3, found);
		}
		SetFrameSlot(assistant, RSSYMmatchstring, RefVar(NILREF));
	}
	newton_catch("evt.ex")
	{
		result = AllocateArray(RSSYMarray, 0);
	}
	end_try;
	return result;
}


// ROM 0x000e7f84 IAInputErrors__FRC6RefVarT1
// Whether the slip's sentence can be parsed: not a single character, not
// all spaces (once its returns and tabs are made spaces - which is kept
// in the slip), fewer than sixteen words.
Ref
IAInputErrors(RefArg /*rcvr*/, RefArg assistant)
{
	RefVar sentence(GetFrameSlotRef(assistant, RSSYMmatchstring));
	if (ISNIL(sentence))
		return NILREF;
	ULong size = (ULong) Length(sentence);
	ULong chars = size / sizeof(UniChar) - 1;
	if (chars == 1)
		return NILREF;
	sentence = CleanString(RefVar(), sentence);
	SetFrameSlot(assistant, RSSYMmatchstring, sentence);
	UniChar space = U_CONST_CHAR(' ');
	const UniChar* text = (const UniChar*) BinaryData(sentence);
	Boolean blank = true;
	if (size / sizeof(UniChar) != 1)
	{
		for (ULong i = 0; i < chars; i++)
			if (text[i] != space)
				blank = false;
		if (!blank)
			return Length(RefVar(SplitString(RefVar(), sentence))) < 16 ? TRUEREF : NILREF;
	}
	return NILREF;
}


// ROM 0x000e8128 RemoveTrailingPunct__FRC6RefVarT1
// The phrase without the punctuation marks at its end (magic pointer
// 249); the phrase itself when it has none, and nil when it is nothing
// else or is fifty characters or more.
Ref
RemoveTrailingPunct(RefArg /*rcvr*/, RefArg str)
{
	char ascii[52];
	char shortened[52];
	char one[2];
	if (ISNIL(str))
		return NILREF;
	ULong size = (ULong) Length(str);
	long chars = (long) (size / sizeof(UniChar)) - 1;
	if (chars >= 0x32)
		return NILREF;
	ConvertFromUnicode(GetCString(str), ascii, kMacRomanEncoding, 0x7fffffff);
	RefVar marks(kTrailingMarks);
	RefVar mark;
	one[1] = 0;
	long last = (long) (size / sizeof(UniChar)) - 2;
	long keep;
	long i = last;
	for (;;)
	{
		keep = chars;
		if (i < 0)
			break;
		one[0] = ascii[i];
		mark = MakeString(one);
		keep = i;
		if (ISNIL(FFindStringInArray(RefVar(), marks, mark)))
			break;
		i--;
	}
	if (last == keep)
		return str;
	if (chars <= keep)
		return NILREF;
	for (long j = 0; j <= keep; j++)
		shortened[j] = ascii[j];
	shortened[keep + 1] = 0;
	return MakeString(shortened);
}


void
RegisterParseUtterNatives(void)
{
	RegisterNativeFunction("parseUtter__FRC6RefVarT1", (void*) parseUtter, 1);
	RegisterNativeFunction("FIaAtWork", (void*) FIaAtWork, 1);
}


void
RegisterAllAssistantNatives(void)
{
	RegisterAssistantNatives();
	RegisterLexiconNatives();
	RegisterHeuristicsNatives();
	RegisterPhraseNatives();
	RegisterParseUtterNatives();
}
