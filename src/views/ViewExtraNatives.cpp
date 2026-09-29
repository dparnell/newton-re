/*
	File:		views/ViewExtraNatives.cpp

	Contains:	The view natives that are not in ViewNatives.cpp: the
				debugging ones a developer types into the Inspector (DV,
				ViewAutopsy), KeyboardInput, the vertical layouts
				(FormatVertical, ReFlow/ReflowPreflight), GrayShrink, the
				splash graphic, and the overview's SyncScroll.
				RegisterViewExtraNatives binds them.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "View.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "REPTranslators.h"
#include "NewtonTime.h"
#include "UserTasks.h"		// Wait
#include "Pictures.h"		// TPixelObj
#include "Ports.h"
#include "Draw.h"			// CopyBits
#include "Rects.h"
#include "Screen.h"			// StartDrawing, StopDrawing
#include "Locale.h"			// GetPreference, SetPreference
#include "SplashScreen.h"	// DrawSplashGraphic


// ROM 0x001ea130 FDV
// DV(view): a view's description printed to the Inspector (TView::Dump)
// and the view flashed - hilited and unhilited eight times - so that it
// can be found on the screen.  The view may be named as GetView takes it.
static Ref
FDV(RefArg rcvr, RefArg name)
{
	TView* view = GetView(rcvr, name);
	if (view == nil)
		gREPout->Print("No View found(hidden?)\r");
	else
	{
		view->Dump(0);
		for (long i = 0; i < 8; i++)
		{
			view->Hilite(true);
			Wait(5);
			view->Hilite(false);
			Wait(5);
		}
	}
	return NILREF;
}


// ROM 0x0025e454 FViewAutopsy
// ViewAutopsy(arg): an integer sets gSlowMotion (drawing shown step by
// step); anything else turns gOutlineViews - every view framed in light
// gray - on or off and redraws the screen.
static Ref
FViewAutopsy(RefArg /*rcvr*/, RefArg arg)
{
	if (ISINT(arg))
		gSlowMotion = RVALUE(arg);
	else
	{
		gOutlineViews = !gOutlineViews;
		gRootView->Dirty(nil);
	}
	return NILREF;
}


// ROM 0x001ec628 FKeyboardInputX
// view:KeyboardInput() - whether the view is taking keys now: it is the
// key view, the caret is showing, and a keyboard is active.
static Ref
FKeyboardInputX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view != nil && gRootView->CaretEnabled() && gRootView->fCaretView == view
	 && gRootView->KeyboardActive())
		return TRUEREF;
	return NILREF;
}


// ROM 0x001f0aa4 FormatVertical__FRC6RefVarN21
// view:FormatVertical(rect, spread): the view's children stacked from the
// top of the rect (SetChildrenVertical).  With spread they are spaced
// apart by the rect's height less the children's total height divided by
// ChildrenHeight's count (which is one more than the children).
//
// ROM BUG kept: an even spread would be the height less the children's
// total, over their number; the ROM divides the total by the count and
// takes that from the height, so the gap is nearly the whole rect.
static Ref
FormatVertical(RefArg rcvr, RefArg bounds, RefArg spread)
{
	TView* view = FailGetView(rcvr);
	Rect rect;
	if (!FromObject(bounds, rect))
		ThrowMsg("not a rectangle");
	long spacing = 0;
	if (NOTNIL(spread))
	{
		long count;
		long total = view->ChildrenHeight(&count);
		if (count == 0)
			spacing = 0;
		else
			spacing = (short) (rect.bottom - rect.top) - total / count;
	}
	return MAKEINT(view->SetChildrenVertical(spacing + rect.top, spacing));
}


// ROM 0x0003ec94 FGrayShrink
// view:GrayShrink(bitmap, style): a bitmap drawn shrunk into the view -
// into its bounds, or into the second rectangle of the style's
// `transform` - with the style's `grayLevels`, if it has an array of them,
// put in the user's preferences for the time the bits are copied (the
// shrinking turns pixels into grays through them).  A one-bit bitmap is
// flagged 0x1000000 first.
//
// ROM BUGS kept: the destination is offset by the view's top-left even
// when it is the view's bounds, which are already global, so with no
// transform the bitmap lands the view's own offset down and to the right;
// and the preference is put back whenever the style has a grayLevels at
// all, so a grayLevels that is not an array sets the preference to nil.
static Ref
FGrayShrink(RefArg rcvr, RefArg bitmap, RefArg style)
{
	TView* view = FailGetView(rcvr);
	RefVar transform(GetProtoVariable(style, RSSYMtransform, nil));
	Rect rect;
	if (ISNIL(transform))
		rect = view->viewBounds;
	else
	{
		RefVar to(GetArraySlotRef(transform, 0));
		if (ISINT(to))
			Throw((ExceptionName) "evt.ex.graf", (void*) -8810, nil);
		to = GetArraySlotRef(transform, 1);
		if (!FromObject(to, rect))
			Throw((ExceptionName) "evt.ex.graf", (void*) -8810, nil);
	}
	OffsetRect(&rect, view->viewBounds.left, view->viewBounds.top);
	TPixelObj pixels;
	newton_try
	{
		pixels.Init(bitmap);
		PixelMap* map = pixels.Pixels();
		if ((map->pixMapFlags & 0xff) == 1)
			map->pixMapFlags |= 0x1000000;
		GrafPort* port;
		GetPort(&port);
		RefVar saved;
		RefVar levels(GetProtoVariable(style, RSSYMgraylevels, nil));
		if (IsArray(levels))
		{
			saved = GetPreference(RSSYMgraylevels);
			SetPreference(RSSYMgraylevels, levels);
		}
		StartDrawing(nil, nil);
		CopyBits(map, &port->portBits, &map->bounds, &rect, 0, nil);
		StopDrawing(nil, &rect);
		if (NOTNIL(levels))
			SetPreference(RSSYMgraylevels, saved);
	}
	cleanup
	{
		pixels.~TPixelObj();
	}
	end_try;
	return NILREF;
}


// ROM 0x0014708c FDisplaySplashGraphic
// view:DrawGraphic(box): the maker's splash picture drawn centred in the
// box (DrawSplashGraphic).  ==> true when there was one - the script
// that asks draws its own default picture when there is not, which on
// the MP2x00, with no TMainSplashScreenInfo registered, is always.
static Ref
FDisplaySplashGraphic(RefArg /*rcvr*/, RefArg bounds)
{
	Rect box;
	FromObject(bounds, box);
	UChar drawn;
	TSplashScreenInfo* info = DrawSplashGraphic(&drawn, box);
	if (info != nil)
		info->Delete();
	return drawn ? TRUEREF : NILREF;
}


// ROM 0x001eaf74 FSyncScrollX
// roll:SyncScroll(items, index, direction) - a roll's items scrolled a
// step (TView::SyncScroll); items that are a soup cursor (a frame) go
// through TView::SyncScrollSoup instead, which takes no index.
static Ref
FSyncScrollX(RefArg rcvr, RefArg items, RefArg index, RefArg direction)
{
	TView* view = FailGetView(rcvr);
	if ((ObjectFlags(items) & kObjFrame) != 0)
		return view->SyncScrollSoup(items, direction);
	return view->SyncScroll(items, index, direction);
}


void	RegisterKeyHelpSlipNatives(void);		// KeyHelpSlip.cpp


void
RegisterViewExtraNatives(void)
{
	RegisterKeyHelpSlipNatives();
	RegisterNativeFunction("FSyncScrollX", (void*) FSyncScrollX, 3);
	RegisterNativeFunction("FDisplaySplashGraphic", (void*) FDisplaySplashGraphic, 1);
	RegisterNativeFunction("FGrayShrink", (void*) FGrayShrink, 2);
	RegisterNativeFunction("FDV", (void*) FDV, 1);
	RegisterNativeFunction("FViewAutopsy", (void*) FViewAutopsy, 1);
	RegisterNativeFunction("FKeyboardInputX", (void*) FKeyboardInputX, 0);
	RegisterNativeFunction("FormatVertical__FRC6RefVarN21", (void*) FormatVertical, 2);
}
