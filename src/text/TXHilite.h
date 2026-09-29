/*
	File:		text/TXHilite.h

	Contains:	The hilite: the selection of a document, and how the pen
				and the arrow keys change it.

				The hilite is a TXOffsetRange (`fRange`) and a *state* -
				0 hidden, 1 shown, 2 the view inactive, when a selection is
				drawn as a frame in XOR rather than inverted.  Drawing is
				always in XOR (`InvertRect`, or the frame), so drawing the
				same hilite again takes it away: `SetHiliteState` draws it
				off and on, and extending a selection (`SetHiliteStart`,
				`SetHiliteEnd`) inverts only the stretch that changed.  A
				selection is drawn a frame at a time and within a frame as
				a first partial line, a block of whole lines and a last
				partial line (`HiliteRange`, `HiliteFrame`, `HiliteLine`,
				`HiliteRect`).  An empty range - the caret - is not drawn
				here: the Newton's root view draws the caret.  A range that
				is one picture (a graphics run whose flags have bit 1) draws
				its own hilite (TXGraphicsRun::SetHilite, DrawHilite).

				`Click` counts the clicks (within the pen's double-click
				time and one pixel: `CalcCountClicks`), selects a character
				boundary, a word or a line by that count (`GetClickRange`),
				or extends the selection with the shift flag, and follows
				the pen (`DragHilite`: the selection grown from the anchor
				as the pen moves, the view scrolled when it leaves it -
				`CalcAutoScrollParams`, faster the longer it stays out).
				`ArrowKey` moves or extends by a character, a word or a
				line (`LeftRightArrows`, `UpDownArrows`: up and down keep
				the horizontal position they started from).

				The ROM's object is 0x48 bytes and its vtable has no
				destructor (Textension deletes it with operator delete).
				TXNewtHilite - the root view's key view and caret - is NOT
				YET (it comes with TXView).

	Reconstructed from the MP2x00 US ROM (0x0023b124-0x0023cba8); each
	function cites its origin.  Click, DragHilite, HiliteLine and
	HiliteFrame were read from the assembly.
*/

#ifndef __TXHILITE_H
#define __TXHILITE_H

#ifndef __TXDISPLAY_H
#include "TXDisplay.h"
#endif

class Textension;
struct TXClickCommandInfo;		// what a click in a run asks for (TXView)

// What a click loop calls back with, as the pen is followed.
typedef void	(*TXClickLoopProc)(unsigned char inLoop, void* scroll, void* data);

// the hilite's states
enum
{
	kTXHiliteOff		= 0,
	kTXHiliteOn			= 1,
	kTXHiliteInactive	= 2
};

// Click and ArrowKey's flags
enum
{
	kTXClickLines		= 1,	// by line (arrows: to the line's end)
	kTXClickExtend		= 2,	// extend the selection
	kTXClickWords		= 8		// by word (arrows)
};


class TXHilite
{
public:
					TXHilite();										// ROM 0x0023b124 __ct__8TXHiliteFv

	virtual Boolean	Click(TXPointingDevice* pen, long flags, TXClickCommandInfo* command, TXClickLoopProc proc, void* data);	// ROM 0x0023b244 Click__8TXHiliteFP16TXPointingDevicelP18TXClickCommandInfoPFUcPvT2_vPv
	virtual void	SetHiliteState(char state);						// ROM 0x0023b9dc SetHiliteState__8TXHiliteFc
	// ==> whether the range changed.
	virtual Boolean	SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean scroll);	// ROM 0x0023c2c8 SetHiliteRange__8TXHiliteFRC13TXOffsetRangeUcT2
	virtual long	CalcCountClicks(Point pt, long now, long doubleClickTime);	// ROM 0x0023b1c4 CalcCountClicks__8TXHiliteF5PointlT2
	virtual void	DoClickLoop(Boolean inLoop, void* scroll);		// ROM 0x0023b4b4 DoClickLoop__8TXHiliteFUcPv - nothing
	virtual void	DragHilite(TXOffsetRange anchor, TXPointingDevice* pen, long flags, TXRun* run, TXClickCommandInfo* command, TXClickLoopProc proc, void* data);	// ROM 0x0023c8ec DragHilite__8TXHiliteF13TXOffsetRangeP16TXPointingDevicelP5TXRunP18TXClickCommandInfoPFUcPvT2_vPv

	void			SetHandlers(Textension* text, TXDisplay* display);	// ROM 0x0023b1bc SetHandlers__8TXHiliteFP10TextensionP9TXDisplay
	// A boundary that is not after a line break, and not at the text's
	// end, belongs to the character before it.
	void			AdjustCharOffset(TXOffsetPos* offset);			// ROM 0x0023b4b8 AdjustCharOffset__8TXHiliteFP8TXOffset
	void			ArrowKey(unsigned char key, long flags);		// ROM 0x0023b53c ArrowKey__8TXHiliteFUcl - 0x1c..0x1f
	Boolean			LeftRightArrows(Boolean right, long flags, TXOffsetPos* offset);	// ROM 0x0023b62c LeftRightArrows__8TXHiliteFUclP8TXOffset
	Boolean			UpDownArrows(Boolean up, long flags, TXOffsetPos* offset);	// ROM 0x0023b738 UpDownArrows__8TXHiliteFUclP8TXOffset
	void			Activate(Boolean active, Boolean show);			// ROM 0x0023b8e4 Activate__8TXHiliteFUcT1
	void			Draw(void);										// ROM 0x0023b90c Draw__8TXHiliteFv
	void			Invalid(Boolean hide);							// ROM 0x0023bae8 Invalid__8TXHiliteFUc
	void			GetHiliteRange(TXOffsetRange* range) const;		// ROM 0x0023bb24 GetHiliteRange__8TXHiliteCFP13TXOffsetRange
	void			HiliteRect(const TXLongRect& r, long frame) const;	// ROM 0x0023bb38 HiliteRect__8TXHiliteCFRC10TXLongRectl
	void			HiliteLine(long line, long frame, TXOffsetRange range, TXLongRect* r) const;	// ROM 0x0023bc24 HiliteLine__8TXHiliteCFlT113TXOffsetRangeP10TXLongRect
	void			HiliteFrame(long frame, TXOffsetRange range, long firstLine, long lastLine) const;	// ROM 0x0023bd1c HiliteFrame__8TXHiliteCFl13TXOffsetRangeN21
	void			CalcRangePosition(TXOffsetRange range, TXRunPositionInfo* where);	// ROM 0x0023bef8 CalcRangePosition__8TXHiliteF13TXOffsetRangeP17TXRunPositionInfo
	void			CalcCaretRect(void);							// ROM 0x0023bf90 CalcCaretRect__8TXHiliteFv
	// The hilite's outline as a region (only its frame when asked;
	// `global` is NOT YET - QuickDraw's LocalToGlobal is not in qd/).
	RgnHandle		GetHiliteRgn(Boolean frameOnly, Boolean global);	// ROM 0x0023bfdc GetHiliteRgn__8TXHiliteFUcT1
	Boolean			IsPointInHilite(Point pt);						// ROM 0x0023c0bc IsPointInHilite__8TXHiliteF5Point
	void			GetCaretRect(TXLongRect* r);					// ROM 0x0023c178 GetCaretRect__8TXHiliteFP10TXLongRect
	void			HiliteRange(TXOffsetRange range);				// ROM 0x0023c1ac HiliteRange__8TXHiliteF13TXOffsetRange
	TXRun*			IsCustomHilite(const TXOffsetRange* range);		// ROM 0x0023c288 IsCustomHilite__8TXHiliteFPC13TXOffsetRange - the picture that draws its own
	void			SetHiliteStart(TXOffsetPos start);				// ROM 0x0023c35c SetHiliteStart__8TXHiliteF8TXOffset
	void			SetHiliteEnd(TXOffsetPos end);					// ROM 0x0023c4e4 SetHiliteEnd__8TXHiliteF8TXOffset
	void			ExtendHilite(TXOffsetRange range);				// ROM 0x0023c674 ExtendHilite__8TXHiliteF13TXOffsetRange
	Boolean			GetClickRange(Point pt, int clicks, TXOffsetRange* range);	// ROM 0x0023c720 GetClickRange__8TXHiliteF5PointiP13TXOffsetRange
	void			CalcAutoScrollParams(Point* pt, long elapsed, TXLongPoint* scroll);	// ROM 0x0023c7e8 CalcAutoScrollParams__8TXHiliteFP5PointlP11TXLongPoint

	Textension*		fText;			// +0x04
	TXDisplay*		fDisplay;		// +0x08
	long			fClickCount;	// +0x0c
	unsigned long	fLastClickTime;	// +0x10
	Point			fLastClickPt;	// +0x14
	Boolean			fAutoScroll;	// +0x18
	char			fState;			// +0x19
	Boolean			fDrawn;			// +0x1a
	Boolean			fWordSelection;	// +0x1b  the selection was made by words
	long			fVisible;		// +0x1c  negative: not drawn
	TXLongRect		fCaretRect;		// +0x20  top -1: to be worked out
	long			fCaretFrame;	// +0x30
	TXOffsetRange	fRange;			// +0x34
	long			fUpDownH;		// +0x44  -1: to be worked out
};

unsigned long	TXCurrentTicks(void);								// ROM 0x0024dd18 TXCurrentTicks__Fv - Ticks()

#endif	/* __TXHILITE_H */
