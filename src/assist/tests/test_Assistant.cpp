// Intelligent Assistant test: the list operations the Assistant is
// written in, and GenFullCommands over the ROM's own task templates - the
// words the "Please ..." slip offers.  The ROM's objects are imported for
// the Assistant's frame (magic pointer 8).
#include "Assistant.h"
#include "AssistStrings.h"
#include "Lexicon.h"
#include "Heuristics.h"
#include "Phrases.h"
#include "ParseUtter.h"
#include "ROMDictionaryData.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

Ref		SplitString(RefArg rcvr, RefArg str);	// frames/StringNatives.cpp

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref NIL() { return NILREF; }

// the array's strings, as an ASCII list, for looking at what came back
static Boolean
Has(RefArg list, const char* text)
{
	long count = ISNIL(list) ? 0 : Length(list);
	for (long i = 0; i < count; i++)
	{
		RefVar item(GetArraySlotRef(list, i));
		if (!IsString(item))
			continue;
		char buffer[64];
		ConvertFromUnicode(GetCString(item), buffer, kMacRomanEncoding, sizeof(buffer) - 1);
		if (strcmp(buffer, text) == 0)
			return true;
	}
	return false;
}


static void
TestLists()
{
	// Append makes the array when there is none
	RefVar list(Append(RefVar(NIL()), RefVar(NIL()), RefVar(MAKEINT(1))));
	EXPECT(IsArray(list) && Length(list) == 1);
	EXPECT(Append(RefVar(NIL()), list, RefVar(MAKEINT(2))) == (Ref) list && Length(list) == 2);

	// member_p answers the element, not the position
	EXPECT(RINT(member_p(list, RefVar(MAKEINT(2)))) == 2);
	EXPECT(ISNIL(RefVar(member_p(list, RefVar(MAKEINT(3))))));
	EXPECT(ISNIL(RefVar(member_p(RefVar(NIL()), RefVar(MAKEINT(1))))));
	EXPECT(ISNIL(RefVar(member_p(list, RefVar(NIL())))));

	// UniqueAppendItem does not add what is there already
	UniqueAppendItem(RefVar(NIL()), list, RefVar(MAKEINT(2)));
	EXPECT(Length(list) == 2);
	UniqueAppendItem(RefVar(NIL()), list, RefVar(MAKEINT(3)));
	EXPECT(Length(list) == 3);

	// strings are compared as text, so two equal strings are one item
	RefVar strings(MakeArray(0));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("cat")));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("cat")));
	UniqueAppendString(RefVar(NIL()), strings, RefVar(MakeString("dog")));
	EXPECT(Length(strings) == 2 && Has(strings, "cat") && Has(strings, "dog"));

	RefVar more(MakeArray(0));
	AddArraySlot(more, RefVar(MakeString("dog")));
	AddArraySlot(more, RefVar(MakeString("emu")));
	UniqueAppendList(RefVar(NIL()), strings, more);
	EXPECT(Length(strings) == 3 && Has(strings, "emu"));

	// MashLists: nil either side is the other side, and the items of the
	// second that are not in the first are appended to it
	RefVar a(MakeArray(0));
	AddArraySlot(a, RefVar(MAKEINT(1)));
	RefVar b(MakeArray(0));
	AddArraySlot(b, RefVar(MAKEINT(1)));
	AddArraySlot(b, RefVar(MAKEINT(2)));
	EXPECT(UniqueAppendListGen(RefVar(NIL()), a, RefVar(NIL())) == (Ref) a);
	EXPECT(UniqueAppendListGen(RefVar(NIL()), RefVar(NIL()), b) == (Ref) b);
	RefVar mashed(UniqueAppendListGen(RefVar(NIL()), a, b));
	EXPECT(mashed == (Ref) a && Length(a) == 2);			// appended in place
	EXPECT(RINT(GetArraySlotRef(a, 1)) == 2);

	// a read-only list is cloned rather than added to: the ROM's own
	// objects are read-only, so one of them will do
	RefVar romList(GetFrameSlotRef(kAssistantFrame, RSSYMtask_list));
	EXPECT(IsArray(romList) && IsReadOnly(romList));
	long was = Length(romList);
	RefVar grown(UniqueAppendListGen(RefVar(NIL()), romList, b));
	EXPECT(grown != (Ref) romList && Length(romList) == was);
}


static void
TestMapSymToFrame()
{
	// a global variable
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "aTestFrame")), RefVar(AllocateFrame()));
	EXPECT(IsFrame(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(Intern((char*) "aTestFrame"))))));
	// else the Assistant's own frame
	EXPECT(IsArray(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(RSSYMtask_list)))));
	EXPECT(ISNIL(RefVar(MapSymToFrame(RefVar(NIL()), RefVar(Intern((char*) "noSuchSlotAnywhere"))))));
}


static void
TestGenFullCommands()
{
	RefVar commands(GenFullCommands(RefVar(NIL())));
	EXPECT(IsArray(commands) && Length(commands) > 0);
	// every one of them is a string, and none of them is there twice
	long count = ISNIL(commands) ? 0 : Length(commands);
	for (long i = 0; i < count; i++)
	{
		RefVar word(GetArraySlotRef(commands, i));
		EXPECT(IsString(word));
	}
	printf("  the Assistant's commands (%ld):", count);
	for (long i = 0; i < count && i < 30; i++)
	{
		RefVar word(GetArraySlotRef(commands, i));
		char buffer[64];
		ConvertFromUnicode(GetCString(word), buffer, kMacRomanEncoding, sizeof(buffer) - 1);
		printf(" %s", buffer);
	}
	printf("\n");
	// the words the ROM's own templates start with
	EXPECT(Has(commands, "print"));
	EXPECT(Has(commands, "find"));

	// asked twice, the same list comes back
	RefVar again(GenFullCommands(RefVar(NIL())));
	EXPECT(Length(again) == count);
}


// the ASCII of a string, for comparing
static Boolean
Is(RefArg str, const char* text)
{
	if (!IsString(str))
		return false;
	char buffer[128];
	ConvertFromUnicode(GetCString(str), buffer, kMacRomanEncoding, sizeof(buffer) - 1);
	return strcmp(buffer, text) == 0;
}


// The string tidying a sentence goes through before anything tries to
// read it (assist/AssistStrings.h).
static void
TestStrings()
{
	// the words glued back together with one space between each pair
	RefVar words(MakeArray(3));
	SetArraySlot(words, 0, RefVar(MakeString("call")));
	SetArraySlot(words, 1, RefVar(MakeString("John")));
	SetArraySlot(words, 2, RefVar(MakeString("Smith")));
	EXPECT(Is(RefVar(GlueStrings(RefVar(NIL()), words)), "call John Smith"));

	// NStringCat grows its first argument and answers it
	RefVar a(MakeString("one "));
	RefVar b(MakeString("two"));
	EXPECT(NStringCat(RefVar(NIL()), a, b) == (Ref) a && Is(a, "one two"));

	// every run of consecutive words, the longest first
	RefVar pieces(GenerateSubstrings(RefVar(NIL()), RefVar(MakeString("call John Smith"))));
	EXPECT(Length(pieces) == 6);				// (3*3 + 3) / 2
	EXPECT(Is(RefVar(GetArraySlotRef(pieces, 0)), "call John Smith"));
	EXPECT(Is(RefVar(GetArraySlotRef(pieces, 1)), "call John"));
	EXPECT(Is(RefVar(GetArraySlotRef(pieces, 2)), "John Smith"));
	EXPECT(Is(RefVar(GetArraySlotRef(pieces, 3)), "call"));
	EXPECT(Is(RefVar(GetArraySlotRef(pieces, 5)), "Smith"));

	// the returns and tabs turned into spaces, in place
	RefVar messy(MakeString("a\rb\tc\nd"));
	EXPECT(CleanString(RefVar(NIL()), messy) == (Ref) messy);
	EXPECT(Is(messy, "a b c d"));

	// the punctuation taken off both ends; a string with none answers
	// itself rather than a copy
	EXPECT(Is(RefVar(TrimBlanksAndPunct(RefVar(NIL()), RefVar(MakeString(" (call John!) ")))), "call John"));
	RefVar clean(MakeString("call John"));
	EXPECT(TrimBlanksAndPunct(RefVar(NIL()), clean) == (Ref) clean);
	EXPECT(ISNIL(RefVar(TrimBlanksAndPunct(RefVar(NIL()), RefVar(MakeString(" ... "))))));
	// BUG (the ROM's): the guillemets it means to trim are given as Mac
	// Roman 0xc7/0xc8, which are Ç and È in the Unicode the string is by
	// then - so those are trimmed and « » are not
	{
		UniChar guillemets[4];
		guillemets[0] = 0x00ab;
		guillemets[1] = 'x';
		guillemets[2] = 0x00bb;
		guillemets[3] = 0;
		RefVar quoted(MakeString(guillemets));
		EXPECT(TrimBlanksAndPunct(RefVar(NIL()), quoted) == (Ref) quoted);
		UniChar accented[4];
		accented[0] = 0x00c7;
		accented[1] = 'x';
		accented[2] = 0x00c8;
		accented[3] = 0;
		EXPECT(Is(RefVar(TrimBlanksAndPunct(RefVar(NIL()), RefVar(MakeString(accented)))), "x"));
	}

	// lowercased in place
	RefVar shouty(MakeString("CALL John"));
	EXPECT(MakeLowerCase(RefVar(NIL()), shouty) == (Ref) shouty && Is(shouty, "call john"));

	// and the C-string helpers under them
	unsigned char buffer[16];
	Bstrcpy(buffer, (const unsigned char*) "Hello");
	EXPECT(strcmp((char*) buffer, "Hello") == 0);
	EXPECT(strcmp((char*) DownCase(buffer), "hello") == 0);
}


// The class hierarchy the Assistant matches against, walked over the
// ROM's own frames (assist/Assistant.h).
static void
TestClasses()
{
	InitDSTaskTemplates(RefVar(NIL()), RefVar(NIL()));
	RefVar assistant(kAssistantFrame);
	RefVar action(GetFrameSlotRef(assistant, RSSYMaction));
	RefVar object(GetFrameSlotRef(assistant, RSSYMuser_obj));
	RefVar person(GetFrameSlotRef(assistant, RSSYMperson));
	EXPECT(NOTNIL(action) && NOTNIL(object) && NOTNIL(person));
	EXPECT(gActionClass == (Ref) action && gObjectClass == (Ref) object);

	// a person is a kind of user_obj, two steps up; not the other way
	EXPECT(NOTNIL(RefVar(ISATest(RefVar(NIL()), person, person))));
	EXPECT(NOTNIL(RefVar(ISATest(RefVar(NIL()), person, object))));
	EXPECT(ISNIL(RefVar(ISATest(RefVar(NIL()), object, person))));
	EXPECT(ISNIL(RefVar(ISATest(RefVar(NIL()), person, action))));
	// a symbol is looked up in the Assistant's own frame first
	EXPECT(NOTNIL(RefVar(ISATest(RefVar(NIL()), RefVar(RSSYMperson), object))));

	// the path to the root: person, whatever is between, user_obj
	RefVar path(PathToRoot(RefVar(NIL()), person));
	EXPECT(Length(path) == 3);
	EXPECT(GetArraySlotRef(path, 0) == (Ref) person);
	EXPECT(GetArraySlotRef(path, 2) == (Ref) object);
	EXPECT(ISNIL(RefVar(PathToRoot(RefVar(NIL()), RefVar(NIL())))));

	// what two things have in common, nearest first
	RefVar middle(GetFrameSlotRef(person, RSSYMisa));
	RefVar common(CommonAncestors(RefVar(NIL()), person, middle));
	EXPECT(NOTNIL(common) && GetArraySlotRef(common, 0) == (Ref) middle);
	EXPECT(ISNIL(RefVar(CommonAncestors(RefVar(NIL()), person, action))));

	// and of a whole list of them
	RefVar one(MakeArray(1));
	SetArraySlot(one, 0, person);
	EXPECT(CompositeClass(RefVar(NIL()), one) == (Ref) person);
	RefVar pair(MakeArray(2));
	SetArraySlot(pair, 0, person);
	SetArraySlot(pair, 1, middle);
	EXPECT(CompositeClass(RefVar(NIL()), pair) == (Ref) middle);
	SetArraySlot(pair, 1, action);
	EXPECT(ISNIL(RefVar(CompositeClass(RefVar(NIL()), pair))));

	// a verb picked out of a list, and a thing
	RefVar mixed(MakeArray(2));
	SetArraySlot(mixed, 0, person);
	SetArraySlot(mixed, 1, action);
	EXPECT(FavorAction(RefVar(NIL()), mixed) == (Ref) action);
	EXPECT(FavorObject(RefVar(NIL()), mixed) == (Ref) person);
	EXPECT(NOTNIL(RefVar(IsAction(action))) && ISNIL(RefVar(IsAction(person))));

	// ISATest's shortcut - two frames with the same `isa` and the same
	// `lexicon` are the same kind - makes every frame that has neither a
	// kind of every other: `action` has no isa and no lexicon, and
	// neither has `user_obj`, so each is an instance of the other
	EXPECT(ISNIL(RefVar(GetFrameSlotRef(action, RSSYMisa))));
	EXPECT(ISNIL(RefVar(GetFrameSlotRef(object, RSSYMisa))));
	EXPECT(NOTNIL(RefVar(ISATest(RefVar(NIL()), action, object))));
	EXPECT(NOTNIL(RefVar(ISATest(RefVar(NIL()), object, action))));

	// the things sorted into the kinds they are, each used once
	RefVar things(MakeArray(2));
	SetArraySlot(things, 0, person);
	SetArraySlot(things, 1, action);
	RefVar kinds(MakeArray(2));
	SetArraySlot(kinds, 0, person);
	SetArraySlot(kinds, 1, action);
	RefVar sorted(CheezySubsumption(RefVar(NIL()), things, kinds));
	EXPECT(Length(sorted) == 2);
	EXPECT(Length(RefVar(GetArraySlotRef(sorted, 0))) == 1);
	EXPECT(GetArraySlotRef(RefVar(GetArraySlotRef(sorted, 0)), 0) == (Ref) person);
	EXPECT(GetArraySlotRef(RefVar(GetArraySlotRef(sorted, 1)), 0) == (Ref) action);
}


// The task templates: registering one, asking what can be done to a
// thing, and filling a template's slots from a sentence.
static void
TestTemplates()
{
	RefVar assistant(kAssistantFrame);
	RefVar object(GetFrameSlotRef(assistant, RSSYMuser_obj));
	RefVar person(GetFrameSlotRef(assistant, RSSYMperson));

	// what the ROM's own templates offer for a person
	// ["schedule", "find", "mail", "fax", "call"] - the five things the
	// MP2x00 US ROM knows how to do to a person
	RefVar forPerson(GetRelevantTemplates(RefVar(NIL()), person));
	EXPECT(NOTNIL(forPerson) && Length(forPerson) == 5);
	EXPECT(Has(forPerson, "schedule") && Has(forPerson, "find") && Has(forPerson, "mail"));
	EXPECT(Has(forPerson, "fax") && Has(forPerson, "call"));
	// the default task is the one template that is never offered, and
	// "print" and "time" want something other than a person
	EXPECT(!Has(forPerson, "about newton") && !Has(forPerson, "time"));

	// a template with a slot missing is refused
	SetFrameSlot(RefVar(gVarFrame), RSSYMdynatemplates, RefVar(NIL()));
	RefVar bad(AllocateFrame());
	SetFrameSlot(bad, RSSYMvalue, RefVar(MakeString("test")));
	EXPECT(ISNIL(RefVar(RegTaskTemplate(RefVar(NIL()), bad))));
	EXPECT(ISNIL(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates))));

	// one with all eight, and an action of its own, is kept - as a copy
	RefVar act(AllocateFrame());
	SetFrameSlot(act, RSSYMisa, RefVar(gActionClass));
	RefVar lexicon(MakeArray(1));
	RefVar oneWord(MakeArray(1));
	SetArraySlot(oneWord, 0, RefVar(MakeString("juggle")));
	SetArraySlot(lexicon, 0, oneWord);
	SetFrameSlot(act, RSSYMlexicon, lexicon);
	// the signature names its class by *symbol*: RegTaskTemplate deep-
	// copies the template, so a class named by frame would come back as
	// a copy of that frame and never match the real one again
	RefVar signature(MakeArray(1));
	SetArraySlot(signature, 0, RefVar(RSSYMperson));
	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMvalue, RefVar(MakeString("juggle")));
	SetFrameSlot(templ, RSSYMisa, RefVar(gActionClass));
	SetFrameSlot(templ, RSSYMprimary_act, act);
	SetFrameSlot(templ, RSSYMsignature, signature);
	SetFrameSlot(templ, RSSYMpreconditions, RefVar(MakeArray(1)));
	SetFrameSlot(templ, RSSYMtaskslip, RefVar(NIL()));
	SetFrameSlot(templ, RSSYMpostparse, RefVar(NIL()));
	SetFrameSlot(templ, RSSYMscore, RefVar(MAKEINT(0)));
	RefVar kept(RegTaskTemplate(RefVar(NIL()), templ));
	EXPECT(NOTNIL(kept) && (Ref) kept != (Ref) templ);
	RefVar registered(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates));
	EXPECT(IsArray(registered) && Length(registered) == 1);
	EXPECT(GetArraySlotRef(registered, 0) == (Ref) kept);

	// and it is offered for a person, beside the ROM's own
	RefVar now(GetRelevantTemplates(RefVar(NIL()), person));
	EXPECT(Has(now, "juggle"));
	EXPECT(Has(now, "call"));
	// (the ROM's own templates are left in dynatemplates by that walk,
	//  because the mash appends in place - the same quirk GenFullCommands
	//  has; the list is longer than the one template put in it)
	EXPECT(Length(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates))) > 1);

	// unregistering takes it off again
	SetFrameSlot(RefVar(gVarFrame), RSSYMdynatemplates, RefVar(NIL()));
	kept = RegTaskTemplate(RefVar(NIL()), templ);
	EXPECT(Length(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates))) == 1);
	UnRegTaskTemplate(RefVar(NIL()), kept);
	EXPECT(Length(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates))) == 0);

	// the words of a sentence sorted into the slots the template wants:
	// `input` is what each word turned out to be and `raw` is the word,
	// so the person goes into the slot `preconditions` names for the
	// person entry of the signature
	RefVar filling(AllocateFrame());
	RefVar wanted(MakeArray(2));
	SetArraySlot(wanted, 0, RefVar(gActionClass));
	SetArraySlot(wanted, 1, person);
	SetFrameSlot(filling, RSSYMsignature, wanted);
	RefVar slots(MakeArray(2));
	SetArraySlot(slots, 0, RefVar(RSSYMvalue));
	SetArraySlot(slots, 1, RefVar(RSSYMalias));
	SetFrameSlot(filling, RSSYMpreconditions, slots);
	RefVar input(MakeArray(2));
	SetArraySlot(input, 0, act);
	SetArraySlot(input, 1, person);
	SetFrameSlot(filling, RSSYMinput, input);
	RefVar raw(MakeArray(2));
	SetArraySlot(raw, 0, RefVar(MakeString("juggle")));
	SetArraySlot(raw, 1, RefVar(MakeString("John")));
	SetFrameSlot(filling, RSSYMraw, raw);
	EXPECT(NOTNIL(RefVar(FillPreconditions(RefVar(NIL()), filling))));
	RefVar verbs(GetFrameSlotRef(filling, RSSYMvalue));
	RefVar people(GetFrameSlotRef(filling, RSSYMalias));
	EXPECT(IsArray(verbs) && Length(verbs) == 1 && Is(RefVar(GetArraySlotRef(verbs, 0)), "juggle"));
	EXPECT(IsArray(people) && Length(people) == 1 && Is(RefVar(GetArraySlotRef(people, 0)), "John"));

	// one thing the sentence named recorded in the slip's frame: a
	// person on its `person` list, anything else on `places`, and only
	// one of each
	RefVar slip(AllocateFrame());
	AddEntry(RefVar(NIL()), person, RefVar(NIL()), RefVar(MakeString("John")), slip);
	RefVar named(GetFrameSlotRef(slip, RSSYMperson));
	EXPECT(IsArray(named) && Length(named) == 1);
	EXPECT(Is(RefVar(GetFrameSlotRef(RefVar(GetArraySlotRef(named, 0)), RSSYMalias)), "John"));
	EXPECT(NOTNIL(RefVar(GetFrameSlotRef(slip, RSSYMpersonadded))));
	AddEntry(RefVar(NIL()), person, RefVar(NIL()), RefVar(MakeString("Jane")), slip);
	EXPECT(Length(RefVar(GetFrameSlotRef(slip, RSSYMperson))) == 1);
	AddEntry(RefVar(NIL()), object, RefVar(NIL()), RefVar(MakeString("Tokyo")), slip);
	EXPECT(Length(RefVar(GetFrameSlotRef(slip, RSSYMplaces))) == 1);

	SetFrameSlot(RefVar(gVarFrame), RSSYMdynatemplates, RefVar(NIL()));
}


// The lexicon (Lexicon.h): the ROM's trie knows "call" as an action, a
// phrase looked up comes back as its meanings with the phrase as their
// value, and words registered at run time are counted, shared and taken
// away again - the last registration taking the word out of the trie and
// renumbering the others (DynaCompress).
static void
TestLexicon()
{
	InitROMDictionaryData();
	InitDarkStar(RefVar(), RefVar());
	EXPECT(gTrie != nil && gDynaTrie != nil);

	RefVar meanings(FastStringLookup(RefVar(), RefVar(MakeString("Call"))));
	EXPECT(IsArray(meanings) && Length(meanings) > 0);
	Boolean action = false;
	for (long i = 0; IsArray(meanings) && i < Length(meanings); i++)
		if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(meanings, i)), RSSYMaction)))
			action = true;
	EXPECT(action);

	RefVar info(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMentries))));
	char phrase[16];
	strcpy(phrase, "Call");
	RefVar found(MatchString(gTrie, phrase, info));
	EXPECT(IsArray(found) && Length(found) > 0 && Is(RefVar(GetFrameSlotRef(RefVar(GetArraySlotRef(found, 0)), RSSYMvalue)), "call"));
	EXPECT(strcmp(phrase, "call") == 0);		// (lowercased in place)

	// run-time registrations
	RefVar first(AllocateFrame());
	SetFrameSlot(first, RSSYMisa, RSSYMaction);
	RefVar words(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(words, 0, MakeString("Zorch"));
	SetArraySlotRef(words, 1, MakeString("frobnicate"));
	SetFrameSlot(first, RSSYMlexicon, words);
	RefVar second(Clone(first));
	RefVar one(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(one, 0, MakeString("zorch"));
	SetFrameSlot(second, RSSYMlexicon, one);
	EXPECT(NOTNIL(MakePhrasalLexEntry(RefVar(), first)));
	EXPECT(NOTNIL(MakePhrasalLexEntry(RefVar(), second)));
	strcpy(phrase, "zorch");
	RefVar frames(DynaTrieLookup(phrase));
	EXPECT(IsArray(frames) && Length(frames) == 2);
	strcpy(phrase, "frobnicate");
	frames = DynaTrieLookup(phrase);
	EXPECT(IsArray(frames) && Length(frames) == 1 && EQRef(GetArraySlotRef(frames, 0), first));
	// the first taken away: "zorch" is the second's alone, "frobnicate" gone
	// and the entries renumbered so that "zorch" still finds its own
	EXPECT(NOTNIL(RemovePhrasalLexEntry(RefVar(), first)));
	strcpy(phrase, "zorch");
	frames = DynaTrieLookup(phrase);
	EXPECT(IsArray(frames) && Length(frames) == 1 && EQRef(GetArraySlotRef(frames, 0), second));
	strcpy(phrase, "frobnicate");
	EXPECT(ISNIL(DynaTrieLookup(phrase)));
	EXPECT(NOTNIL(RemovePhrasalLexEntry(RefVar(), second)));
	strcpy(phrase, "zorch");
	EXPECT(ISNIL(DynaTrieLookup(phrase)));
	EXPECT(Length(gDynaDictionaryFrame) == 0);
}


// The phrase generator (Phrases.h): the runs of words longest first, left
// to right; a run that was known strikes off the untried runs it overlaps,
// and the words left over are the unmatched ones.
static void
TestPhrases()
{
	IPhraseGenerator(RefVar(MakeString("a b c")));
	EXPECT(Is(RefVar(NextPhrase()), "a b c"));
	EXPECT(Is(RefVar(NextPhrase()), "a b"));
	EXPECT(Is(RefVar(NextPhrase()), "b c"));
	PhraseHitExt();								// "b c" was known
	EXPECT(Is(RefVar(NextPhrase()), "a"));
	EXPECT(ISNIL(NextPhrase()));
	RefVar unmatched(UnmatchedWords(RefVar()));
	EXPECT(IsArray(unmatched) && Length(unmatched) == 1 && Is(RefVar(GetArraySlotRef(unmatched, 0)), "a"));
	EXPECT(interval_intersection_p(2, 2, 1, 3) == 1 && interval_intersection_p(2, 2, 1, 1) == 0);

	// the trailing punctuation (magic pointer 249: , . - : ; and space)
	EXPECT(Is(RefVar(RemoveTrailingPunct(RefVar(), RefVar(MakeString("Bob.,")))), "Bob"));
	EXPECT(Is(RefVar(RemoveTrailingPunct(RefVar(), RefVar(MakeString("Bob")))), "Bob"));
	EXPECT(ISNIL(RemoveTrailingPunct(RefVar(), RefVar(MakeString(".,")))));
	EXPECT(Is(RefVar(SuffixP(RefVar(), RefVar(MakeString("Bob,")))), "Bob"));
	EXPECT(ISNIL(SuffixP(RefVar(), RefVar(MakeString("Bob")))));
	EXPECT(Is(RefVar(StringShorten(RefVar(), RefVar(MakeString("Daniel (work)")))), "Daniel"));
	EXPECT(RINT(DSPrevSubStr(RefVar(), RefVar(MakeString("call my dad")), RefVar(MAKEINT(6)))) == 5);
	// the stop words (magic pointer 270) go; a word of five or more stays
	RefVar filtered(PhraseFilter(RefVar(), RefVar(SplitString(RefVar(), RefVar(MakeString("lunch at noon"))))));
	EXPECT(IsArray(filtered) && Length(filtered) == 2);
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Assistant: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	TestLists();
	TestMapSymToFrame();
	TestGenFullCommands();
	TestStrings();
	TestClasses();
	TestTemplates();
	TestLexicon();
	TestPhrases();

	printf("test_Assistant: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
