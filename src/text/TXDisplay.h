/*
	File:		text/TXDisplay.h

	Contains:	The display: the formatted text drawn into a view, scrolled,
				hit-tested, and redrawn after an edit.

				A `TXDisplay` draws through the *view region* it is given
				(`SetViewRgn`: the part of the port the text shows in) with
				the port's origin at nought (`Focus`, `UnFocus`), the frames
				(TXFrames.h) turning the document's coordinates into the
				port's.  `Draw` erases and draws every line of every frame
				the rectangle crosses, a band of equal lines at a time
				(`DrawFrameText`, `DrawLineGroup`: TXFrames::SectLines,
				then TXLine::DoLineLayout and Draw for each line), and the
				hilite over it (TXHilite.h).  `Scroll` moves the bits
				(`ScrollRect`, over QuickDraw's) and draws what was
				uncovered; the scroll is never allowed past the text
				(`AdjustScrollValues`), and after an edit that shortened the
				text the view scrolls back (`CheckScroll`).

				An edit is bracketed by `BeginEdit` (the hilite taken away,
				the frames caught - TXFrameFormatter's edit note) and
				`EndEdit`, which works out for each frame what to redraw:
				the lines that changed (`FrameEndEdit`), with the lines after
				them moved by blitting when their height changed
				(`ScrollFrame`, `UpdateScrolledArea`), the frame's bottom
				erased when the text got shorter (`EraseFrameBottom`), and
				the hilite put back.

				`PointToChar`, `CharToPoint` and `GetLineHilite` are the
				hit-testing a hilite and a click are made of.

				The ROM's object is 0x24 bytes; `fDrawVisLevel` (negative:
				nothing is drawn) and `fLastEditAction` (what the last edit
				did to the display: 0x10 redrawn, 0x20 frames invalidated,
				0x40 a hilite moved) are statics.

				TXNewtDisplay - the Newton's, over a TView's visible region
				and the screen's locks - is NOT YET (it comes with TXView).

	Reconstructed from the MP2x00 US ROM (0x00235b00-0x00237540); each
	function cites its origin.  EndEdit, FrameEndEdit, UpdateScrolledArea,
	IsHiliteVisible and CheckScroll were read from the assembly.
*/

#ifndef __TXDISPLAY_H
#define __TXDISPLAY_H

#ifndef __TXFRAMES_H
#include "TXFrames.h"
#endif
#ifndef __TXLINE_H
#include "TXLine.h"
#endif

class TXHilite;
class TXFormatter;
class TXStyledText;
class TXRulerRange;

// What drawing needs set up and put back: the port, and the clip and
// origin the display replaced.
struct TXDrawEnv
{
	GrafPtr			fPort;			// +0x00
	RgnHandle		fClip;			// +0x04  (in a TXEditInfo, -1: drawing was off)
	Point			fOrigin;		// +0x08
};

// An edit's bracket.  The ROM's is 0x18 bytes.
struct TXEditInfo
{
	Boolean			fDoEdit;		// +0x00  false: the edit failed; only put things back
	long			fOldCountFrames;	// +0x04
	TXDrawEnv		fEnv;			// +0x08
	char			fOldHiliteState;	// +0x14
};

// What a display is made of (Textension::ITextension).
struct TXDisplayHandlers
{
	TXHilite*		fHilite;		// +0x00
	TXStyledText*	fText;			// +0x04
	TXFormatter*	fFormatter;		// +0x08
	TXFrames*		fFrames;		// +0x0c  nil: a TXMonoFrame is made
	TXRulerRange*	fRulers;		// +0x10
};

// The band arrays DrawFrameText borrows (TXFrames::SectLines).
class TXTempLines : public TXTempReferences
{
public:
	virtual void*	CreateNewReference(void);						// ROM 0x00235b00 CreateNewReference__11TXTempLinesFv - a TXArray of TXSectLine
	virtual void	FreeReference(void* ref);						// ROM 0x00235b10 FreeReference__11TXTempLinesFPv
};

extern TXTempReferences*	gTXTempLines;						// (the ROM's unnamed word at 0x0c104d8c, TXDisplay::Start's)


class TXDisplay
{
public:
					TXDisplay();									// ROM 0x00237390 __ct__9TXDisplayFv
	virtual			~TXDisplay();									// ROM 0x002373f0 __dt__9TXDisplayFv - the frames and the line deleted

	virtual void	FreeData(void);									// ROM 0x002374cc FreeData__9TXDisplayFv
	virtual void	Draw(const Rect& r);							// ROM 0x00235b20 Draw__9TXDisplayFRC4Rect
	virtual void	Focus(RgnHandle* savedClip, Point* savedOrigin);	// ROM 0x00236604 Focus__9TXDisplayFPPP6RegionP5Point
	virtual void	UnFocus(RgnHandle savedClip, Point savedOrigin);	// ROM 0x00236688 UnFocus__9TXDisplayFPP6Region5Point
	virtual void	BeginEdit(TXEditInfo* info);					// ROM 0x00236094 BeginEdit__9TXDisplayFP10TXEditInfo
	virtual void	EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret);	// ROM 0x00236180 EndEdit__9TXDisplayFRC10TXEditInfolT2P8TXOffset

	static void		Start(void);									// ROM 0x002365c4 Start__9TXDisplaySFv - gTXTempLines made

	void			SetHandlers(TXDisplayHandlers* handlers);		// ROM 0x0023745c SetHandlers__9TXDisplayFP17TXDisplayHandlers
	void			SetViewRgn(RgnHandle rgn);						// ROM 0x002365b4 SetViewRgn__9TXDisplayFPP6Region
	void			SetDrawEnv(TXDrawEnv* env);						// ROM 0x002366cc SetDrawEnv__9TXDisplayFP9TXDrawEnv - nests
	void			RestoreDrawEnv(const TXDrawEnv& env);			// ROM 0x00236720 RestoreDrawEnv__9TXDisplayFRC9TXDrawEnv
	void			InvalidDraw(void);								// ROM 0x00236760 InvalidDraw__9TXDisplayFv
	void			GetViewFrames(TXSectFrames* frames) const;		// ROM 0x0023678c GetViewFrames__9TXDisplayCFP12TXSectFrames
	void			DrawLineGroup(const TXSectLine& band, RgnHandle clip);	// ROM 0x002367d0 DrawLineGroup__9TXDisplayFRC11TXSectLinesPP6Region
	Boolean			DrawFrameText(long frame, const Rect* r);		// ROM 0x002368c4 DrawFrameText__9TXDisplayFlPC4Rect - ==> whether anything showed
	Boolean			ScrollRect(const Rect& r, long dh, long dv, RgnHandle update, Boolean extend);	// ROM 0x00236a5c ScrollRect__9TXDisplayFRC4RectlT2PP6RegionUc
	void			UpdateScrolledArea(RgnHandle update, const TXFrameEditInfo& info);	// ROM 0x00236a98 UpdateScrolledArea__9TXDisplayFPP6RegionRC15TXFrameEditInfo
	void			FrameEndEdit(const TXFrameEditInfo& info, long above, long height);	// ROM 0x00236ba4 FrameEndEdit__9TXDisplayFRC15TXFrameEditInfolT2
	void			EraseFrameBottom(long frame);					// ROM 0x00236e10 EraseFrameBottom__9TXDisplayFl
	void			ScrollFrame(const TXFrameEditInfo& info);		// ROM 0x00236e9c ScrollFrame__9TXDisplayFRC15TXFrameEditInfo
	void			UpdateOverflowLines(const TXFrameEditInfo& info);	// ROM 0x00236f7c UpdateOverflowLines__9TXDisplayFRC15TXFrameEditInfo
	void			DoLineLayout(long line);						// ROM 0x00236ff0 DoLineLayout__9TXDisplayFl
	// The scroll by (d->h, d->v) cut to what the text allows.
	void			AdjustScrollValues(TXLongPoint* d);				// ROM 0x00235be8 AdjustScrollValues__9TXDisplayFP11TXLongPoint
	void			Scroll(TXLongPoint* d);							// ROM 0x00235cf4 Scroll__9TXDisplayFP11TXLongPoint
	// Scrolled further than the text now reaches: scrolled back (drawn
	// when `draw`); ==> whether it was.
	Boolean			CheckScroll(Boolean draw);						// ROM 0x00235df4 CheckScroll__9TXDisplayFUc
	void			GetScrolledValues(TXLongPoint* scrolled);		// ROM 0x00235f1c GetScrolledValues__9TXDisplayFP11TXLongPoint
	void			Activate(Boolean active, Boolean show);			// ROM 0x00235f38 Activate__9TXDisplayFUcT1
	// Whether the hilite's start (or end) shows; `d` the scroll that
	// would centre it.
	Boolean			IsHiliteVisible(TXLongPoint* d, Boolean end);	// ROM 0x00235f48 IsHiliteVisible__9TXDisplayFP11TXLongPointUc
	// The character boundary at a point of the port; ==> the run there.
	TXAttrObject*	PointToChar(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past);	// ROM 0x00237048 PointToChar__9TXDisplayF5PointP13TXOffsetRangePUcT3
	void			CharToPoint(TXOffset offset, Boolean atStart, TXLongPoint* pt, long* height, long* ascent);	// ROM 0x00237178 CharToPoint__9TXDisplayF8TXOffsetP11TXLongPointPiT3
	Point			CharToPoint(TXOffset offset, Boolean atStart, long* height, long* ascent);	// ROM 0x0023729c CharToPoint__9TXDisplayF8TXOffsetPiT2
	long			PointToLine(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past) const;	// ROM 0x002372f0 PointToLine__9TXDisplayCF5PointP13TXOffsetRangePUcT3
	void			GetLineHilite(long line, TXOffsetRange range, TXLineHilite* hilite, Boolean wholeLine);	// ROM 0x00237340 GetLineHilite__9TXDisplayFl13TXOffsetRangeP12TXLineHiliteUc
	void			DisableDrawing(void);							// ROM 0x002374f0 DisableDrawing__9TXDisplayFv
	void			EnableDrawing(void);							// ROM 0x00237518 EnableDrawing__9TXDisplayFv

	static unsigned long	fLastEditAction;						// ROM 0x0c104d90 fLastEditAction__9TXDisplay
	static long				fDrawVisLevel;							// ROM 0x0c104d94 fDrawVisLevel__9TXDisplay

	TXHilite*		fHilite;		// +0x04
	TXFrames*		fFrames;		// +0x08
	TXStyledText*	fText;			// +0x0c
	TXFormatter*	fFormatter;		// +0x10
	TXFrameFormatter* fFrameFormatter;	// +0x14
	TXLine*			fLine;			// +0x18  the one line laid out at a time
	unsigned char	fDrawLevel;		// +0x1c  SetDrawEnv's nesting
	RgnHandle		fViewRgn;		// +0x20
};

#endif	/* __TXDISPLAY_H */
