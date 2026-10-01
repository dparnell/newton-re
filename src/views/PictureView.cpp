/*
	File:		views/PictureView.cpp

	Contains:	TPictureView: a view showing a picture.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PictureView.h"
#include "Pictures.h"
#include "ObjectHeap.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Ports.h"
#include "DragDrop.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"


// ROM 0x00188d38 ClassID__12TPictureViewCFv
long
TPictureView::ClassID(void) const
{
	return clPictureView;
}


// ROM 0x00188d40 DerivedFrom__12TPictureViewCFl
Boolean
TPictureView::DerivedFrom(long id) const
{
	return id == clPictureView || TView::DerivedFrom(id);
}


// ROM 0x00189b10 RealDraw__12TPictureViewFR5TRect
void
TPictureView::RealDraw(Rect& /*bounds*/)
{
	DrawUsingRect(viewBounds);
}


// ROM 0x00189cd0 DrawUsingRect__12TPictureViewFRC5TRect
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


// ROM 0x00189b18 Hilite__12TPictureViewFUc
// A picture with a mask (an icon frame with a 'mask slot), visible, is
// hilited by drawing its mask inverted (DrawPicture's mode -2: the mask
// in srcXor) through the view's visible region; anything else is hilited
// as any view is.
void
TPictureView::Hilite(Boolean on)
{
	RefVar icon(GetValue(RSSYMicon, RefVar(NILREF)));
	if (ISNIL(icon) || !IsFrame(icon) || !FrameHasSlot(icon, RSSYMmask) || !VisibleDeep())
	{
		TView::Hilite(on);
		return;
	}
	TRegion saved(SetupVisRgn());
	TRegionVar visible(saved);
	newton_try
	{
		ULong justify = fViewJustify & vjJustifyMask;
		if (justify == 0 && ISNIL(GetCacheProto(kIndexViewJustify)))
			justify = vjCenterH | vjCenterV;
		DrawPicture(icon, viewBounds, justify, -2);
	}
	newton_catch_all
	{
		GrafPort* port;
		GetPort(&port);
		CopyRgn(visible, port->visRgn);
		rethrow;
	}
	end_try;
	GrafPort* port;
	GetPort(&port);
	CopyRgn(visible, port->visRgn);
}


// ROM 0x00189cb8 DrawHilites__12TPictureViewFUc
// Undrawing the hilites (on false) is TView's Hilite, on; drawing them
// does nothing.
void
TPictureView::DrawHilites(Boolean on)
{
	if (!on)
		TView::Hilite(true);
}


// ROM 0x00189cc8 ClickOptions__12TPictureViewFv
long
TPictureView::ClickOptions(void)
{
	return 3;
}


// ROM 0x00189dc4 DrawScaledData__12TPictureViewFRC5TRectT1P5TRect
// TView's, then the picture drawn into the rectangle it comes to.
void
TPictureView::DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds)
{
	TView::DrawScaledData(src, dst, bounds);
	DrawUsingRect(*bounds);
}


// ROM 0x00189dec AddDragInfo__12TPictureViewFP9TDragInfo
// TView's (the script's), else the view itself as a 'picture item named
// as a drawing.
Boolean
TPictureView::AddDragInfo(TDragInfo* dragInfo)
{
	if (TView::AddDragInfo(dragInfo))
		return true;
	dragInfo->AddDragItem(RefVar(RSSYMpicture), fContext, RefVar(Rdrawingname));
	return true;
}


// ROM 0x00189e30 GetDropData__12TPictureViewFRC6RefVarT1
// TView's (the script's), else a frame of the view's bounds moved to the
// origin and a deep copy of its icon.
Ref
TPictureView::GetDropData(RefArg dragType, RefArg dragRef)
{
	RefVar data(TView::GetDropData(dragType, dragRef));
	if (ISNIL(data))
	{
		data = AllocateFrame();
		Rect bounds = viewBounds;
		OffsetRect(&bounds, -viewBounds.left, -viewBounds.top);
		SetFrameSlot(data, RSSYMviewbounds, RefVar(ToObject(bounds)));
		SetFrameSlot(data, RSSYMicon, RefVar(DeepClone(RefVar(GetProto(RSSYMicon)))));
	}
	return data;
}
