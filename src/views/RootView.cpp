/*
	File:		views/RootView.cpp

	Contains:	TRootView: the update regions and their redraw, the views
				it keeps track of.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "RootView.h"
#include "Keyboard.h"
#include "Bits.h"
#include "ParagraphView.h"
#include "Pictures.h"
#include "Regions.h"
#include "Unicode.h"
#include "Locale.h"
#include "Interpreter.h"
#include "Commands.h"
#include "Application.h"
#include "Rects.h"
#include "Ports.h"
#include "Draw.h"
#include "Screen.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "DynamicArray.h"
#include "NewtonExceptions.h"

Boolean	gNewtIsAliveAndWell = true;			// ROM 0x0c102604 gNewtIsAliveAndWell (host: no boot splash)

const long kUpdateRegionCount = 3;


// ROM 0x001b3d04 ClassID__9TRootViewCFv
long
TRootView::ClassID(void) const
{
	return clRootView;
}


// ROM 0x001b3d0c DerivedFrom__9TRootViewCFl
Boolean
TRootView::DerivedFrom(long id) const
{
	return id == clRootView || TView::DerivedFrom(id);
}


// ROM 0x001b3d40 Constructor__9TRootViewFRC6RefVar
// The root view from its template: the update regions, the idler list,
// the shared empty view list, the selection stack and keyboard arrays,
// the context (a clone of Rrootcontext protoed to the template) built as
// a view whose parent is itself, and the whole screen dirtied.  NOT YET
// RECONSTRUCTED: the recognition's InitCorrection, the keyboard gestalt.
void
TRootView::Constructor(RefArg templ)
{
	fHiliter = nil;
	fUpdateRegions = new TUpdateRegion[kUpdateRegionCount];
	for (long i = 0; i < kUpdateRegionCount; i++)
	{
		fUpdateRegions[i].fFiller = nil;
		SetEmptyRgn(fUpdateRegions[i].fRegion);
	}
	SetEmptyRect(&fDirtyScreen);
	fIdlers = new CDynamicArray(sizeof(IdlerRecord), 4);
	fChildrenHighWater = 0;
	fIdlersHighWater = 0;
	fIdlingViews = nil;
	fPopup = nil;
	fPassthruKeyboard = false;
	fCaretView = nil;
	fCaretOffset = 0;
	fCaretLength = 0;
	fPreserveHilites = false;
	fCaretBits = new TBits;
	Rect caretBox;
	SetRect(&caretBox, 0, 0, 12, 11);
	fCaretBits->Constructor(caretBox);
	fCaretShowing = false;
	fCaretPoint.h = -0x8000;
	fCaretPoint.v = 0;
	fCaretDrawnView = nil;
	fCaretHidden = 0;
	fDefaultButton = nil;
	fCaretSlip = nil;
	fModalView = nil;
	if (TView::gEmptyViewList == nil)
		TView::gEmptyViewList = TViewList::Make();
	fSelectionStack = AllocateArray(RSSYMarray, 0);
	fKeyboards = AllocateArray(RSSYMarray, 0);
	RefVar context(Clone(RefVar(Rrootcontext)));
	SetFrameSlot(context, RSSYM_proto, templ);
	TView::Constructor(context, this);
	Dirty(nil);
}


// ROM 0x001b3eb4 __dt__9TRootViewFv
TRootView::~TRootView()
{
	delete fIdlers;
	delete[] fUpdateRegions;
}


// ROM 0x001b56c8 RealDoCommand__9TRootViewFRC6RefVar
// aeKeyboardConnected: the parameter says whether a keyboard is
// connected - the hard key map cleared when it is, the caret's view
// asked to give it up when not; the popup synced, the root dirtied.
// NOT YET RECONSTRUCTED: the root view's other commands (the hiliter,
// the clipboard, ...).
Boolean
TRootView::RealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == aeKeyboardConnected)
	{
		gKeyboardConnected = CommandParameter(cmd) != 0;
		if (!gKeyboardConnected)
			CheckForCaretRemoval();
		else
			ClearHardKeymap();
		if (fPopup != nil)
			fPopup->Sync();
		Dirty(nil);
		return true;
	}
	return TView::RealDoCommand(cmd);
}


// ROM 0x001b6fac KeyboardConnected__9TRootViewFv
// A hardware keyboard, or a keyboard passed through a soft one.
Boolean
TRootView::KeyboardConnected(void)
{
	return gKeyboardConnected || fPassthruKeyboard;
}


// ROM 0x001b6fd4 CommandKeyboardConnected__9TRootViewFv
Boolean
TRootView::CommandKeyboardConnected(void)
{
	return gKeyboardConnected;
}


// ROM 0x001b6fe4 KeyboardActive__9TRootViewFv
// A keyboard connected, or an on-screen keyboard registered as active
// (flags bit 2).
Boolean
TRootView::KeyboardActive(void)
{
	if (KeyboardConnected())
		return true;
	if (NOTNIL(fKeyboards))
	{
		long count = Length(fKeyboards) / 2;
		for (long i = 0; i < count; i++)
			if (RINT(GetArraySlotRef(fKeyboards, i * 2 + 1)) & 4)
				return true;
	}
	return false;
}


// ROM 0x001b6df4 ConnectPassthruKeyboard__9TRootViewFUc
// A keyboard connected (or not) through a soft keyboard; the caret's
// view is asked whether it keeps the caret when it goes (DerivedFrom
// clEditView: the ROM's virtual call, NOT YET).
void
TRootView::ConnectPassthruKeyboard(Boolean connected)
{
	fPassthruKeyboard = connected;
	if (!connected)
		CheckForCaretRemoval();
}


// ROM 0x001b6ad0 CheckForCaretRemoval__9TRootViewFv
// NOT YET RECONSTRUCTED: the ROM asks the caret view DerivedFrom(clEditView).
void
TRootView::CheckForCaretRemoval(void)
{
	if (fCaretView != nil)
		fCaretView->DerivedFrom(clEditView);
}


// ROM 0x001b6e04 HandleKeyIn__9TRootViewFUlUcP5TView
// A modifier key (shift, caps lock, option, control) on a soft keyboard:
// the other registered keyboards that show the modifiers (flags bit 0)
// are dirtied - the first one found.
void
TRootView::HandleKeyIn(ULong keyCode, Boolean /*isDown*/, TView* keyboard)
{
	RefVar context;
	if (keyboard != nil)
		context = keyboard->fContext;
	if (keyCode != kShiftKey && keyCode != kCapsLockKey && keyCode != kOptionKey && keyCode != kControlKey)
		return;
	long count = Length(fKeyboards) / 2;
	for (long i = 0; i < count; i++)
	{
		RefVar other(GetArraySlotRef(fKeyboards, i * 2));
		if ((RINT(GetArraySlotRef(fKeyboards, i * 2 + 1)) & 1) && !EQRef(other, context))
		{
			TView* view = GetView(other);
			if (view != nil)
			{
				view->Dirty(nil);
				return;
			}
		}
	}
}


// ROM 0x001b8044 RemoveAllViews__9TRootViewFv
// NOT YET RECONSTRUCTED beyond the children: the ROM forgets the key view,
// the popup and the clipboards first.
void
TRootView::RemoveAllViews(void)
{
	fPopup = nil;
	fPassthruKeyboard = false;
	fCaretView = nil;
	fCaretOffset = 0;
	fCaretLength = 0;
	fPreserveHilites = false;
	fCaretBits = new TBits;
	Rect caretBox;
	SetRect(&caretBox, 0, 0, 12, 11);
	fCaretBits->Constructor(caretBox);
	fCaretShowing = false;
	fCaretPoint.h = -0x8000;
	fCaretPoint.v = 0;
	fCaretDrawnView = nil;
	fCaretHidden = 0;
	fDefaultButton = nil;
	fCaretSlip = nil;
	fHiliter = nil;
	TView::RemoveAllViews();
}


// ROM 0x001b4838 PostDraw__9TRootViewFR5TRect
// NOT YET RECONSTRUCTED: the ink in the rect redrawn (TController::
// UpdateInk) and the stroke groups updated.
void
TRootView::PostDraw(Rect& /*bounds*/)
{ }


// ROM 0x001b4844 RealDraw__9TRootViewFR5TRect
// The boot splash (the screen painted black, the logo, the version) until
// the system is up; nothing after: the root's format fills the screen.
// NOT YET RECONSTRUCTED: the splash's picture and text (the screen is
// painted black).
void
TRootView::RealDraw(Rect& /*bounds*/)
{
	if (gNewtIsAliveAndWell)
		return;
	Rect screen;
	SetRect(&screen, 0, 0, ScreenWidth(), ScreenHeight());
	PaintRect(&screen);
}


// host: the screen is the current port's rectangle
long
TRootView::ScreenWidth(void) const
{
	GrafPort* port = GetCurrentPort();
	return port->portRect.right - port->portRect.left;
}


long
TRootView::ScreenHeight(void) const
{
	GrafPort* port = GetCurrentPort();
	return port->portRect.bottom - port->portRect.top;
}


// ROM 0x001b52e4 Dirty__9TRootViewFPC5TRect
// The rect (the whole view for nil) into the update region, with the root
// as its filler.
void
TRootView::Dirty(const Rect* rect)
{
	if (rect == nil)
		rect = &viewBounds;
	TRectangularRegion rgn(*rect);
	Invalidate(rgn, nil);
}


// ROM 0x001b44d4 GetCommonParent__9TRootViewFP5TViewT1
// The nearest view both are in (one of them when it contains the other),
// nil when none: the root view's parent is taken as nil.
TView*
TRootView::GetCommonParent(TView* a, TView* b)
{
	for ( ; a != nil; a = (a->fParent == a) ? nil : a->fParent)
		for (TView* view = b; view != nil; view = (view->fParent == view) ? nil : view->fParent)
			if (view == a)
				return a;
	return nil;
}


// ROM 0x001b4524 Invalidate__9TRootViewFC11TBaseRegionP5TView
// The region joins the update regions under the filler (the view that
// paints its background; nil or the root: everything merges into the
// first slot under the root).  A slot whose filler contains the filler,
// or is contained by it, takes the region under the outer one; a free
// slot takes it as it is; otherwise the slot whose common parent with
// the filler is below the root takes it under that parent, absorbing the
// other slots with the same common parent; failing all, everything
// merges under the root.
void
TRootView::Invalidate(RgnHandle rgn, TView* filler)
{
	TUpdateRegion* slots = fUpdateRegions;
	if (filler == nil || filler == this)
	{
		slots[0].fFiller = this;
		UnionRgn(slots[0].fRegion, rgn, slots[0].fRegion);
		for (long i = 1; i < kUpdateRegionCount; i++)
			if (slots[i].fFiller != nil)
			{
				UnionRgn(slots[0].fRegion, slots[i].fRegion, slots[0].fRegion);
				slots[i].fFiller = nil;
				SetEmptyRgn(slots[i].fRegion);
			}
		return;
	}
	TView* common[kUpdateRegionCount] = { nil, nil, nil };
	for (long i = 0; i < kUpdateRegionCount; i++)
	{
		TView* current = slots[i].fFiller;
		if (current == nil)
		{
			slots[i].fFiller = filler;
			UnionRgn(slots[i].fRegion, rgn, slots[i].fRegion);
			return;
		}
		if (current == this)
		{
			UnionRgn(slots[i].fRegion, rgn, slots[i].fRegion);
			return;
		}
		common[i] = GetCommonParent(current, filler);
		if (common[i] == current || common[i] == filler)
		{
			slots[i].fFiller = common[i];
			UnionRgn(slots[i].fRegion, rgn, slots[i].fRegion);
			return;
		}
	}
	for (long i = 0; i < kUpdateRegionCount; i++)
	{
		if (common[i] == this)
			continue;
		slots[i].fFiller = common[i];
		UnionRgn(slots[i].fRegion, rgn, slots[i].fRegion);
		for (long j = i + 1; j < kUpdateRegionCount && common[j] != nil; j++)
		{
			if (common[j] == common[i])
			{
				UnionRgn(slots[i].fRegion, slots[j].fRegion, slots[i].fRegion);
				for (long k = j; k < kUpdateRegionCount - 1; k++)
				{
					common[k] = common[k + 1];
					slots[k].fFiller = slots[k + 1].fFiller;
					slots[k + 1].fRegion.Swap(slots[k].fRegion.Swap(slots[k + 1].fRegion));
				}
				common[kUpdateRegionCount - 1] = nil;
				slots[kUpdateRegionCount - 1].fFiller = nil;
				SetEmptyRgn(slots[kUpdateRegionCount - 1].fRegion);
				j--;
			}
		}
		return;
	}
	slots[0].fFiller = this;
	UnionRgn(slots[0].fRegion, rgn, slots[0].fRegion);
	for (long i = 1; i < kUpdateRegionCount; i++)
	{
		UnionRgn(slots[0].fRegion, slots[i].fRegion, slots[0].fRegion);
		slots[i].fFiller = nil;
		SetEmptyRgn(slots[i].fRegion);
	}
}


// ROM 0x001b47f8 Validate__9TRootViewFC11TBaseRegion
// The region needs no update after all.
void
TRootView::Validate(RgnHandle rgn)
{
	for (long i = 0; i < kUpdateRegionCount; i++)
		DiffRgn(fUpdateRegions[i].fRegion, rgn, fUpdateRegions[i].fRegion);
}


// ROM 0x001b43d8 SmartInvalidate__9TRootViewFRC5TRect
// The rect dirtied in the deepest visible window (a child of the root
// view, then of that, ...) whose bounds enclose it, cut to the port.
void
TRootView::SmartInvalidate(const Rect& rect)
{
	TView* view = this;
	for ( ; ; )
	{
		TView* inner = nil;
		TBackwardViewListLoop loop(view->fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
		{
			if ((child->fFlags & vVisible) == 0)
				continue;
			Rect outer;
			child->OuterBounds(&outer);
			Rect overlap;
			if (!SectRect(&rect, &outer, &overlap))
				continue;
			if (rect.left >= child->viewBounds.left && rect.top >= child->viewBounds.top
			 && rect.right <= child->viewBounds.right && rect.bottom <= child->viewBounds.bottom)
				inner = child;
			break;
		}
		if (inner == nil)
			break;
		view = inner;
	}
	GrafPort* port;
	GetPort(&port);
	Rect dirty;
	SectRect(&rect, &port->portRect, &dirty);
	view->Dirty(&dirty);
}


// ROM 0x001b4868 SmartScreenDirty__9TRootViewFRC5TRect
// The rect joins what the screen must be shown again (an unset rect,
// top -32768, takes the rect as it is).
void
TRootView::SmartScreenDirty(const Rect& rect)
{
	if (fDirtyScreen.top == -32768)
		fDirtyScreen = rect;
	if (EmptyRect(&rect))
		return;
	if (EmptyRect(&fDirtyScreen))
		fDirtyScreen = rect;
	else
	{
		if (rect.top < fDirtyScreen.top)
			fDirtyScreen.top = rect.top;
		if (rect.left < fDirtyScreen.left)
			fDirtyScreen.left = rect.left;
		if (rect.bottom > fDirtyScreen.bottom)
			fDirtyScreen.bottom = rect.bottom;
		if (rect.right > fDirtyScreen.right)
			fDirtyScreen.right = rect.right;
	}
}


// ROM 0x001b4870 NeedsUpdate__9TRootViewFv
// Whether Update has anything to do: a dirty screen rect, the caret
// wrong, the default button or caret slip changed, or an update region.
Boolean
TRootView::NeedsUpdate(void)
{
	if (!EmptyRect(&fDirtyScreen))
		return true;
	if (!CaretValid(nil))
		return true;
	TView* button;
	TView* slip;
	FindDefaultButtonAndCaretSlip(fCaretView, &button, &slip);
	if (fCaretSlip != slip || fDefaultButton != button)
		return true;
	for (long i = 0; i < kUpdateRegionCount; i++)
		if (fUpdateRegions[i].fFiller != nil)
			return true;
	return false;
}


// ROM 0x001b4914 Update__9TRootViewFP5TRect
// The screen brought up to date: the rectangle (when given) invalidated
// first; the caret checked (CaretValid) - it is taken off the screen
// when it is wrong or a dirty region covers it - and the default button
// and caret slip found; then, under one drawing bracket, the dirty
// screen rectangle shown (SmartScreenDirty's) and each update region
// less the port's visible region (the ROM: clipRgn) painted by its
// filler; the caret drawn again at its point when it was taken away.
void
TRootView::Update(Rect* rect)
{
	if (rect != nil)
	{
		TRectangularRegion rgn(*rect);
		Invalidate(rgn, nil);
	}
	if (!NeedsUpdate())
		return;
	Point caretPt;
	Boolean caretInvalid = !CaretValid(&caretPt);
	UpdateDefaultButtonAndCaretSlip();
	if (!gSlowMotion)
		StartDrawing(nil, nil);
	if (!caretInvalid && fCaretShowing)
	{
		// the caret is where it should be, unless a dirty region covers it
		Rect caretRect;
		GetCaretRect(&caretRect);
		for (long i = 0; i < kUpdateRegionCount; i++)
			if (fUpdateRegions[i].fFiller != nil && RectInRgn(&caretRect, fUpdateRegions[i].fRegion))
			{
				caretInvalid = true;
				break;
			}
	}
	if (fCaretShowing && caretInvalid)
		RestoreBitsUnderCaret();
	if (!EmptyRect(&fDirtyScreen))
	{
		StartDrawing(nil, &fDirtyScreen);
		StopDrawing(nil, &fDirtyScreen);
	}
	SetEmptyRect(&fDirtyScreen);
	TRegionVar pending;
	for (long i = 0; i < kUpdateRegionCount; i++)
	{
		TUpdateRegion* slot = &fUpdateRegions[i];
		if (slot->fFiller == nil)
			continue;
		GrafPort* port;
		GetPort(&port);
		DiffRgn(slot->fRegion, port->visRgn, pending);
		RgnHandle dirty = slot->fRegion.Swap(pending.StealRegion());
		TView* filler = slot->fFiller;
		if (EmptyRgn(slot->fRegion))
			slot->fFiller = nil;
		TView::Update(dirty, filler);
		DisposeCachedRgn(dirty);
		pending.Take(NewCachedRgn());
	}
	if (caretInvalid && CaretEnabled())
		DrawCaret(caretPt);
	if (!gSlowMotion)
		StopDrawing(nil, nil);
}


// ROM 0x001b42e4 ForgetAboutView__9TRootViewFP5TView
// A view is going: the pointers to it are dropped (the hiliter, the caret
// view, the popup, the caret slip or default button, the modal view), its
// idlers removed (when it has the hint), a filler that was it becomes its
// parent.  NOT YET RECONSTRUCTED: its keyboard unregistered, the modal
// dialog exited.
void
TRootView::ForgetAboutView(TView* view)
{
	if (fHiliter == view)
		fHiliter = nil;
	if (view->fFlags & vHasIdlerHint)
		RemoveAllIdlers(view);
	if (fCaretView == view)
		CaretViewGone();
	if (fPopup == view)
		SetPopup(view, false);
	if (fCaretSlip == view)
		fCaretSlip = nil;
	else if (fDefaultButton == view)
		fDefaultButton = nil;
	for (long i = 0; i < kUpdateRegionCount; i++)
		if (fUpdateRegions[i].fFiller == view)
			fUpdateRegions[i].fFiller = view->fParent;
	if (fModalView == view)
		fModalView = nil;
}


/*------------------------------------------------------------------------------
	T h e   k e y   v i e w   a n d   t h e   c a r e t
------------------------------------------------------------------------------*/

// the caret's rectangle from its point: 12 wide from 5 left of the
// point, 11 down from it
// ROM 0x001b7210 CaretPointToRect__FR6TPointP5TRect
static void
CaretPointToRect(const Point& pt, Rect* rect)
{
	if (pt.h == -0x8000)
	{
		SetEmptyRect(rect);
		return;
	}
	rect->left = pt.h - 5;
	rect->right = rect->left + 12;
	rect->top = pt.v;
	rect->bottom = rect->top + 11;
}


// the caret's own no-place point
static const short kNoCaret = -0x8000;

// the old key view is usable: there and not being deleted
static inline Boolean
KeyViewUsable(TView* view)
{
	return view != nil && (view->fFlags & vIsBeingDeleted) != vIsBeingDeleted;
}


// ROM 0x001b5f24 DoAutoShift__FP14TParagraphViewl
// The soft keyboards' shift set for a paragraph's caret: down at the
// start of the text or after white space, up otherwise (KeyIn(shift) when
// that changes it); ==> the character KeyIn answered.
static UniChar
DoAutoShift(TParagraphView* view, long offset)
{
	Boolean shifted = KeyDown(kShiftKey, false);
	Boolean wantShift = offset == 0;
	if (offset != 0)
	{
		RefVar text(view->Text());
		const UniChar* chars = (const UniChar*) BinaryData(text);
		if (IsWhiteSpace(chars[offset - 1]))
			wantShift = true;
	}
	if (wantShift != shifted)
		return KeyIn(kShiftKey, wantShift, nil);
	return shifted;
}


// ROM 0x001b608c SetKeyView__9TRootViewFP5TViewlT2Uc
// The key view set with a caret offset and selection length: a usable
// old key view (there, not being deleted) that is not the new one has
// its selection (GetSelection) pushed on the selection stack; the new
// view, when there is one, has the offset and length fixed by its
// SetCaretOffset (and with flushWord an old paragraph's word at the
// caret is flushed first); then CommonSetKeyView.
void
TRootView::SetKeyView(TView* view, long offset, long length, Boolean flushWord)
{
	TView* old = fCaretView;
	Boolean usable = KeyViewUsable(old);
	if (usable && old != view)
		PushSelection(old, RefVar(old->GetSelection()));
	if (view != nil)
	{
		if (flushWord && usable && old->DerivedFrom(clParagraphView))
			((TParagraphView*) old)->FlushWordAtCaret();
		view->SetCaretOffset(&offset, &length);
	}
	CommonSetKeyView(view, offset, length);
}


// ROM 0x001b5fbc SetKeyViewSelection__9TRootViewFP5TViewRC6RefVarUc
// The key view set from a caret info frame ({offset, length} for a
// paragraph; the view's SetSelection reads it): with pushOld a usable
// old key view that is not the new one has its selection pushed; nil
// clears the key view.
void
TRootView::SetKeyViewSelection(TView* view, RefArg selection, Boolean pushOld)
{
	TView* old = fCaretView;
	if (pushOld && KeyViewUsable(old) && old != view)
		PushSelection(old, RefVar(old->GetSelection()));
	long offset = 0;
	long length = 0;
	if (view != nil)
		view->SetSelection(selection, &offset, &length);
	CommonSetKeyView(view, offset, length);
}


// ROM 0x001b6174 CommonSetKeyView__9TRootViewFP5TViewlT2
// The key view, offset and length stored.  When the view changes: unless
// hilites are being preserved, a usable old view is told
// ActivateSelection(false) - except when old and new are paragraphs of
// the same hilite (edit) view, or one is the other's hilite view; the
// new view is told ActivateSelection(true).  The hiliter becomes the
// view (a paragraph's hilite view when it has one) for a selection, nil
// for none.  Without a keyboard connected and without a selection, the
// soft keyboards' shift follows the view: a vCapsRequired edit view
// presses it, a vCapsRequired paragraph gets DoAutoShift, another
// paragraph releases it.  The registered keyboards that listen (flags
// bit 1) get viewCaretChangedScript([context, offset, length]) (an
// array of nils for no view).
void
TRootView::CommonSetKeyView(TView* view, long offset, long length)
{
	TView* old = fCaretView;
	fCaretView = view;
	fCaretOffset = offset;
	fCaretLength = length;
	if (view != old)
	{
		if (!fPreserveHilites && KeyViewUsable(old))
		{
			Boolean deactivate = true;
			if (view != nil)
			{
				Boolean viewIsPara = view->DerivedFrom(clParagraphView);
				Boolean oldIsPara = old->DerivedFrom(clParagraphView);
				if (viewIsPara && oldIsPara)
				{
					TView* h1 = ((TDataView*) old)->GetHiliteView();
					TView* h2 = ((TDataView*) view)->GetHiliteView();
					if (h1 != nil && h2 != nil && h1 == h2)
						deactivate = false;
				}
				if (deactivate && viewIsPara && ((TDataView*) view)->GetHiliteView() == old)
					deactivate = false;
				if (deactivate && oldIsPara && ((TDataView*) old)->GetHiliteView() == view)
					deactivate = false;
			}
			if (deactivate)
				old->ActivateSelection(false);
		}
		if (view != nil)
			view->ActivateSelection(true);
	}
	if (view == nil || length == 0)
		fHiliter = nil;
	else if (length > 0)
	{
		TView* hiliter = view;
		if (view->DerivedFrom(clParagraphView))
		{
			TView* h = ((TDataView*) view)->GetHiliteView();
			if (h != nil)
				hiliter = h;
		}
		fHiliter = hiliter;
	}
	if (view != nil && !KeyboardConnected() && length == 0)
	{
		if (view->fFlags & vCapsRequired)
		{
			if (view->DerivedFrom(clEditView))
			{
				if (!KeyDown(kShiftKey, false))
					KeyIn(kShiftKey, true, nil);
			}
			else if (view->DerivedFrom(clParagraphView))
				DoAutoShift((TParagraphView*) view, offset);
		}
		else if (view->DerivedFrom(clParagraphView) && KeyDown(kShiftKey, false))
			KeyIn(kShiftKey, false, nil);
	}
	if (NOTNIL(fKeyboards))
	{
		RefVar args;
		long count = Length(fKeyboards) / 2;
		for (long i = 0; i < count; i++)
		{
			if ((RINT(GetArraySlotRef(fKeyboards, i * 2 + 1)) & 2) == 0)
				continue;
			if (ISNIL(args))
			{
				args = AllocateArray(RSSYMarray, 3);
				if (view != nil)
				{
					SetArraySlotRef(args, 0, view->fContext);
					SetArraySlotRef(args, 1, MAKEINT(offset));
					SetArraySlotRef(args, 2, MAKEINT(length));
				}
			}
			RefVar keyboard(GetArraySlotRef(fKeyboards, i * 2));
			DoProtoMessageIfDefined(keyboard, RSSYMviewcaretchangedscript, args, nil);
		}
	}
}


// ROM 0x001b4198 HoldPendingKeyView__9TRootViewFRC6RefVarT1
void
TRootView::HoldPendingKeyView(RefArg view, RefArg info)
{
	fPendingKeyView = view;
	fPendingKeyInfo = info;
}


// ROM 0x001b41bc ActivatePendingKeyView__9TRootViewFv
// The held key view made the key view (with its caret info), and
// forgotten.
void
TRootView::ActivatePendingKeyView(void)
{
	TView* view = GetView(fPendingKeyView);
	if (view != nil)
		SetKeyViewSelection(view, fPendingKeyInfo, true);
	fPendingKeyView = NILREF;
	fPendingKeyInfo = NILREF;
}


// ROM 0x001b6588 CleanSelectionStack__9TRootViewFP5TViewUc
// The selection stack's entries for views that are gone (no viewCObject)
// or for the view given removed; with trim a stack of twenty or more
// loses its oldest ten.
void
TRootView::CleanSelectionStack(TView* view, Boolean trim)
{
	RefVar context;
	if (view != nil)
		context = view->fContext;
	for (long i = 0; i < Length(fSelectionStack); )
	{
		RefVar entry(GetArraySlotRef(fSelectionStack, i));
		if (ISNIL(GetProtoVariable(entry, RSSYMviewcobject, nil)) || EQRef(entry, context))
			ArrayRemoveCount(fSelectionStack, i, 2);
		else
			i += 2;
	}
	if (trim && Length(fSelectionStack) > 19)
		ArrayRemoveCount(fSelectionStack, 0, 10);
}


// ROM 0x001b69a8 PushSelection__9TRootViewFP5TViewRC6RefVar
// The view's context and caret info pushed (the stack cleaned first).
void
TRootView::PushSelection(TView* view, RefArg info)
{
	CleanSelectionStack(view, true);
	long count = Length(fSelectionStack);
	SetLength(fSelectionStack, count + 2);
	SetArraySlotRef(fSelectionStack, count, view->fContext);
	SetArraySlot(fSelectionStack, count + 1, info);
}


// ROM 0x001b6868 PopSelection__9TRootViewFv
// The newest entry (after cleaning) as a caret info frame {view, info};
// nil for none.
Ref
TRootView::PopSelection(void)
{
	if (Length(fSelectionStack) == 0)
		return NILREF;
	CleanSelectionStack(nil, false);
	RefVar result;
	long count = Length(fSelectionStack);
	if (count > 0)
	{
		result = Clone(RefVar(Rcanonicalcaretinfo));
		SetFrameSlot(result, RSSYMview, RefVar(GetArraySlotRef(fSelectionStack, count - 2)));
		SetFrameSlot(result, RSSYMinfo, RefVar(GetArraySlotRef(fSelectionStack, count - 1)));
		ArrayRemoveCount(fSelectionStack, count - 2, 2);
	}
	return result;
}


// ROM 0x001b66b8 GetSelectionStack__9TRootViewFv
Ref
TRootView::GetSelectionStack(void)
{
	return fSelectionStack;
}


// ROM 0x001b66d4 FindRestorableKeyView__9TRootViewFP5TViewPUl
// The newest stacked key view within the view (the view itself or a
// descendant); index: its place in the stack.
TView*
TRootView::FindRestorableKeyView(TView* view, ULong* index)
{
	for (long i = Length(fSelectionStack) - 2; i >= 0; i -= 2)
	{
		TView* stacked = GetView(RefVar(GetArraySlotRef(fSelectionStack, i)));
		if (stacked == nil || stacked == gRootView)
			continue;
		for (TView* v = stacked; v != gRootView; v = v->fParent)
			if (v == view)
			{
				if (index != nil)
					*index = i;
				return stacked;
			}
	}
	return nil;
}


// ROM 0x001b678c RestoreKeyView__9TRootViewFP5TView
// The newest stacked key view within the view made the key view again
// with its caret info; ==> whether there was one.
Boolean
TRootView::RestoreKeyView(TView* view)
{
	ULong index;
	TView* stacked = FindRestorableKeyView(view, &index);
	if (stacked == nil)
		return false;
	SetKeyViewSelection(stacked, RefVar(GetArraySlotRef(fSelectionStack, index + 1)), true);
	return true;
}


// ROM 0x001b6a34 GetPreserveHilites__9TRootViewFv
Boolean
TRootView::GetPreserveHilites(void)
{
	return fPreserveHilites;
}


// ROM 0x001b6a20 SetPreserveHilites__9TRootViewFUc
void
TRootView::SetPreserveHilites(Boolean preserve)
{
	fPreserveHilites = preserve;
}


// ROM 0x001b6f44 GetRemoteWriting__9TRootViewFv
// The remoteWriting preference (a keyboard elsewhere writing here).
Boolean
TRootView::GetRemoteWriting(void)
{
	return NOTNIL(GetPreference(RSSYMremotewriting));
}


// ROM 0x001b6f6c SetRemoteWriting__9TRootViewFUc
void
TRootView::SetRemoteWriting(Boolean on)
{
	SetPreference(RSSYMremotewriting, RefVar(MAKEBOOLEAN(on)));
}


// ROM 0x001b707c CaretEnabled__9TRootViewFv
// A caret shows for a key view without a selection when something can
// type: remote writing, a keyboard connected, or an active on-screen
// keyboard (flags bit 2).
Boolean
TRootView::CaretEnabled(void)
{
	if (fCaretView == nil || fCaretLength != 0)
		return false;
	if (GetRemoteWriting())
		return true;
	if (KeyboardConnected())
		return true;
	if (NOTNIL(fKeyboards))
	{
		long count = Length(fKeyboards) / 2;
		for (long i = 0; i < count; i++)
			if (RINT(GetArraySlotRef(fKeyboards, i * 2 + 1)) & 4)
				return true;
	}
	return false;
}


// ROM 0x001b70cc CaretValid__9TRootViewFP6TPoint
// Whether the caret on the screen is right: always while hidden; when no
// caret should show, right when none shows; else the caret's point (pt
// answers it) must be the shown caret's for the same view - a point
// nowhere is right when nothing shows or the shown one is nowhere, and a
// key view not visible needs nothing.
Boolean
TRootView::CaretValid(Point* pt)
{
	if (pt != nil)
	{
		pt->h = kNoCaret;
		pt->v = 0;
	}
	if (fCaretHidden > 0)
		return true;
	if (!CaretEnabled())
		return !fCaretShowing;
	Point caret;
	GetCaretPoint(&caret);
	if (pt != nil)
		*pt = caret;
	if (caret.h == kNoCaret)
	{
		if (!fCaretShowing || fCaretPoint.h == kNoCaret)
			return true;
	}
	if (!fCaretView->VisibleDeep())
		return true;
	if (fCaretShowing && fCaretDrawnView == fCaretView && fCaretPoint.h == caret.h && fCaretPoint.v == caret.v)
		return true;
	return false;
}


// ROM 0x001b72a0 GetCaretPoint__9TRootViewFP6TPoint
// The caret's point from the key view's OffsetToCaret: the rectangle's
// left and bottom (nowhere: h = -0x8000, as an empty rect gives).
void
TRootView::GetCaretPoint(Point* pt)
{
	Rect caret;
	fCaretView->OffsetToCaret(fCaretOffset, &caret);
	pt->h = caret.left;
	pt->v = caret.bottom;
}


// ROM 0x001b7314 GetCaretRect__9TRootViewFP5TRect
// Where the caret is drawn (empty when it is not).
void
TRootView::GetCaretRect(Rect* rect)
{
	if (!fCaretShowing)
	{
		SetEmptyRect(rect);
		return;
	}
	CaretPointToRect(fCaretPoint, rect);
}


// ROM 0x001b7344 DrawCaretBits__FR5TRectUc
// The caret: the outside bitmap in the rectangle (mode 3 - drawn; 1 -
// erased), the inside one a pixel in with the other mode.
static void
DrawCaretBits(const Rect& rect, Boolean erase)
{
	Rect inside = rect;
	InsetRect(&inside, 1, 1);
	StartDrawing(nil, nil);
	Rect outer = rect;
	DrawBitmap(RefVar(Rcaretbitsoutside), &outer, erase ? 1 : 3);
	DrawBitmap(RefVar(Rcaretbitsinside), &inside, erase ? 3 : 1);
	StopDrawing(nil, nil);
}


// ROM 0x001b73dc GetCaretClipView__FP5TView
// The view the caret is clipped to: a view that is not a paragraph, or a
// paragraph's hilite view (its edit view, NOT YET: GetHiliteView), else
// the paragraph's window - the first ancestor below the root that is an
// application or floats.
static TView*
GetCaretClipView(TView* view)
{
	if (!view->DerivedFrom(clParagraphView))
		return view;
	// NOT YET RECONSTRUCTED: view->GetHiliteView() (TDataView: the enclosing edit view)
	TView* v = view;
	for ( ; ; )
	{
		TView* parent = v->fParent;
		if (parent == gRootView)
			return v;
		if (v->fFlags & (vApplication | vFloating))
			return v;
		v = parent;
	}
}


// ROM 0x001b745c DrawCaret__9TRootViewF6TPoint
// The caret drawn at the point for the key view (unless a selection or
// HideCaret): the screen under its rectangle (clipped to the port's
// bounds) saved in fCaretBits, the port's visible region narrowed to the
// key view's (SetupVisRgn) less what is in front of it up to its clip
// view (NarrowVisByIntersectingObscuringSiblingsAndUncles), the bits
// drawn, the caret remembered as showing at the point for the view; a
// point nowhere is remembered as not showing.
void
TRootView::DrawCaret(Point pt)
{
	if (fCaretView == nil)
		return;
	if (fCaretLength != 0 || fCaretHidden != 0)
		return;
	if (pt.h == kNoCaret)
	{
		fCaretShowing = false;
		fCaretPoint = pt;
		fCaretDrawnView = fCaretView;
		return;
	}
	Rect caretRect;
	CaretPointToRect(pt, &caretRect);
	Rect saved = caretRect;
	GrafPort* port;
	GetPort(&port);
	Rect onScreen;
	SectRect(&port->portBits.bounds, &caretRect, &onScreen);
	if (!EmptyRect(&onScreen))
	{
		Rect dst = onScreen;
		OffsetRect(&dst, -saved.left, -saved.top);		// where in the bits (the bits' bounds are 0,0-based)
		fCaretBits->CopyFromScreen(onScreen, dst, 0, nil);
	}
	TView* clipView = GetCaretClipView(fCaretView);
	TRegion savedRgn(fCaretView->SetupVisRgn());
	TRegionVar savedVis(savedRgn);
	fCaretView->NarrowVisByIntersectingObscuringSiblingsAndUncles(clipView, &caretRect);
	DrawCaretBits(caretRect, false);
	fCaretShowing = true;
	fCaretPoint = pt;
	fCaretDrawnView = fCaretView;
	GetPort(&port);
	CopyRgn(savedVis, port->visRgn);
}


// ROM 0x001b7698 RestoreBitsUnderCaret__9TRootViewFv
// The saved bits put back where the caret was (the port clipped to the
// whole screen for it); the caret no longer showing.
void
TRootView::RestoreBitsUnderCaret(void)
{
	if (!fCaretShowing)
		return;
	Rect caretRect;
	GetCaretRect(&caretRect);
	Rect src;
	SetRect(&src, 0, 0, 12, 11);
	GrafPort* port;
	GetPort(&port);
	RgnHandle savedClip = port->clipRgn;
	RgnHandle screenRgn = NewRgn();
	RectRgn(screenRgn, &port->portBits.bounds);		// the ROM: GetGrafInfo's screen map (the port's map here: the tests draw offscreen)
	port->clipRgn = screenRgn;
	fCaretBits->Draw(src, caretRect, 0, nil);
	GetPort(&port);
	port->clipRgn = savedClip;
	DisposeRgn(screenRgn);
	fCaretShowing = false;
}


// ROM 0x001b7adc HideCaret__9TRootViewFv
// The caret taken off the screen and kept off (a count).
void
TRootView::HideCaret(void)
{
	if (fCaretShowing)
		RestoreBitsUnderCaret();
	fCaretHidden++;
}


// ROM 0x001b7b0c ShowCaret__9TRootViewFv
// One HideCaret undone (the caret comes back with the next Update).
void
TRootView::ShowCaret(void)
{
	if (--fCaretHidden < 0)
		fCaretHidden = 0;
}


// ROM 0x001b7b6c DirtyCaret__9TRootViewFv
// The caret's rectangle to be redrawn, the caret no longer counted as
// showing.
void
TRootView::DirtyCaret(void)
{
	if (!fCaretShowing)
		return;
	Rect caretRect;
	GetCaretRect(&caretRect);
	SmartInvalidate(caretRect);
	fCaretShowing = false;
}


// ROM 0x001b6bac FindDefaultButtonAndCaretSlip__9TRootViewFP5TViewPP5TViewT2
// For a key view, with a keyboard connected: the view its _defaultButton
// variable names, and the slip - the first ancestor (the view itself
// included) with a hilite or drag-shadow frame, or the root's child.
void
TRootView::FindDefaultButtonAndCaretSlip(TView* view, TView** button, TView** slip)
{
	TView* theButton = nil;
	TView* theSlip = nil;
	if (view != nil && CommandKeyboardConnected())
	{
		TView* named = GetView(RefVar(GetVariable(view->fContext, RSSYM_defaultbutton, nil, 0)));
		if (named != nil)
			theButton = named;
		TView* v = view;
		while (v != this)
		{
			theSlip = v;
			ULong frame = v->fViewFormat & vfFrameMask;
			if (frame == vfFrameHilite || frame == vfFrameDragShadow)
				break;
			v = v->fParent;
			if (v == this)
				break;
		}
		if (v == this && view == this)
			theSlip = nil;
	}
	*button = theButton;
	*slip = theSlip;
}


// ROM 0x001b6c60 UpdateDefaultButtonAndCaretSlip__9TRootViewFv
// The default button and caret slip found for the key view; a change
// dirties the old view (when there was one) or the new (the ROM does
// one thing per call: the old one dirtied first, the new one taken on
// the next).
void
TRootView::UpdateDefaultButtonAndCaretSlip(void)
{
	TView* button;
	TView* slip;
	FindDefaultButtonAndCaretSlip(fCaretView, &button, &slip);
	if (fDefaultButton != button)
	{
		if (fDefaultButton != nil)
		{
			fDefaultButton->Dirty(nil);
			return;
		}
		fDefaultButton = button;
		if (button != nil)
		{
			button->Dirty(nil);
			return;
		}
	}
	if (fCaretSlip != slip)
	{
		if (fCaretSlip != nil)
		{
			fCaretSlip->Dirty(nil);
			return;
		}
		fCaretSlip = slip;
		if (slip != nil)
			slip->Dirty(nil);
	}
}


// ROM 0x001b6d5c GetKeyboardIndex__9TRootViewFRC6RefVar
// Where the context is in the keyboards array; -1 for not.
long
TRootView::GetKeyboardIndex(RefArg context)
{
	long count = Length(fKeyboards);
	for (long i = 0; i < count; i += 2)
		if (EQRef(GetArraySlotRef(fKeyboards, i), context))
			return i;
	return -1;
}


// ROM 0x001b6a3c RegisterKeyboard__9TRootViewFRC6RefVarUl
// An on-screen keyboard's context and flags added (or its flags set).
void
TRootView::RegisterKeyboard(RefArg context, ULong flags)
{
	long index = GetKeyboardIndex(context);
	if (index == -1)
	{
		index = Length(fKeyboards);
		SetLength(fKeyboards, index + 2);
		SetArraySlot(fKeyboards, index, context);
	}
	SetArraySlotRef(fKeyboards, index + 1, MAKEINT(flags));
}


// ROM 0x001b6b44 UnregisterKeyboard__9TRootViewFRC6RefVar
// The keyboard removed; the caret's view asked whether it goes on
// (CheckForCaretRemoval without a key view, the key view's DerivedFrom
// otherwise, NOT YET); ==> whether it was registered.
Boolean
TRootView::UnregisterKeyboard(RefArg context)
{
	long index = GetKeyboardIndex(context);
	if (index == -1)
		return false;
	ArrayRemoveCount(fKeyboards, index, 2);
	if (fCaretView == nil)
	{
		CheckForCaretRemoval();
		return true;
	}
	fCaretView->DerivedFrom(clParagraphView);
	return true;
}


// ROM 0x001b420c CaretViewGone__9TRootViewFv
// The key view has gone: the newest stacked selection (PopSelection)
// becomes the key view again, or there is none.
void
TRootView::CaretViewGone(void)
{
	RefVar saved(PopSelection());
	if (ISNIL(saved))
		SetKeyViewSelection(nil, RefVar(NILREF), false);
	else
	{
		TView* view = GetView(RefVar(GetFrameSlotRef(saved, RSSYMview)));
		SetKeyViewSelection(view, RefVar(GetFrameSlotRef(saved, RSSYMinfo)), false);
	}
}


// ROM 0x002635d0 ViewContainsCaretView__FP5TView
// Whether the caret view is the view or under it.
Boolean
TRootView::ViewContainsCaretView(TView* view)
{
	TView* caret = fCaretView;
	if (caret == nil)
		return false;
	for ( ; ; )
	{
		if (caret == view)
			return true;
		if (caret == this)
			return false;
		caret = caret->fParent;
	}
}


// ROM 0x001b7bb0 SetPopup__9TRootViewFP5TViewUc
// The popup view set (the previous one noted in the new one's popup slot),
// or, for set false, the view let go: the popup its context's popup slot
// names takes its place.  NOT YET RECONSTRUCTED: the previous popup
// closed (aeDropChild) when the new one is nil.
void
TRootView::SetPopup(TView* view, Boolean set)
{
	TView* previous = fPopup;
	if (!set)
	{
		if (previous != view)
			return;
		RefVar older(GetFrameSlotRef(previous->fContext, RSSYMpopup));
		if (ISNIL(older))
			fPopup = nil;
		else
			fPopup = (TView*) RefToAddress(GetFrameSlotRef(older, RSSYMviewcobject));
		return;
	}
	if (previous != nil && previous != view && view != nil)
		SetFrameSlot(view->fContext, RSSYMpopup, previous->fContext);
	if (view != nil)
		fPopup = view;
}


// ROM 0x001b7e6c GetClipboard__9TRootViewFP5TView
// The clipboard the view is (NOT YET RECONSTRUCTED: no clipboards).
TView*
TRootView::GetClipboard(TView* /*view*/)
{
	return nil;
}


// ROM 0x002e8b18 SetModalView__FP5TView
// The view marked modal (its viewJustify's private bit); NOT YET: the
// recognition disabled for it.
void
TRootView::SetModalView(TView* view)
{
	view->fViewJustify |= vjIsModal;
	fModalView = view;
	Rect bounds;
	view->OuterBounds(&bounds);
}


/*------------------------------------------------------------------------------
	I d l e r s
------------------------------------------------------------------------------*/

// ROM 0x001b4b60 MoveLow__FP13CDynamicArray
// An array's storage re-made at its size (the ROM moves it low in the
// heap: the block freed and allocated again, its contents kept; the host
// copies the elements out and back).
static void
MoveLow(CDynamicArray* array, Size elementSize)
{
	ArrayIndex count = array->GetArraySize();
	if (count == 0)
		return;
	Size size = count * elementSize;
	Ptr copy = NewPtr(size);
	if (copy == nil)
		return;
	array->GetElementsAt(0, copy, count);
	array->SetArraySize(0);
	array->SetArraySize(count);
	array->ReplaceElementsAt(0, copy, count);
	DisposPtr(copy);
}


// ROM 0x001b50d0 GetIdlingView__9TRootViewFP5TView
// The idling record of a view whose Idle is running, nil when it is not.
IdlingView*
TRootView::GetIdlingView(TView* view)
{
	for (IdlingView* idling = fIdlingViews; idling != nil; idling = idling->fNext)
		if (idling->fView == view)
			return idling;
	return nil;
}


// ROM 0x001b5100 UnlinkIdleView__9TRootViewFP5TView
// A view's idling record taken out of the list (its idler was removed
// while its Idle ran: IdleViews must not touch the entry again).
void
TRootView::UnlinkIdleView(TView* view)
{
	IdlingView** link = &fIdlingViews;
	for (IdlingView* idling = fIdlingViews; idling != nil; link = &idling->fNext, idling = idling->fNext)
		if (idling->fView == view)
		{
			*link = idling->fNext;
			return;
		}
}


// ROM 0x001b4f8c AddIdler__9TRootViewFP5TViewUll
// An idler set for the view: due delay milliseconds from now, with the
// arg its Idle gets - an existing one for the view and arg re-timed, a
// new one appended, the view given the hint and the application's next
// idle time brought forward.  A delay of 0 removes the idler (as
// RemoveIdler).  ==> 0 (removing: the time that was left).
ULong
TRootView::AddIdler(TView* view, ULong delay, long arg)
{
	if (delay == 0)
		return RemoveIdler(view, arg);
	TTime due(delay, kMilliseconds);
	TTime now = GetGlobalTime();
	CompAdd(&now.time, &due.time);
	for (ArrayIndex i = 0; i < fIdlers->GetArraySize(); i++)
	{
		IdlerRecord* idler = (IdlerRecord*) fIdlers->SafeElementPtrAt(i);
		if (idler->fView == view && idler->fArg == arg)
		{
			idler->fTime = due;
			view->SetFlags(vHasIdlerHint);
			gApplication->UpdateNextIdleTime(due);
			return 0;
		}
	}
	IdlerRecord record;
	record.fView = view;
	record.fArg = arg;
	record.fTime = due;
	fIdlers->InsertElementsBefore(fIdlers->GetArraySize(), &record, 1);
	view->SetFlags(vHasIdlerHint);
	gApplication->UpdateNextIdleTime(due);
	return 0;
}


// ROM 0x001b5124 RemoveIdler__9TRootViewFP5TViewl
// The view's idler with the arg removed; ==> the time it had left, in
// the clock's units (0 when it was due).
ULong
TRootView::RemoveIdler(TView* view, long arg)
{
	ULong left = 0;
	for (ArrayIndex i = 0; i < fIdlers->GetArraySize(); )
	{
		IdlerRecord* idler = (IdlerRecord*) fIdlers->SafeElementPtrAt(i);
		if (idler->fView == view && idler->fArg == arg)
		{
			TTime now = GetGlobalTime();
			Int64 remaining = idler->fTime.time;
			CompSub(&now.time, &remaining);
			Int64 zero = { 0, 0 };
			if (CompCompare(&remaining, &zero) > 0)
				left = remaining.lo;
			fIdlers->RemoveElementsAt(i, 1);
			UnlinkIdleView(view);
			continue;
		}
		i++;
	}
	return left;
}


// ROM 0x001b5238 RemoveAllIdlers__9TRootViewFP5TView
// Every idler of the view removed, and its hint cleared.
void
TRootView::RemoveAllIdlers(TView* view)
{
	for (ArrayIndex i = 0; i < fIdlers->GetArraySize(); )
	{
		IdlerRecord* idler = (IdlerRecord*) fIdlers->SafeElementPtrAt(i);
		if (idler->fView == view)
		{
			fIdlers->RemoveElementsAt(i, 1);
			UnlinkIdleView(view);
			continue;
		}
		i++;
	}
	view->ClearFlags(vHasIdlerHint);
}


// ROM 0x001b4bf4 IdleViews__9TRootViewFv
// The idlers whose time has come (within 10 ms) run: each view's
// Idle(arg) while it is on the idling list - the delay it answers (in
// milliseconds) re-times its idler from the time it was due (from now
// when that is past), 0 removes it; a view that removed the idler as
// it ran (or went) is left alone.  The children and idler arrays are
// packed when they shrank.  ==> the earliest time an idler is due, now
// when the caret must blink (CaretValid NOT YET: no caret), zero when
// there is nothing to wait for.
TTime
TRootView::IdleViews(void)
{
	if ((long) fChildren->Count() < fChildrenHighWater)
		MoveLow(fChildren, sizeof(TView*));
	fChildrenHighWater = fChildren->Count();
	if ((long) fIdlers->GetArraySize() < fIdlersHighWater)
		MoveLow(fIdlers, sizeof(IdlerRecord));
	fIdlersHighWater = fIdlers->GetArraySize();
	TTime now = GetGlobalTime();
	TTime next;
	next.time.hi = 0x7fffffff;
	next.time.lo = 0;
	TTime soon(10, kMilliseconds);
	IdlingView idling;
	idling.fNext = fIdlingViews;
	idling.fView = nil;
	fIdlingViews = &idling;
	for (ArrayIndex i = 0; i < fIdlers->GetArraySize(); i++)
	{
		IdlerRecord* idler = (IdlerRecord*) fIdlers->SafeElementPtrAt(i);
		if (GetIdlingView(idler->fView) != nil)
			continue;
		Int64 due = idler->fTime.time;
		CompSub(&soon.time, &due);
		if (CompCompare(&now.time, &due) < 0)
		{
			if (CompCompare(&idler->fTime.time, &next.time) < 0)
				next = idler->fTime;
			continue;
		}
		idling.fView = idler->fView;
		long delay = 0;
		newton_try
		{
			delay = idler->fView->Idle(idler->fArg);
		}
		newton_catch_all
		{
			delay = 0;
		}
		end_try;
		idler = (IdlerRecord*) fIdlers->SafeElementPtrAt(i);
		if (GetIdlingView(idling.fView) == nil)
			continue;
		if (delay == 0)
		{
			fIdlers->RemoveElementsAt(i, 1);
			i--;
			continue;
		}
		TTime step(delay, kMilliseconds);
		CompAdd(&step.time, &idler->fTime.time);
		if (CompCompare(&idler->fTime.time, &now.time) < 0)
		{
			idler->fTime = now;
			CompAdd(&step.time, &idler->fTime.time);
		}
		if (CompCompare(&idler->fTime.time, &next.time) < 0)
			next = idler->fTime;
	}
	UnlinkIdleView(idling.fView);
	if (next.time.hi == 0x7fffffff && next.time.lo == 0)
	{
		next.time.hi = 0;
		next.time.lo = 0;
	}
	return next;
}
