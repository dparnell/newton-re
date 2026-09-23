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
	kWordInfoHasTrainingData= 0x0001,	// the entry carries the engine's training data
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

// Keeping up with the text: every edit of a paragraph has to be
// answered here, or the corrector would offer a word's alternatives for
// whatever now sits at its offsets.
void	OffsetCorrectionInfo(RefArg list, TView* view, long at, long removed, long inserted);	// ROM 0x000762c0 OffsetCorrectionInfo__FRC6RefVarP5TViewlN23
void	OffsetCorrectionInfo(TView* view, long at, long removed, long inserted);	// ROM 0x0007779c OffsetCorrectionInfo__FP5TViewlN22
void	ClearCorrectionRange(RefArg list, TView* view, long at, long length);	// ROM 0x000765bc ClearCorrectionRange__FRC6RefVarP5TViewlT3
void	DeletedCorrectionInfo(RefArg list, TView* view);	// ROM 0x00076758 DeletedCorrectionInfo__FRC6RefVarP5TView
void	RemoveCorrectionInfo(RefArg list, TView* view);	// ROM 0x00076830 RemoveCorrectionInfo__FRC6RefVarP5TView
void	RemoveCorrectionInfo(TView* view);				// ROM 0x00077d88 RemoveCorrectionInfo__FP5TView
void	ClearEmptyEntries(RefArg list);					// ROM 0x00078d04 ClearEmptyEntries__FRC6RefVar
// The entry for a range of a view's text, made when there is none.
Ref		GetWordInfo(RefArg list, TView* view, long at, long length);	// ROM 0x00076f58 GetWordInfo__FRC6RefVarP5TViewlT3
// Two entries made into one, and the pieces of that.
void	MergeWordInfo(RefArg list, long first, long second);	// ROM 0x00076dc0 MergeWordInfo__FRC6RefVarlT2
Ref		MergeWords(RefArg first, RefArg second);		// ROM 0x00076c48 MergeWords__FRC6RefVarT1
Ref		MakeWordList(RefArg interp);					// ROM 0x0007866c MakeWordList__FRC6RefVar
void	MergeStrokes(RefArg bundle, RefArg other);		// ROM 0x00078840 MergeStrokes__FRC6RefVarT1
// The entries covering a range of a view's text taken away and put
// back, which is how an undo keeps a deleted word's alternatives.
Ref		ExtractRange(RefArg list, TView* view, long from, long to);	// ROM 0x00077190 ExtractRange__FRC6RefVarP5TViewlT3
void	InsertRange(RefArg list, RefArg range, TView* view);	// ROM 0x00077378 InsertRange__FRC6RefVarT1P5TView

// The readings edited one at a time, which is what the corrector does
// as the writer chooses between them.
long	FindMatchingWord(RefArg info, RefArg word);		// ROM 0x000774c8 FindMatchingWord__FRC6RefVarT1 - by characters; -1 for none
void	InsertWordInterp(RefArg info, RefArg interp, long index);	// ROM 0x00077644 InsertWordInterp__FRC6RefVarT1l
void	RemoveWordInterp(RefArg info, long index);		// ROM 0x000776e4 RemoveWordInterp__FRC6RefVarl
void	DeleteMatchingWord(RefArg info, RefArg word);	// ROM 0x0017b18c DeleteMatchingWord__FRC6RefVarT1

// Learning from the list: a word falling off the end of it is the
// machine's last chance to learn from what the writer did with it.
Ref		DoEntryLearning(RefArg info, long which);		// ROM 0x00077ea0 DoEntryLearning__FRC6RefVarl
void	DoOverflowLearning(RefArg list);				// ROM 0x000793d0 DoOverflowLearning__FRC6RefVar
void	AutoAdd(RefArg info);							// ROM 0x000794ec AutoAdd__FRC6RefVar
// The word the recogniser has just put onto a page registered with the
// machine.
Ref		AddWordInfo(TView* view, long start, long stop, TUnitPublic* unit);	// ROM 0x00079790 AddWordInfo__FP5TViewlT2P11TUnitPublic

#endif	/* __CORRECTINFO_H */
