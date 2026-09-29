/*
	File:		text/TXLinesHeights.h

	Contains:	How tall the lines of a document are, and the control
				characters of a stretch of a paragraph - what the
				formatter and the lines lay the text out with.

				`TXLinesHeights` keeps the height of every line without a
				word per line: a TXArray of `TXLineHeightGroup`s, each a
				run of consecutive lines that share one height (and one
				natural height), so a document of single-spaced lines in
				one font is a single group however long it is.  Setting a
				line's height splits its group in two or three, or folds
				it into a neighbour that already has that height
				(`Concat`); removing lines joins the groups either side
				when they end up equal.  It also keeps the total height
				and the last line's number, so the height of the whole
				text is never summed.  `PixelToLine` walks the groups to
				the line a pixel falls on.  The ROM's object is 0x20
				bytes.

				`TXFormatReflowLines` is the first and last line a
				reflow touched (-1: none).

				`TXParagCtrlChars` records where the control characters
				(tabs, the line break) of a stretch of a paragraph are -
				up to thirty-two, stopping at the line break - so that
				laying a line out does not search the text again for
				each one.  The ROM's object is 0x90 bytes.

	Reconstructed from the MP2x00 US ROM (0x002390d4-0x002396e4,
	0x0023992c-0x00239978, 0x00239b4c-0x00239e0c, 0x00242a2c-0x00242b7c);
	each function cites its origin.
*/

#ifndef __TXLINESHEIGHTS_H
#define __TXLINESHEIGHTS_H

#ifndef __TXRULER_H
#include "TXRuler.h"
#endif
#ifndef __TXCHARS_H
#include "TXChars.h"
#endif


// A run of lines that share a height: the ROM's element is 12 bytes.
struct TXLineHeightGroup
{
	long			fCount;			// +0x00  lines in the run
	long			fHeight;		// +0x04  each one's height
	long			fNaturalHeight;	// +0x08  ... and what it would be at single spacing
};


// The lines a reflow touched.
class TXFormatReflowLines
{
public:
	void			Reset(void);									// ROM 0x0023962c Reset__19TXFormatReflowLinesFv
	Boolean			GetFirst(long* line) const;						// ROM 0x0023964c GetFirst__19TXFormatReflowLinesCFPl - ==> whether there is one
	Boolean			GetLast(long* line) const;						// ROM 0x00239668 GetLast__19TXFormatReflowLinesCFPl

	long			fFirst;			// +0x00
	long			fLast;			// +0x04
	Boolean			fFlag08;		// +0x08  set by Reset
	Boolean			fFlag09;		// +0x09  cleared by Reset
};


class TXLinesHeights : public TXArray
{
public:
					TXLinesHeights();								// ROM 0x002390d4 __ct__14TXLinesHeightsFv

	void			FreeData(void);									// ROM 0x0023912c FreeData__14TXLinesHeightsFv - every line gone
	// Groups `a` and `b` run together when they are equal (group nil: b
	// added to a; the caller takes b out), or `group` added to whichever
	// of the two equals it; ==> whether anything was run together.
	Boolean			Concat(long a, long b, const TXLineHeightGroup* group);	// ROM 0x00239158 Concat__14TXLinesHeightsFlT1PC17TXLineHeightGroup
	// Line `line` given a height; ==> an error, or noErr.  (The reflow
	// lines are not looked at, in the ROM either.)
	NewtonErr		SetLineHeightInfo(const TXLineHeightInfo& info, long line, TXFormatReflowLines* reflow);	// ROM 0x00239228 SetLineHeightInfo__14TXLinesHeightsFRC16TXLineHeightInfolP19TXFormatReflowLines
	// A line added before `line` (-1: at the end) with that height.
	NewtonErr		InsertLine(const TXLineHeightInfo& info, TXFormatReflowLines* reflow, long line);	// ROM 0x002393f8 InsertLine__14TXLinesHeightsFRC16TXLineHeightInfoP19TXFormatReflowLinesl
	NewtonErr		InsertLineHeightInfo(const TXLineHeightInfo& info, long line);	// ROM 0x002394d0 InsertLineHeightInfo__14TXLinesHeightsFRC16TXLineHeightInfol
	void			RemoveLines(long count, long line, TXFormatReflowLines* reflow);	// ROM 0x002394dc RemoveLines__14TXLinesHeightsFlT1P19TXFormatReflowLines
	// The group line `*line` is in; `*line` comes back as its place in
	// the group, `*index` (when asked) as the group's index.
	TXLineHeightGroup* LineToHeightGroup(long* line, long* index) const;	// ROM 0x00239684 LineToHeightGroup__14TXLinesHeightsCFPlT1
	// How many of the group's lines from `line` on `*pixels` reaches
	// into (the last one partly); `*pixels` comes back less their height.
	long			HeightToCountLines(const TXLineHeightGroup& group, long line, long* pixels) const;	// ROM 0x0023992c HeightToCountLines__14TXLinesHeightsCFRC17TXLineHeightGrouplPl
	// The line `*pixels` below the top of line `line` falls on; `*pixels`
	// comes back as where that line's top is (past the last line: the
	// line after it, and the whole height).  The group and the place in
	// it through the last two (when asked).
	long			PixelToLine(long* pixels, long line, TXLineHeightGroup** group, long* lineInGroup) const;	// ROM 0x00239b4c PixelToLine__14TXLinesHeightsCFPllPP17TXLineHeightGroupT1
	long			GetLinesHeight(long first, long last) const;	// ROM 0x00239c4c GetLinesHeight__14TXLinesHeightsCFlT1 - lines first to last, both included
	void			GetLineHeightInfo(long line, TXLineHeightInfo* info) const;	// ROM 0x00239d30 GetLineHeightInfo__14TXLinesHeightsCFlP16TXLineHeightInfo
	Boolean			EqualGroup(long index, const TXLineHeightGroup& group) const;	// ROM 0x00239d68 EqualGroup__14TXLinesHeightsCFlRC17TXLineHeightGroup - the same heights (the count aside); false for no such group
	Boolean			EqualGroup(long a, long b) const;				// ROM 0x00239dc0 EqualGroup__14TXLinesHeightsCFlT1

	long			fTotalHeight;	// +0x18
	long			fLastLine;		// +0x1c  the last line's number (-1: none)
};


const long	kTXParagCtrlCharsMax	= 32;

class TXParagCtrlChars
{
public:
	// The control characters of `start` to `end` (at most 0x7fff on)
	// found, up to thirty-two and up to the first line break.
	void			Define(TXChars* chars, long start, long end);	// ROM 0x00242a2c Define__16TXParagCtrlCharsFP7TXCharslT2
	long			GetCurrCtrlOffset(void);						// ROM 0x00242b1c GetCurrCtrlOffset__16TXParagCtrlCharsFv - where the current one is (-1: none left)
	UniChar			GetCurrCtrlChar(void);							// ROM 0x00242b44 GetCurrCtrlChar__16TXParagCtrlCharsFv - which it is (0: none left)
	void			Invalid(void);									// ROM 0x00242b6c Invalid__16TXParagCtrlCharsFv

	long			fStart;			// +0x00
	long			fEnd;			// +0x04  how far the search got
	long			fCount;			// +0x08
	short			fOffsets[kTXParagCtrlCharsMax];	// +0x0c  from fStart
	UniChar			fChars[kTXParagCtrlCharsMax];	// +0x4c
	long			fCurrent;		// +0x8c
};


// The one a line being laid out uses (TXLine::DefineRuns).
extern TXParagCtrlChars	gTXParagCtrlChars;					// ROM 0x0c104de0 gTXParagCtrlChars


#endif	/* __TXLINESHEIGHTS_H */
