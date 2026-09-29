/*
	File:		text/TXLine.h

	Contains:	One line of styled text, laid out: the pieces it is cut
				into, how wide each is, where it starts, and how it is
				drawn and hit-tested.

				`DoLineLayout` lays out the characters [start, start +
				length) of a TXStyledText (TXStyledText.h) in a width.  The
				line is cut into *pieces* (`TXLineRunInfo`) wherever its
				run changes (TXRun.h) and at every control character in
				it (`gTXParagCtrlChars`, TXLinesHeights.h): each piece is
				a run's stretch of text (kind 0), a tab (kind 9), or a
				line end (kind 13 or 10), the last two a character each.
				The ruler of the paragraph (TXRuler.h, out of the
				TXRulerRange) supplies the margins and the tab stops; the
				pieces are measured by their runs, and a tab's width is
				the ruler's - worked out at once for a left tab, or once
				the text after it is known for the others
				(`DefineRunWidths`; a decimal tab aligns on its character,
				`CalcAlignTabWidth`).

				The paragraph's justification then places the line: right
				and centred move its start along (`fLeft`), full spreads the
				room left over the text runs after the last tab, in
				proportion to what each run says it can take (a thirty-
				second of its size a space: `CalcFullJustifPortions`,
				`DefineRunsExtraWidths`) - except on a paragraph's last
				line.  Justified and right-aligned lines leave their
				trailing spaces out of their visible length.

				After that the line answers the questions a display asks:
				`Draw`, `CharacterToPixel`, `PixelToCharacter` (a tap), and
				`GetLineHilite` (the stretch of the line a selection
				covers).  Widths and positions are 16.16 throughout.

				The ROM's object is 0x30 bytes.

	Reconstructed from the MP2x00 US ROM (0x0023cba8-0x0023ded4); each
	function cites its origin.
*/

#ifndef __TXLINE_H
#define __TXLINE_H

#ifndef __TXRUN_H
#include "TXRun.h"
#endif

class TXStyledText;
class TXRulerRange;
class TXRuler;
struct TXPendingTab;

// One piece of a line.  The ROM's element is 0x18 bytes.
struct TXLineRunInfo
{
	TXRun*			fRun;			// +0x00
	long			fOffset;		// +0x04  from the line's start
	long			fLength;		// +0x08
	Fixed			fWidth;			// +0x0c
	Fixed			fExtra;			// +0x10  the full justification's share
	unsigned char	fKind;			// +0x14  0 text, else the control character (9 a tab)
};

// The stretch of a line a selection covers.
struct TXLineHilite
{
	Fixed			fLeft;			// +0x00
	Fixed			fWidth;			// +0x04
};

class TXLine
{
public:
					TXLine(TXStyledText* text, TXRulerRange* rulers);	// ROM 0x0023cba8 __ct__6TXLineFP12TXStyledTextP12TXRulerRange
	virtual			~TXLine();										// ROM 0x0023cc0c __dt__6TXLineFv

	// The characters [start, start + length) laid out in `width` pixels;
	// nothing to do when the line already starts there.
	void			DoLineLayout(long start, long length, long width);	// ROM 0x0023d8fc DoLineLayout__6TXLineFlN21
	// Drawn on the line's rectangle, the baseline `baseline` below its top.
	void			Draw(const Rect& line, int baseline);			// ROM 0x0023cfb8 Draw__6TXLineFRC4Recti
	// Where the character at the offset starts, in whole pixels.
	short			CharacterToPixel(TXOffset offset, Boolean atStart);	// ROM 0x0023d1c8 CharacterToPixel__6TXLineF8TXOffset
	// The character boundary (or a picture's whole range) at `pixel`;
	// ==> the run it is in.
	TXRun*			PixelToCharacter(Fixed pixel, TXOffsetRange* range);	// ROM 0x0023d66c PixelToCharacter__6TXLineFlP13TXOffsetRange
	// The stretch of the line [range) covers; `wholeLine` takes it out to
	// the line's edges where the range runs off them.  An empty range is
	// a one-pixel caret.
	void			GetLineHilite(TXOffsetRange range, TXLineHilite* hilite, Boolean wholeLine);	// ROM 0x0023d424 GetLineHilite__6TXLineF13TXOffsetRangeP12TXLineHiliteUc

	// the pieces
	void			DefineRuns(long start, long length);			// ROM 0x0023dd80 DefineRuns__6TXLineFlT1
	void			InsertRun(long at, long length, TXRun* run);	// ROM 0x0023dcc8 InsertRun__6TXLineFlT1P5TXRun
	long			CalcVisibleLength(const UniChar* text, long length, long* trailing);	// ROM 0x0023dc34 CalcVisibleLength__6TXLineFPCUslPl
	Fixed			DefineRunWidths(const UniChar* text, Fixed available, Boolean firstLine);	// ROM 0x0023cc58 DefineRunWidths__6TXLineFPCUslUc
	Fixed			CalcAlignTabWidth(const UniChar* text, TXPendingTab* pending, Fixed since, const TXLineRunInfo& run);	// ROM 0x0023de28 CalcAlignTabWidth__6TXLineFPCUsP12TXPendingTablRC13TXLineRunInfo
	Fixed			CalcFullJustifPortions(const UniChar* text, long* portions, long* count);	// ROM 0x0023ce20 CalcFullJustifPortions__6TXLineFPCUsPlT2
	void			DefineRunsExtraWidths(const UniChar* text, Fixed extra);	// ROM 0x0023cf00 DefineRunsExtraWidths__6TXLineFPCUsl
	void			GetRunDisplayInfo(const UniChar* text, const TXLineRunInfo* run, TXLineRunDisplayInfo* info);	// ROM 0x0023d7f4 GetRunDisplayInfo__6TXLineFPCUsPC13TXLineRunInfoP20TXLineRunDisplayInfo
	Fixed			RunMeasure(const UniChar* text, const TXLineRunInfo* run);	// ROM 0x0023d8c4 RunMeasure__6TXLineFPCUsPC13TXLineRunInfo
	Fixed			RunCharToPixel(const UniChar* text, const TXLineRunInfo* run, long offset);	// ROM 0x0023d81c RunCharToPixel__6TXLineFPCUsPC13TXLineRunInfol
	// The piece the offset is in, the pixel it starts at into `pixel`;
	// -1 for a line with none.
	long			CharToRun(TXOffset offset, Boolean atStart, Fixed* pixel);	// ROM 0x0023d0f4 CharToRun__6TXLineF8TXOffsetPl
	// The piece at `*pixel`, which becomes the pixel within it.
	long			PixelToRun(Fixed* pixel);						// ROM 0x0023d5e0 PixelToRun__6TXLineFPl
	long			CalcRunHilite(const UniChar* text, long from, long to, long index, Fixed* left, Fixed* width, Fixed* x, Boolean wholeLine);	// ROM 0x0023d2ac CalcRunHilite__6TXLineFPCUslN22PlN25Uc

	TXLineRunInfo*	GetRunInfo(long index) const	{ return (TXLineRunInfo*) fRunInfos->GetElementPtr(index); }

	TXStyledText*	fText;			// +0x04
	TXRulerRange*	fRulers;		// +0x08
	TXRuler*		fRuler;			// +0x0c  the paragraph's
	long			fStart;			// +0x10  -1: not laid out
	long			fLength;		// +0x14
	long			fVisibleLength;	// +0x18  less trailing spaces, where the justification wants that
	Boolean			fInvisible;		// +0x1c  nothing on it shows
	TXArray*		fRunInfos;		// +0x20  of TXLineRunInfo
	long			fLast;			// +0x24  the last piece's index
	Fixed			fLeft;			// +0x28  where the text starts
	Fixed			fWidth;			// +0x2c  the whole line's
};

#endif	/* __TXLINE_H */
