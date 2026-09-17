/*
	File:		views/ParagraphView.h

	Contains:	TParagraphView (clParagraphView, 81): a view of styled text -
				the class of protoStaticText and every editable paragraph.
				Its text slot is a string, its styles slot the style runs
				([length, style, ...], see StyleRuns.h; a single font spec
				when there are none), its tabs slot the tab stops; the text
				is wrapped into the bounds a line at a time, each line the
				height its fonts need (or viewLineSpacing when the view has
				one that fits its font), aligned by the viewJustify text
				bits, and an ellipsis marks text that does not fit.  The
				ROM's object is 0xd0 bytes and caches the lines (LineInfo)
				and the drawn text objects; the host keeps the lines.

				Display, and the caret: the paragraph can be the key view -
				SetCaretOffset keeps its caret offset (fCaretOffset), and
				OffsetToCaret/PointToCaret place the caret from the line
				cache (host: the text measured up to the offset; the ROM
				asks its text objects - CharBounds).  NOT YET RECONSTRUCTED:
				editing (insertion, the hilites, ink words, the recogniser's
				word and gesture handling, the edit commands), the tab stops
				(tabs draw as characters), the text objects (each line is
				laid out from the text when drawn), the bounds recalculation
				of vCalculateBounds paragraphs (the lines are all laid out;
				the view keeps its bounds), the locale's break tables in
				the word breaks (see FindWordBreaks), printing.

	Reconstructed from the MP2100 D ROM (0x0016b14c-0x0016e290,
	0x0017a778-0x0017aa60, 0x00180df0-0x001835b0); each function cites its
	origin.
*/

#ifndef __PARAGRAPHVIEW_H
#define __PARAGRAPHVIEW_H

#ifndef __DATAVIEW_H
#include "DataView.h"
#endif
#ifndef __TEXTVIEW_H
#include "TextView.h"		// vjOneLineOnly, vjNoLineLimits
#endif
#ifndef __TEXT_H
#include "Text.h"
#endif

// one line of the paragraph as FillAllCaches (0x0016dc68) records it: the
// ROM's LineInfo is 0x24 bytes - the offsets of the line's first
// character and of the one after its last, its first text object and the
// one after its last, whether it ends in white space, and its box
struct LineInfo
{
	long		fStart;				// +0x00  the line's first character
	long		fEnd;				// +0x04  after its last
	long		fFirstObj;			// +0x08  its first text object (host: the first style run)
	long		fEndObj;			// +0x0c  after its last
	Boolean		fEndsWithSpace;		// +0x10
	long		fAscent;			// +0x14  the baseline below the line's top
	long		fHeight;			// +0x18  the line's height
	Rect		fBounds;			// +0x1c  its box (the text's width)
};

class TParagraphView : public TDataView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x001803dc ClassID__14TParagraphViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x001803e4 DerivedFrom__14TParagraphViewCFl
	virtual			~TParagraphView();									// ROM 0x00182624 __dt__14TParagraphViewFv
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x00180df0 Constructor__14TParagraphViewFRC6RefVarP5TView
	virtual void	SetupDone(void);									// ROM 0x00181608 SetupDone__14TParagraphViewFv
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0016b14c RealDraw__14TParagraphViewFR5TRect
	virtual void	SetBounds(const Rect& bounds);						// ROM 0x00180418 SetBounds__14TParagraphViewFRC5TRect
	virtual void	SetCaretOffset(long* offset, long* length);			// ROM 0x00181008 SetCaretOffset__14TParagraphViewFPlT1
	virtual Ref		GetSelection(void);									// ROM 0x00181080 GetSelection__14TParagraphViewFv
	virtual void	SetSelection(RefArg selection, long* offset, long* length);	// ROM 0x001811a8 SetSelection__14TParagraphViewFRC6RefVarPlT2
	virtual void	ActivateSelection(Boolean on);						// ROM 0x00181308 ActivateSelection__14TParagraphViewFUc
	virtual void	OffsetToCaret(long offset, Rect* caret);			// ROM 0x00173b04 OffsetToCaret__14TParagraphViewFlP5TRect
	virtual void	PointToCaret(Point& pt, Rect* caret, Rect* bounds);	// ROM 0x001736f8 PointToCaret__14TParagraphViewFR6TPointP5TRectT2

	Ref			Text(void);												// ROM 0x00183034 Text__14TParagraphViewFv
	Ref			Styles(void);											// ROM 0x00183478 Styles__14TParagraphViewFv
	Ref			GetStyles(void);										// ROM 0x00183134 GetStyles__14TParagraphViewFv
	Ref			Tabs(void);												// ROM 0x001834e4 Tabs__14TParagraphViewFv
	Ref			GetDefaultViewStyle(void);								// ROM 0x0017a9ec GetDefaultViewStyle__14TParagraphViewFv
	long		GetInterLineSpacing(void);								// ROM 0x0016b490 GetInterLineSpacing__14TParagraphViewFv
	void		CreateAllCaches(void);									// ROM 0x0016dad8 CreateAllCaches__14TParagraphViewFv
	void		ClearAllCaches(void);									// ROM 0x0016dc00 ClearAllCaches__14TParagraphViewFv
	void		RefillAllCaches(void);									// ROM 0x0016e28c RefillAllCaches__14TParagraphViewFv
	void		FillAllCaches(void);									// ROM 0x0016dc68 FillAllCaches__14TParagraphViewFPs
	void		OffsetCachedBounds(Point& delta);						// ROM 0x0016b94c OffsetCachedBounds__14TParagraphViewFR6TPoint
	long		FindLineContainingCharOffset(long offset);				// ROM 0x0017a728 FindLineContainingCharOffset__14TParagraphViewFl (host: the line's index, -1 for none)
	void		OffsetToBounds(long offset, Rect* bounds);				// ROM 0x00179f50 OffsetToBounds__14TParagraphViewFlP5TRect
	long		PointToOffset(const Point& pt);							// ROM 0x00179550 PointToOffset__14TParagraphViewFRC6TPoint10MarginSizeUcP5TRectPP8LineInfoPlPUc (host: the nearest character)
	void		FlushWordAtCaret(void);									// ROM 0x00176cac FlushWordAtCaret__14TParagraphViewFv
	long		TextLength(void);										// the text's characters (host)

	long		LineCount(void) const				{ return fLineCount; }
	const LineInfo&	Line(long index) const			{ return fLines[index]; }
	const Rect&	TextBounds(void) const				{ return fTextBounds; }

	long		fTextFlags;			// +0x30  the input view's text flags (-1 until SetupDone)
	long		fTransferMode;		// +0x34  viewTransferMode (srcOr when none)
	long		fLineSpacing;		// +0x38  viewLineSpacing (0 when none)
	long		fLineHeight;		// +0x3c  the default style's height (ascent + descent + leading), then the last line's
	Rect		fCachedBounds;		// +0x40  the bounds the lines were laid out in
	Boolean		fCalculateBounds;	// +0x58  vCalculateBounds is set
	long		fCaretOffset;		// +0x60  the caret's character offset (SetCaretOffset)
	RefStruct	fWordBreakTable;	// +0x64  the locale's
	RefStruct	fLineBreakTable;	// +0x68
	LineInfo*	fLines;				// +0x74  the line cache (nil until made)
	long		fLineCount;
	long		fLineCapacity;
	Boolean		fCachesValid;		// +0x78
	TextOptions	fTextOptions;		// +0x84  the width and alignment the lines are laid out with
	Rect		fTextBounds;		// +0xa0  the lines' union

private:
	void		LayoutRuns(RefArg styles, long textLength);
	void		DrawLine(const UniChar* text, const LineInfo& line, Boolean ellipsis);
	void		DisposeRuns(void);

	// host: the style runs as records for DoTextOnce
	StyleRecord**	fRunStyles;
	short*			fRunLengths;
	long			fRunCount;
	RefStruct		fRunSpecs;		// the specs of the runs (an array; a single spec's run covers the text)
};

void	GrowLineInfoCache(LineInfo** cache, long* capacity);			// ROM 0x0017e9fc GrowLineInfoCache__FPPP8LineInfol

#endif	/* __PARAGRAPHVIEW_H */
