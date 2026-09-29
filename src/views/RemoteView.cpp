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
// while, the port's visible region mapped from the view's bounds back to
// the child's (the scaler maps it forward again as it draws), and the
// child drawn whole - or, when the view's printView has a format that
// uses the full page, as much of it as the print form covers; the visible
// region put back, the child hidden again and the scaling stopped, even
// when the drawing throws.
// (The region is the port's visRgn, [port,#0x24] at 0x001a67c4 and
// 0x001a6968 - the decompiler names that word clipRgn, the ROM's GrafPort
// having its clipRgn at +0x28, as SetupScalingRegions reads it.)
void
TRemoteView::RealDraw(Rect& bounds)
{
	if (fChild == nil)
		return;
	TRegionVar saved;
	GrafPort* port;
	GetPort(&port);
	RgnHandle vis = port->visRgn;
	CopyRgn(vis, saved);
	newton_try
	{
		if ((fTransform.fFlags & kTransformSetUp) == 0)
			fTransform.Setup(&fChild->viewBounds, &viewBounds, true);
		TQDScaler::StartScaling(&fTransform, true, 1);
		fChild->SetFlags(vVisible);
		MapRgn(vis, &fTransform.fDst, &fTransform.fSrc);
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
		CopyRgn(saved, port->visRgn);
		fChild->ClearFlags(vVisible);
		TQDScaler::StopScaling();
	}
	end_try;
	GetPort(&port);
	CopyRgn(saved, port->visRgn);
	fChild->ClearFlags(vVisible);
	TQDScaler::StopScaling();
}
