/*
	File:		recognition/Learning.h

	Contains:	What the machine keeps of what is written on it: the words
				the writer adds by hand or has added for them, and the
				abbreviations that expand into whole words.

				Three of the dictionaries are the writer's own and start
				empty (`InitDictionaries` makes them): 31 the user
				dictionary, 35 the expand dictionary and 36 the auto-add
				one.  The first two are what the Prefs slips show; the
				third is the machine's own record of what it added on the
				writer's behalf, so that it can take it out again.

				A word is added to a dictionary through
				`AddWordWithCount`, which keeps the dictionary frame's
				`count` slot in step and refuses to go past its `limit` -
				which is how the user dictionary's "full" message comes
				about.

				The expand dictionary is a dictionary and an array
				together: the dictionary's attribute for a word is the
				index of the expansion in the frame's `list`, and
				`ExpandWord` puts the pieces back together - the
				punctuation the writer wrote around the abbreviation is
				kept, and a capital is carried over onto the expansion.

	Reconstructed from the MP2x00 US ROM (0x001aa600-0x001ab280); each
	function cites its origin.
*/

#ifndef __LEARNING_H
#define __LEARNING_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __UNICODE_H
#include "Unicode.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

// Whether the word's first letter is a capital.  The word is left as it
// was found.
Boolean	Capitalized(UniChar* word);						// ROM 0x001aa8ec Capitalized__FPUs

// The punctuation taken off both ends of the word, in place.  What came
// off is answered as two pointers the caller disposes of (nil for none);
// the word itself is moved down over its leading punctuation.
void	CollectPunctSymbols(UniChar* word, UniChar** leading, UniChar** trailing);	// ROM 0x001aa680 CollectPunctSymbols__FPUsPPUsT2

// Whether the expand dictionary knows this word, and where its expansion
// is in the dictionary frame's `list`.
Boolean	GetExpandIndex(const UniChar* word, ULong* index);	// ROM 0x001aa600 GetExpandIndex__FPUsPUl

// The word written out in full, as a Handle of UniChars the caller
// disposes of; nil when there is nothing to expand.
Handle	ExpandWord(UniChar* word);						// ROM 0x001aa930 ExpandWord__FPUs

// A word added to the dictionary of that id, with the frame's `count`
// kept in step; ==> the count before the word was added.  airusResult is
// -15 when the dictionary is as full as its `limit` allows.
long	AddWordWithCount(long id, UByte* word, ULong attribute);	// ROM 0x001aab74 AddWordWithCount__FlPUcUl

// Whether this is the word the auto-add dictionary was last offered -
// which is how writing the same unknown word twice in a row adds it only
// once.  A word that is not the last one becomes the last one.
Boolean	LastWordSame(RefArg word);						// ROM 0x001aae08 LastWordSame__FRC6RefVar

#endif	/* __LEARNING_H */
