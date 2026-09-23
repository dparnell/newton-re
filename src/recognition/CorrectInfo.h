/*
	File:		recognition/CorrectInfo.h

	Contains:	The *correction information* - what the machine remembers
				about the words already on the page, so that a word can
				still be corrected long after it was written.

				A word info frame (`WordInfo.h`) says what the recogniser
				made of one piece of writing.  The correction information
				is the list of them: `vars.correctInfo`, a frame with an
				`info` array holding one word info per word that has gone
				onto a page, each carrying where it went - the view's id,
				and the `start` and `stop` character offsets - so that it
				can be found again from a character offset alone
				(`FindWordInfo`).

				That is what the corrector reads when the writer taps a
				word and asks for its other readings, and it is why a
				word can be corrected after the paragraph around it has
				been edited: the offsets are moved along with the text
				(`OffsetCorrectionInfo`) rather than being worked out
				afresh.

				A word info in the list is not always the recogniser's.
				`MakeWordInfo(view, offset, length)` makes one for a
				stretch of text nobody wrote - typed, or pasted - so that
				the corrector has something to work with there too: the
				ink at the offset when there is any, and otherwise just
				the characters.

	Reconstructed from the MP2x00 US ROM (0x000761f0-0x00078d04); each
	function cites its origin.
*/

#ifndef __CORRECTINFO_H
#define __CORRECTINFO_H

#include "RecObject.h"
#include "Objects.h"

class TView;
class TUnitPublic;

// What a word info's `flags` slot says.  (kWordInfoIsInk, 8, is in
// WordInfo.h; these are the ones the correction list sets.)
enum
{
	kWordInfoKnown			= 0x0002,	// the corrector knows about this word
	kWordInfoAutoAdded		= 0x0004	// the word was added to the dictionary for it
};

// The list itself: vars.correctInfo, whose `info` array holds the word
// infos and whose `max` says how many are kept.
Ref		CorrectInfo(void);								// ROM 0x000761f0 CorrectInfo__Fv
void	InitCorrection(RefArg info);					// ROM 0x0007627c InitCorrection__FRC6RefVar - an empty `info` array
void	InitCorrection(void);							// ROM 0x00076bf0 InitCorrection__Fv - the global one, forty words deep

// A word info for a stretch of a view's text that nobody wrote: the ink
// at the offset when there is any, else the characters themselves.
Ref		MakeWordInfo(TView* view, long offset, long length);	// ROM 0x0007824c MakeWordInfo__FP5TViewlT2
// One out of a word (a string) or a bundle of strokes.
Ref		MakeWordInfo(RefArg word);						// ROM 0x0007817c MakeWordInfo__FRC6RefVar
// One reading: a protoWordInterp frame.
Ref		MakeWordInterp(RefArg word);					// ROM 0x00078548 MakeWordInterp__FRC6RefVar
Ref		MakeWordInterp(RefArg word, long score, long index, long label);	// ROM 0x00078598 MakeWordInterp__FRC6RefVarlN22
// The readings replaced by a list of plain words.
void	SetWordList(RefArg info, RefArg words);			// ROM 0x00078340 SetWordList__FRC6RefVarT1
// Where the word went: the view's id, the two offsets and the flags.
void	SetOffsetInfo(RefArg info, TView* view, long start, long stop, long flags);	// ROM 0x00078438 SetOffsetInfo__FRC6RefVarP5TViewlN23

// The readings, one at a time.
Ref		GetNthEntry(RefArg info, long index);			// ROM 0x00077c64 GetNthEntry__FRC6RefVarl - the index'th protoWordInterp
Ref		GetNthWord(RefArg info, long index);			// ROM 0x00077ce0 GetNthWord__FRC6RefVarl - its `word`
// The four characters of the unit's type, as the recogniser wrote them.
ULong	UnitID(RefArg info);							// ROM 0x00077be4 UnitID__FRC6RefVar
Boolean	TestWordInfoFlags(RefArg info, long flags);		// ROM 0x00077d34 TestWordInfoFlags__FRC6RefVarl - all of them set
void	ClearWordInfoFlags(RefArg info, long flags);	// ROM 0x00077e30 ClearWordInfoFlags__FRC6RefVarl

// The word at a character offset of a view, out of a correction list.
long	FindWordInfoIndex(RefArg list, TView* view, long offset);	// ROM 0x000768fc FindWordInfoIndex__FRC6RefVarP5TViewl - -1 for none
Ref		FindWordInfo(RefArg list, TView* view, long offset);		// ROM 0x00076a30 FindWordInfo__FRC6RefVarP5TViewl
Ref		FindWordInfo(TView* view, long offset);					// ROM 0x00078500 FindWordInfo__FP5TViewl - out of the global list

// A word info added to a list.  The one that takes a unit makes the
// frame from it and marks it as the corrector's (flags 3).
Ref		AddWordInfo(RefArg list, RefArg info);			// ROM 0x000770cc AddWordInfo__FRC6RefVarT1
Ref		AddWordInfo(RefArg list, TView* view, long start, long stop, TUnitPublic* unit);	// ROM 0x00076b64 AddWordInfo__FRC6RefVarP5TViewlT3P11TUnitPublic

// The writing at a character offset of a paragraph, as a stroke bundle.
Ref		GetStrokesAt(TView* view, long offset);			// ROM 0x00078960 GetStrokesAt__FP5TViewl
// A word the machine added to the dictionary for this entry taken out
// again, because the entry is going.
void	AutoRemove(RefArg info);						// ROM 0x0007959c AutoRemove__FRC6RefVar

#endif	/* __CORRECTINFO_H */
