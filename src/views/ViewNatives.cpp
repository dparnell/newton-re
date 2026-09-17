/*
	File:		views/ViewNatives.cpp

	Contains:	The NewtonScript view functions: the globals (GetView,
				AddView, RemoveView, SetValue, GetValue, RelBounds, SetBounds,
				RefreshViews, GetViewFlags, BuildContext, GetRoot) and the
				methods every view inherits from the root view's template
				(the ROM's Rviewroot: Dirty, DirtyBox, Show, Hide, _Open,
				_Close, _Toggle, Parent, ChildViewFrames, SyncView,
				SyncChildren, MoveBehind, GlobalBox, LocalBox, GlobalOuterBox,
				VisibleBox, GetDrawBox, SetOrigin, RedoChildren; the scripts
				Open, Toggle and the global Visible re-expressed as source).
				Show/Hide/Open/Close dispatch aeShow/aeHide/aeAddChild/
				aeDropChild commands through the application (Application.h)
				to the views, as the ROM does.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "RootView.h"
#include "DrawShape.h"
#include "Application.h"
#include "Commands.h"
#include "Keyboard.h"
#include "PickView.h"
#include "ROMConstants.h"
#include "NewtonTime.h"
#include "CompMath.h"
#include "Rects.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "Animate.h"
#include "Stroke.h"


// ROM 0x001f0234 FGetView__FRC6RefVarT1
// GetView(name): the context of the view the name means (GetView).
static Ref
FGetView(RefArg rcvr, RefArg name)
{
	TView* view = GetView(rcvr, name);
	return view != nil ? (Ref) view->fContext : NILREF;
}


// ROM 0x001b7cf4 FGetRoot
static Ref
FGetRoot(RefArg /*rcvr*/)
{
	return gRootView != nil ? (Ref) gRootView->fContext : NILREF;
}


// ROM 0x001f19ac FGetFlags__FRC6RefVarT1
// GetViewFlags(view): the view's flags word (the private bits too), 0 for
// no view.
static Ref
FGetFlags(RefArg rcvr, RefArg context)
{
	TView* view = GetView(rcvr, context);
	return MAKEINT(view != nil ? (long) view->fFlags : 0);
}


// ROM 0x0025c9e4 FBuildContext
// BuildContext(template): the context a view would run in.
static Ref
FBuildContext(RefArg rcvr, RefArg templ)
{
	GetView(rcvr);
	return TView::BuildContext(templ, true);
}


// ROM 0x001f1fa0 FAddView__FRC6RefVarN21
// AddView(parent, template): the template's view made under the parent
// and the template added to the parent's viewChildren; ==> the context.
static Ref
FAddView(RefArg rcvr, RefArg parent, RefArg templ)
{
	TView* parentView = FailGetView(rcvr, parent);
	RefVar context(TView::BuildContext(templ, true));
	TView* view = NOTNIL(context) ? BuildView(parentView, context) : nil;
	if (view == nil)
		return NILREF;
	RefVar children(parentView->GetProto(RSSYMviewchildren));
	if (ISNIL(children))
	{
		children = AllocateArray(RSSYMviewchildren, 1);
		SetArraySlotRef(children, 0, templ);
		parentView->SetContextSlot(RSSYMviewchildren, children);
	}
	else
		AddArraySlot(children, templ);
	return context;
}


// ROM 0x001f1fac FAddStepView__FRC6RefVarN21
static Ref
FAddStepView(RefArg rcvr, RefArg parent, RefArg templ)
{
	TView* parentView = FailGetView(rcvr, parent);
	RefVar context(TView::BuildContext(templ, true));
	TView* view = NOTNIL(context) ? BuildView(parentView, context) : nil;
	if (view == nil)
		return NILREF;
	RefVar children(parentView->GetProto(RSSYMstepchildren));
	if (ISNIL(children))
	{
		children = AllocateArray(RSSYMstepchildren, 1);
		SetArraySlotRef(children, 0, templ);
		parentView->SetContextSlot(RSSYMstepchildren, children);
	}
	else
		AddArraySlot(children, templ);
	return context;
}


// ROM 0x001f1fb8 CommonRemoveView__FRC6RefVarN31
// RemoveView/RemoveStepView(parent, child): the child's view (found by its
// context, or as the child whose template is the frame) removed and its
// template taken out of the parent's array of the name (unless the array
// is read-only: a warning).
static Ref
CommonRemoveView(RefArg rcvr, RefArg parent, RefArg child, RefArg arrayName)
{
	TView* parentView = FailGetView(rcvr, parent);
	if ((parentView->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
		BadWickedNaughtyNoot(0x1267);
	TView* childView = GetView(rcvr, child);
	if (childView == nil)
	{
		BadWickedNaughtyNoot(0x1268);
		TViewLoop loop(parentView->fChildren);
		for (TView* view = loop.Next(); view != nil; view = loop.Next())
			if (EQRef(child, GetFrameSlotRef(view->fContext, RSSYM_proto)))
			{
				childView = view;
				break;
			}
		if (childView == nil)
			ThrowMsg((char*) "nil view");
	}
	if ((childView->fFlags & vIsBeingDeleted) != vIsBeingDeleted)
	{
		RefVar templ(GetFrameSlotRef(childView->fContext, RSSYM_proto));
		parentView->RemoveChildView(childView);
		RefVar children(parentView->GetProto(arrayName));
		if (NOTNIL(children))
		{
			if (ObjectFlags(children) & kObjReadOnly)
				BadWickedNaughtyNoot(0x126a);
			else
				ArrayRemove(children, templ);
		}
	}
	else
		BadWickedNaughtyNoot(0x1269);
	return NILREF;
}


// ROM 0x001f2160 FRemoveView__FRC6RefVarN21
static Ref
FRemoveView(RefArg rcvr, RefArg parent, RefArg child)
{
	return CommonRemoveView(rcvr, parent, child, RSSYMviewchildren);
}


// ROM 0x001f216c FRemoveStepView__FRC6RefVarN21
static Ref
FRemoveStepView(RefArg rcvr, RefArg parent, RefArg child)
{
	return CommonRemoveView(rcvr, parent, child, RSSYMstepchildren);
}


// ROM 0x001f1934 FSetValue__FRC6RefVarN31
// SetValue(view, slot, value): through the view when there is one (the
// view synced, Changed sent), else the slot set in the frame.
static Ref
FSetValue(RefArg rcvr, RefArg context, RefArg slot, RefArg value)
{
	TView* view = GetView(rcvr, context);
	if (view != nil)
		view->SetValue(slot, value);
	else
		SetFrameSlot(context, slot, value);
	return NILREF;
}


// ROM 0x001f1890 FGetValue__FRC6RefVarN31
// GetValue(view, slot, type): through the view when there is one, else
// the slot through the frame's proto chain.
static Ref
FGetValue(RefArg rcvr, RefArg context, RefArg slot, RefArg type)
{
	TView* view = GetView(rcvr, context);
	if (view != nil)
		return view->GetValue(slot, type);
	if (ISNIL(context))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, slot);
	return GetProtoVariable(context, slot, nil);
}


// ROM 0x001f1bfc FRelBounds__FRC6RefVarN41
// RelBounds(left, top, width, height): a bounds frame.
static Ref
FRelBounds(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg width, RefArg height)
{
	Rect r;
	SetRect(&r, RINT(left), RINT(top), RINT(left) + RINT(width), RINT(top) + RINT(height));
	return ToObject(r);
}


// ROM 0x001f1cd0 FSetBounds__FRC6RefVarN41
// SetBounds(left, top, right, bottom): a bounds frame.
static Ref
FSetBounds(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	Rect r;
	SetRect(&r, RINT(left), RINT(top), RINT(right), RINT(bottom));
	return ToObject(r);
}


// ROM 0x001f2178 FRefreshViews__FRC6RefVar
// RefreshViews(): the update regions redrawn.
static Ref
FRefreshViews(RefArg /*rcvr*/)
{
	gRootView->Update(nil);
	return NILREF;
}


/*------------------------------------------------------------------------------
	T h e   v i e w   m e t h o d s
------------------------------------------------------------------------------*/

// ROM 0x001f3c10 FDirtyX
static Ref
FDirtyX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		view->Dirty(nil);
	return NILREF;
}


// ROM 0x001ec828 FDirtyBoxX
// :DirtyBox(bounds): the part of the view in the bounds frame.
static Ref
FDirtyBoxX(RefArg rcvr, RefArg bounds)
{
	TView* view = GetView(rcvr);
	Rect r;
	if (view != nil && FromObject(bounds, r))
		view->Dirty(&r);
	return NILREF;
}


// ROM 0x001ec99c FShowX
// :Show(): aeShow dispatched to the view through the application.
static Ref
FShowX(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	if ((view->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
		BadWickedNaughtyNoot(0x126e);
	else
	{
		RefVar cmd(MakeCommand(aeShow, view, kNoParameter));
		gApplication->DispatchCommand(cmd);
	}
	return NILREF;
}


// ROM 0x001eca14 FHideX
// :Hide(): aeHide dispatched to the view (NOT YET RECONSTRUCTED: the
// modal-safe views list under a modal dialog, RemoveModalSafeView).
static Ref
FHideX(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	if ((view->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
		BadWickedNaughtyNoot(0x126f);
	else
	{
		RefVar cmd(MakeCommand(aeHide, view, kNoParameter));
		gApplication->DispatchCommand(cmd);
	}
	return NILREF;
}


// ROM 0x001f3a50 RealOpenX__FRC6RefVarUc
// The view opened: aeAddChild dispatched to its _parent's view (with
// the template as the frame parameter) when there is none, aeShow to it
// when it is hidden - the parameter kNoModalCheck for a modal one; ==>
// whether anything was done.
static Ref
RealOpenX(RefArg context, Boolean modal)
{
	TView* view = GetView(context);
	Long parameter = modal ? kNoModalCheck : kNoParameter;
	if (view == nil)
	{
		TView* parent = FailGetView(RefVar(GetProtoVariable(context, RSSYM_parent, nil)));
		RefVar cmd(MakeCommand(aeAddChild, parent, parameter));
		CommandSetFrameParameter(cmd, context);
		gApplication->DispatchCommand(cmd);
		return TRUEREF;
	}
	if ((view->fFlags & vVisible) == 0)
	{
		RefVar cmd(MakeCommand(aeShow, view, parameter));
		gApplication->DispatchCommand(cmd);
		return TRUEREF;
	}
	return NILREF;
}


// ROM 0x001b7cbc FSetPopupX
// :SetPopup(): the view made the root's popup (a picker: closed by a tap
// elsewhere, NOT YET).
static Ref
FSetPopupX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		gRootView->SetPopup(view, true);
	return NILREF;
}


static Ref FOpenX(RefArg rcvr);		// (defined below)

// ROM 0x001f2a3c FDoPopup__FRC6RefVarN41
// :DoPopup(pickItems, x, y, callbackContext): a popup menu (the ROM's
// canonicalPopup, a picker) opened over the items at (x, y).  When x is a
// bounds frame it is used as the popup's box (relative to the receiver
// view), else x and y are an offset from the view's top-left (the whole
// screen when there is no view); the picker sizes and places itself.  The
// receiver view is remembered in the popup's `info` (so its items can
// reach it), and the callbackContext gets the pick.  Nothing when there
// are no items or popups are inhibited.  ==> the popup's context.  NOT
// YET RECONSTRUCTED: the modal case (FFilterDialog).
static Ref
FDoPopup(RefArg rcvr, RefArg pickItems, RefArg x, RefArg y, RefArg callbackContext)
{
	if (ISNIL(pickItems) || Length(pickItems) == 0 || gInhibitPopup)
		return NILREF;
	TView* view = GetView(rcvr);
	Rect box;
	Boolean haveView = view != nil;
	if (IsFrame(x) && FromObject(x, box))
	{
		// a bounds frame, local to the view
		if (haveView)
			OffsetRect(&box, view->viewBounds.left, view->viewBounds.top);
	}
	else
	{
		long ox = ISINT(x) ? RINT(x) : 0;
		long oy = ISINT(y) ? RINT(y) : 0;
		long baseLeft = 0, baseTop = 0;
		if (haveView)
		{
			baseLeft = view->viewBounds.left;
			baseTop = view->viewBounds.top;
		}
		SetRect(&box, baseLeft + ox, baseTop + oy, baseLeft + ox, baseTop + oy);
	}
	RefVar templ(Clone(RefVar(Rprotopicker)));		// DEVIATION: the ROM uses canonicalPopup (a scrolling popup wrapper); a plain picker suffices
	RefVar boundsFrame(ToObject(box));
	if (haveView)
		SetFrameSlot(boundsFrame, RSSYMinfo, RefVar(AddressToRef(view)));
	SetFrameSlot(templ, RSSYMbounds, boundsFrame);
	SetFrameSlot(templ, RSSYMpickitems, pickItems);
	// the popup is a top-level view: parented to the receiver's window (the
	// root view here), so RealOpenX adds it there
	SetFrameSlot(templ, RSSYM_parent, RefVar(gRootView->fContext));
	RefVar context;
	if (NOTNIL(callbackContext))
	{
		SetFrameSlot(templ, RSSYMcallbackcontext, callbackContext);
		context = TView::BuildContext(templ, true);
		// build the popup under the root (like AddView), then show it -
		// FOpenX finds the now-built, hidden view and dispatches aeShow
		BuildView(gRootView, context);
		FOpenX(context);
	}
	gRecognition.IgnoreClicks(0);
	return NOTNIL(context) ? (Ref) context : (Ref) templ;
}


/*------------------------------------------------------------------------------
	T h e   k e y   v i e w
------------------------------------------------------------------------------*/

// ROM 0x001f0288 FSetKeyView__FRC6RefVarN21
// SetKeyView(view, offsetOrInfo): the view (a name from the context; nil
// clears the key view) made the key view with a paragraph caret info of
// the offset (nil: 0) and no length, or with the caret info frame given.
static Ref
FSetKeyView(RefArg rcvr, RefArg name, RefArg offsetOrInfo)
{
	TView* view = nil;
	if (NOTNIL(name))
		view = GetView(rcvr, name);
	if (ISINT(offsetOrInfo) || ISNIL(offsetOrInfo))
	{
		RefVar info(Clone(RefVar(Rcanonicalparacaretinfo)));
		SetFrameSlot(info, RSSYMoffset, RefVar(MAKEINT(ISNIL(offsetOrInfo) ? 0 : RINT(offsetOrInfo))));
		SetFrameSlot(info, RSSYMlength, RefVar(MAKEINT(0)));
		gRootView->SetKeyViewSelection(view, info, true);
	}
	else
		gRootView->SetKeyViewSelection(view, offsetOrInfo, true);
	return NILREF;
}


// ROM 0x001f039c FGetKeyView
static Ref
FGetKeyView(RefArg /*rcvr*/)
{
	return gRootView->fCaretView != nil ? (Ref) gRootView->fCaretView->fContext : NILREF;
}


// ROM 0x001f03c0 FNextKeyView
// NextKeyView(view, direction, kind): the context of the view that follows
// (direction 1) or precedes (-1) the given view in the tab order of the
// kind; nil when there is none.
static Ref
FNextKeyView(RefArg /*rcvr*/, RefArg viewRef, RefArg direction, RefArg kind)
{
	TView* view = FailGetView(viewRef);
	if (view == nil)
		return NILREF;
	TView* next = view->NextKeyView(view, RINT(direction), RINT(kind));
	return next != nil ? (Ref) next->fContext : NILREF;
}


// ROM 0x001f0444 FGetCaretBox
// The caret's rectangle as a bounds frame with the key view and its
// offset (-1 for a selection); nil when no caret shows.
static Ref
FGetCaretBox(RefArg /*rcvr*/)
{
	TView* view = gRootView->fCaretView;
	Rect box;
	gRootView->GetCaretRect(&box);
	if (view == nil || EmptyRect(&box))
		return NILREF;
	RefVar result(ToObject(box));
	SetFrameSlot(result, RSSYMview, view->fContext);
	SetFrameSlot(result, RSSYMoffset, RefVar(MAKEINT(gRootView->fCaretLength == 0 ? gRootView->fCaretOffset : -1)));
	return result;
}


// ROM 0x001f12ac FGetCaretInfo
// {view: the key view's context, info: its selection}; nil for none.
static Ref
FGetCaretInfo(RefArg /*rcvr*/)
{
	TView* view = gRootView->fCaretView;
	if (view == nil)
		return NILREF;
	RefVar info(Clone(RefVar(Rcanonicalcaretinfo)));
	SetFrameSlot(info, RSSYMview, view->fContext);
	SetFrameSlot(info, RSSYMinfo, RefVar(view->GetSelection()));
	return info;
}


// ROM 0x001f0500 FSetRemoteWriting
static Ref
FSetRemoteWriting(RefArg /*rcvr*/, RefArg on)
{
	gRootView->SetRemoteWriting(NOTNIL(on));
	return NILREF;
}


// ROM 0x001f0534 FGetRemoteWriting
static Ref
FGetRemoteWriting(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->GetRemoteWriting());
}


// ROM 0x001f0560 FKeyboardConnected
static Ref
FKeyboardConnected(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->KeyboardConnected());
}


// ROM 0x001f058c FCommandKeyboardConnected
static Ref
FCommandKeyboardConnected(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->CommandKeyboardConnected());
}


// ROM 0x001b6830 FRestoreKeyView
// RestoreKeyView(view): the newest stacked key view within it made the key view again
static Ref
FRestoreKeyView(RefArg /*rcvr*/, RefArg context)
{
	TView* view = GetView(context);
	return MAKEBOOLEAN(gRootView->RestoreKeyView(view));
}


// ROM 0x001b66c4 FGetSelectionStack
static Ref
FGetSelectionStack(RefArg /*rcvr*/)
{
	return gRootView->GetSelectionStack();
}


// ROM 0x001f05b8 FRegisterOpenKeyboard
// RegisterOpenKeyboard(flags): the context registered as an on-screen keyboard
static Ref
FRegisterOpenKeyboard(RefArg rcvr, RefArg flags)
{
	gRootView->RegisterKeyboard(rcvr, RINT(flags));
	return NILREF;
}


// ROM 0x001f0600 FUnregisterOpenKeyboard
static Ref
FUnregisterOpenKeyboard(RefArg rcvr)
{
	return MAKEBOOLEAN(gRootView->UnregisterKeyboard(rcvr));
}


// ROM 0x001f1348 FViewContainsCaretView
static Ref
FViewContainsCaretView(RefArg /*rcvr*/, RefArg context)
{
	if (ISNIL(context))
		return NILREF;
	TView* view = FailGetView(context);
	return MAKEBOOLEAN(gRootView->ViewContainsCaretView(view));
}


// ROM 0x001ecaa8 FTrackHiliteX
// :TrackHilite(unit): the unit's stroke's ink taken off; the view hilited
// while the pen is inside it (within 10 pixels) and un-hilited when it
// leaves, a tick at a time, until the stroke ends; ==> whether the pen
// ended inside.  Each turn inside runs the buttonPressedScript, and its
// non-nil answer ends the tracking (when the newt_feature proto variable
// is set).  Before that: the busy box is shown (0x35) when there is no
// buttonPressedScript, and the _sound proto variable (the click when
// there is none) played - NOT YET RECONSTRUCTED: BusyBoxSend, FClicker,
// FPlaySound.  With no unit (nil) the pen is taken to be at the view's
// centre, and the tracking ends after two turns.
static Ref
FTrackHiliteX(RefArg rcvr, RefArg unit)
{
	TStrokePublic* stroke = nil;
	if (NOTNIL(unit))
	{
		stroke = StrokeFromRef(unit);
		stroke->InkOff(true);
	}
	// NOT YET RECONSTRUCTED: BusyBoxSend(0x35) when there is no buttonPressedScript; the _sound / FClicker
	TView* view = FailGetView(rcvr);
	Boolean selected = (view->fFlags & vSelected) != 0;
	Boolean wasInside = false;
	Point centre;
	centre.h = (view->viewBounds.left + view->viewBounds.right) / 2;
	centre.v = (view->viewBounds.top + view->viewBounds.bottom) / 2;
	Point delta;
	delta.h = delta.v = 10;
	for (long turn = 0; ; )
	{
		Point pt = stroke != nil ? stroke->FinalPoint() : centre;
		Boolean inside = view->Distance(pt, &delta) != 0x10000;
		if (inside == wasInside)
			Wait(1);
		else
		{
			selected = !selected;
			view->Select(selected, false);
			wasInside = inside;
		}
		if (inside)
		{
			RefVar result(DoMessageIfDefined(rcvr, RSSYMbuttonpressedscript, RefVar(NILREF), nil));
			if (NOTNIL(result) && NOTNIL(GetProtoVariable(rcvr, RSSYMnewt_feature, nil)))
				return result;		// (the ROM: BusyBoxSend(0x36) first)
		}
		turn++;
		Boolean done = stroke != nil ? stroke->Done() : turn == 2;
		if (done)
			return MAKEBOOLEAN(inside);
	}
}


// ROM 0x001ecd9c FTrackButtonX
// :TrackButton(unit): TrackHilite, then the buttonClickScript when the
// pen ended inside; the view is un-hilited after, even when a script
// throws.  ==> TrackHilite's answer.
static Ref
FTrackButtonX(RefArg rcvr, RefArg unit)
{
	RefVar result;
	unwind_protect
	{
		result = FTrackHiliteX(rcvr, unit);
		if (NOTNIL(result))
			DoMessage(rcvr, RSSYMbuttonclickscript, RefVar(NILREF));
	}
	on_unwind
	{
		TView* view = GetView(rcvr);
		if (view != nil)
			view->Select(false, false);
	}
	end_unwind;
	return result;
}


// ROM 0x001ece78 FHiliteX
// :Hilite(on): the view selected (hilited) or not.
static Ref
FHiliteX(RefArg rcvr, RefArg on)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		view->Select(NOTNIL(on), false);
	return TRUEREF;
}


// ROM 0x001eceb4 FHiliteUniqueX
// :HiliteUnique(on): the view selected (hilited) or not, its siblings
// un-hilited first.
static Ref
FHiliteUniqueX(RefArg rcvr, RefArg on)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		view->Select(NOTNIL(on), true);
	return TRUEREF;
}


// ROM 0x001ee9e8 FSetupIdleX
// :SetupIdle(milliseconds): the view's idler set (0 removes it) - its
// viewIdleScript runs when the time comes, its answer the next delay.
static Ref
FSetupIdleX(RefArg rcvr, RefArg delay)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		gRootView->AddIdler(view, (ULong) RINT(delay), 0);
	return NILREF;
}


// host: IdleViews() - the due idlers run (the ROM's event loop does this;
// DEVIATION: a global for the tests); ==> the next idle time in
// milliseconds from now, nil for none
static Ref
FIdleViews(RefArg /*rcvr*/)
{
	TTime next = gRootView->IdleViews();
	if (next.time.hi == 0 && next.time.lo == 0)
		return NILREF;
	TTime now = GetGlobalTime();
	Int64 left = next.time;
	CompSub(&now.time, &left);
	if (left.hi < 0)
		return MAKEINT(0);
	return MAKEINT((long) (left.lo / kMilliseconds));
}


// ROM 0x001f3b54 FOpenX
static Ref
FOpenX(RefArg rcvr)
{
	return RealOpenX(rcvr, false);
}


// ROM 0x001f3b70 FCloseX
// :Close(): aeDropChild dispatched to the parent - the view hidden and
// removed; a view still being set up is marked for deletion instead
// (the Constructor throws -8501).
static Ref
FCloseX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view == nil)
		return NILREF;
	if ((view->fFlags & vIsInSetupForm) && (view->fFlags & vIsBeingDeleted) != vIsBeingDeleted)
	{
		view->SetFlags(vIsBeingDeleted);
		return NILREF;
	}
	if ((view->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
	{
		BadWickedNaughtyNoot(0x126b);
		return NILREF;
	}
	RefVar cmd(MakeCommand(aeDropChild, view->fParent, (Long) view));
	gApplication->DispatchCommand(cmd);
	return NILREF;
}


// ROM 0x001ec87c FToggleX
// :_Toggle(): the view closed when it exists, else opened under its
// _parent's view.
static Ref
FToggleX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view != nil)
	{
		if ((view->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
			BadWickedNaughtyNoot(0x126c);
		else
		{
			view->Hide();
			view->fParent->RemoveChildView(view);
		}
		return NILREF;
	}
	TView* parent = FailGetView(RefVar(GetProtoVariable(rcvr, RSSYM_parent, nil)));
	if ((parent->fFlags & vIsBeingDeleted) == vIsBeingDeleted)
		BadWickedNaughtyNoot(0x126d);
	else
	{
		TView* child = parent->AddChild(rcvr);
		if (child != nil)
			child->Show();
	}
	return NILREF;
}


// ROM 0x001f3a40 FParentX
// :Parent(): the _parent frame.
static Ref
FParentX(RefArg rcvr)
{
	if (ISNIL(rcvr))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, RSSYM_parent);
	return GetProtoVariable(rcvr, RSSYM_parent, nil);
}


// ROM 0x001ed4b4 FChildViewFramesX
static Ref
FChildViewFramesX(RefArg rcvr)
{
	return FailGetView(rcvr)->ChildViewFrames();
}


// ROM 0x001ed4cc FSyncViewX
static Ref
FSyncViewX(RefArg rcvr)
{
	FailGetView(rcvr)->Sync();
	return NILREF;
}


// ROM 0x001ed2a4 FSyncChildrenX
// :SyncChildren(): the children brought up to date with viewChildren
// (a view still being set up: NOT YET: the ROM delays it).
static Ref
FSyncChildrenX(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	if (view->fFlags & vIsInSetupForm)
	{
		BadWickedNaughtyNoot(0x1271);
		return NILREF;
	}
	newton_try
	{
		view->AddViews(true);
	}
	newton_catch_all
	{
		view->ClearFlags(vIsBeingDeleted);
		NextHandler(&_info);
	}
	end_try;
	return NILREF;
}


// ROM 0x001ed198 FRedoChildrenX
// :RedoChildren(): the children removed (a view still being set up: NOT
// YET: the ROM delays it; a Throw clears the deletion marks).
static Ref
FRedoChildrenX(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	if (view->fFlags & vIsInSetupForm)
	{
		BadWickedNaughtyNoot(0x1270);
		return NILREF;
	}
	newton_try
	{
		view->RemoveAllViews();
	}
	newton_catch_all
	{
		view->ClearFlags(vIsBeingDeleted);
		NextHandler(&_info);
	}
	end_try;
	return NILREF;
}


// ROM 0x001ed3ec FMoveBehindX
// :MoveBehind(view): behind the other view (nil: to the front; 'first?..
// the ROM's other symbol: to the back as a floater... NOT YET: taken as
// to the front).
static Ref
FMoveBehindX(RefArg rcvr, RefArg behind)
{
	if (EQRef(rcvr, behind))
		return NILREF;
	TView* view = FailGetView(rcvr);
	TView* parent = view->fParent;
	if (parent == view)
		return NILREF;
	if (ISNIL(behind))
		view->BringToFront();
	else if (IsSymbol(behind))
	{
		ULong flags = view->fFlags;
		view->fFlags |= vFloating;
		view->BringToFront();
		view->fFlags = flags;
	}
	else
	{
		TView* other = FailGetView(behind);
		if (other->fParent == parent)
			parent->MoveChildBehind(view, other);
	}
	return NILREF;
}


// ROM 0x001ed4e8 CommonBox__FRC6RefVarP5TRect
// The view's bounds: as set, or, while it is being set up, justified
// from its template.
static Boolean
CommonBox(RefArg rcvr, Rect* bounds)
{
	TView* view = FailGetView(rcvr);
	if ((view->fFlags & vIsInSetupForm) == 0)
		*bounds = view->viewBounds;
	else
	{
		if (!FromObject(RefVar(view->GetProto(RSSYMviewbounds)), *bounds))
			return false;
		view->JustifyBounds(bounds);
	}
	return true;
}


// ROM 0x001ed580 FGlobalBoxX
static Ref
FGlobalBoxX(RefArg rcvr)
{
	Rect bounds;
	if (!CommonBox(rcvr, &bounds))
		return NILREF;
	return ToObject(bounds);
}


// ROM 0x001ed5b0 FLocalBoxX
// :LocalBox(): the bounds with the top left at (0, 0).
static Ref
FLocalBoxX(RefArg rcvr)
{
	Rect bounds;
	if (!CommonBox(rcvr, &bounds))
		return NILREF;
	OffsetRect(&bounds, -bounds.left, -bounds.top);
	return ToObject(bounds);
}


// ROM 0x001ed67c FGlobalOuterBoxX
static Ref
FGlobalOuterBoxX(RefArg rcvr)
{
	Rect bounds;
	FailGetView(rcvr)->OuterBounds(&bounds);
	return ToObject(bounds);
}


// ROM 0x001ed6ac FVisibleBox
// :VisibleBox(): the bounds cut to the port's visible region.
static Ref
FVisibleBox(RefArg rcvr)
{
	TView* view = FailGetView(rcvr);
	GrafPort* port;
	GetPort(&port);
	Rect bounds;
	SectRect(&view->viewBounds, &(*port->visRgn)->rgnBBox, &bounds);
	return ToObject(bounds);
}


// ROM 0x001ec7e4 FGetDrawBoxX
// :GetDrawBox(): the port's visible region's bounds - what a draw script
// is asked to draw.
static Ref
FGetDrawBoxX(RefArg /*rcvr*/)
{
	GrafPort* port;
	GetPort(&port);
	return ToObject((*port->visRgn)->rgnBBox);
}


// ROM 0x001ee950 FSetOriginX
// :SetOrigin(x, y): the contents scrolled to the origin.
static Ref
FSetOriginX(RefArg rcvr, RefArg x, RefArg y)
{
	TView* view = FailGetView(rcvr);
	Point origin = MakePoint(RINT(x), RINT(y));
	view->SetOrigin(origin);
	return NILREF;
}


/*------------------------------------------------------------------------------
	E f f e c t s   a n d   d r a g g i n g
------------------------------------------------------------------------------*/

// ROM 0x001ecef0 FDragX
// :Drag(unit, bounds): the view dragged with the unit's stroke, kept
// within the bounds frame (nil: the application area).  ==> true.
static Ref
FDragX(RefArg rcvr, RefArg unit, RefArg bounds)
{
	TView* view = FailGetView(rcvr);
	TStrokePublic* stroke = StrokeFromRef(unit);
	stroke->InkOff(true);
	Rect limit;
	if (ISNIL(bounds))
		GetAppAreaBounds(&limit);
	else
		FromObject(bounds, limit);
	view->Drag(stroke, limit);
	return TRUEREF;
}


// ROM 0x001ee22c FDeleteX
// :Delete(message, args): the view crumpled into the trash - the trash
// effect set up, the message sent to the view (which removes it), the
// effect run.  ==> nil.
static Ref
FDeleteX(RefArg rcvr, RefArg message, RefArg args)
{
	if (NOTNIL(rcvr))
	{
		TView* view = FailGetView(rcvr);
		TAnimate effect;
		effect.SetupTrashEffect(view);
		view->RunScript(message, args, true);
		effect.DoEffect(RefVar(NILREF));
	}
	return NILREF;
}


// ROM 0x001ee2cc FEffectX
// :Effect(effect, offScreen, sound, message, args): a plain effect of the
// effect word (nil: the viewEffect) set up as a show (offScreen non-nil:
// the image drawn from the view) or a hide, the message sent to the view
// when there is one, the effect run with the sound.  ==> nil.
static Ref
FEffectX(RefArg rcvr, RefArg effect, RefArg offScreen, RefArg sound, RefArg message, RefArg args)
{
	if (NOTNIL(rcvr))
	{
		TView* view = FailGetView(rcvr);
		TAnimate anim;
		anim.SetupPlainEffect(view, ISNIL(offScreen), ISINT(effect) ? RINT(effect) : 0);
		if (NOTNIL(message))
			DoMessageIfDefined(rcvr, message, args, nil);
		anim.DoEffect(sound);
	}
	return NILREF;
}


// ROM 0x001ee3b0 FSlideEffectX
// :SlideEffect(distance, direction, sound, message, args): the view's
// outer bounds slid (TAnimate::SetupSlideEffect), the message sent, the
// effect run.  ==> nil.
static Ref
FSlideEffectX(RefArg rcvr, RefArg distance, RefArg direction, RefArg sound, RefArg message, RefArg args)
{
	if (NOTNIL(rcvr))
	{
		TView* view = FailGetView(rcvr);
		TAnimate anim;
		Rect bounds;
		view->OuterBounds(&bounds);
		anim.SetupSlideEffect(view, bounds, RINT(distance), RINT(direction));
		if (NOTNIL(message))
			DoMessageIfDefined(rcvr, message, args, nil);
		anim.DoEffect(sound);
	}
	return NILREF;
}


// ROM 0x001ee4a4 FRevealEffectX
// :RevealEffect(distance, bounds, sound, message, args): the bounds frame
// (local to the view) slid the distance with new contents coming in from
// no direction (0), the message sent, the effect run.  ==> nil.
static Ref
FRevealEffectX(RefArg rcvr, RefArg distance, RefArg bounds, RefArg sound, RefArg message, RefArg args)
{
	if (NOTNIL(rcvr))
	{
		TView* view = FailGetView(rcvr);
		Rect box;
		if (FromObject(bounds, box))
		{
			TAnimate anim;
			OffsetRect(&box, view->viewBounds.left, view->viewBounds.top);
			anim.SetupSlideEffect(view, box, RINT(distance), 0);
			if (NOTNIL(message))
				DoMessageIfDefined(rcvr, message, args, nil);
			anim.DoEffect(sound);
		}
	}
	return NILREF;
}


// ROM 0x001ee620 FDoScrubEffect__FRC6RefVarT1
// DoScrubEffect(view, unit): the poof over the unit's bounds, its ink
// left; ==> nil.
static Ref
FDoScrubEffect(RefArg rcvr, RefArg unit)
{
	TUnitPublic* theUnit = UnitFromRef(unit);
	TView* view = FailGetView(rcvr);
	Rect bounds;
	theUnit->Bounds(&bounds);
	theUnit->Stroke()->InkOff(false);
	TAnimate anim;
	anim.SetupPoofEffect(view, bounds);
	anim.DoEffect(RefVar(Rpoof));
	return NILREF;
}


// the ROM's NewtonScript view functions, as source
// ROM 0x00438ae9 (object) Visible
// ROM 0x004724dd (object) Rviewroot.Open (DEVIATION: the screen rotation prompt for a small display is not asked)
// ROM 0x00472439 (object) Rviewroot.Toggle
static const ScriptFunctionEntry gViewScriptFunctions[] = {
	{ "Visible", "func(view) band(GetViewFlags(view), 1) <> 0" },
	{ nil, nil }
};
static const char* const kOpenSource = "func() :_Open()";
static const char* const kToggleSource = "func() if not viewCObject or not Visible(self) then :Open() else :close()";


void
RegisterViewNatives(void)
{
	RegisterNativeFunction("FSetupIdleX", (void*) FSetupIdleX, 1);
	RegisterNativeFunction("FSetPopupX", (void*) FSetPopupX, 0);
	RegisterNativeFunction("FDoPopup__FRC6RefVarN41", (void*) FDoPopup, 4);
	RegisterNativeFunction("FSetKeyView__FRC6RefVarN21", (void*) FSetKeyView, 2);
	RegisterNativeFunction("FGetKeyView", (void*) FGetKeyView, 0);
	RegisterNativeFunction("FNextKeyView", (void*) FNextKeyView, 3);
	RegisterNativeFunction("FGetCaretBox", (void*) FGetCaretBox, 0);
	RegisterNativeFunction("FGetCaretInfo", (void*) FGetCaretInfo, 0);
	RegisterNativeFunction("FSetRemoteWriting", (void*) FSetRemoteWriting, 1);
	RegisterNativeFunction("FGetRemoteWriting", (void*) FGetRemoteWriting, 0);
	RegisterNativeFunction("FKeyboardConnected", (void*) FKeyboardConnected, 0);
	RegisterNativeFunction("FCommandKeyboardConnected", (void*) FCommandKeyboardConnected, 0);
	RegisterNativeFunction("FRestoreKeyView", (void*) FRestoreKeyView, 1);
	RegisterNativeFunction("FGetSelectionStack", (void*) FGetSelectionStack, 0);
	RegisterNativeFunction("FRegisterOpenKeyboard", (void*) FRegisterOpenKeyboard, 1);
	RegisterNativeFunction("FUnregisterOpenKeyboard", (void*) FUnregisterOpenKeyboard, 0);
	RegisterNativeFunction("FViewContainsCaretView", (void*) FViewContainsCaretView, 1);
	RegisterNativeFunction("FTrackHiliteX", (void*) FTrackHiliteX, 1);
	RegisterNativeFunction("FTrackButtonX", (void*) FTrackButtonX, 1);
	RegisterNativeFunction("FHiliteX", (void*) FHiliteX, 1);
	RegisterNativeFunction("FHiliteUniqueX", (void*) FHiliteUniqueX, 1);
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "IdleViews")), RefVar(MakeCFunction((void*) FIdleViews, 0, nil)));
	RegisterShapeNatives();
	RegisterApplicationNatives();
	RegisterKeyboardNatives();
	RegisterPickNatives();
	RegisterNativeFunction("FGetView__FRC6RefVarT1", (void*) FGetView, 1);
	RegisterNativeFunction("FGetRoot", (void*) FGetRoot, 0);
	RegisterNativeFunction("FGetFlags__FRC6RefVarT1", (void*) FGetFlags, 1);
	RegisterNativeFunction("FBuildContext", (void*) FBuildContext, 1);
	RegisterNativeFunction("FAddView__FRC6RefVarN21", (void*) FAddView, 2);
	RegisterNativeFunction("FAddStepView__FRC6RefVarN21", (void*) FAddStepView, 2);
	RegisterNativeFunction("FRemoveView__FRC6RefVarN21", (void*) FRemoveView, 2);
	RegisterNativeFunction("FRemoveStepView__FRC6RefVarN21", (void*) FRemoveStepView, 2);
	RegisterNativeFunction("FSetValue__FRC6RefVarN31", (void*) FSetValue, 3);
	RegisterNativeFunction("FGetValue__FRC6RefVarN31", (void*) FGetValue, 3);
	RegisterNativeFunction("FRelBounds__FRC6RefVarN41", (void*) FRelBounds, 4);
	RegisterNativeFunction("FSetBounds__FRC6RefVarN41", (void*) FSetBounds, 4);
	RegisterNativeFunction("FRefreshViews__FRC6RefVar", (void*) FRefreshViews, 0);
	RegisterNativeFunction("FDirtyX", (void*) FDirtyX, 0);
	RegisterNativeFunction("FDirtyBoxX", (void*) FDirtyBoxX, 1);
	RegisterNativeFunction("FShowX", (void*) FShowX, 0);
	RegisterNativeFunction("FHideX", (void*) FHideX, 0);
	RegisterNativeFunction("FOpenX", (void*) FOpenX, 0);
	RegisterNativeFunction("FCloseX", (void*) FCloseX, 0);
	RegisterNativeFunction("FToggleX", (void*) FToggleX, 0);
	RegisterNativeFunction("FParentX", (void*) FParentX, 0);
	RegisterNativeFunction("FChildViewFramesX", (void*) FChildViewFramesX, 0);
	RegisterNativeFunction("FSyncViewX", (void*) FSyncViewX, 0);
	RegisterNativeFunction("FSyncChildrenX", (void*) FSyncChildrenX, 0);
	RegisterNativeFunction("FRedoChildrenX", (void*) FRedoChildrenX, 0);
	RegisterNativeFunction("FMoveBehindX", (void*) FMoveBehindX, 1);
	RegisterNativeFunction("FGlobalBoxX", (void*) FGlobalBoxX, 0);
	RegisterNativeFunction("FLocalBoxX", (void*) FLocalBoxX, 0);
	RegisterNativeFunction("FGlobalOuterBoxX", (void*) FGlobalOuterBoxX, 0);
	RegisterNativeFunction("FVisibleBox", (void*) FVisibleBox, 0);
	RegisterNativeFunction("FGetDrawBoxX", (void*) FGetDrawBoxX, 0);
	RegisterNativeFunction("FSetOriginX", (void*) FSetOriginX, 2);
	RegisterNativeFunction("FDragX", (void*) FDragX, 2);
	RegisterNativeFunction("FDeleteX", (void*) FDeleteX, 2);
	RegisterNativeFunction("FEffectX", (void*) FEffectX, 5);
	RegisterNativeFunction("FSlideEffectX", (void*) FSlideEffectX, 5);
	RegisterNativeFunction("FRevealEffectX", (void*) FRevealEffectX, 5);
	RegisterNativeFunction("FDoScrubEffect__FRC6RefVarT1", (void*) FDoScrubEffect, 1);
	InstallScriptFunctions(gViewScriptFunctions);
}


// The methods every view inherits, as the ROM's root template (Rviewroot)
// has them: a frame of native function objects and the two scripts, for
// the root template's _proto on a host that does not use the ROM's.
Ref
MakeViewMethods(void)
{
	static const struct { const char* fName; void* fFn; long fArgs; } kMethods[] = {
		{ "Dirty", (void*) FDirtyX, 0 }, { "DirtyBox", (void*) FDirtyBoxX, 1 },
		{ "show", (void*) FShowX, 0 }, { "Hide", (void*) FHideX, 0 }, { "_Hide", (void*) FHideX, 0 },
		{ "_Open", (void*) FOpenX, 0 }, { "close", (void*) FCloseX, 0 }, { "_Close", (void*) FCloseX, 0 },
		{ "_Toggle", (void*) FToggleX, 0 }, { "Parent", (void*) FParentX, 0 },
		{ "ChildViewFrames", (void*) FChildViewFramesX, 0 }, { "SyncView", (void*) FSyncViewX, 0 },
		{ "SyncChildren", (void*) FSyncChildrenX, 0 }, { "RedoChildren", (void*) FRedoChildrenX, 0 },
		{ "MoveBehind", (void*) FMoveBehindX, 1 }, { "GlobalBox", (void*) FGlobalBoxX, 0 },
		{ "LocalBox", (void*) FLocalBoxX, 0 }, { "GlobalOuterBox", (void*) FGlobalOuterBoxX, 0 },
		{ "VisibleBox", (void*) FVisibleBox, 0 }, { "GetDrawBox", (void*) FGetDrawBoxX, 0 },
		{ "SetOrigin", (void*) FSetOriginX, 2 },
		{ "Drag", (void*) FDragX, 2 }, { "delete", (void*) FDeleteX, 2 }, { "Effect", (void*) FEffectX, 5 },
		{ "SlideEffect", (void*) FSlideEffectX, 5 }, { "RevealEffect", (void*) FRevealEffectX, 5 },
		{ "DrawShape", (void*) FDrawShape, 2 }, { "AddUndoAction", (void*) FAddUndoAction, 2 }, { "SetupIdle", (void*) FSetupIdleX, 1 }, { "SetPopup", (void*) FSetPopupX, 0 }, { "DoPopup", (void*) FDoPopup, 4 },
		{ "TrackHilite", (void*) FTrackHiliteX, 1 }, { "TrackButton", (void*) FTrackButtonX, 1 },
		{ "hilite", (void*) FHiliteX, 1 }, { "HiliteUnique", (void*) FHiliteUniqueX, 1 },
		{ nil, nil, 0 } };
	RefVar methods(AllocateFrame());
	for (long i = 0; kMethods[i].fName != nil; i++)
		SetFrameSlot(methods, RefVar(Intern((char*) kMethods[i].fName)), RefVar(MakeCFunction(kMethods[i].fFn, kMethods[i].fArgs, nil)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Open")), RefVar(CompileScriptFunction(kOpenSource)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Toggle")), RefVar(CompileScriptFunction(kToggleSource)));
	return methods;
}
