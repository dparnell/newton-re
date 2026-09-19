/*
	File:		views/DataView.cpp

	Contains:	TDataView.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "DataView.h"


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
// subclass that holds text answers how well the word fits it.
//
// NOT YET RECONSTRUCTED: TParagraphView::HandleWord 0x00172760, which is
// the answer that matters - a paragraph says how well a word written at
// the point falls in its text.  Until it is there the edit view finds no
// text under any point, which is right for an empty page and wrong for
// one that has been written on.
long
TDataView::HandleWord(const UniChar* /*text*/, ULong /*length*/, const Rect& /*box*/,
					  const Point& /*pt*/, ULong /*a*/, ULong /*b*/, RefArg /*word*/,
					  Boolean /*flag*/, long* /*outOffset*/, TUnitPublic* /*unit*/)
{
	return 0;
}

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
// something.
//
// NOT YET RECONSTRUCTED: TParagraphView::HiliteText, which is the one
// that matters.
void
TDataView::HiliteText(long /*offset*/, long /*length*/, Boolean /*on*/)
{ }
