/*
	File:		text/TXOffset.h

	Contains:	A place in the text, and a stretch of it.

				The ROM's `TXOffset` is two words: a character offset, and
				a flag saying whether an offset that falls exactly on a
				boundary (between two runs, two lines, two paragraphs)
				belongs to what *starts* there or to what *ends* there.
				The caret at the end of a line and the caret at the start
				of the next are the same offset told apart by that flag.

				Most of the engine passes it in two registers, and the
				reconstruction renders those as a `TXOffset` (a long,
				TXArray.h) and a separate `atStart` argument.  Where the
				ROM passes one *by address* and writes the flag back -
				`TXRulerRange::CharRangeToParagRange`, the hilite's caret
				moves - the struct itself is needed: that is
				`TXOffsetPos` here (the name is the reconstruction's; the
				ROM calls it TXOffset, which the long already is).

				`TXOffsetRange` is two of them, a start and an end: a
				selection, a word, a line.  The ROM's is 0x10 bytes.

	Reconstructed from the MP2x00 US ROM (0x00233fd0-0x00234130); each
	function cites its origin.
*/

#ifndef __TXOFFSET_H
#define __TXOFFSET_H

#ifndef __TXARRAY_H
#include "TXArray.h"
#endif


// The ROM's TXOffset: 8 bytes.
struct TXOffsetPos
{
	TXOffset	fOffset;			// +0x00
	Boolean		fAtStart;			// +0x04  an offset on a boundary belongs to what starts there

	bool		operator==(const TXOffsetPos& other) const;		// ROM 0x00233fd0 __eq__8TXOffsetCFRC8TXOffset
};


class TXOffsetRange
{
public:
				TXOffsetRange()			{ }
				TXOffsetRange(const TXOffsetPos& start, const TXOffsetPos& end);	// ROM 0x00233ff8 __ct__13TXOffsetRangeFRC8TXOffsetT1
				TXOffsetRange(TXOffset start, TXOffset end, Boolean startAtStart, Boolean endAtStart);	// ROM 0x0023403c __ct__13TXOffsetRangeFlT1UcT3

	void		Set(TXOffset start, TXOffset end, Boolean startAtStart, Boolean endAtStart);	// ROM 0x0023408c Set__13TXOffsetRangeFlT1UcT3
	bool		operator==(const TXOffsetRange& other) const;	// ROM 0x002340a8 __eq__13TXOffsetRangeCFRC13TXOffsetRange
	void		Offset(long delta);								// ROM 0x002340ec Offset__13TXOffsetRangeFl - both ends moved by delta
	void		CheckBounds(void);								// ROM 0x0023411c CheckBounds__13TXOffsetRangeFv - the ends put in order

	long		Length(void) const		{ return fEnd.fOffset - fStart.fOffset; }

	TXOffsetPos	fStart;				// +0x00
	TXOffsetPos	fEnd;				// +0x08
};


void	TXSwapLong(long* a, long* b);							// ROM 0x00234108 TXSwapLong__FPlT1


#endif	/* __TXOFFSET_H */
