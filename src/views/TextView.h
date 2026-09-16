/*
	File:		views/TextView.h

	Contains:	TTextView (clTextView, 98): a view that shows its text slot
				in its viewFont - the class of protoTitle, protoTextButton
				and the like.  A single line (viewJustify's oneLineOnly)
				is drawn at the view's left, aligned by the text bits (the
				horizontal ones as a QD flush across the width; vertically
				at the top - viewLineSpacing below it when the view has
				one - centred, or at the bottom), else the text is wrapped
				into the bounds (TextBox).  The ROM's object is 0x34 bytes:
				TView and the transfer mode.

	Reconstructed from the MP2100 D ROM (0x00250874-0x00250bb0); each
	function cites its origin.
*/

#ifndef __TEXTVIEW_H
#define __TEXTVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

// the text-limit bits of viewJustify (the NTK's oneLineOnly and
// noLineLimits; oneLineOnly as TTextView::RealDraw 0x0025090c tests it)
enum
{
	vjOneLineOnly				= 0x00800000,
	vjNoLineLimits				= 0x01000000
};

class TTextView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x00250874 ClassID__9TTextViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x0025087c DerivedFrom__9TTextViewCFl
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x002508b0 Constructor__9TTextViewFRC6RefVarP5TView
	virtual void	RealDraw(Rect& bounds);								// ROM 0x0025090c RealDraw__9TTextViewFR5TRect

	long		fTransferMode;		// +0x30  viewTransferMode (srcOr when none)
};

#endif	/* __TEXTVIEW_H */
