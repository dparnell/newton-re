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

	Not in the DDK; reconstructed from the MP2x00 US ROM (0x000a1a68-
	0x000abb40), each function citing its origin.  NOT YET: everything the
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
	virtual long	ClassID(void) const;					// ROM 0x000a1a68 ClassID__9TEditViewCFv
	virtual Boolean	DerivedFrom(long id) const;				// ROM 0x000a1a70 DerivedFrom__9TEditViewCFl
	virtual void	Constructor(RefArg context, TView* parent);	// ROM 0x000a41f4 Constructor__9TEditViewFRC6RefVarP5TView
	virtual void	SetupDone(void);						// ROM 0x000a648c SetupDone__9TEditViewFv

	virtual void	PostDraw(Rect& drawBounds);				// ROM 0x000a5eb8 PostDraw__9TEditViewFR5TRect
	virtual void	DrawHiliting(void);						// ROM 0x000a609c DrawHiliting__9TEditViewFv
	virtual void	DrawHilitedData(void);					// ROM 0x000a6310 DrawHilitedData__9TEditViewFv
	virtual void	HiliteAll(void);						// ROM 0x000a751c HiliteAll__9TEditViewFv
	virtual void	RemoveAllHilites(void);					// ROM 0x000a7574 RemoveAllHilites__9TEditViewFv
	virtual long	GlobalHiliteBounds(Rect* bounds);		// ROM 0x000a77cc GlobalHiliteBounds__9TEditViewFP5TRect
	virtual void	GlobalHiliteResizeBounds(Rect* bounds);	// ROM 0x000a7990 GlobalHiliteResizeBounds__9TEditViewFP5TRect
	virtual void	GlobalHilitePinnedBounds(Rect* bounds);	// ROM 0x000a7a00 GlobalHilitePinnedBounds__9TEditViewFP5TRect
	virtual Boolean	PointInHilite(Point& pt);				// ROM 0x000a7a54 PointInHilite__9TEditViewFR6TPoint
	virtual void	ActivateSelection(Boolean on);		// ROM 0x000aa860 ActivateSelection__9TEditViewFUc
	virtual void	BuildKeyChildList(TViewList* list, long a, long b);	// ROM 0x000ab9b4 BuildKeyChildList__9TEditViewFP9TViewListlT2
	virtual void	OffsetToCaret(long offset, Rect* caret);	// ROM 0x000a2ee4 OffsetToCaret__9TEditViewFlP5TRect

	// the editor's own
	void			GlobalSelectedBounds(Rect* bounds);		// ROM 0x000a788c GlobalSelectedBounds__9TEditViewFP5TRect - the hilited children's own bounds
	long			CountHilites(void);						// ROM 0x000a7abc CountHilites__9TEditViewFv
	Boolean			HasHilitedChildren(long atLeast, TView** first);	// ROM 0x000a4170 HasHilitedChildren__9TEditViewFlPP5TView
	void			DetermineKeyView(void);					// ROM 0x000a7388 DetermineKeyView__9TEditViewFv
	void			InvalAllHilites(void);					// ROM 0x000a6270 InvalAllHilites__9TEditViewFv
	void			DirtyBoxHilites(void);					// ROM 0x000a6010 DirtyBoxHilites__9TEditViewFv

	// the caret rectangle, which the editor keeps in its own coordinates
	void			SetCaretRectLocal(const Rect& r);		// ROM 0x000aa894 SetCaretRectLocal__9TEditViewFRC5TRect
	void			SetCaretRectGlobal(const Rect& r);		// ROM 0x000aa8a4 SetCaretRectGlobal__9TEditViewFRC5TRect
	Point			GetCaretLocalTopLeft(void);			// ROM 0x000aa92c GetCaretLocalTopLeft__9TEditViewFv
	Point			GetCaretGlobalTopLeft(void);		// ROM 0x000aa938 GetCaretGlobalTopLeft__9TEditViewFv

	// the children that are selected, and the way between them
	TView**		GetHilitedViewsSorted(void);		// ROM 0x000aae90 GetHilitedViewsSorted__9TEditViewFv - CountHilites of them, the caller's to delete[]
	TView*			MoveBetweenParagraphs(long v, long direction);	// ROM 0x000ab60c MoveBetweenParagraphs__9TEditViewFlT1
	void			AlignToLineSpacing(Rect* r, long top, long ascent);	// ROM 0x000a2bc4 AlignToLineSpacing__9TEditViewFP5TRectlT2

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
void	ToOutsideGrayBorder(Rect* r, const Rect* limit);		// ROM 0x000a3498 ToOutsideGrayBorder__FP5TRectPC5TRect

#endif	/* __EDITVIEW_H */
