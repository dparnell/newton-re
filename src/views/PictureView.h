/*
	File:		views/PictureView.h

	Contains:	TPictureView (clPictureView, 76): a view showing its icon
				slot - a bitmap frame drawn into the bounds by the
				viewJustify bits (centred when the template has none) in
				the viewTransferMode.  The ROM's object is a TView (0x30
				bytes).  Hilited, a picture with a mask has its mask
				inverted; it drags as a 'picture item (a frame of its
				bounds and a copy of its icon) and draws scaled into a
				remote view.

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
	virtual void	Hilite(Boolean on);									// ROM 0x00189b18 Hilite__12TPictureViewFUc
	virtual void	DrawHilites(Boolean on);							// ROM 0x00189cb8 DrawHilites__12TPictureViewFUc
	virtual long	ClickOptions(void);									// ROM 0x00189cc8 ClickOptions__12TPictureViewFv
	virtual void	DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds);	// ROM 0x00189dc4 DrawScaledData__12TPictureViewFRC5TRectT1P5TRect
	virtual Boolean	AddDragInfo(TDragInfo* dragInfo);					// ROM 0x00189dec AddDragInfo__12TPictureViewFP9TDragInfo
	virtual Ref		GetDropData(RefArg dragType, RefArg dragRef);		// ROM 0x00189e30 GetDropData__12TPictureViewFRC6RefVarT1

	void		DrawUsingRect(const Rect& bounds);						// ROM 0x00189cd0 DrawUsingRect__12TPictureViewFRC5TRect
};

#endif	/* __PICTUREVIEW_H */
