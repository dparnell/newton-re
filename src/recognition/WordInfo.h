/*
	File:		recognition/WordInfo.h

	Contains:	The *word info frame* - what a recognised (or unrecognised)
				piece of writing looks like to NewtonScript.

				`MakeWordInfo` builds one out of a `TUnitPublic`: a clone
				of the ROM's `protoWordInfo` with

				    unitID    the unit's four-character type as a string
				    words     an array of protoWordInterp frames, one
				              per reading, each with its word, score,
				              index and label
				    strokes   a stroke bundle of the writing itself
				    unitData  the recogniser's training data, when the
				              user has asked for it to be kept
				    flags     what the system makes of it
				    ink       filled in later, by the view that keeps it

				It is made once per unit and hung off the unit's public
				face (`TUnitPublic::WordInfo`), because everything that
				asks about a unit's words asks through it.

				The flag that matters here is `kWordInfoIsInk` (8): the
				engine could not read the writing, or there is nothing to
				read.  `MakeWordInfo` sets it and empties the `words`
				array, so a frame carrying ink never also carries a
				reading, and `GetInkCommand` is what turns such a frame
				into the command a view answers.

	Reconstructed from the MP2x00 US ROM (0x00077bb0-0x00078830); each
	function cites its origin.
*/

#ifndef __WORDINFO_H
#define __WORDINFO_H

#include "RecObject.h"
#include "objects.h"

class TUnitPublic;

// What the word info frame's `flags` slot says about the writing.
enum
{
	kWordInfoIsInk			= 0x0008		// unread: the frame carries ink, not words
};

// The label a recogniser puts on an ordinary word, as opposed to
// punctuation, a number or a gesture.  It is the one label the word
// list's dictionary filter treats specially.
enum { kWordLabelWord = 0x28 };

Ref		MakeWordInfo(TUnitPublic* unit);			// ROM 0x00077fd8 MakeWordInfo__FP11TUnitPublic
void	SetWordInfoFlags(RefArg info, long flags);	// ROM 0x00077dc0 SetWordInfoFlags__FRC6RefVarl - or'd in
Ref		EncodeUnitID(TUnitPublic* unit);			// ROM 0x00077bb0 EncodeUnitID__FP11TUnitPublic - the type as a two-character string
Ref		MakeWordList(TUnitPublic* unit);			// ROM 0x000786b4 MakeWordList__FP11TUnitPublic - the readings as protoWordInterp frames

void	RegisterWordInfoNatives(void);			// WordUnitToWordInfo (WordInfo.cpp)

#endif	/* __WORDINFO_H */
