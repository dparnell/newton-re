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
	0x000abb40), each function citing its origin.  Typing is here end to
	end - a key becomes a word (JamText), the word is offered to the
	children that hold text (HandleWord) and becomes a paragraph of its
	own when nobody takes it (AddNewParagraph) - along with the tap that
	puts the caret where the typing goes.  NOT YET: the rest of what the
	recogniser drives (HandleInk, HandleShape, HandleCaret,
	HandleLineGesture, Scrub, PlaybackInk, and the geometry
	AddNewParagraph uses for a written word), the selection (SetSelection,
	GetSelection), drag and drop, TrackScale and TrackDistort, most of the
	commands (RealDoCommand answers the keys and the tap) and the drawing
	of the resize border itself (DrawResizeBorder, TRect::Scale over
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
	virtual Boolean	RealDoCommand(RefArg cmd);				// ROM 0x000a4360 RealDoCommand__9TEditViewFRC6RefVar (partial: see the definition)
	virtual long	Idle(long reason);						// ROM 0x000a9f64 Idle__9TEditViewFl
	// the editor's own virtuals, which start after TView's at +0x11c
	// (NOT YET RECONSTRUCTED: DrawScaledViews, the one at +0x11c)
	virtual void	PositionCaret(Point& pt, Boolean click);	// ROM 0x000a9fb0 PositionCaret__9TEditViewFR6TPointUc (vtable +0x120)
	virtual void	HandleTap(Point& pt);					// ROM 0x000aaba4 HandleTap__9TEditViewFR6TPoint (vtable +0x124)
	virtual long	Scrub(class TUnitPublic* unit);				// ROM 0x000a6d38 Scrub__9TEditViewFP11TUnitPublic (vtable +0x128)
	virtual long	HandleCaret(class TUnitPublic* unit);	// ROM 0x000ab334 HandleCaret__9TEditViewFP11TUnitPublic
	virtual long	HandleLineGesture(class TUnitPublic* unit);	// ROM 0x000ab528 HandleLineGesture__9TEditViewFP11TUnitPublic

	// the editor's own
	void			GlobalSelectedBounds(Rect* bounds);		// ROM 0x000a788c GlobalSelectedBounds__9TEditViewFP5TRect - the hilited children's own bounds
	long			CountHilites(void);						// ROM 0x000a7abc CountHilites__9TEditViewFv
	Boolean			HasHilitedChildren(long atLeast, TView** first);	// ROM 0x000a4170 HasHilitedChildren__9TEditViewFlPP5TView
	void			DetermineKeyView(void);					// ROM 0x000a7388 DetermineKeyView__9TEditViewFv
	Boolean			AddHiliter(TUnitPublic* unit);			// ROM 0x000a6fb0 AddHiliter__9TEditViewFP11TUnitPublic - a hilite stroke offered to the children
	TView*			TextContainingPoint(Point& pt, Rect* box, long* score);	// ROM 0x000a8844 TextContainingPoint__9TEditViewFR6TPointP5TRectPl
	// Ink handed to the page: a unit's stroke, a bundle of strokes from
	// the recogniser, or the strokes themselves.  All three end in the
	// free HandleInk below, which makes an ink shape of them and adds it
	// as a child.  HandleInkWord is the other way a word can end up on
	// the page - as an ink word in a paragraph of its own.
	long			HandleInk(TUnitPublic* unit);			// ROM 0x000a6798 HandleInk__9TEditViewFP11TUnitPublic
	long			HandleInk(RefArg bundle);				// ROM 0x000a6854 HandleInk__9TEditViewFRC6RefVar
	void			HandleInkWord(RefArg cmd);				// ROM 0x000a6888 HandleInkWord__9TEditViewFRC6RefVar
	TView*			AddForm(RefArg form);					// ROM 0x000ab28c AddForm__9TEditViewFRC6RefVar - the context frame made a child, undoably
	void			JamText(UniChar* text, ULong length);	// ROM 0x000ab70c JamText__9TEditViewFPUsUl - typed text put on the page as a word
	// the editor's own, which is not TDataView's: the box the word
	// filled and the box it may grow into, the unit it came from, the
	// style it is in, and where in the text it ended up.  ==> the view
	// the word went into.
	TView*			HandleWord(UniChar* text, ULong length, Rect& box, Rect& room,
							   class TUnitPublic* unit, RefArg info, long* outOffset);	// ROM 0x000abaa4 HandleWord__9TEditViewFPUsUlR5TRectT3P11TUnitPublicRC6RefVarPl
	// A word that no child would take, made into a paragraph of its own.
	TView*			AddNewParagraph(UniChar* text, ULong length, Rect& box, Rect& room,
									class TUnitPublic* unit, RefArg info, long* outOffset,
									RefArg inkFont);	// ROM 0x000a1b2c AddNewParagraph__9TEditViewFPUsUlR5TRectT3P11TUnitPublicRC6RefVarPlT6
	void			InvalAllHilites(void);					// ROM 0x000a6270 InvalAllHilites__9TEditViewFv
	Boolean			ScrubHilite(const Rect& bounds);			// ROM 0x000a75f4 ScrubHilite__9TEditViewFRC5TRect - a scrub over the selection deletes it
	void			DeleteHilitedViews(void);				// ROM 0x000a8750 DeleteHilitedViews__9TEditViewFv
	void			ResetHilitesForNewWord(void);			// ROM 0x000a4204 ResetHilitesForNewWord__9TEditViewFv
	Boolean			ValidateCaret(Boolean scrolled);			// ROM 0x000aa9b0 ValidateCaret__9TEditViewFUc - ==> whether the caret is still this view's
	void			DirtyBoxHilites(void);					// ROM 0x000a6010 DirtyBoxHilites__9TEditViewFv

	// the caret rectangle, which the editor keeps in its own coordinates
	void			SetCaretRectLocal(const Rect& r);		// ROM 0x000aa894 SetCaretRectLocal__9TEditViewFRC5TRect
	void			SetCaretRectGlobal(const Rect& r);		// ROM 0x000aa8a4 SetCaretRectGlobal__9TEditViewFRC5TRect
	Point			GetCaretLocalTopLeft(void);			// ROM 0x000aa92c GetCaretLocalTopLeft__9TEditViewFv
	Point			GetCaretGlobalTopLeft(void);		// ROM 0x000aa938 GetCaretGlobalTopLeft__9TEditViewFv

	// the children that are selected, and the way between them
	TView**		GetHilitedViewsSorted(void);		// ROM 0x000aae90 GetHilitedViewsSorted__9TEditViewFv - CountHilites of them, the caller's to delete[]
	TView*			MoveBetweenParagraphs(long v, long direction);	// ROM 0x000ab60c MoveBetweenParagraphs__9TEditViewFlT1
	// Where a new paragraph should really go: `want` is where it was put,
	// `measured` is the box its text needs, and `result` comes back as
	// `measured` moved to line up with whatever of the editor's children it
	// is nearly aligned with already.  ==> 0.
	long			AlignBounds(Rect& want, Rect& measured, Rect* result);	// ROM 0x000a26c4 AlignBounds__9TEditViewFR5TRectT1P5TRect
	void			AlignToLineSpacing(Rect* r, long top, long ascent);	// ROM 0x000a2bc4 AlignToLineSpacing__9TEditViewFP5TRectlT2

	short			fLineSpacing;		// +0x30  viewLineSpacing, read by SetupDone
	// +0x32 not yet known
	long			fClickOptions;		// +0x34  the mask GlobalHiliteBounds answers through (~2: not resizable)
	Rect			fCaretRect;			// +0x38  top == -32768 while there is none
	Boolean			fTapPending;		// +0x40  a tap is waiting to become a caret (cleared by SetupDone)
	// +0x41..0x43 not yet known
	Point			fTapPoint;			// +0x44  where that tap was
	// +0x46..0x47 not yet known
	long			fTextFlags;			// +0x48  GetInputViewTextFlags of the view's own
	Boolean			fHilitingChildren;	// +0x4c  AddHiliter is carrying a hilite out on the children (the ROM sets it and nothing in its TEditView reads it)
};


// A rectangle grown to the outside of the gray resize border (twelve pixels),
// kept within limit when there is one.
void	ToOutsideGrayBorder(Rect* r, const Rect* limit);		// ROM 0x000a3498 ToOutsideGrayBorder__FP5TRectPC5TRect

// Whether a caret gesture is one a text editor takes: the caret kinds 2
// and 3 (the plain caret and the one with a tail) pointing up, right or
// down, 5 pointing up, and 6 at 135 degrees.
Boolean	ValidTextEditCaret(class TUnitPublic* unit);			// ROM 0x000ab490 ValidTextEditCaret__FP11TUnitPublic
// Whether a line gesture is one a text editor takes: up, down, right or
// left.
Boolean	ValidLineGesture(class TUnitPublic* unit);			// ROM 0x000ab6dc ValidLineGesture__FP11TUnitPublic

// The value moved to the nearest multiple of the grid (an edit view's
// line spacing), rounding to the nearer; a grid of nothing leaves it as
// it was.
// How far apart two ranges are, which is what tells the editor whether
// two things are on the same line or in the same column: 0 when one
// range holds the other, 1 when they merely overlap, and the gap between
// them when they do not.
long	RangeDistance(long aLow, long aHigh, long bLow, long bHigh);	// ROM 0x000a2670 RangeDistance__FlN31

long	AlignToGrid(long v, long grid);						// ROM 0x002628c8 AlignToGrid__FlT1

// Whether the view takes ink words (bit 0) or text (bit 1) from the
// recogniser.
long	TextOrInkWordsEnabled(TView* view);					// ROM 0x001a2aa4 TextOrInkWordsEnabled__FP5TView

// Whether the corrector - the list of alternative readings a written
// word can be put right from - is on the screen.
Boolean	CorrectorUp(void);									// ROM 0x001767b8 CorrectorUp__Fv
// Remote writing turned off while the corrector is up, and put back
// afterwards.  ==> 1 the corrector was up, 2 remote writing was on.
ULong	SetRemoteForCorrector(void);						// ROM 0x00177470 SetRemoteForCorrector__Fv
void	RestoreRemoteForCorrector(ULong state);				// ROM 0x001774e0 RestoreRemoteForCorrector__Fl

// The view whose text was last changed remembered in the globals, which
// is what `lastTextChanged` answers a script.
void	TimeStampTextChange(TView* view);					// ROM 0x000a39f4 TimeStampTextChange__FP5TView

extern Boolean	gAboutToOpenSoftKeyboard;					// ROM 0x0c100cf0 gAboutToOpenSoftKeyboard
extern Boolean	gLassoedDrag;								// ROM 0x0c100ce0 gLassoedDrag
// Whether a word put on a page is offered to the dictionary as one it
// might learn.  HandleWord sets it on the way in; the recogniser clears
// it for words it is sure it already knows.
extern Boolean	gAddWordInfo;								// ROM 0x0c101710 gAddWordInfo

class TStroke;

// Whether the view expects numbers rather than words, which is what says
// how tall the x-height of an ink word written into it should be.  Its
// text flags say so outright, or its recognition flags leave numbers,
// the time or a phone number as the only thing it takes, or it names one
// custom dictionary and that dictionary is the numbers one.
Boolean	ViewExpectsNumbers(TView* view);					// ROM 0x0017fb04 ViewExpectsNumbers__FP5TView

// Strokes, or a bundle of them, made into an ink shape and added to the
// page as a child of its own.
UniChar*	MakeNullTerminatedString(UniChar* text, ULong length);	// ROM 0x000a3e70 MakeNullTerminatedString__FPUsUl (the caller frees it)
void	HandleInk(TEditView* view, TStroke** strokes);		// ROM 0x00140834 HandleInk__FP9TEditViewPP7TStroke
long	HandleInk(TEditView* view, RefArg bundle);			// ROM 0x00140754 HandleInk__FP9TEditViewRC6RefVar

#endif	/* __EDITVIEW_H */
