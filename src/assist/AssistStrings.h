/*
	File:		assist/AssistStrings.h

	Contains:	The string tidying a sentence written in the "Please ..."
				slip goes through before the Assistant tries to make sense
				of it: the white space normalised, the punctuation taken
				off the ends, the words split apart, and every run of
				consecutive words made into a string of its own so that
				each can be looked up in the lexicon.

				These live in the same ROM file as the task templates
				(assist/Assistant.h); they are here because they are about
				text rather than about matching.  A few small helpers from
				the lexicon file (0x0007d334-0x0007d78c) and the
				Data Stream file (0x0008337c-0x000833e4) come with them,
				because nothing else uses them.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __ASSISTSTRINGS_H
#define __ASSISTSTRINGS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"

// The C string copied, terminator and all; ==> the destination.
unsigned char*	Bstrcpy(unsigned char* to, const unsigned char* from);	// ROM 0x00086e50 Bstrcpy__FPUcT1
// The C string lowercased in place; ==> it.
unsigned char*	DownCase(unsigned char* str);							// ROM 0x0007d334 DownCase__FPUc
// `b` appended to `a` *in place* - `a` is grown and written to, so a
// read-only string must be cloned first.  ==> `a`.
Ref		NStringCat(RefArg rcvr, RefArg a, RefArg b);					// ROM 0x0008337c NStringCat__FRC6RefVarN21

// The string's text as single bytes, in a pointer the caller disposes
// of.  (This, StringLeftTrim, StringRightTrim and SplitString are the
// same ROM file's neighbours, reconstructed in frames/StringNatives.cpp
// because the string functions a script sees use them.)
char*	NewASCIIString(RefArg str);										// ROM 0x0007d78c NewASCIIString__FRC6RefVar

// StrAssoc(str, alist): the element of the list (of arrays) whose first
// element is the string.
Ref		StringAssoc(RefArg rcvr, RefArg str, RefArg alist);				// ROM 0x00083668 StringAssoc__FRC6RefVarN21
// "Name (label)" without the label; the string itself when it has none.
Ref		StringShorten(RefArg rcvr, RefArg str);							// ROM 0x00083794 StringShorten__FRC6RefVarT1
// "str (note)".
Ref		StringAnnotate(RefArg rcvr, RefArg str, RefArg note);			// ROM 0x000838c4 StringAnnotate__FRC6RefVarN21
// The first `limit` words of the string, split at white space.
Ref		StringDicer(RefArg str, ULong limit);							// ROM 0x000839f0 StringDicer__FRC6RefVarUl
// The two strings the same, case aside: TRUE or nil.
Ref		DSStringEQ(RefArg rcvr, RefArg a, RefArg b);					// ROM 0x00083c9c DSStringEQ__FRC6RefVarN21
// Every word of the first list one of the second (case aside).
Ref		DSPartialStrMatch(RefArg rcvr, RefVar words, RefVar of);		// ROM 0x00083cd8 DSPartialStrMatch__FRC6RefVar6RefVarT2
// A word of two to four characters on the stop list.
Ref		DSHasStopString(RefArg rcvr, RefArg words);						// ROM 0x00083e98 DSHasStopString__FRC6RefVarT1
// PrevSubStr(str, index): the start of the word the index is in.
Ref		DSPrevSubStr(RefArg rcvr, RefArg str, RefArg index);			// ROM 0x00083fa8 DSPrevSubStr__FRC6RefVarN21

// The array of strings as one string, a space between each pair.
Ref		GlueStrings(RefArg rcvr, RefArg strings);						// ROM 0x00084064 GlueStrings__FRC6RefVarT1
// GenSubStrings(str): every run of consecutive words of the string as a
// string of its own, the longest first.
Ref		GenerateSubstrings(RefArg rcvr, RefArg str);					// ROM 0x00084294 GenerateSubstrings__FRC6RefVarT1
// CleanString(str): the returns, line feeds and tabs in it turned into
// spaces, in place; ==> the string.
Ref		CleanString(RefArg rcvr, RefArg str);							// ROM 0x00084518 CleanString__FRC6RefVarT1
// The white space and punctuation taken off both ends: a new string, or
// the one given when there was nothing to take off, or nil when there
// was nothing else.
Ref		TrimBlanksAndPunct(RefArg rcvr, RefArg str);					// ROM 0x000845c0 TrimBlanksAndPunct__FRC6RefVarT1
// The string lowercased in place; ==> it.
Ref		MakeLowerCase(RefArg rcvr, RefArg str);							// ROM 0x000847c8 MakeLowerCase__FRC6RefVarT1

#endif	/* __ASSISTSTRINGS_H */
