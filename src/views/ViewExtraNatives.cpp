/*
	File:		views/ViewExtraNatives.cpp

	Contains:	The view natives that are not in ViewNatives.cpp: the
				debugging ones a developer types into the Inspector (DV,
				ViewAutopsy), KeyboardInput and ConnectPassthruKeyboard, the vertical layouts
				(FormatVertical; ReFlow and ReflowPreflight are Reflow.cpp's), GrayShrink, the
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
#include "Text.h"			// DrawRichString
#include "Fonts.h"			// CreateTextStyleRecord
#include "RichString.h"
#include "Interpreter.h"	// DoMessage
#include <string.h>
#include "host/RomBugs.h"


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


// ROM 0x001ec680 FConnectPassthruKeyboard
// ConnectPassthruKeyboard(connected): a keyboard connected (non-nil) or
// gone (nil) through a soft keyboard - the root's passthru flag, which
// KeyboardConnected and KeyboardActive answer with.
static Ref
FConnectPassthruKeyboard(RefArg /*rcvr*/, RefArg connected)
{
	gRootView->ConnectPassthruKeyboard(NOTNIL(connected));
	return NILREF;
}


// ROM 0x001f0aa4 FormatVertical__FRC6RefVarN21
// view:FormatVertical(rect, spread): the view's children stacked from the
// top of the rect (SetChildrenVertical).  With spread they are spaced
// apart by the rect's height less the children's total height divided by
// ChildrenHeight's count (which is one more than the children).
//
// ROM BUG (fixed): an even spread would be the height less the children's
// total, over their number; the ROM divides the total by the count and
// takes that from the height, so the gap is nearly the whole rect.  The
// fix spreads them evenly: the height less the total, over the count.
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
			spacing = RomBugFixed() ? ((short) (rect.bottom - rect.top) - total) / count
									: (short) (rect.bottom - rect.top) - total / count;
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
// ROM BUGS (fixed): the destination is offset by the view's top-left even
// when it is the view's bounds, which are already global, so with no
// transform the bitmap lands the view's own offset down and to the right;
// and the preference is put back whenever the style has a grayLevels at
// all, so a grayLevels that is not an array sets the preference to nil.
// The fix offsets only a transform's rectangle, and puts the preference
// back only when it was changed.
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
	if (!RomBugFixed() || NOTNIL(transform))
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
		if (RomBugFixed() ? IsArray(levels) : NOTNIL(levels))
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
void	RegisterReflowNatives(void);			// Reflow.cpp


Ref		FStrFilled(RefArg rcvr, RefArg str);			// frames/StringNatives.cpp


// ROM 0x001ec6b4 FDrawExpando
// An expando's viewDrawScript: its lines drawn, each a label and a text
// side by side.  Line after line from lineIndent below the view's top: the
// label that line's setup1 message answers (given the view's target) in
// labelStyle at the view's left, and the text its setup2 answers - the
// view's `empty` when that is empty - in textStyle indent pixels to the
// right; then lineHeight down.  The line numbered `split` is not drawn:
// it leaves insertHeight instead (where the expanded part goes).  The text
// is drawn in transfer mode 1 (srcOr), with no width to fit.
static Ref
FDrawExpando(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	long top = view->viewBounds.top;
	long left = view->viewBounds.left;
	Long y = RINT(GetProtoVariable(rcvr, RSSYMlineindent, nil)) + top;
	Long numLines = RINT(GetProtoVariable(rcvr, RSSYMnumlines, nil));
	Long split = RINT(GetProtoVariable(rcvr, RSSYMsplit, nil));
	RefVar lines(GetProtoVariable(rcvr, RSSYMlines, nil));
	Long indent = RINT(GetProtoVariable(rcvr, RSSYMindent, nil));
	Long lineHeight = RINT(GetProtoVariable(rcvr, RSSYMlineheight, nil));
	RefVar empty(GetProtoVariable(rcvr, RSSYMempty, nil));
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlot(args, 0, RefVar(GetProtoVariable(rcvr, RSSYMtarget, nil)));
	RefVar line, text;
	StyleRecord labelStyle, textStyle;
	CreateTextStyleRecord(RefVar(GetProtoVariable(rcvr, RSSYMlabelstyle, nil)), &labelStyle);
	CreateTextStyleRecord(RefVar(GetProtoVariable(rcvr, RSSYMtextstyle, nil)), &textStyle);
	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fTransferMode = 1;
	for (long i = 0; i < numLines; i++)
	{
		long advance;
		if (i == split)
			advance = RINT(GetProtoVariable(rcvr, RSSYMinsertheight, nil));
		else
		{
			line = GetArraySlot(lines, i);
			text = DoMessage(line, RSSYMsetup1, args);
			TRichString rich(text);
			FPoint at;
			at.x = left << 16;
			at.y = y << 16;
			DrawRichString(rich, 0, rich.Length(), &labelStyle, at, &options, nil);
			text = DoMessage(line, RSSYMsetup2, args);
			if (ISNIL(FStrFilled(rcvr, text)))
				text = empty;
			rich.SetStringData(text);
			at.x = (left + indent) << 16;
			at.y = y << 16;
			DrawRichString(rich, 0, rich.Length(), &textStyle, at, &options, nil);
			advance = lineHeight;
		}
		y += advance;
	}
	DisposeStyleRecord(&textStyle);
	DisposeStyleRecord(&labelStyle);
	return NILREF;
}


void
RegisterViewExtraNatives(void)
{
	RegisterNativeFunction("FDrawExpando", (void*) FDrawExpando, 0);
	RegisterKeyHelpSlipNatives();
	RegisterReflowNatives();
	RegisterNativeFunction("FSyncScrollX", (void*) FSyncScrollX, 3);
	RegisterNativeFunction("FDisplaySplashGraphic", (void*) FDisplaySplashGraphic, 1);
	RegisterNativeFunction("FGrayShrink", (void*) FGrayShrink, 2);
	RegisterNativeFunction("FDV", (void*) FDV, 1);
	RegisterNativeFunction("FViewAutopsy", (void*) FViewAutopsy, 1);
	RegisterNativeFunction("FKeyboardInputX", (void*) FKeyboardInputX, 0);
	RegisterNativeFunction("FConnectPassthruKeyboard", (void*) FConnectPassthruKeyboard, 1);
	RegisterNativeFunction("FormatVertical__FRC6RefVarN21", (void*) FormatVertical, 2);
}
