/*
	File:		views/EditView.h

	Contains:	TEditView, the editor a page of a notebook application is
				written on - what turns strokes into paragraphs and shapes,
				and what holds them while they are moved, scaled and
				scrubbed.  Class 77, and derived straight from TView rather
				than from TContainerView, although the two answer the same
				questions about a selection.

				What it is, as far as this file goes, is an editor *over its
				children*: it keeps no hilite of its own, and every question
				about the selection is put to the children that are hilited
				themselves.  Its own answer is the click options that go with
				the whole selection - the children's, AND-ed together (bit 2
				OR-ed), masked by fClickOptions - and bit 1 of that says the
				selection may be resized, which is what puts a resize border
				round it.

	Not in the DDK; reconstructed from the MP2100 D ROM (0x000a2c68-
	0x000acd40), each function citing its origin.  NOT YET: everything the
	recogniser drives (HandleWord, HandleInk, HandleShape, HandleCaret,
	HandleLineGesture, Scrub, JamText, AddNewParagraph, PlaybackInk), the
	caret and selection (PositionCaret, SetSelection, GetSelection,
	ValidateCaret, the caret rectangle), drag and drop, TrackScale and
	TrackDistort, the commands (RealDoCommand, GetValue, SetValue) and the
	drawing of the resize border itself (DrawResizeBorder, TRect::Scale over
	gEditViewTransform).
*/

#ifndef __EDITVIEW_H
#define __EDITVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif


class TEditView : public TView
{
public:
	virtual long	ClassID(void) const;					// ROM 0x000a2c68 ClassID__9TEditViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x000a2c70 DerivedFrom__9TEditViewCFl
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x000a53f4 Constructor__9TEditViewFRC6RefVarP5TView
	virtual void	SetupDone(void);						// ROM 0x000a768c SetupDone__9TEditViewFv

	virtual void	PostDraw(Rect& drawBounds);				// ROM 0x000a70b8 PostDraw__9TEditViewFR5TRect
	virtual void	DrawHiliting(void);						// ROM 0x000a729c DrawHiliting__9TEditViewFv
	virtual void	DrawHilitedData(void);					// ROM 0x000a7510 DrawHilitedData__9TEditViewFv
	virtual void	HiliteAll(void);						// ROM 0x000a871c HiliteAll__9TEditViewFv
	virtual void	RemoveAllHilites(void);					// ROM 0x000a8774 RemoveAllHilites__9TEditViewFv
	virtual long	GlobalHiliteBounds(Rect* bounds);		// ROM 0x000a89cc GlobalHiliteBounds__9TEditViewFP5TRect
	virtual void	GlobalHiliteResizeBounds(Rect* bounds);	// ROM 0x000a8b90 GlobalHiliteResizeBounds__9TEditViewFP5TRect
	virtual void	GlobalHilitePinnedBounds(Rect* bounds);	// ROM 0x000a8c00 GlobalHilitePinnedBounds__9TEditViewFP5TRect
	virtual Boolean	PointInHilite(Point& pt);				// ROM 0x000a8c54 PointInHilite__9TEditViewFR6TPoint

	// the editor's own
	void			GlobalSelectedBounds(Rect* bounds);		// ROM 0x000a8a8c GlobalSelectedBounds__9TEditViewFP5TRect - the hilited children's own bounds
	long			CountHilites(void);						// ROM 0x000a8cbc CountHilites__9TEditViewFv
	Boolean			HasHilitedChildren(long atLeast, TView** first);	// ROM 0x000a5370 HasHilitedChildren__9TEditViewFlPP5TView
	void			DetermineKeyView(void);					// ROM 0x000a8588 DetermineKeyView__9TEditViewFv
	void			InvalAllHilites(void);					// ROM 0x000a7470 InvalAllHilites__9TEditViewFv
	void			DirtyBoxHilites(void);					// ROM 0x000a7210 DirtyBoxHilites__9TEditViewFv

	short			fLineSpacing;		// +0x30  viewLineSpacing, read by SetupDone
	// +0x32 not yet known
	long			fClickOptions;		// +0x34  the mask GlobalHiliteBounds answers through (~2: not resizable)
	Rect			fCaretRect;			// +0x38  top == -32768 while there is none
	Boolean			fUnknown40;			// +0x40  0 from SetupDone
	// +0x41..0x47 not yet known
	long			fTextFlags;			// +0x48  GetInputViewTextFlags of the view's own
	Boolean			fUnknown4C;			// +0x4c  0 from the constructor
};


// A rectangle grown to the outside of the gray resize border (twelve pixels),
// kept within limit when there is one.
void	ToOutsideGrayBorder(Rect* r, const Rect* limit);		// ROM 0x000a4698 ToOutsideGrayBorder__FP5TRectPC5TRect

#endif	/* __EDITVIEW_H */
