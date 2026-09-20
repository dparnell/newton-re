/*
	File:		views/DataView.h

	Contains:	TDataView (clDataView, 83): the base of the views holding
				editable data - paragraphs, polygons, the edit view's
				children; the hilite, caret, word and ink handling all
				views of data share.  NOT YET RECONSTRUCTED: everything but
				the class identity (the hilites and the recogniser's
				gestures are not yet).

	Reconstructed from the MP2x00 US ROM (0x000a2fc0-0x000a3494); each
	function cites its origin.
*/

#ifndef __DATAVIEW_H
#define __DATAVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

class TUnitPublic;

class TDataView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x000a2fc0 ClassID__9TDataViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x000a2fc8 DerivedFrom__9TDataViewCFl
	// declared in the vtable's order, which starts at +0x120.  The slots
	// from +0x128 to +0x138 are NOT YET RECONSTRUCTED, so nothing stands
	// between the two gestures here and GetHiliteView.
	// A caret gesture over the view: its kind (TUnitPublic::CaretType), the
	// angle it points at, and the polyline's corners - the first arm, the
	// caret's own point, the second arm, and a fourth for the kinds that
	// have one.  ==> whether it took it.
	virtual long	HandleCaret(ULong kind, long angle, Point& armA, Point& point,
								Point& armB, Point& tail);			// ROM 0x000a3460 HandleCaret__9TDataViewFUllR6TPointN33 (vtable +0x120: 0)
	// A line gesture: its angle and its two ends.
	virtual long	HandleLineGesture(long angle, Point& from, Point& to);	// ROM 0x000a3468 HandleLineGesture__9TDataViewFlR6TPointT2 (vtable +0x124: 0)
	// declared in the vtable's order, which starts at +0x13c
	virtual TView*	GetHiliteView(void);								// ROM 0x000a31bc GetHiliteView__9TDataViewFv (vtable +0x13c)
	virtual TView*	GetEnclosingEditView(void);						// ROM 0x000a3038 GetEnclosingEditView__9TDataViewFv (vtable +0x140)
	virtual TView*	GetTextView(void);								// ROM 0x000a31c0 GetTextView__9TDataViewFv (vtable +0x144)
	// How well the view would take the word written at the point - what
	// the recogniser asks before handing a word over, and what the edit
	// view asks with a single letter to find the text under a point
	// (TEditView::TextContainingPoint).  ==> 0 for not at all.
	virtual long	HandleWord(const UniChar* text, ULong length, const Rect& box,
							   const Point& pt, ULong a, ULong b, RefArg word,
							   Boolean flag, long* outOffset, TUnitPublic* unit);	// ROM 0x000a3458 HandleWord__9TDataViewFPCUsUlRC5TRectRC6TPointN22RC6RefVarUcPlP11TUnitPublic (vtable +0x148)
	virtual void	HiliteText(long offset, long length, Boolean on);	// ROM 0x000a31c4 HiliteText__9TDataViewFlT1Uc (vtable +0x14c)
};

#endif	/* __DATAVIEW_H */
