/*
	File:		views/RootView.cpp

	Contains:	TRootView: the update regions and their redraw, the views
				it keeps track of.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "RootView.h"
#include "Application.h"
#include "Rects.h"
#include "Ports.h"
#include "Draw.h"
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
// RECONSTRUCTED: the recognition's InitCorrection, the keyboard gestalt,
// the caret's saved bits (TBits).
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
	fCaretView = nil;
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
// NOT YET RECONSTRUCTED: the root view's commands (clicks, keys, the
// clipboard).
Boolean
TRootView::RealDoCommand(RefArg cmd)
{
	return TView::RealDoCommand(cmd);
}


// ROM 0x001b8044 RemoveAllViews__9TRootViewFv
// NOT YET RECONSTRUCTED beyond the children: the ROM forgets the key view,
// the popup and the clipboards first.
void
TRootView::RemoveAllViews(void)
{
	fPopup = nil;
	fCaretView = nil;
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
// Whether Update has anything to do: a dirty screen rect or an update
// region (NOT YET: the caret's validity, the default button and caret
// slip having changed).
Boolean
TRootView::NeedsUpdate(void)
{
	if (!EmptyRect(&fDirtyScreen))
		return true;
	for (long i = 0; i < kUpdateRegionCount; i++)
		if (fUpdateRegions[i].fFiller != nil)
			return true;
	return false;
}


// ROM 0x001b4914 Update__9TRootViewFP5TRect
// The update regions redrawn (a rect given is invalidated first): each
// slot's region, less what the port cannot show (which stays pending),
// drawn through TView::Update with its filler.  NOT YET RECONSTRUCTED:
// the caret hidden and redrawn, the screen's dirty rect flushed
// (StartDrawing/StopDrawing).
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


// ROM 0x001b420c CaretViewGone__9TRootViewFv
// NOT YET RECONSTRUCTED: the ROM restores the key view selection the
// selection stack holds.
void
TRootView::CaretViewGone(void)
{
	fCaretView = nil;
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
