// Word validation test (recognition/Words.h): the pieces a word is
// judged by - spaces, letters, punctuation at the ends, capitalisation -
// and the two natives over them, ValidateWord and LookupWord.  Runs over
// the standalone heap with the frames object system started, because the
// natives answer Refs; without the ROM's objects the Unicode tables are
// the host's Latin-1 fallbacks, which is enough for these words.
#include "Words.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// a UniChar string of an ASCII one, in a buffer of its own
static void
Uni(UniChar* out, const char* text)
{
	long i = 0;
	for (; text[i] != 0; i++)
		out[i] = (UniChar) text[i];
	out[i] = 0;
}


static Boolean
Is(const UniChar* text, const char* expected)
{
	long i = 0;
	for (; expected[i] != 0; i++)
		if (text[i] != (UniChar) expected[i])
			return false;
	return text[i] == 0;
}


// the string a native left behind, as text
static Boolean
RefIs(RefArg str, const char* expected)
{
	if (ISNIL(str))
		return false;
	return Is((const UniChar*) BinaryData(str), expected);
}


static Ref
Str(const char* text)
{
	UniChar buffer[64];
	Uni(buffer, text);
	return MakeString(buffer);
}


int
main()
{
	InitHostStandaloneHeap();
	InitObjects();

	UniChar word[80];

	// spaces and letters
	Uni(word, "two words");
	EXPECT(HasSpaces(word) && HasChars(word));
	Uni(word, "Parnell");
	EXPECT(!HasSpaces(word) && HasChars(word));
	Uni(word, "1234");
	EXPECT(!HasChars(word));					// only the two ASCII ranges count as letters

	// the punctuation the recogniser strips, in order, with the s rule
	Uni(word, "!\"'(),.:;?");
	for (long i = 0; i < 10; i++)
		EXPECT(IsPunctSymbol(word, i));
	Uni(word, "ab");
	EXPECT(!IsPunctSymbol(word, 0) && !IsPunctSymbol(word, 1));
	Uni(word, "Jones's");
	EXPECT(IsPunctSymbol(word, 5));				// the plain apostrophe is punctuation wherever it is
	Uni(word, "Jones s");
	word[5] = 0x2019;							// a right single quote after an s is not
	EXPECT(!IsPunctSymbol(word, 5));
	word[4] = (UniChar) 'x';
	EXPECT(IsPunctSymbol(word, 5));				// (the character before it is what matters)
	Uni(word, "(s)");
	EXPECT(IsPunctSymbol(word, 0) && !IsPunctSymbol(word, 2));

	// stripped off both ends, and left alone in the middle
	Uni(word, "(Smith).");
	StripRecognitionWord(word);
	EXPECT(Is(word, "Smith"));
	Uni(word, "Jones's");
	StripRecognitionWord(word);
	EXPECT(Is(word, "Jones's"));
	Uni(word, "Parnell");
	StripRecognitionWord(word);
	EXPECT(Is(word, "Parnell"));

	// how it is capitalised
	Uni(word, "Parnell");
	EXPECT(CheckCapAttributes(word) == kCapStartsUpper);
	Uni(word, "PARNELL");
	EXPECT(CheckCapAttributes(word) == (kCapStartsUpper | kCapAllUpper));
	Uni(word, "parnell");
	EXPECT(CheckCapAttributes(word) == 0);

	// ValidateWord: with no dictionaries nothing is ever found, so a
	// well-formed word comes back as "not in them" and nothing else
	RefVar flags(FValidateWord(RefVar(NILREF), RefVar(Str("hello")), RefVar(NILREF)));
	EXPECT(RINT(flags) == kWordNotFound);
	flags = FValidateWord(RefVar(NILREF), RefVar(Str("HELLO")), RefVar(NILREF));
	EXPECT(RINT(flags) == (kWordNotFound | kWordStartsUpper | kWordIsAllCaps));
	flags = FValidateWord(RefVar(NILREF), RefVar(Str("a")), RefVar(NILREF));
	EXPECT((RINT(flags) & kWordTooShort) != 0 && (RINT(flags) & kWordKnownOrBad) != 0);
	flags = FValidateWord(RefVar(NILREF), RefVar(Str("two words")), RefVar(NILREF));
	EXPECT((RINT(flags) & kWordHasSpaces) != 0 && (RINT(flags) & kWordKnownOrBad) != 0);
	flags = FValidateWord(RefVar(NILREF), RefVar(Str("1234")), RefVar(NILREF));
	EXPECT((RINT(flags) & kWordHasNoLetters) != 0 && (RINT(flags) & kWordKnownOrBad) != 0);
	// the word is stripped where it lies, and said to have been
	RefVar punctuated(Str("(Smith)"));
	flags = FValidateWord(RefVar(NILREF), punctuated, RefVar(NILREF));
	EXPECT((RINT(flags) & kWordWasStripped) != 0 && RefIs(punctuated, "Smith"));

	// LookupWord: the word back when it is one the dictionaries do not
	// have, nil when it is not a word at all.  The original is not touched.
	RefVar original(Str("Parnell."));
	RefVar looked(FLookupWord(RefVar(NILREF), original));
	EXPECT(RefIs(looked, "Parnell") && RefIs(original, "Parnell."));
	EXPECT(RefIs(RefVar(FLookupWord(RefVar(NILREF), RefVar(Str("Daniel")))), "Daniel"));
	EXPECT(RefIs(RefVar(FLookupWord(RefVar(NILREF), RefVar(Str("Jones's")))), "Jones's"));
	EXPECT(ISNIL(FLookupWord(RefVar(NILREF), RefVar(Str("a")))));
	EXPECT(ISNIL(FLookupWord(RefVar(NILREF), RefVar(Str("two words")))));
	EXPECT(ISNIL(FLookupWord(RefVar(NILREF), RefVar(Str("1234")))));

	// the punctuation stripped where it lies, and the capitalisation bit
	RefVar shouted(Str("(Smith)"));
	EXPECT(RINT(FStripRecognitionWord(RefVar(NILREF), shouted)) == kCapStartsUpper && RefIs(shouted, "Smith"));
	RefVar quiet(Str("smith,"));
	EXPECT(RINT(FStripRecognitionWord(RefVar(NILREF), quiet)) == 0 && RefIs(quiet, "smith"));

	// the word recogniser is not the one reading here
	EXPECT(ISNIL(FWRecIsBeingUsed(RefVar(NILREF))));
	gWordID = 'WREC';
	EXPECT(NOTNIL(FWRecIsBeingUsed(RefVar(NILREF))));
	gWordID = 0;

	// the dictionaries: without the ROM's objects there is no list to
	// clone, so it comes out empty and every id answers nil
	InitDictionaries();
	EXPECT(IsArray(RefVar(Dictionaries())));
	EXPECT(ISNIL(FFindDictionaryFrame(RefVar(NILREF), RefVar(MAKEINT(31)))));

	printf("test_Words: %d failures\n", failures);
	return failures != 0;
}
