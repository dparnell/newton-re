/*
	File:		views/RemoteView.h

	Contains:	TRemoteView (view class 88, and 87): a view that shows its
				one child scaled into its own bounds - a thumbnail.

				The child is kept out of sight (its vVisible cleared, in
				the context and in the view) so that it is never drawn
				where it is; the remote view draws it itself, through a
				transform from the child's bounds (or, when printing a
				full page, the print form's) onto its own, the port's clip
				mapped back the other way while it does.  The book
				reader's PageThumbnail is one, over a page made of the
				page's block views; the print preview is another.

	Reconstructed from the MP2x00 US ROM (0x001a6720-0x001a69b0); each
	function cites its origin.
*/

#ifndef __REMOTEVIEW_H
#define __REMOTEVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif
#ifndef __TRANSFORM_H
#include "Transform.h"
#endif

// 0x58 bytes on the MessagePad
class TRemoteView : public TView
{
public:
					TRemoteView() : fChild(nil)	{ fTransform.fFlags = 0; }
	virtual long	ClassID(void) const;								// ROM 0x001a6720 ClassID__11TRemoteViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x001a6728 DerivedFrom__11TRemoteViewCFl
	virtual void	Constructor(RefArg context, TView* parent);			// ROM 0x001a675c Constructor__11TRemoteViewFRC6RefVarP5TView
	virtual void	RealDraw(Rect& bounds);								// ROM 0x001a678c RealDraw__11TRemoteViewFR5TRect

	TView*			fChild;				// +0x30  the view shown
	TTransform		fTransform;			// +0x34  its bounds onto ours
	Rect			fDrawn;				// +0x50  the part of it drawn
};

#endif	/* __REMOTEVIEW_H */
