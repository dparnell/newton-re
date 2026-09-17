/*
	File:		views/PickView.h

	Contains:	TPickView (clPickView, 91): protoPicker and the popup menus -
				a view listing its pickItems (strings, frames {item, mark,
				pickable, icon, indent, fixedHeight, keyCommand}, bitmap or
				picture frames (with width/height for a grid), ink, and the
				symbols 'pickSeparator and 'pickSolidSeparator), each item a
				row of its own height: the text items pickTextItemHeight,
				pictures their bounds plus the top and bottom margins, the
				separators 6; a text wider than pickMaxWidth is cut with an
				ellipsis; the marks (pickItemsMarkable) sit in a column
				pickMarkWidth wide after pickLeftMargin, the icons before the
				text (indent).  SetupForm lays the items out and sizes and
				places the view: below (or above) the template's bounds,
				within the application area (a taller list gets scrollers
				and Scroll moves the child origin).  RealDraw draws the rows
				and inverts the picked item; PickItem runs pickActionScript
				(the item's index plus topItem, or a grid item frame) on the
				callbackContext or the view, closing an autoclose picker
				first; Hide of an unpicked autoclose picker runs
				pickCancelledScript.  The ROM's object is 0xbc bytes.

				NOT YET RECONSTRUCTED: tracking the pen (TrackStroke on
				aeClick: the strokes), the key commands and type-select
				(GetKeyCommandInfo, HandleKeyDown, KeyToNextItem: a command
				keyboard is never connected on the host), ink items
				(DrawStrokeBundle), the pickable test inside a grid picture
				(FPtInPicture), the flashing of the picked item (Wait).

	Reconstructed from the MP2100 D ROM (0x00185690-0x0018a800); each
	function cites its origin.
*/

#ifndef __PICKVIEW_H
#define __PICKVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif
#ifndef __FONTS_H
#include "Fonts.h"
#endif

// which item, and which cell of a grid item
struct PickStuff
{
	long		fItem;			// +0x00  -1 for none
	Boolean		fIsGrid;		// +0x04
	long		fX;				// +0x08  the cell
	long		fY;				// +0x0c
};

// a grid item's layout (from its width/height/cellFrame/outerFrame)
struct PickGridInfo
{
	long		fOuterFrame;	// +0x00
	long		fCellFrame;		// +0x04
	long		fRows;			// +0x08  height
	long		fColumns;		// +0x0c  width
	long		fCellWidth;		// +0x10
	long		fCellHeight;	// +0x14
};

class TPickView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x00185854 ClassID__9TPickViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x00186c70 DerivedFrom__9TPickViewCFl
	virtual			~TPickView();										// ROM 0x00189878 __dt__9TPickViewFv
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x001885c0 Constructor__9TPickViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x001890f4 RealDoCommand__9TPickViewFRC6RefVar
	virtual void	SetupForm(void);									// ROM 0x00187350 SetupForm__9TPickViewFv
	virtual void	Hide(void);											// ROM 0x00188fa4 Hide__9TPickViewFv
	virtual void	RealDraw(Rect& bounds);								// ROM 0x001885e0 RealDraw__9TPickViewFR5TRect

	PickGridInfo*	GetGridInfo(RefArg item, const Rect& bounds);		// ROM 0x00185690 GetGridInfo__9TPickViewFRC6RefVarP5TRect
	Ref			GetDisplayIcon(RefArg item);							// ROM 0x00186d38 GetDisplayIcon__9TPickViewFRC6RefVar
	long		GetDisplayIndent(RefArg item);							// ROM 0x00186d7c GetDisplayIndent__9TPickViewFRC6RefVar
	long		GetDisplayFixedHeight(RefArg item);						// ROM 0x00186e00 GetDisplayFixedHeight__9TPickViewFRC6RefVar
	Ref			GetDisplayItem(long index, Boolean* pickable, UniChar* mark);	// ROM 0x00189ed8 GetDisplayItem__9TPickViewFlPUcPUs
	Ref			GetItemNoText(long index);								// ROM 0x0018a118 GetItemNoText__9TPickViewFl
	Ref			GetOverflows(void);										// ROM 0x00187098 GetOverflows__9TPickViewFv
	void		Scroll(RefArg direction, Boolean unpick);				// ROM 0x0018718c Scroll__9TPickViewFRC6RefVarUc
	void		GetItemRect(PickStuff* item, Rect* r);					// ROM 0x001893b4 GetItemRect__9TPickViewFP9PickStuffP5TRect
	void		GetGridItemRect(PickStuff* item, Rect* r);				// ROM 0x001892dc GetGridItemRect__9TPickViewFP9PickStuffP5TRect
	void		InvertItem(PickStuff* item);							// ROM 0x00189478 InvertItem__9TPickViewFP9PickStuff
	void		FlashItem(PickStuff* item);								// ROM 0x0018949c FlashItem__9TPickViewFP9PickStuff
	void		SubItem(Point& pt, PickStuff* item);					// ROM 0x001894e4 SubItem__9TPickViewFR6TPointP9PickStuff
	void		Item(Point& pt, PickStuff* item);						// ROM 0x001895b8 Item__9TPickViewFR6TPointP9PickStuff
	void		PickableItem(Point& pt, PickStuff* item);				// ROM 0x0018966c PickableItem__9TPickViewFR6TPointP9PickStuff
	void		PickItem(PickStuff* item);								// ROM 0x00189a7c PickItem__9TPickViewFP9PickStuff
	void		GetItemFlags(PickStuff* item, Boolean* pickable, UniChar* mark);	// ROM 0x00189e38 GetItemFlags__9TPickViewFP9PickStuffPUcPUs
	Boolean		IsItemNoPickable(long index);							// ROM 0x00189e64 IsItemNoPickable__9TPickViewFl
	Boolean		HandleKeyDown(UniChar ch, ULong parameter);				// ROM 0x0018a4b0 HandleKeyDown__9TPickViewFUsUl
	void		KeyToNextItem(long from);								// ROM 0x0018a17c KeyToNextItem__9TPickViewFl
	void		KeyToPrevItem(long from);								// ROM 0x0018a320 KeyToPrevItem__9TPickViewFl
	void		SetItemFlags(PickStuff* item, Boolean pickable, UniChar mark);	// ROM 0x00189e7c SetItemFlags__9TPickViewFP9PickStuffUcUs
	long		GetItemLength(long index);								// ROM 0x00189ea0 GetItemLength__9TPickViewFl
	void		SetItemLength(PickStuff* item, long length);			// ROM 0x00189eb4 SetItemLength__9TPickViewFP9PickStuffl

	long		ItemTop(long index) const	{ return index == 0 ? 0 : fItemBottoms[index - 1]; }
	long		ItemBottom(long index) const	{ return fItemBottoms[index]; }

	Boolean		fAutoClose;			// +0x30  pickAutoClose: closed when an item is picked
	Boolean		fHasMarks;			// +0x31  an item has a mark
	RefStruct	fPickItems;			// +0x34  (the ROM: a RefStruct*)
	short*		fItemBottoms;		// +0x38  (a handle) each item's bottom, from the top of the list
	ULong*		fItemFlags;			// +0x3c  (a handle) per item: bits 0-15 the mark, bit 16 not pickable, bits 20- the text length (negative: cut, an ellipsis after)
	PickGridInfo**	fGrids;			// +0x40  (a handle) per item: its grid, nil for none
	FontInfo	fFontInfo;			// +0x44
	StyleRecord	fStyle;				// +0x54  the viewFont
	PickStuff	fPicked;			// +0x74  the item picked (-1: none)
	long		fTextItemHeight;	// +0x84  pickTextItemHeight
	long		fItemCount;			// +0x88
	long		fTextLeft;			// +0x8c  the text's offset from the left (the margin, and the marks' column)
	long		fMarkLeft;			// +0x90  the marks'
	long		fRightMargin;		// +0x98  pickRightMargin (19 more with scrollers)
	long		fTopMargin;			// +0x9c
	long		fBottomMargin;		// +0xa0
	RefStruct	fKeyCommands;		// +0xa4  per item, with a command keyboard (NOT YET: nil)
	Fixed		fKeyCommandWidth;	// +0xa8
	RefStruct	fTypeSelect;		// +0xac  the characters typed to select an item (the ROM: a RefStruct*)
	ULong		fLastKeyTime;		// +0xb0  when the last was typed (Ticks)
	long		fTypeSelectTimeout;	// +0xb4  ticks: a pause longer starts the string again
	Boolean		fPicking;			// +0xb8  an item is being picked (Hide runs no cancel script)
};

void	RegisterPickNatives(void);												// PickViewKeyDown (the ROM's protoPicker viewKeyDownScript)
void	GetAppAreaBounds(Rect* bounds);										// ROM 0x001858f4 GetAppAreaBounds__FP5TRect - vars.displayParams' application area
Boolean	AdjustPopupInRect(Rect& bounds, long width, long height, const Rect& within, short frame);	// ROM 0x00186e84 AdjustPopupInRect__FR5TRectlT2RC5TRects - ==> placed above

#endif	/* __PICKVIEW_H */
