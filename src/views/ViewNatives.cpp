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

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RootView.h"
#include "EditView.h"
#include "DataView.h"
#include "ParagraphView.h"
#include "StyleRuns.h"
#include "RichString.h"
#include "REPTranslators.h"
#include "Unicode.h"
#include "DrawShape.h"
#include "Application.h"
#include "Commands.h"
#include "Keyboard.h"
#include "PickView.h"
#include "ROMConstants.h"
#include "NewtonTime.h"
#include "CompMath.h"
#include "Rects.h"
#include "Pictures.h"
#include "Draw.h"
#include "DrawShape.h"
#include "Regions.h"
#include "RegionVars.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "Animate.h"
#include "Stroke.h"
#include "DragDrop.h"


// ROM 0x001ede1c FGetView__FRC6RefVarT1
// GetView(name): the context of the view the name means (GetView).
static Ref
FGetView(RefArg rcvr, RefArg name)
{
	TView* view = GetView(rcvr, name);
	return view != nil ? (Ref) view->fContext : NILREF;
}


// ROM 0x001b581c FGetRoot
static Ref
FGetRoot(RefArg /*rcvr*/)
{
	return gRootView != nil ? (Ref) gRootView->fContext : NILREF;
}


// ROM 0x001ef594 FGetFlags__FRC6RefVarT1
// GetViewFlags(view): the view's flags word (the private bits too), 0 for
// no view.
static Ref
FGetFlags(RefArg rcvr, RefArg context)
{
	TView* view = GetView(rcvr, context);
	return MAKEINT(view != nil ? (long) view->fFlags : 0);
}


// ROM 0x0025e91c FBuildContext
// BuildContext(template): the context a view would run in.
static Ref
FBuildContext(RefArg rcvr, RefArg templ)
{
	GetView(rcvr);
	return TView::BuildContext(templ, true);
}


// ROM 0x001efb88 FAddView__FRC6RefVarN21
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


// ROM 0x001efb94 FAddStepView__FRC6RefVarN21
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


// ROM 0x001efba0 CommonRemoveView__FRC6RefVarN31
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


// ROM 0x001efd48 FRemoveView__FRC6RefVarN21
static Ref
FRemoveView(RefArg rcvr, RefArg parent, RefArg child)
{
	return CommonRemoveView(rcvr, parent, child, RSSYMviewchildren);
}


// ROM 0x001efd54 FRemoveStepView__FRC6RefVarN21
static Ref
FRemoveStepView(RefArg rcvr, RefArg parent, RefArg child)
{
	return CommonRemoveView(rcvr, parent, child, RSSYMstepchildren);
}


// ROM 0x001ef51c FSetValue__FRC6RefVarN31
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


// ROM 0x001ef478 FGetValue__FRC6RefVarN31
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


// ROM 0x001ef7e4 FRelBounds__FRC6RefVarN41
// RelBounds(left, top, width, height): a bounds frame.
static Ref
FRelBounds(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg width, RefArg height)
{
	Rect r;
	SetRect(&r, RINT(left), RINT(top), RINT(left) + RINT(width), RINT(top) + RINT(height));
	return ToObject(r);
}


// ROM 0x001ef8b8 FSetBounds__FRC6RefVarN41
// SetBounds(left, top, right, bottom): a bounds frame.
static Ref
FSetBounds(RefArg /*rcvr*/, RefArg left, RefArg top, RefArg right, RefArg bottom)
{
	Rect r;
	SetRect(&r, RINT(left), RINT(top), RINT(right), RINT(bottom));
	return ToObject(r);
}


// ROM 0x001efd60 FRefreshViews__FRC6RefVar
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

// ROM 0x001f17f8 FDirtyX
static Ref
FDirtyX(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		view->Dirty(nil);
	return NILREF;
}


// ROM 0x001ea410 FDirtyBoxX
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


// ROM 0x001ea584 FShowX
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


// ROM 0x001ea5fc FHideX
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


// ROM 0x001f1638 RealOpenX__FRC6RefVarUc
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


// ROM 0x001b57e4 FSetPopupX
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

// ROM 0x001f0624 FDoPopup__FRC6RefVarN41
// :DoPopup(pickItems, x, y, callbackContext): a popup menu (the ROM's
// canonicalPopup, a picker) opened over the items at (x, y).  When x is a
// bounds frame it is used as the popup's box (relative to the receiver
// view), else x and y are an offset from the view's top-left (the whole
// screen when there is no view); the picker sizes and places itself.  The
// receiver view is remembered in the popup's `info` (so its items can
// reach it), and the callbackContext gets the pick.  Nothing when there
// are no items or popups are inhibited.  ==> the popup's context.  NOT
// YET RECONSTRUCTED: the modal case (FFilterDialog).
Ref
DoPopupMenu(RefArg rcvr, RefArg pickItems, RefArg x, RefArg y, RefArg callbackContext)
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


static Ref
FDoPopup(RefArg rcvr, RefArg pickItems, RefArg x, RefArg y, RefArg callbackContext)
{
	return DoPopupMenu(rcvr, pickItems, x, y, callbackContext);
}


/*------------------------------------------------------------------------------
	T h e   k e y   v i e w
------------------------------------------------------------------------------*/

// ROM 0x001ede70 FSetKeyView__FRC6RefVarN21
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


// ROM 0x001edf84 FGetKeyView
static Ref
FGetKeyView(RefArg /*rcvr*/)
{
	return gRootView->fCaretView != nil ? (Ref) gRootView->fCaretView->fContext : NILREF;
}


// ROM 0x001edfa8 FNextKeyView
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


// ROM 0x001ee02c FGetCaretBox
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


// ROM 0x001eee94 FGetCaretInfo
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


// ROM 0x001ee0e8 FSetRemoteWriting
static Ref
FSetRemoteWriting(RefArg /*rcvr*/, RefArg on)
{
	gRootView->SetRemoteWriting(NOTNIL(on));
	return NILREF;
}


// ROM 0x001ee11c FGetRemoteWriting
static Ref
FGetRemoteWriting(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->GetRemoteWriting());
}


// ROM 0x001ee148 FKeyboardConnected
static Ref
FKeyboardConnected(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->KeyboardConnected());
}


// ROM 0x001ee174 FCommandKeyboardConnected
static Ref
FCommandKeyboardConnected(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gRootView->CommandKeyboardConnected());
}


// ROM 0x001b4358 FRestoreKeyView
// RestoreKeyView(view): the newest stacked key view within it made the key view again
static Ref
FRestoreKeyView(RefArg /*rcvr*/, RefArg context)
{
	TView* view = GetView(context);
	return MAKEBOOLEAN(gRootView->RestoreKeyView(view));
}


// ROM 0x001b41ec FGetSelectionStack
static Ref
FGetSelectionStack(RefArg /*rcvr*/)
{
	return gRootView->GetSelectionStack();
}


// ROM 0x001ee1a0 FRegisterOpenKeyboard
// RegisterOpenKeyboard(flags): the context registered as an on-screen keyboard
static Ref
FRegisterOpenKeyboard(RefArg rcvr, RefArg flags)
{
	gRootView->RegisterKeyboard(rcvr, RINT(flags));
	return NILREF;
}


// ROM 0x001ee1e8 FUnregisterOpenKeyboard
static Ref
FUnregisterOpenKeyboard(RefArg rcvr)
{
	return MAKEBOOLEAN(gRootView->UnregisterKeyboard(rcvr));
}


// ROM 0x001eef30 FViewContainsCaretView
static Ref
FViewContainsCaretView(RefArg /*rcvr*/, RefArg context)
{
	if (ISNIL(context))
		return NILREF;
	TView* view = FailGetView(context);
	return MAKEBOOLEAN(gRootView->ViewContainsCaretView(view));
}


// ROM 0x001ebcb4 FSetHiliteNoUpdateX
// :SetHiliteNoUpdate(start, end, exclusive): the characters from `start`
// to `end` hilited in a view that holds data.  `exclusive` first takes
// the hilites off everything else in the editor the view is written on -
// a paragraph asks the editor, so that the selection moves rather than
// being added to; any other data view clears only itself.  ==> true when
// the view could take a selection at all, nil when it could not.
static Ref
FSetHiliteNoUpdateX(RefArg rcvr, RefArg start, RefArg end, RefArg exclusive)
{
	TView* view = GetView(rcvr);
	if (view == nil || !view->DerivedFrom(clDataView))
		return NILREF;
	if (NOTNIL(exclusive))
	{
		TView* owner = view;
		if (view->DerivedFrom(clParagraphView))
		{
			owner = ((TDataView*) view)->GetEnclosingEditView();
			if (owner == nil)
				owner = view;
		}
		owner->RemoveAllHilites();
	}
	// (the ROM reads `start` twice, once for each side of the subtraction)
	((TDataView*) view)->HiliteText(RINT(start), RINT(end) - RINT(start), true);
	return TRUEREF;
}


// ROM 0x001ebdc8 FSetHiliteX
// The same, and the screen brought up to date when anything was hilited.
static Ref
FSetHiliteX(RefArg rcvr, RefArg start, RefArg end, RefArg exclusive)
{
	RefVar hilited(FSetHiliteNoUpdateX(rcvr, start, end, exclusive));
	if (NOTNIL(hilited))
		gRootView->Update(nil);
	return hilited;
}


// ROM 0x001ef240 FPositionCaret
// :PositionCaret(x, y, click): the caret put where the point says in an
// edit view - the page of a notebook application - which is what an
// application does when it opens, so that whatever is typed or written
// next has somewhere to go.  The point is given in the view's own
// coordinates and moved by where the page is scrolled to.  `click` asks
// for the click the machine makes when the caret moves.  ==> nil.
static Ref
FPositionCaret(RefArg rcvr, RefArg x, RefArg y, RefArg click)
{
	TView* view = FailGetView(rcvr);
	if (!view->DerivedFrom(clEditView))
		ThrowMsg("not an edit view");
	if ((view->fFlags & (vReadOnly | vWriteProtected)) != 0)
		ThrowMsg("read-only view");
	Point pt;
	pt.h = (short) RINT(x);
	pt.v = (short) RINT(y);
	Point origin = view->ContentsOrigin();
	pt.v += origin.v;
	pt.h += origin.h;
	((TEditView*) view)->PositionCaret(pt, NOTNIL(click));
	return NILREF;
}


// ROM 0x001ea690 FTrackHiliteX
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


// ROM 0x001ea984 FTrackButtonX
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


// ROM 0x001eaa60 FHiliteX
// :Hilite(on): the view selected (hilited) or not.
static Ref
FHiliteX(RefArg rcvr, RefArg on)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		view->Select(NOTNIL(on), false);
	return TRUEREF;
}


// ROM 0x001eaa9c FHiliteUniqueX
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


// ROM 0x001ec5d0 FSetupIdleX
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


// ROM 0x001f173c FOpenX
static Ref
FOpenX(RefArg rcvr)
{
	return RealOpenX(rcvr, false);
}


// ROM 0x001f1758 FCloseX
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


// ROM 0x001ea464 FToggleX
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


// ROM 0x001f1628 FParentX
// :Parent(): the _parent frame.
static Ref
FParentX(RefArg rcvr)
{
	if (ISNIL(rcvr))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, RSSYM_parent);
	return GetProtoVariable(rcvr, RSSYM_parent, nil);
}


// ROM 0x001eb09c FChildViewFramesX
static Ref
FChildViewFramesX(RefArg rcvr)
{
	return FailGetView(rcvr)->ChildViewFrames();
}


// ROM 0x001eb0b4 FSyncViewX
static Ref
FSyncViewX(RefArg rcvr)
{
	FailGetView(rcvr)->Sync();
	return NILREF;
}


// ROM 0x001eae8c FSyncChildrenX
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


// ROM 0x001ead80 FRedoChildrenX
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


// ROM 0x001eafd4 FMoveBehindX
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


// ROM 0x001eb0d0 CommonBox__FRC6RefVarP5TRect
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


// ROM 0x001eb168 FGlobalBoxX
static Ref
FGlobalBoxX(RefArg rcvr)
{
	Rect bounds;
	if (!CommonBox(rcvr, &bounds))
		return NILREF;
	return ToObject(bounds);
}


// ROM 0x001eb198 FLocalBoxX
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


// ROM 0x001eb264 FGlobalOuterBoxX
static Ref
FGlobalOuterBoxX(RefArg rcvr)
{
	Rect bounds;
	FailGetView(rcvr)->OuterBounds(&bounds);
	return ToObject(bounds);
}


// ROM 0x001eb294 FVisibleBox
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


// ROM 0x001ea3cc FGetDrawBoxX
// :GetDrawBox(): the port's visible region's bounds - what a draw script
// is asked to draw.
static Ref
FGetDrawBoxX(RefArg /*rcvr*/)
{
	GrafPort* port;
	GetPort(&port);
	return ToObject((*port->visRgn)->rgnBBox);
}


// ROM 0x001ec538 FSetOriginX
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

// ROM 0x001eaad8 FDragX
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


// ROM 0x001f0b5c FDragAndDrop
// :DragAndDrop(unit, bounds, limit, copy, dragItems): the view's data
// (the dragItems array) dragged with the unit's stroke and dropped on the
// view under the pen at the end.  ==> whether it was dropped.
static Ref
FDragAndDrop(RefArg rcvr, RefArg unit, RefArg bounds, RefArg limit, RefArg copy, RefArg dragItems)
{
	TView* view = FailGetView(rcvr);
	TStrokePublic* stroke = StrokeFromRef(unit);
	Rect b;
	FromObject(bounds, b);
	Rect lim;
	Rect* limitP = nil;
	if (NOTNIL(limit) && FromObject(limit, lim))
		limitP = &lim;
	TDragInfo dragInfo(dragItems);
	Boolean did = view->DragAndDrop(stroke, b, limitP, nil, NOTNIL(copy), dragInfo, nil);
	return MAKEBOOLEAN(did);
}


// ROM 0x001ebe14 FDeleteX
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


// ROM 0x001ebeb4 FEffectX
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


// ROM 0x001ebf98 FSlideEffectX
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


// ROM 0x001ec08c FRevealEffectX
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


// ROM 0x001ec208 FDoScrubEffect__FRC6RefVarT1
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
// ROM 0x0041a751 (object) Visible
// ROM 0x00448dc5 (object) Rviewroot.Open (DEVIATION: the screen rotation prompt for a small display is not asked)
// ROM 0x00448c65 (object) Rviewroot.Toggle
static const ScriptFunctionEntry gViewScriptFunctions[] = {
	{ "Visible", "func(view) band(GetViewFlags(view), 1) <> 0" },
	{ nil, nil }
};
static const char* const kOpenSource = "func() :_Open()";
static const char* const kToggleSource = "func() if not viewCObject or not Visible(self) then :Open() else :close()";


// ROM 0x001ecfc4 TableLookup
// An association list looked up: the array is key, value, key, value, ...
// and one last slot, the answer when no key matches.  The keys are
// compared with EQ, so symbols and integers match and strings do not.
Ref
FTableLookup(RefArg /*rcvr*/, RefArg table, RefArg key)
{
	RefVar result;
	long length = Length(table);
	for (long i = 0; i < length - 2; i += 2)
	{
		if (EQRef(GetArraySlotRef(table, i), key))
		{
			result = GetArraySlotRef(table, i + 1);
			break;
		}
	}
	if (ISNIL(result))
		result = GetArraySlotRef(table, length - 1);
	return result;
}


// ROM 0x001f0608 FModalState
// Whether a modal dialog is up: the ROM counts them in gModalCount
// (0x0c102618), which the modal dialog code raises and lowers.
//
// NOT YET RECONSTRUCTED: the modal dialogs themselves, so the count stays
// at nought and nothing is ever modal - which is the truth on a host that
// cannot put one up.
Ref
FModalState(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gModalCount >= 1);
}


// ROM 0x001eb2f4 FLayoutVerticallyX
// :LayoutColumn(entries, index): as many of the entries from index on as
// fit down the view, in a new array.  Each entry's own `height` is asked
// of it, unless the view says `allCollapsed` or the entry says
// `collapsed`, when the view's `collapsedHeight` (nil meaning none) is
// taken instead.  The list is built until the heights fill the view, so
// the one that crosses the bottom edge is included.
static Ref
FLayoutVerticallyX(RefArg rcvr, RefArg entries, RefArg index)
{
	TView* view = FailGetView(rcvr);
	long height = (short) ((unsigned short) view->viewBounds.bottom - (unsigned short) view->viewBounds.top);
	long slot = RINT(index);
	long count = Length(entries);
	Boolean allCollapsed = NOTNIL(GetProtoVariable(rcvr, RSSYMallcollapsed, nil));
	RefVar collapsedHeightRef(GetVariable(rcvr, RSSYMcollapsedheight, nil, false));
	long collapsedHeight = ISNIL(collapsedHeightRef) ? 0 : RINT(collapsedHeightRef);
	RefVar result(MakeArray(0));
	RefVar entry;
	long used = 0;
	while (used < height && slot < count)
	{
		entry = GetArraySlotRef(entries, slot);
		slot++;
		long h = RINT(GetProtoVariable(entry, RSSYMheight, nil));
		if (allCollapsed || NOTNIL(GetProtoVariable(entry, RSSYMcollapsed, nil)))
			h = collapsedHeight;
		used += h;
		AddArraySlot(result, entry);
	}
	return result;
}

/* -------------------------------------------------------------------------------
	A time down a view

	The Dates application's day view is a strip of the day: the top of it
	is midnight and the bottom is midnight again, so a y in the view is a
	time and a time is a y.  The ROM keeps the arithmetic with the meeting
	views it is for (0x001ca128, beside LayoutMeeting and TMeetingView);
	the two natives that reach it are here, in the view functions, which
	is where their callers look for them.

	Both snap the time to the nearest quarter of an hour, and both do it
	with the same expression - `t - ((t + 8) mod 15 - 8)` - which tips at
	*seven* minutes past the quarter rather than seven and a half: 7 comes
	back as 15 but 6 as 0.
------------------------------------------------------------------------------- */

// The whole day the view's height stands for (intl/Dates.h has the same
// constant; the ROM writes 1440 into each of the two natives).
const long kDayMinutes = 1440;

// The nearest quarter of an hour, as the two below round (the ROM writes
// this out in each of them).
static long
RoundToQuarterHour(long minutes)
{
	return minutes - ((minutes + 8) % 15 - 8);
}


// ROM 0x001ca128 TimeToPosition__FlN31
// How far down an `extent` of pixels the time is, `span` minutes being
// the whole of it and `base` the time at the top.
long
TimeToPosition(long time, long extent, long base, long span)
{
	return extent * (RoundToQuarterHour(time) - base) / span;
}


// ROM 0x001ca16c PositionToTime__FlN31
// And back: the time at that many pixels down.
long
PositionToTime(long position, long extent, long base, long span)
{
	return RoundToQuarterHour(span * position / extent + base);
}


// ROM 0x001ecc1c FPositionToTime__FRC6RefVarN21
// PositionToTime(view, y): the time y pixels down the view, the view's
// height being the whole day (1440 minutes from midnight).
static Ref
FPositionToTime(RefArg /*rcvr*/, RefArg context, RefArg position)
{
	long y = RINT(position);
	TView* view = FailGetView(context);
	long height = (short) ((unsigned short) view->viewBounds.bottom - (unsigned short) view->viewBounds.top);
	return MAKEINT(PositionToTime(y, height, 0, kDayMinutes));
}


// ROM 0x001ecc88 FTimeToPosition__FRC6RefVarN21
// TimeToPosition(view, minutes): and back again.
static Ref
FTimeToPosition(RefArg /*rcvr*/, RefArg context, RefArg time)
{
	TView* view = FailGetView(context);
	long height = (short) ((unsigned short) view->viewBounds.bottom - (unsigned short) view->viewBounds.top);
	return MAKEINT(TimeToPosition(RINT(time), height, 0, kDayMinutes));
}


// ROM 0x001eb4b0 FLayoutTableX
// :LayoutTable(spec, column, row): the cells of a table, as view
// templates, in a new array - as many of them as fit in the view.
//
// The spec frame says how big the table is - `tabAcross` columns and
// `tabDown` rows - and what a cell is made of: `tabProtos` the prototype,
// `tabValues` what goes in the slot `tabValueSlot` names, `tabWidths` and
// `tabHeights` the sizes, `indentx` and `indenty` where the first cell
// starts.  Each of the four may be an array, which is walked round and
// round for as long as the cells last - seven widths for the days of a
// week, one height for each row of it - or a single value every cell
// takes.  The walk does not start at the beginning: the proto and value
// arrays are entered at `across * row + column`, so a table laid out from
// the middle picks up the prototype the cell there should have.
//
// `tabSetup`, when the spec has one, is sent to it for every cell as
// :tabSetup(cell, column, row) - with the column and row *counted from
// one*, since both are the counter after it has been stepped on.
//
// The rows overlap by a pixel: a row's top is the last one's bottom less
// one, so a grid of framed cells draws its lines once rather than twice.
// The columns do not overlap - a cell's left is the last one's right.
//
// The laying out stops at the first cell that would cross the view's
// right edge and at the first row that would cross the bottom one, so a
// table too big for its view comes back cut short rather than clipped.
static Ref
FLayoutTableX(RefArg rcvr, RefArg spec, RefArg column, RefArg row)
{
	TView* view = FailGetView(rcvr);
	Rect bounds = view->viewBounds;
	long across = RINT(GetVariable(spec, RSSYMtabacross, nil, false));
	long down = RINT(GetVariable(spec, RSSYMtabdown, nil, false));
	if (down == 0 || across == 0)
		return NILREF;

	RefVar protos(GetVariable(spec, RSSYMtabprotos, nil, false));
	long protoCount = IsArray(protos) ? Length(protos) : 0;
	RefVar valueSlot(GetVariable(spec, RSSYMtabvalueslot, nil, false));
	RefVar values(GetVariable(spec, RSSYMtabvalues, nil, false));
	long valueCount = IsArray(values) ? Length(values) : 0;
	RefVar heights(GetVariable(spec, RSSYMtabheights, nil, false));
	long heightCount = IsArray(heights) ? Length(heights) : 0;
	RefVar widths(GetVariable(spec, RSSYMtabwidths, nil, false));
	long widthCount = IsArray(widths) ? Length(widths) : 0;

	RefVar result(MakeArray(0));
	RefVar cell;
	long firstRow = RINT(row);
	long protoIndex = protoCount != 0 ? (across * firstRow + RINT(column)) % protoCount : 0;
	long valueIndex = valueCount != 0 ? (across * firstRow + RINT(column)) % valueCount : 0;
	long heightIndex = heightCount != 0 ? firstRow % heightCount : 0;

	RefVar indentX(GetVariable(spec, RefVar(Intern((char*) "indentx")), nil, false));
	RefVar indentY(GetVariable(spec, RefVar(Intern((char*) "indenty")), nil, false));
	long y = ISNIL(indentY) ? 0 : RINT(indentY);

	long thisRow = firstRow;
	long nextRow = firstRow + 1;
	while (thisRow < down)
	{
		long thisColumn = RINT(column);
		long x = ISNIL(indentX) ? 0 : RINT(indentX);
		long widthIndex = widthCount != 0 ? thisColumn % widthCount : 0;

		Rect box;
		box.top = (short) y;
		if (heightCount == 0)
			box.bottom = (short) (RINT(heights) + y);
		else
		{
			box.bottom = (short) (RINT(GetArraySlotRef(heights, heightIndex)) + y);
			if (++heightIndex >= heightCount)
				heightIndex = 0;
		}
		y = box.bottom - 1;					// the next row starts on this one's last line
		if (y >= (short) (bounds.bottom - bounds.top))
			break;

		RefVar rowNumber(MAKEINT(nextRow));
		while (thisColumn++ < across)
		{
			box.left = (short) x;
			if (widthCount == 0)
				box.right = (short) (RINT(widths) + x);
			else
			{
				box.right = (short) (RINT(GetArraySlotRef(widths, widthIndex)) + x);
				if (++widthIndex >= widthCount)
					widthIndex = 0;
			}
			x = box.right;
			if (x > (short) (bounds.right - bounds.left))
				break;

			cell = AllocateFrame();
			if (protoCount == 0)
				SetFrameSlot(cell, RSSYM_proto, protos);
			else
			{
				SetFrameSlot(cell, RSSYM_proto, RefVar(GetArraySlotRef(protos, protoIndex)));
				if (++protoIndex >= protoCount)
					protoIndex = 0;
			}
			if (NOTNIL(valueSlot))
			{
				if (valueCount == 0)
					SetFrameSlot(cell, valueSlot, values);
				else
				{
					SetFrameSlot(cell, valueSlot, RefVar(GetArraySlotRef(values, valueIndex)));
					if (++valueIndex >= valueCount)
						valueIndex = 0;
				}
			}
			SetFrameSlot(cell, RSSYMviewbounds, RefVar(ToObject(box)));

			RefVar setup(GetVariable(spec, RSSYMtabsetup, nil, false));
			if (NOTNIL(setup))
			{
				RefVar args(MakeArray(3));
				SetArraySlot(args, 0, cell);
				SetArraySlot(args, 1, RefVar(MAKEINT(thisColumn)));
				SetArraySlot(args, 2, rowNumber);
				DoMessage(spec, RSSYMtabsetup, args);
			}
			AddArraySlot(result, cell);
		}
		thisRow = nextRow++;
	}
	return result;
}


// ROM 0x00191800 PolygonDescription__FRC6RefVar
// What a drawing is called when it has to be described in words: the
// ROM's " -sketch- " when it carries ink, " -shape- " when it does not.
static Ref
PolygonDescription(RefArg item)
{
	return NOTNIL(RefVar(GetProtoVariable(item, RSSYMink, nil))) ? Rinkname : Rshapename;
}


// ROM 0x001ed314 FExtractData__FRC6RefVarN31
// ExtractData(data, separator, maxLength): everything a note says, in one
// string - the line the Notes overview shows for it.  `data` is the
// note's children (paragraphs, sketches, shapes and whatever else has
// been dropped on it) and the answer is their text run together with the
// separator between the pieces, stopping at maxLength characters.
//
// The children are read in the order they sit on the page - sorted by
// viewBounds.top - but only when there are fewer than forty of them.
// Past that the sort is skipped and they come out in the order the note
// stored them: a long note is not worth sorting for one line of summary.
//
// The words come first, in one pass over the children, and the drawings
// in a second, so a note that begins with a sketch still reads as its
// text first.  A child that is the same object as the one before it is
// passed over.  Anything with a stationery of its own that is neither
// 'para nor 'poly says only "data".
//
// Carriage returns and tabs in the answer become spaces, so that the one
// line stays one line.
static Ref
FExtractData(RefArg /*rcvr*/, RefArg data, RefArg separator, RefArg maxLength)
{
	if (ISNIL(data) || Length(data) == 0)
		return MakeString("");

	long count = Length(data);
	long separatorLength = (Length(separator) - 2) / 2;
	long maxChars = RINT(maxLength);
	RefVar items;
	RefVar item;
	RefVar stationery;
	RefVar piece;
	RefVar previous;
	RefVar pieces(MakeArray(count * 2 - 1));
	long used = 0;						// characters in the answer so far
	long written = 0;					// pieces written
	long slot = 0;

	if (count < 40)
	{
		RefVar path(AllocateArray(RefVar(RSSYMpathexpr), 2));
		SetArraySlot(path, 0, RefVar(RSSYMviewbounds));
		SetArraySlot(path, 1, RefVar(RSSYMtop));
		items = Clone(data);
		SortArray(items, RefVar(RSSYM_3C), path);
	}
	else
		items = data;

	// the text
	for (long i = 0; i < count && used < maxChars; i++)
	{
		item = GetArraySlotRef(items, i);
		if ((Ref) item != (Ref) previous)
		{
			stationery = GetProtoVariable(item, RSSYMviewstationery, nil);
			if (EQRef(stationery, RSSYMpara) || ISNIL(stationery))
			{
				RefVar text(GetProtoVariable(item, RSSYMtext, nil));
				if (NOTNIL(text))
				{
					long room = maxChars - used;
					long length = Ustrlen((UniChar*) BinaryData(text));
					if (length > room)
						length = room;
					piece = ExtractRichStringFromParaSlots(text,
								RefVar(GetProtoVariable(item, RSSYMstyles, nil)), 0, (ULong) length);
					SetArraySlot(pieces, slot++, piece);
					TRichString rich(piece);
					used += rich.Length();
					if (++written < count)
					{
						SetArraySlot(pieces, slot++, separator);
						used += separatorLength;
					}
				}
			}
		}
		previous = item;
	}

	// and the drawings
	previous = NILREF;
	for (long i = 0; i < count && used < maxChars; i++)
	{
		item = GetArraySlotRef(items, i);
		if ((Ref) item != (Ref) previous)
		{
			stationery = GetProtoVariable(item, RSSYMviewstationery, nil);
			if (ISNIL(RefVar(GetProtoVariable(item, RSSYMink, nil))))
			{
				if (ISNIL(stationery) || EQRef(stationery, RSSYMpara))
					piece = NILREF;					// a paragraph: its text went in above
				else if (EQRef(stationery, RSSYMpoly))
					piece = PolygonDescription(item);
				else
					piece = Rdataname;				// "data", for anything else
			}
			else
				piece = PolygonDescription(item);
			if (NOTNIL(piece))
			{
				SetArraySlot(pieces, slot++, piece);
				used += (Length(piece) - 2) / 2;
				if (++written < count)
				{
					SetArraySlot(pieces, slot++, separator);
					used += separatorLength;
				}
			}
		}
		previous = item;
	}

	RefVar result(Stringer(pieces));
	TRichString rich(result);
	long length = rich.Length();
	UniChar* text = (UniChar*) BinaryData(result);
	for (long i = 0; i < length; i++)
	{
		if (text[i] == 0x0d || text[i] == 0x09)		// a return or a tab
			text[i] = ' ';
	}
	return result;
}


// ROM 0x001f0f10 FGetHiliteOffsets__FRC6RefVar
// GetHiliteOffsets(): where the current selection is - the `offset` of
// the `hilites` of whichever view owns them - or nil when nothing is
// selected anywhere.
static Ref
FGetHiliteOffsets(RefArg /*rcvr*/)
{
	TView* hiliter = gRootView->fHiliter;
	if (hiliter == nil)
		return NILREF;
	return hiliter->GetValue(RSSYMhilites, RSSYMoffset);
}


// ROM 0x000e3490 ToGlobalCoordinates__FRC6RefVarPsN32
// A point (or two) in the view's own coordinates moved into the screen's:
// the x's take the view's left edge, the y's its top.  Any of the four
// may be nil.
void
ToGlobalCoordinates(RefArg context, short* x, short* y, short* x2, short* y2)
{
	TView* view = FailGetView(context);
	Rect bounds = view->viewBounds;
	if (x != nil)
		*x = (short) (*x + bounds.left);
	if (x2 != nil)
		*x2 = (short) (*x2 + bounds.left);
	if (y != nil)
		*y = (short) (*y + bounds.top);
	if (y2 != nil)
		*y2 = (short) (*y2 + bounds.top);
}


// ROM 0x0003e85c FCopyBits
// :CopyBits(picture, x, y, mode): a bitmap or picture frame drawn with
// its top left at that point of the view.  The box handed to DrawPicture
// is the point alone, so the picture is drawn at its own size.
static Ref
FCopyBits(RefArg rcvr, RefArg picture, RefArg x, RefArg y, RefArg mode)
{
	if (!ISINT(x))
		ThrowMsg("param not an integer");
	if (!ISINT(y))
		ThrowMsg("param not an integer");
	short px = (short) RINT(x);
	short py = (short) RINT(y);
	ToGlobalCoordinates(rcvr, &px, &py, nil, nil);
	Rect box;
	box.top = py;
	box.left = px;
	box.bottom = py;
	box.right = px;
	DrawPicture(picture, box, 0, ISNIL(mode) ? 0 : RINT(mode));
	return NILREF;
}


// ROM 0x001edcbc FDoDrawing
// :DoDrawing(message, args): the view's own message sent with the port
// set to the view's visible region, so that a script may draw outside a
// viewDrawScript.  The caret is taken down first when it stands over the
// view, and the port's clipping and the caret are put back however the
// message ends.  A view that is not visible all the way up is not drawn
// in at all, and the message is not sent.
static Ref
FDoDrawing(RefArg rcvr, RefArg message, RefArg args)
{
	TView* view = FailGetView(rcvr);
	RefVar result;
	if (view->VisibleDeep())
	{
		TRegion vis(view->SetupVisRgn());
		TRegionVar saved(vis);
		Rect caret;
		gRootView->GetCaretRect(&caret);
		Rect bounds;
		view->OuterBounds(&bounds);
		Boolean overCaret = Overlaps(&caret, &bounds);
		unwind_protect
		{
			if (overCaret)
				gRootView->HideCaret();
			result = DoMessage(rcvr, message, args);
		}
		on_unwind
		{
			GrafPort* port;
			GetPort(&port);
			CopyRgn(saved, port->visRgn);
			if (overCaret)
				gRootView->ShowCaret();
		}
		end_unwind;
	}
	return result;
}

// ROM 0x0003ead4 FDrawXBitmap
// :DrawXBitmap(bounds, picture, index, mode): one image out of a strip.
// The picture holds several of them side by side, all the width of the
// bounds; `index` says which, counting from 0.  The bounds are the
// view's own, so they are moved to the screen first, and the piece of
// the picture to take is the bounds moved to that cell of the strip.
// Nothing is drawn for a negative index or a context with no view.
static Ref
FDrawXBitmap(RefArg rcvr, RefArg bounds, RefArg picture, RefArg index, RefArg mode)
{
	TView* view = GetView(rcvr);
	long which = RINT(index);
	if (which < 0 || view == nil)
		return NILREF;
	TPixelObj pix;
	unwind_protect
	{
		Rect dst;
		if (!FromObject(bounds, dst))
			Throw((ExceptionName) "evt.ex.graf", (void*) -8801, nil);	// (the graf error DrawShape.cpp names kGrafErrBadBounds)
		OffsetRect(&dst, view->viewBounds.left, view->viewBounds.top);
		pix.Init(picture);
		Rect src = dst;
		const Rect& pixels = pix.Pixels()->bounds;
		OffsetRect(&src, which * (dst.right - dst.left) + (pixels.left - dst.left),
				   pixels.top - dst.top);
		GrafPort* port;
		GetPort(&port);
		CopyBits(pix.Pixels(), &port->portBits, &src, &dst, RINT(mode), nil);
	}
	on_unwind
	{ }
	end_unwind;
	return NILREF;
}

// ROM 0x001ee9c8 FCaretRelativeToVisibleRect
// CaretRelativeToVisibleRect(bounds) - which way the caret has gone out
// of the bounds frame: nil when it has not gone out at all, 'inBox when
// it is in, and 'top, 'bottom, 'left or 'right for the side it is past.
// A scrolling view asks this to work out where it has to scroll to.
static Ref
FCaretRelativeToVisibleRect(RefArg rcvr, RefArg bounds)
{
	TParagraphView* para = FailGetParagraphView(rcvr);
	Rect visible;
	FromObject(bounds, visible);
	switch (para->CaretRelativeToVisibleRect(visible))
	{
	case 1:		return RSSYMinbox;
	case 2:		return RSSYMbottom;
	case 3:		return RSSYMtop;
	case 4:		return RSSYMleft;
	case 5:		return RSSYMright;
	}
	return NILREF;
}


// ROM 0x001f11f4 FDropHilites
// DropHilites() - the selection taken off, when it belongs to this view
// or to something inside it.  The root view knows which view owns the
// hilites; this walks up from it looking for the receiver, and only if it
// finds it does anything happen, so a view cannot drop somebody else's
// selection.  The hilites go through the undoable aeRemoveAllHilites
// command, and the caret goes with them.  ==> nil.
static Ref
FDropHilites(RefArg rcvr)
{
	TView* view = GetView(rcvr);
	TView* hiliter = gRootView->fHiliter;
	if (view == nil || hiliter == nil)
		return NILREF;
	for (TView* walk = hiliter; walk != view; walk = walk->fParent)
		if (walk == (TView*) gRootView)
			return NILREF;				// the selection is somewhere else
	RefVar cmd(MakeCommand(aeRemoveAllHilites, hiliter, 0x8000000));
	gApplication->DispatchCommand(cmd);
	gRootView->SetKeyView(nil, 0, 0, false);
	return NILREF;
}


// ROM 0x001f1014 FGetHilitedTextItems__FRC6RefVar
// GetHilitedTextItems() - the text of everything that is selected, as an
// array of strings.  The root view knows which view owns the hilites; it
// is asked for them as a list of [context, start, end] and each range is
// turned back into text by the view it came from, so a selection that
// spans several paragraphs answers one string per paragraph.
static Ref
FGetHilitedTextItems(RefArg /*rcvr*/)
{
	TView* hiliter = gRootView->fHiliter;
	if (hiliter == nil)
		return NILREF;
	RefVar hilites(hiliter->GetValue(RSSYMhilites, RSSYMoffset));
	if (ISNIL(hilites))
		return NILREF;
	long count = Length(hilites);
	if (count <= 0)
		return NILREF;
	RefVar items(MakeArray(0));
	for (long i = 0; i < count; i++)
	{
		RefVar entry(GetArraySlotRef(hilites, i));
		RefVar context(GetArraySlotRef(entry, 0));
		RefVar end(GetArraySlotRef(entry, 2));
		RefVar start(GetArraySlotRef(entry, 1));
		TView* view = FailGetView(context);
		RefVar text(view->GetRangeText(RINT(start), RINT(end) - RINT(start)));
		if (NOTNIL(text))
			AddArraySlot(items, text);
	}
	return items;
}


void
RegisterViewNatives(void)
{
	RegisterNativeFunction("FLayoutVerticallyX", (void*) FLayoutVerticallyX, 2);
	RegisterNativeFunction("FLayoutTableX", (void*) FLayoutTableX, 3);
	RegisterNativeFunction("FPositionToTime__FRC6RefVarN21", (void*) FPositionToTime, 2);
	RegisterNativeFunction("FTimeToPosition__FRC6RefVarN21", (void*) FTimeToPosition, 2);
	RegisterNativeFunction("FGetHiliteOffsets__FRC6RefVar", (void*) FGetHiliteOffsets, 0);
	RegisterNativeFunction("FExtractData__FRC6RefVarN31", (void*) FExtractData, 3);
	RegisterNativeFunction("FCopyBits", (void*) FCopyBits, 4);
	RegisterNativeFunction("FDrawXBitmap", (void*) FDrawXBitmap, 4);
	RegisterNativeFunction("FDoDrawing", (void*) FDoDrawing, 2);
	RegisterNativeFunction("FModalState", (void*) FModalState, 0);
	RegisterNativeFunction("TableLookup", (void*) FTableLookup, 2);
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
	RegisterNativeFunction("FPositionCaret", (void*) FPositionCaret, 3);
	RegisterNativeFunction("FSetHiliteNoUpdateX", (void*) FSetHiliteNoUpdateX, 3);
	RegisterNativeFunction("FSetHiliteX", (void*) FSetHiliteX, 3);
	RegisterNativeFunction("FTrackButtonX", (void*) FTrackButtonX, 1);
	RegisterNativeFunction("FHiliteX", (void*) FHiliteX, 1);
	RegisterNativeFunction("FCaretRelativeToVisibleRect", (void*) FCaretRelativeToVisibleRect, 1);
	RegisterNativeFunction("FDropHilites", (void*) FDropHilites, 0);
	RegisterNativeFunction("FGetHilitedTextItems__FRC6RefVar", (void*) FGetHilitedTextItems, 0);
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
	RegisterNativeFunction("FDragAndDrop", (void*) FDragAndDrop, 5);
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
		{ "Drag", (void*) FDragX, 2 }, { "DragAndDrop", (void*) FDragAndDrop, 5 }, { "delete", (void*) FDeleteX, 2 }, { "Effect", (void*) FEffectX, 5 },
		{ "SlideEffect", (void*) FSlideEffectX, 5 }, { "RevealEffect", (void*) FRevealEffectX, 5 },
		{ "DrawShape", (void*) FDrawShape, 2 }, { "AddUndoAction", (void*) FAddUndoAction, 2 }, { "SetupIdle", (void*) FSetupIdleX, 1 }, { "SetPopup", (void*) FSetPopupX, 0 }, { "DoPopup", (void*) FDoPopup, 4 },
		{ "TrackHilite", (void*) FTrackHiliteX, 1 }, { "TrackButton", (void*) FTrackButtonX, 1 },
		{ "hilite", (void*) FHiliteX, 1 }, { "HiliteUnique", (void*) FHiliteUniqueX, 1 },
		{ "LayoutColumn", (void*) FLayoutVerticallyX, 2 }, { "LayoutTable", (void*) FLayoutTableX, 3 },
		{ "CopyBits", (void*) FCopyBits, 4 }, { "DoDrawing", (void*) FDoDrawing, 2 },
		{ "DrawXBitmap", (void*) FDrawXBitmap, 4 },
		{ nil, nil, 0 } };
	RefVar methods(AllocateFrame());
	for (long i = 0; kMethods[i].fName != nil; i++)
		SetFrameSlot(methods, RefVar(Intern((char*) kMethods[i].fName)), RefVar(MakeCFunction(kMethods[i].fFn, kMethods[i].fArgs, nil)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Open")), RefVar(CompileScriptFunction(kOpenSource)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Toggle")), RefVar(CompileScriptFunction(kToggleSource)));
	return methods;
}
