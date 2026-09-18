/*
	File:		views/PictureView.h

	Contains:	TPictureView (clPictureView, 76): a view showing its icon
				slot - a bitmap frame drawn into the bounds by the
				viewJustify bits (centred when the template has none) in
				the viewTransferMode.  The ROM's object is a TView (0x30
				bytes).  NOT YET RECONSTRUCTED: hiliting (Hilite,
				DrawHilites: the ROM inverts the picture), drag and drop of
				the picture, scaled drawing.

	Reconstructed from the MP2x00 US ROM (0x00188d38-0x00188d74,
	0x00189b10-0x00189dec); each function cites its origin.
*/

#ifndef __PICTUREVIEW_H
#define __PICTUREVIEW_H

#ifndef __VIEW_H
#include "View.h"
#endif

class TPictureView : public TView
{
public:
	virtual long	ClassID(void) const;								// ROM 0x00188d38 ClassID__12TPictureViewCFv
	virtual Boolean	DerivedFrom(long id) const;							// ROM 0x00188d40 DerivedFrom__12TPictureViewCFl
	virtual void	RealDraw(Rect& bounds);								// ROM 0x00189b10 RealDraw__12TPictureViewFR5TRect

	void		DrawUsingRect(const Rect& bounds);						// ROM 0x00189cd0 DrawUsingRect__12TPictureViewFRC5TRect
};

#endif	/* __PICTUREVIEW_H */
