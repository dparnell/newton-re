/*
	File:		views/DataView.cpp

	Contains:	TDataView.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DataView.h"
#include "Hilites.h"
#include "RegionVars.h"
#include "Regions.h"
#include "Ports.h"


// ROM 0x000a2fc0 ClassID__9TDataViewCFv
long
TDataView::ClassID(void) const
{
	return clDataView;
}


// ROM 0x000a2fc8 DerivedFrom__9TDataViewCFl
Boolean
TDataView::DerivedFrom(long id) const
{
	return id == clDataView || TView::DerivedFrom(id);
}


// ROM 0x000a3460 HandleCaret__9TDataViewFUllR6TPointN33
// The base takes no caret gesture.
long
TDataView::HandleCaret(ULong /*kind*/, long /*angle*/, Point& /*armA*/, Point& /*point*/,
					   Point& /*armB*/, Point& /*tail*/)
{
	return 0;
}


// ROM 0x000a3468 HandleLineGesture__9TDataViewFlR6TPointT2
long
TDataView::HandleLineGesture(long /*angle*/, Point& /*from*/, Point& /*to*/)
{
	return 0;
}


// ROM 0x000a31d8 DrawHilitedData__9TDataViewFv
// What is selected, drawn: the view drawn again clipped to each hilite's
// area in turn (a paragraph's selected characters); a view with nothing
// selected is drawn whole, with its frame.
void
TDataView::DrawHilitedData(void)
{
	if (!Hilited())
	{
		Rect bounds;
		OuterBounds(&bounds);
		Draw(bounds, false);
		return;
	}
	TRegionVar savedClip;
	GetClip(savedClip);
	Rect bounds = viewBounds;
	TRegionVar area;
	HiliteLoop loop(this);
	while (loop.Next())
	{
		loop.fCurrent->Area(area);
		OffsetRgn(area, viewBounds.left, viewBounds.top);
		SetClip(area);
		RealDraw(bounds);
	}
	SetClip(savedClip);
}


// ROM 0x000a3478 HandleInk__9TDataViewFRC6RefVarUc
// A plain data view takes no ink.
long
TDataView::HandleInk(RefArg cmd, Boolean reallyDoIt)
{
	return 0;
}


// ROM 0x000a3470 HandleInkWord__9TDataViewFRC6RefVarUc
// ... nor an ink word.
long
TDataView::HandleInkWord(RefArg cmd, Boolean reallyDoIt)
{
	return 0;
}


// ROM 0x000a31bc GetHiliteView__9TDataViewFv
// The view a selection in this one belongs to: itself, unless a
// subclass says otherwise (a container answers for its children).
//
// The ROM's body is a bare `mov pc,lr`, which is what `return this`
// compiles to on the ARM - r0 already holds it on entry - and not an
// empty method.  The reconstruction had it answering nil, which is a
// different thing entirely: nobody owned the selection.
TView*
TDataView::GetHiliteView(void)
{
	return this;
}


// ROM 0x000a31c0 GetTextView__9TDataViewFv
// The view holding this one's text: itself, for a paragraph.  A bare
// `mov pc,lr` again - `return this`.
TView*
TDataView::GetTextView(void)
{
	return this;
}


// ROM 0x000a3458 HandleWord__9TDataViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic
// How well the view would take the word: the base takes nothing, and a
// subclass that holds text answers how well the word fits it
// (TParagraphView::HandleWord 0x00172760 is the answer that matters).
long
TDataView::HandleWord(const UniChar* /*text*/, ULong /*length*/, const Rect& /*box*/,
					  const Point& /*pt*/, ULong /*a*/, ULong /*b*/, RefArg /*word*/,
					  Boolean /*flag*/, long* /*outOffset*/, TUnitPublic* /*unit*/)
{
	return 0;
}

// ROM 0x000a3480 SaveAddedUnitBounds__9TDataViewFRC5TRectRC6TPointUl
// A plain data view keeps nothing about the word that went into it;
// TParagraphView is what records it.
void
TDataView::SaveAddedUnitBounds(const Rect& /*box*/, const Point& /*base*/,
							   ULong /*inkEndTime*/)
{
}


// ROM 0x000a3034 HandleTap__9TDataViewFR6TPoint
void
TDataView::HandleTap(Point& /*pt*/)
{ }


// ROM 0x000a3038 GetEnclosingEditView__9TDataViewFv
// The editor this view is written on: its parent when that is one, and
// the parent's parent when the parent is a container gathering it with
// others.  ==> nil when it is on neither.
TView*
TDataView::GetEnclosingEditView(void)
{
	TView* parent = fParent;
	if (parent->DerivedFrom(clEditView))
		return parent;
	if (parent->DerivedFrom(clContainerView))
		return parent->fParent;
	return nil;
}


// ROM 0x000a31c4 HiliteText__9TDataViewFlT1Uc
// The run of characters hilited, or the hilite taken off it.  The base
// has no text, so it does nothing; a paragraph is where it means
// something (TParagraphView::HiliteText).
void
TDataView::HiliteText(long /*offset*/, long /*length*/, Boolean /*on*/)
{ }


// ROM 0x000a31b8 AddHilited__9TDataViewFRC6RefVarP9TEditView
TView*
TDataView::AddHilited(RefArg /*hilite*/, TEditView* /*editor*/)
{
	return this;
}


// ROM 0x000a3494 CleanupData__9TDataViewFv
void
TDataView::CleanupData(void)
{ }


// ROM 0x000a3084 DiceHilited__9TDataViewFRC6RefVarP9TEditViewR6TPointUc
TView*
TDataView::DiceHilited(RefArg hilite, TEditView* editor, Point& offset, Boolean keep)
{
	Point origin = fParent->ContentsOrigin();
	Point at;
	at.h = (short) (offset.h + origin.h);
	at.v = (short) (offset.v + origin.v);
	Point editorOrigin = ((TView*) editor)->ContentsOrigin();
	Point by;
	by.h = (short) (at.h - editorOrigin.h);
	by.v = (short) (at.v - editorOrigin.v);
	TView* view = AddHilited(hilite, editor);
	if (view != nil)
	{
		view->DoMoveCommand(by);
		if (!keep)
			DeleteHilited(hilite);
		else
			RemoveHilite(hilite);
	}
	return view;
}
