/*
	File:		views/PictureView.cpp

	Contains:	TPictureView: a view showing a picture.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "PictureView.h"
#include "Pictures.h"
#include "ObjectHeap.h"


// ROM 0x0018ad68 ClassID__12TPictureViewCFv
long
TPictureView::ClassID(void) const
{
	return clPictureView;
}


// ROM 0x0018ad70 DerivedFrom__12TPictureViewCFl
Boolean
TPictureView::DerivedFrom(long id) const
{
	return id == clPictureView || TView::DerivedFrom(id);
}


// ROM 0x0018bb40 RealDraw__12TPictureViewFR5TRect
void
TPictureView::RealDraw(Rect& /*bounds*/)
{
	DrawUsingRect(viewBounds);
}


// ROM 0x0018bd00 DrawUsingRect__12TPictureViewFRC5TRect
// The icon (GetValue: through the proto and parent chains) drawn in the
// rectangle by the viewJustify bits - centred both ways for a template
// without a viewJustify - in the viewTransferMode (srcCopy without one).
void
TPictureView::DrawUsingRect(const Rect& bounds)
{
	RefVar icon(GetValue(RSSYMicon, RefVar(NILREF)));
	if (ISNIL(icon))
		return;
	ULong justify = fViewJustify & vjJustifyMask;
	if (justify == 0 && ISNIL(GetCacheProto(kIndexViewJustify)))
		justify = vjCenterH | vjCenterV;
	RefVar mode(GetProto(RSSYMviewtransfermode));
	DrawPicture(icon, bounds, justify, ISNIL(mode) ? 0 : RINT(mode));
}
