/*
	File:		text/TXStyledText.h

	Contains:	Styled text: the characters (TXChars.h) and the runs
				that say what each stretch of them is (TXRun.h's
				TXRunRange), with the port they are measured in.

				It is the thing a line (TXLine.h) is laid out over.  Of its
				own it answers two questions: the word around an offset
				(`CharToWord` - a double tap - over the locale's word or
				line break table, looking no further than 64 characters
				either way, with the spaces after a word taken with it, or
				the word before them when the tap was on the spaces), and
				how far the caret moves over the character next to it
				(`AdvanceOffset`: one, or a whole run standing for one
				thing in the text, a picture).

				The styled text owns its characters and runs: deleting it
				deletes them.  With no port of its own it measures in the
				current one.

				The ROM's object is 0x10 bytes.

	Reconstructed from the MP2x00 US ROM (0x002461bc-0x0024659c); each
	function cites its origin.
*/

#ifndef __TXSTYLEDTEXT_H
#define __TXSTYLEDTEXT_H

#ifndef __TXRUN_H
#include "TXRun.h"
#endif

class TXChars;
struct GrafPort;

// CharToWord's flags
enum
{
	kTXWordNoSpaces		= 1,	// the word alone, not the spaces round it
	kTXWordLineBreaks	= 2		// the line break table rather than the word break table
};


class TXStyledText
{
public:
					TXStyledText();									// ROM 0x002461bc __ct__12TXStyledTextFv
	virtual			~TXStyledText();								// ROM 0x00246224 __dt__12TXStyledTextFv - the runs and the characters deleted
	virtual void	SetTextPort(GrafPort* port);					// ROM 0x00246288 SetTextPort__12TXStyledTextFP8GrafPort

	// `kind` is the run range's (TXRunRange: 2 for a document's).
	void			IStyledText(GrafPort* port, TXChars* chars, char kind);	// ROM 0x002461f0 IStyledText__12TXStyledTextFP8GrafPortP7TXCharsc
	GrafPort*		GetTextPort(void) const;						// ROM 0x00246290 GetTextPort__12TXStyledTextCFv - the port, or the current one
	Boolean			IsWordSpace(UniChar c) const;					// ROM 0x002462bc IsWordSpace__12TXStyledTextCFUs - a space or a tab
	// The word at the offset, into `range`; ==> false for none.
	Boolean			CharToWord(TXOffset offset, Boolean atStart, TXOffsetRange* range, char flags);	// ROM 0x00246304 CharToWord__12TXStyledTextF8TXOffsetP13TXOffsetRangec
	// How many characters the caret passes going over the one after the
	// offset (forward) or before it; nought at either end of the text.
	long			AdvanceOffset(long offset, Boolean forward);	// ROM 0x002464f4 AdvanceOffset__12TXStyledTextFlUc

	GrafPort*		fPort;			// +0x04
	TXChars*		fChars;			// +0x08
	TXRunRange*		fRuns;			// +0x0c
};

#endif	/* __TXSTYLEDTEXT_H */
