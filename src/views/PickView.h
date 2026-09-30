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

				A click tracks the pen over the items (TrackStroke) and picks
				the one it ends on; with a keyboard, the arrows and
				type-select move the pick (HandleKeyDown), and an item's key
				command is drawn at its right and sent as a key message when
				it is picked (GetKeyCommandInfo, PickItem).  An ink item (a
				strokeList frame) is drawn no more than 28 high.

	Reconstructed from the MP2x00 US ROM (0x00183660-0x001887d0); each
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

class TStrokePublic;

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
	virtual long	ClassID(void) const;								// ROM 0x00183824 ClassID__9TPickViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x00184c40 DerivedFrom__9TPickViewCFl
	virtual			~TPickView();										// ROM 0x00187848 __dt__9TPickViewFv
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x00186590 Constructor__9TPickViewFRC6RefVarP5TView
	virtual Boolean	RealDoCommand(RefArg cmd);							// ROM 0x001870c4 RealDoCommand__9TPickViewFRC6RefVar
	virtual void	SetupForm(void);									// ROM 0x00185320 SetupForm__9TPickViewFv
	virtual void	Hide(void);											// ROM 0x00186f74 Hide__9TPickViewFv
	virtual void	RealDraw(Rect& bounds);								// ROM 0x001865b0 RealDraw__9TPickViewFR5TRect

	PickGridInfo*	GetGridInfo(RefArg item, const Rect& bounds);		// ROM 0x00183660 GetGridInfo__9TPickViewFRC6RefVarP5TRect
	Ref			GetDisplayIcon(RefArg item);							// ROM 0x00184d08 GetDisplayIcon__9TPickViewFRC6RefVar
	long		GetDisplayIndent(RefArg item);							// ROM 0x00184d4c GetDisplayIndent__9TPickViewFRC6RefVar
	long		GetDisplayFixedHeight(RefArg item);						// ROM 0x00184dd0 GetDisplayFixedHeight__9TPickViewFRC6RefVar
	void		GetKeyCommandInfo(void);								// ROM 0x00184a24 GetKeyCommandInfo__9TPickViewFv
	Ref			GetKeyCommand(long index);								// ROM 0x00184c28 GetKeyCommand__9TPickViewFl
	long		GetKeyCommandModifierWidth(long index);					// ROM 0x00184c74 GetKeyCommandModifierWidth__9TPickViewFl
	Ref			GetDisplayItem(long index, Boolean* pickable, UniChar* mark);	// ROM 0x00187ea8 GetDisplayItem__9TPickViewFlPUcPUs
	Ref			GetItemNoText(long index);								// ROM 0x001880e8 GetItemNoText__9TPickViewFl
	Ref			GetOverflows(void);										// ROM 0x00185068 GetOverflows__9TPickViewFv
	void		Scroll(RefArg direction, Boolean unpick);				// ROM 0x0018515c Scroll__9TPickViewFRC6RefVarUc
	void		GetItemRect(PickStuff* item, Rect* r);					// ROM 0x00187384 GetItemRect__9TPickViewFP9PickStuffP5TRect
	void		GetGridItemRect(PickStuff* item, Rect* r);				// ROM 0x001872ac GetGridItemRect__9TPickViewFP9PickStuffP5TRect
	void		InvertItem(PickStuff* item);							// ROM 0x00187448 InvertItem__9TPickViewFP9PickStuff
	void		FlashItem(PickStuff* item);								// ROM 0x0018746c FlashItem__9TPickViewFP9PickStuff
	void		TrackStroke(TStrokePublic* stroke, PickStuff* item);	// ROM 0x00187918 TrackStroke__9TPickViewFP13TStrokePublicP9PickStuff
	void		SubItem(Point& pt, PickStuff* item);					// ROM 0x001874b4 SubItem__9TPickViewFR6TPointP9PickStuff
	void		Item(Point& pt, PickStuff* item);						// ROM 0x00187588 Item__9TPickViewFR6TPointP9PickStuff
	void		PickableItem(Point& pt, PickStuff* item);				// ROM 0x0018763c PickableItem__9TPickViewFR6TPointP9PickStuff
	void		PickItem(PickStuff* item);								// ROM 0x00187a4c PickItem__9TPickViewFP9PickStuff
	void		GetItemFlags(PickStuff* item, Boolean* pickable, UniChar* mark);	// ROM 0x00187e08 GetItemFlags__9TPickViewFP9PickStuffPUcPUs
	Boolean		IsItemNoPickable(long index);							// ROM 0x00187e34 IsItemNoPickable__9TPickViewFl
	Boolean		HandleKeyDown(UniChar ch, ULong parameter);				// ROM 0x00188480 HandleKeyDown__9TPickViewFUsUl
	void		KeyToNextItem(long from);								// ROM 0x0018814c KeyToNextItem__9TPickViewFl
	void		KeyToPrevItem(long from);								// ROM 0x001882f0 KeyToPrevItem__9TPickViewFl
	void		SetItemFlags(PickStuff* item, Boolean pickable, UniChar mark);	// ROM 0x00187e4c SetItemFlags__9TPickViewFP9PickStuffUcUs
	long		GetItemLength(long index);								// ROM 0x00187e70 GetItemLength__9TPickViewFl
	void		SetItemLength(PickStuff* item, long length);			// ROM 0x00187e84 SetItemLength__9TPickViewFP9PickStuffl

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
	RefStruct	fKeyCommands;		// +0xa4  per item its key command (nil: none has one)
	Fixed		fKeyCommandWidth;	// +0xa8  the widest command letter (its high half)
	RefStruct	fTypeSelect;		// +0xac  the characters typed to select an item (the ROM: a RefStruct*)
	ULong		fLastKeyTime;		// +0xb0  when the last was typed (Ticks)
	long		fTypeSelectTimeout;	// +0xb4  ticks: a pause longer starts the string again
	Boolean		fPicking;			// +0xb8  an item is being picked (Hide runs no cancel script)
};

void	RegisterPickNatives(void);												// PickViewKeyDown (the ROM's protoPicker viewKeyDownScript)
void	GetAppAreaBounds(Rect* bounds);										// ROM 0x001838c4 GetAppAreaBounds__FP5TRect - vars.displayParams' application area
Boolean	AdjustPopupInRect(Rect& bounds, long width, long height, const Rect& within, short frame);	// ROM 0x00184e54 AdjustPopupInRect__FR5TRectlT2RC5TRects - ==> placed above

#endif	/* __PICKVIEW_H */
