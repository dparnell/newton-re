/*
	File:		views/RemoteView.cpp

	Contains:	TRemoteView: a view showing its child scaled into its
				bounds.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RemoteView.h"
#include "ViewFlags.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"


// ROM 0x001a6720 ClassID__11TRemoteViewCFv
long
TRemoteView::ClassID(void) const
{
	return clRemoteView;
}


// ROM 0x001a6728 DerivedFrom__11TRemoteViewCFl
Boolean
TRemoteView::DerivedFrom(long id) const
{
	return id == clRemoteView || TView::DerivedFrom(id);
}


// ROM 0x001a675c Constructor__11TRemoteViewFRC6RefVarP5TView
// The view made with its children; the first of them is the one shown,
// made invisible (its view flag and its context's viewFlags) so that only
// the remote view draws it.
void
TRemoteView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	fChild = fChildren->At(0);
	fChild->fFlags &= ~vVisible;
	ULong flags = RINT(fChild->GetCacheProto(10));		// (the slot cache's viewFlags)
	fChild->SetContextSlot(RSSYMviewflags, RefVar(MAKEINT(flags & ~vVisible)));
}


// ROM 0x001a678c RealDraw__11TRemoteViewFR5TRect
// The child drawn scaled into the view: the transform from its bounds onto
// the view's made once (square: the smaller scale for both), scaling
// started with the visible region mapped, the child made visible for the
// while, the port's clip mapped from the view's bounds back to the
// child's, and the child drawn whole - or, when the view's printView has a
// format that uses the full page, as much of it as the print form covers;
// the clip put back, the child hidden again and the scaling stopped, even
// when the drawing throws.
// ROM BUG, kept: the clip is mapped back to the child's coordinates for
// the scaler to map forward again, but the visible region is not, and the
// scaler (mapVis: TQDScaler::SetupScalingRegions) maps it forward all the
// same - so a remote view whose clipper's visible region is its own bounds
// (one made under the root view, as the book reader's PageThumbnail makes
// its thumbnail) draws only the part of its child that lands in the
// top-left corner, the view's bounds scaled down once more.  A 60x80
// thumbnail of a 206x214 page shows its top-left 17x23 pixels.  (Every
// step - TView::Constructor's clipper, ViewVisibleChanged, TView::Draw's
// SetupVisRgn, ViewIntoBitmap's port, StartScaling, SetupScalingRegions -
// was checked against the ROM's code.)
void
TRemoteView::RealDraw(Rect& bounds)
{
	if (fChild == nil)
		return;
	TRegionVar saved;
	GrafPort* port;
	GetPort(&port);
	RgnHandle clip = port->clipRgn;
	CopyRgn(clip, saved);
	newton_try
	{
		if ((fTransform.fFlags & kTransformSetUp) == 0)
			fTransform.Setup(&fChild->viewBounds, &viewBounds, true);
		TQDScaler::StartScaling(&fTransform, true, 1);
		fChild->SetFlags(vVisible);
		MapRgn(clip, &fTransform.fDst, &fTransform.fSrc);
		fDrawn = fChild->viewBounds;
		RefVar printView(GetVar(RSSYMprintview));
		if (NOTNIL(printView))
		{
			RefVar format(GetFrameSlotRef(printView, RSSYMtheformat));
			if (NOTNIL(format) && NOTNIL(GetProtoVariable(format, RSSYMusefullpage, nil)))
			{
				RefVar printForm(GetFrameSlotRef(printView, RSSYMprintform));
				if (NOTNIL(printForm))
					fDrawn = GetView(printForm)->viewBounds;
			}
		}
		fChild->Draw(fDrawn, false);
	}
	cleanup
	{
		GetPort(&port);
		CopyRgn(saved, port->clipRgn);
		fChild->ClearFlags(vVisible);
		TQDScaler::StopScaling();
	}
	end_try;
	GetPort(&port);
	CopyRgn(saved, port->clipRgn);
	fChild->ClearFlags(vVisible);
	TQDScaler::StopScaling();
}
