/*
	File:		views/RootView.cpp

	Contains:	TRootView: the update regions and their redraw, the views
				it keeps track of.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RootView.h"
#include "SoundSettings.h"	// FClicker
#include <stdio.h>
#include <stdlib.h>
#include "CorrectInfo.h"
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
#include "ArrayIterator.h"
#include "NewtonExceptions.h"
#include "UnitPublic.h"
#include "Recognizer.h"	// gInhibitPopup
#include "Controller.h"		// UpdateInk
#include "Stroke.h"
#include "NewtonTime.h"
#include "ClipboardView.h"
#include "Animate.h"
#include "SoundSettings.h"

// frames/Munger.cpp and frames/ArrayNatives.cpp
void	ArrayInsert(RefArg array, RefArg element, long index);
long	LSearch(RefArg array, RefArg item, RefArg start, RefArg test, RefArg key);

Boolean	gNewtIsAliveAndWell = false;		// ROM 0x0c105510 gNewtIsAliveAndWell (set by TNewtWorld::PreMain once the boot is over; a program without the newt world sets it itself)

const long kUpdateRegionCount = 3;


// ROM 0x001b182c ClassID__9TRootViewCFv
long
TRootView::ClassID(void) const
{
	return clRootView;
}


// ROM 0x001b1834 DerivedFrom__9TRootViewCFl
Boolean
TRootView::DerivedFrom(long id) const
{
	return id == clRootView || TView::DerivedFrom(id);
}


// ROM 0x001b1868 Constructor__9TRootViewFRC6RefVar
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
	SetRect(&caretBox, 0, 0, 11, 12);		// 11 wide, 12 tall: the caret bitmaps' bounds
	fCaretBits->Constructor(caretBox);
	fCaretShowing = false;
	fCaretPoint.h = 0;
	fCaretPoint.v = -0x8000;		// nowhere
	fCaretDrawnView = nil;
	fCaretHidden = 0;
	fDefaultButton = nil;
	fCaretSlip = nil;
	if (TView::gEmptyViewList == nil)
		TView::gEmptyViewList = TViewList::Make();
	fSelectionStack = AllocateArray(RSSYMarray, 0);
	fKeyboards = AllocateArray(RSSYMarray, 0);
	RefVar context(Clone(RefVar(Rrootcontext)));
	SetFrameSlot(context, RSSYM_proto, templ);
	TView::Constructor(context, this);
	// the list the corrector keeps of the words already on the page
	InitCorrection();
	Dirty(nil);
}


// ROM 0x001b19dc __dt__9TRootViewFv
TRootView::~TRootView()
{
	delete fIdlers;
	delete[] fUpdateRegions;
}


// ROM 0x001b31f0 RealDoCommand__9TRootViewFRC6RefVar
// aeKeyboardConnected: the parameter says whether a keyboard is
// connected - the hard key map cleared when it is, the caret's view
// asked to give it up when not; the popup synced, the root dirtied.
// aeHiliteClick is the hilite stroke, which the root draws itself
// (Hiliter).  aeAddData and aeRemoveData are how a clipping's two views
// go on and off the root (AddClipboard, RemoveClipboard) - they are what
// keeps the two arrays.
Boolean
TRootView::RealDoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == aeKeyboardConnected)
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
	if (id == aeHiliteClick)
	{
		// the pen held still on a view and then drawn across it: the root
		// draws the line and hands the stroke back to the view
		Hiliter((TUnitPublic*) CommandParameter(cmd), (TView*) CommandReceiver(cmd));
		CommandSetResult(cmd, 1);
		return true;
	}
	if (id == aeAddData)
	{
		// the view built from the frame parameter, then put at the front
		// of the icons or of the clipboards by which class it is
		TView* view = AddView(RefVar(CommandFrameParameter(cmd)));
		long depth = 1;
		RefVar wanted(GetPreference(RSSYMclipboarddepth));
		if (ISINT(wanted) && RINT(wanted) > 0)
			depth = RINT(wanted);
		Boolean isClipboard = view->DerivedFrom(clClipboard);
		RefStruct& list = isClipboard ? fClipboards : fClipboardIcons;
		if (ISNIL(list))
			list = AllocateArray(RSSYMarray, 0);
		ArrayInsert(list, view->fContext, 0);
		long count = Length(list);
		if (depth < count)
		{
			// one clipping too many: the last goes (its other half goes
			// with it, because removing either removes the pair)
			count--;
			TView* last = GetView(RefVar(GetArraySlotRef(list, count)));
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, last->fId)));
		}
		if (count > 1)
		{
			// the one behind it is dimmed: an icon by its fill pattern,
			// a clipboard by giving up the picture it drew
			if (isClipboard)
			{
				TView* behind = GetView(RefVar(GetArraySlotRef(list, 1)));
				SetFrameSlot(behind->fContext, RSSYMbits, RefVar());
			}
			else
			{
				TView* behind = GetView(RefVar(GetArraySlotRef(list, 1)));
				behind->SetValue(RefVar(RSSYMviewfillpattern), RefVar(MAKEINT(0x10999999)));
			}
		}
		if (isClipboard)
			FPlaySound(RefVar(), RefVar(Raddsound));
		else
			view->Dirty(nil);
		gApplication->PostUndoCommand(aeRemoveData, this, view->fId);
		return true;
	}
	if (id == aeRemoveData)
	{
		TView* view = FindID(CommandParameter(cmd));
		Boolean isClipboard = view->DerivedFrom(clClipboard);
		if (!isClipboard && (view->fFlags & vClipboard) == 0)
			return true;						// not part of a clipping
		long id2 = view->fId;
		RefVar data(view->DataFrame());
		SetFrameSlot(data, RSSYMbits, RefVar());
		RefStruct& list = isClipboard ? fClipboards : fClipboardIcons;
		ArrayRemove(list, view->fContext);
		if (Length(list) == 0)
			list = NILREF;
		else if (!isClipboard)
		{
			// the icon that has come to the front is undimmed again
			TView* front = GetView(RefVar(GetArraySlotRef(list, 0)));
			front->SetValue(RefVar(RSSYMviewfillpattern), RefVar(MAKEINT(0x10000000)));
		}
		RemoveChildView(view);
		RefVar undo(MakeCommand(aeAddData, this, id2));
		CommandSetFrameParameter(undo, data);
		gApplication->PostUndoCommand(undo);
		return true;
	}
	return TView::RealDoCommand(cmd);
}


// ROM 0x001b4ad4 KeyboardConnected__9TRootViewFv
// A hardware keyboard, or a keyboard passed through a soft one.
Boolean
TRootView::KeyboardConnected(void)
{
	return gKeyboardConnected || fPassthruKeyboard;
}


// ROM 0x001b4afc CommandKeyboardConnected__9TRootViewFv
Boolean
TRootView::CommandKeyboardConnected(void)
{
	return gKeyboardConnected;
}


// ROM 0x001b4b0c KeyboardActive__9TRootViewFv
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


// ROM 0x001b491c ConnectPassthruKeyboard__9TRootViewFUc
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


// ROM 0x001b45f8 CheckForCaretRemoval__9TRootViewFv
// NOT YET RECONSTRUCTED: the ROM asks the caret view DerivedFrom(clEditView).
void
TRootView::CheckForCaretRemoval(void)
{
	if (fCaretView != nil)
		fCaretView->DerivedFrom(clEditView);
}


// ROM 0x001b492c HandleKeyIn__9TRootViewFUlUcP5TView
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


// ROM 0x001b5b6c RemoveAllViews__9TRootViewFv
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
	SetRect(&caretBox, 0, 0, 11, 12);		// 11 wide, 12 tall: the caret bitmaps' bounds
	fCaretBits->Constructor(caretBox);
	fCaretShowing = false;
	fCaretPoint.h = 0;
	fCaretPoint.v = -0x8000;		// nowhere
	fCaretDrawnView = nil;
	fCaretHidden = 0;
	fDefaultButton = nil;
	fCaretSlip = nil;
	fHiliter = nil;
	TView::RemoveAllViews();
}


// ROM 0x001b2360 PostDraw__9TRootViewFR5TRect
// The ink waiting to be recognised drawn again over what was just drawn
// (TRecognitionManager::Update, written out in line): the live ink is
// only on the display (the inker's), so an update that paints over it
// puts it into the screen's bits (TController::UpdateInk), and where the
// strays were cleaned up is redrawn.  NOT YET RECONSTRUCTED: the stroke
// groups waiting to be compressed updated (StrokeCentral::
// UpdateCompressGroup 0x001455bc).
void
TRootView::PostDraw(Rect& bounds)
{
	FRect fixed;
	FixRect(&fixed, &bounds);
	if (gRecognition.fLevel != 0)
	{
		long size = RINT(GetPreference(RSSYMuserpensize));
		PenSize(size, size);
		FRect strays = fixed;
		gRecognition.fController->UpdateInk(&strays);
		if (!EmptyRectangle(&strays))
		{
			Rect r;
			UnfixRect(&strays, &r);
			AdjustForInk(&r);
			SmartInvalidate(r);
		}
	}
}


// DEVIATION (library layering): the splash is the application's
// (TNotebook::DrawSplashScreen, in newt above the views), which sets this.
void	(*gDrawSplashScreenProc)(void) = nil;

// ROM 0x001b236c RealDraw__9TRootViewFR5TRect
// The boot splash (TNotebook::DrawSplashScreen: the screen painted black,
// the logo, the version) until the system is up; nothing after: the
// root's format fills the screen.
void
TRootView::RealDraw(Rect& /*bounds*/)
{
	if (gNewtIsAliveAndWell)
		return;
	if (gDrawSplashScreenProc != nil)
		gDrawSplashScreenProc();
	else
	{
		// (host: a test of the views without the application)
		Rect screen;
		SetRect(&screen, 0, 0, ScreenWidth(), ScreenHeight());
		PaintRect(&screen);
	}
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


// ROM 0x001b2e0c Dirty__9TRootViewFPC5TRect
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


// ROM 0x001b1ffc GetCommonParent__9TRootViewFP5TViewT1
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


// ROM 0x001b204c Invalidate__9TRootViewFC11TBaseRegionP5TView
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


// ROM 0x001b2320 Validate__9TRootViewFC11TBaseRegion
// The region needs no update after all.
void
TRootView::Validate(RgnHandle rgn)
{
	for (long i = 0; i < kUpdateRegionCount; i++)
		DiffRgn(fUpdateRegions[i].fRegion, rgn, fUpdateRegions[i].fRegion);
}


// ROM 0x001b1f00 SmartInvalidate__9TRootViewFRC5TRect
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


// ROM 0x001b2390 SmartScreenDirty__9TRootViewFRC5TRect
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


// ROM 0x001b2398 NeedsUpdate__9TRootViewFv
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


// ROM 0x001b243c Update__9TRootViewFP5TRect
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
		// host: NEWTON_TRACE_UPDATE prints each region repainted - how to
		// tell a machine that is busy repainting from one that is stuck
		static const Boolean traceUpdate = getenv("NEWTON_TRACE_UPDATE") != nil;
		if (traceUpdate)
		{
			const Rect& box = (*dirty)->rgnBBox;
			fprintf(stderr, "[update] %d,%d,%d,%d at %lu\n", box.left, box.top, box.right, box.bottom, (unsigned long) Ticks());
		}
		TView::Update(dirty, filler);
		DisposeCachedRgn(dirty);
		pending.Take(NewCachedRgn());
	}
	if (caretInvalid && CaretEnabled())
		DrawCaret(caretPt);
	if (!gSlowMotion)
		StopDrawing(nil, nil);
}


// ROM 0x001b1e0c ForgetAboutView__9TRootViewFP5TView
// A view is going: the pointers to it are dropped (the hiliter, the caret
// view, the popup, the caret slip or default button), its keyboard
// unregistered and its idlers removed (when it has the hint), a filler
// that was it becomes its parent; a modal view's dialog is exited, any
// other view is no longer waiting to be shown after one; the recogniser
// forgets it was clicked.
void
TRootView::ForgetAboutView(TView* view)
{
	if (fHiliter == view)
		fHiliter = nil;
	if (fCaretView == view)
		CaretViewGone();
	if (fPopup == view)
		SetPopup(view, false);
	if (fCaretSlip == view)
		fCaretSlip = nil;
	else if (fDefaultButton == view)
		fDefaultButton = nil;
	UnregisterKeyboard(RefVar(view->fContext));
	if (view->fFlags & vHasIdlerHint)
		RemoveAllIdlers(view);
	for (long i = 0; i < kUpdateRegionCount; i++)
		if (fUpdateRegions[i].fFiller == view)
			fUpdateRegions[i].fFiller = view->fParent;
	if (view->fViewJustify & vjIsModal)
		RealExitModalDialog(view);
	else if (gModalCount != 0)
		RemoveModalSafeView(view);
	if (gRecognition.fPrevClickView == view)
		gRecognition.fPrevClickView = nil;
	if (gRecognition.fClickView == view)
		gRecognition.fClickView = nil;
}


/*------------------------------------------------------------------------------
	T h e   k e y   v i e w   a n d   t h e   c a r e t
------------------------------------------------------------------------------*/

// the caret's rectangle from its point: 11 wide from 5 left of the
// point, 12 down from it (the size of Rcaretbitsoutside); a point nowhere
// (v = -32768) gives a rect nowhere - top and bottom -32768, left and
// right not touched
// ROM 0x001b4d38 CaretPointToRect__FR6TPointP5TRect
static void
CaretPointToRect(const Point& pt, Rect* rect)
{
	if (pt.v == -0x8000)
	{
		rect->top = -0x8000;
		rect->bottom = -0x8000;
		return;
	}
	rect->left = pt.h - 5;
	rect->right = rect->left + 11;
	rect->top = pt.v;
	rect->bottom = rect->top + 12;
}


// the caret's own no-place: a point's v (a rect's top) -32768
static const short kNoCaret = -0x8000;

// the old key view is usable: there and not being deleted
static inline Boolean
KeyViewUsable(TView* view)
{
	return view != nil && (view->fFlags & vIsBeingDeleted) != vIsBeingDeleted;
}


// ROM 0x001b3a4c DoAutoShift__FP14TParagraphViewl
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


// ROM 0x001b3bb4 SetKeyView__9TRootViewFP5TViewlT2Uc
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


// ROM 0x001b3ae4 SetKeyViewSelection__9TRootViewFP5TViewRC6RefVarUc
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


// ROM 0x001b3c9c CommonSetKeyView__9TRootViewFP5TViewlT2
// The key view, offset and length stored.  When the view changes: unless
// hilites are being preserved, a usable old view is told
// ActivateSelection(false) - except when old and new are paragraphs of
// the same enclosing edit view, or one is the other's; the
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
					// two paragraphs of the same page keep each other's
					// selections - which is what lets a stroke that goes
					// through several of them select them all
					TView* e1 = ((TDataView*) old)->GetEnclosingEditView();
					TView* e2 = ((TDataView*) view)->GetEnclosingEditView();
					if (e1 != nil && e2 != nil && e1 == e2)
						deactivate = false;
				}
				if (deactivate && viewIsPara && ((TDataView*) view)->GetEnclosingEditView() == old)
					deactivate = false;
				if (deactivate && oldIsPara && ((TDataView*) old)->GetEnclosingEditView() == view)
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


// ROM 0x001b1cc0 HoldPendingKeyView__9TRootViewFRC6RefVarT1
void
TRootView::HoldPendingKeyView(RefArg view, RefArg info)
{
	fPendingKeyView = view;
	fPendingKeyInfo = info;
}


// ROM 0x001b1ce4 ActivatePendingKeyView__9TRootViewFv
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


// ROM 0x001b40b0 CleanSelectionStack__9TRootViewFP5TViewUc
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


// ROM 0x001b44d0 PushSelection__9TRootViewFP5TViewRC6RefVar
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


// ROM 0x001b4390 PopSelection__9TRootViewFv
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


// ROM 0x001b41e0 GetSelectionStack__9TRootViewFv
Ref
TRootView::GetSelectionStack(void)
{
	return fSelectionStack;
}


// ROM 0x001b41fc FindRestorableKeyView__9TRootViewFP5TViewPUl
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


// ROM 0x001b42b4 RestoreKeyView__9TRootViewFP5TView
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


// ROM 0x001b455c GetPreserveHilites__9TRootViewFv
Boolean
TRootView::GetPreserveHilites(void)
{
	return fPreserveHilites;
}


// ROM 0x001b4548 SetPreserveHilites__9TRootViewFUc
// ==> what it was, so a caller can put it back.
Boolean
TRootView::SetPreserveHilites(Boolean preserve)
{
	Boolean was = fPreserveHilites;
	fPreserveHilites = preserve;
	return was;
}


// ROM 0x001b4a6c GetRemoteWriting__9TRootViewFv
// The remoteWriting preference (a keyboard elsewhere writing here).
Boolean
TRootView::GetRemoteWriting(void)
{
	return NOTNIL(GetPreference(RSSYMremotewriting));
}


// ROM 0x001b4a94 SetRemoteWriting__9TRootViewFUc
void
TRootView::SetRemoteWriting(Boolean on)
{
	SetPreference(RSSYMremotewriting, RefVar(MAKEBOOLEAN(on)));
}


// ROM 0x001b4ba4 CaretEnabled__9TRootViewFv
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


// ROM 0x001b4bf4 CaretValid__9TRootViewFP6TPoint
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
		pt->v = kNoCaret;		// (h not touched)
	}
	if (fCaretHidden > 0)
		return true;
	if (!CaretEnabled())
		return !fCaretShowing;
	Point caret;
	GetCaretPoint(&caret);
	if (pt != nil)
		*pt = caret;
	if (caret.v == kNoCaret)
	{
		if (!fCaretShowing || fCaretPoint.v == kNoCaret)
			return true;
	}
	if (!fCaretView->VisibleDeep())
		return true;
	if (fCaretShowing && fCaretDrawnView == fCaretView && fCaretPoint.h == caret.h && fCaretPoint.v == caret.v)
		return true;
	return false;
}


// ROM 0x001b4dc8 GetCaretPoint__9TRootViewFP6TPoint
// The caret's point from the key view's OffsetToCaret: the rectangle's
// left and a pixel below its bottom (the caret hangs under the line);
// nowhere (the rect's top -32768) is v = -32768, h not touched.
void
TRootView::GetCaretPoint(Point* pt)
{
	Rect caret;
	fCaretView->OffsetToCaret(fCaretOffset, &caret);
	if (caret.top == kNoCaret)
	{
		pt->v = kNoCaret;
		return;
	}
	pt->h = caret.left;
	pt->v = caret.bottom + 1;
}


// ROM 0x001b4e3c GetCaretRect__9TRootViewFP5TRect
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


// ROM 0x001b4e6c DrawCaretBits__FR5TRectUc
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


// ROM 0x001b4f04 GetCaretClipView__FP5TView
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


// ROM 0x001b4f84 DrawCaret__9TRootViewF6TPoint
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
	if (pt.v == kNoCaret)
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


// ROM 0x001b51c0 RestoreBitsUnderCaret__9TRootViewFv
// The saved bits put back where the caret was (the port's visible region
// made the whole screen for it); the caret no longer showing.
void
TRootView::RestoreBitsUnderCaret(void)
{
	if (!fCaretShowing)
		return;
	Rect caretRect;
	GetCaretRect(&caretRect);
	Rect src;
	SetRect(&src, 0, 0, 11, 12);
	GrafPort* port;
	GetPort(&port);
	RgnHandle savedVis = port->visRgn;		// (the ROM's [port,#0x24] at 0x001b522c, 0x001b5264, 0x001b5288: the visRgn)
	RgnHandle screenRgn = NewRgn();
	RectRgn(screenRgn, &port->portBits.bounds);		// the ROM: GetGrafInfo's screen map (the port's map here: the tests draw offscreen)
	port->visRgn = screenRgn;
	fCaretBits->Draw(src, caretRect, 0, nil);
	GetPort(&port);
	port->visRgn = savedVis;
	DisposeRgn(screenRgn);
	fCaretShowing = false;
}


// ROM 0x001b529c DoCaretClick__9TRootViewFP11TUnitPublic
// A click on the caret: when the caret is showing (and no popup is up)
// and the stroke starts within the caret's rectangle let out two pixels,
// the pen is tracked until the stroke ends - the caret drawn inverted
// while the pen is over it (within the caret view's clip) - and, when it
// ends there, the caret view's _caretPopup is popped up at the caret and
// the stroke's ink taken off.  ==> whether the popup came up.
// The pen on the caret clicks (FClicker).
Boolean
TRootView::DoCaretClick(TUnitPublic* unit)
{
	Boolean poppedUp = false;
	if (!fCaretShowing || fPopup != nil)
		return false;
	Rect caretRect;
	GetCaretRect(&caretRect);
	CaretPointToRect(fCaretPoint, &caretRect);
	Rect hitRect = caretRect;
	InsetRect(&hitRect, -2, -2);
	TStrokePublic* stroke = unit->Stroke();
	Point first = stroke->FirstPoint();
	if (!PtInRect(first, &hitRect))
		return false;
	TRegionVar hitRgn;
	RectRgn(hitRgn, &hitRect);
	TView* clipView = GetCaretClipView(fCaretDrawnView);
	TRegion savedRgn(fCaretDrawnView->SetupVisRgn());
	TRegionVar savedVis(savedRgn);
	fCaretDrawnView->NarrowVisByIntersectingObscuringSiblingsAndUncles(clipView, &caretRect);
	GrafPort* port;
	GetPort(&port);
	SectRgn(hitRgn, port->visRgn, hitRgn);		// (the ROM's [port,#0x24] at 0x001b53c4: the visRgn just narrowed)
	if (PtInRgn(first, hitRgn))
	{
		FClicker(RefVar(NILREF));
		Boolean inverted = false;
		do
		{
			Point last = stroke->FinalPoint();
			Boolean inside = PtInRgn(last, hitRgn);
			if (inside == inverted)
				Wait(1);
			else
			{
				DrawCaretBits(caretRect, inside);
				inverted = inside;
			}
		} while (!stroke->Done());
		if (inverted)
		{
			DrawCaretBits(caretRect, false);
			GetPort(&port);
			CopyRgn(savedVis, port->visRgn);
			if (fCaretView != nil)
			{
				RefVar popup(fCaretView->GetVar(RSSYM_caretpopup));
				poppedUp = NOTNIL(popup);
				if (poppedUp)
				{
					RefVar items(GetProtoVariable(popup, RSSYMpopup, nil));
					DoPopupMenu(RefVar(fContext), items, RefVar(MAKEINT(caretRect.right)), RefVar(MAKEINT(caretRect.bottom)), popup);
					stroke->InkOff(true);
				}
			}
			return poppedUp;
		}
	}
	GetPort(&port);
	CopyRgn(savedVis, port->visRgn);
	return poppedUp;
}


// ROM 0x001b5604 HideCaret__9TRootViewFv
// The caret taken off the screen and kept off (a count).
void
TRootView::HideCaret(void)
{
	if (fCaretShowing)
		RestoreBitsUnderCaret();
	fCaretHidden++;
}


// ROM 0x001b5634 ShowCaret__9TRootViewFv
// One HideCaret undone (the caret comes back with the next Update).
void
TRootView::ShowCaret(void)
{
	if (--fCaretHidden < 0)
		fCaretHidden = 0;
}


// ROM 0x001b5694 DirtyCaret__9TRootViewFv
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


// ROM 0x001b46d4 FindDefaultButtonAndCaretSlip__9TRootViewFP5TViewPP5TViewT2
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


// ROM 0x001b4788 UpdateDefaultButtonAndCaretSlip__9TRootViewFv
// The default button and caret slip found for the key view; a change
// dirties the old view (when there was one) and the new (when there is
// one), so that both are drawn again with or without their emphasis.
void
TRootView::UpdateDefaultButtonAndCaretSlip(void)
{
	TView* button;
	TView* slip;
	FindDefaultButtonAndCaretSlip(fCaretView, &button, &slip);
	if (fDefaultButton != button)
	{
		if (fDefaultButton != nil)
			fDefaultButton->Dirty(nil);
		fDefaultButton = button;
		if (button != nil)
			button->Dirty(nil);
	}
	if (fCaretSlip != slip)
	{
		if (fCaretSlip != nil)
			fCaretSlip->Dirty(nil);
		fCaretSlip = slip;
		if (slip != nil)
			slip->Dirty(nil);
	}
}


// ROM 0x001b4884 GetKeyboardIndex__9TRootViewFRC6RefVar
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


// ROM 0x001b4564 RegisterKeyboard__9TRootViewFRC6RefVarUl
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


// ROM 0x001b466c UnregisterKeyboard__9TRootViewFRC6RefVar
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


// ROM 0x001b1d34 CaretViewGone__9TRootViewFv
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


// ROM 0x00265508 ViewContainsCaretView__FP5TView
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


// ROM 0x001b56d8 SetPopup__9TRootViewFP5TViewUc
// The popup view set (the previous one noted in the new one's popup
// slot), or, for `set` false, the view let go: the popup its context's
// popup slot names takes its place.
//
// Setting *nil* is not the same as letting go: it asks for the popup
// that is up to be closed, which is done by sending its parent an
// aeDropChild with the popup as the parameter - the parent hides and
// removes it, and the removal is what calls back with set false and
// takes it off the root.  gInhibitPopup is set so that nothing puts
// another one up while that is happening.  DismissPopup goes round this
// until there are none left.
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
	if (previous != nil && previous != view)
	{
		if (view == nil)
		{
			RefVar cmd(MakeCommand(aeDropChild, previous->fParent, (Long) previous));
			gApplication->DispatchCommand(cmd);
			gInhibitPopup = true;
			return;
		}
		SetFrameSlot(view->fContext, RSSYMpopup, previous->fContext);
	}
	if (view != nil)
		fPopup = view;
}


// ROM 0x001b39e0 SetHilitedView__9TRootViewFP5TView
// Which view owns the selection.  Handing it to another one first tells
// the old owner to drop what it had, so that only one view on the screen
// is ever selected.
void
TRootView::SetHilitedView(TView* view)
{
	if (fHiliter == view)
		return;
	if (fHiliter != nil)
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveAllHilites, fHiliter, kNoParameter)));
	fHiliter = view;
}


// ROM 0x001b2e64 Hiliter__9TRootViewFP11TUnitPublicP5TView
// The hilite stroke drawn while the pen is still down.  The pen is held
// still on a view for three quarters of a second (which is what the
// stroke queue calls a hilite click) and then drawn across it; the ink
// it was leaving is taken off, whatever was selected before is dropped,
// and the line is drawn after the pen until it comes up.
//
// The line is drawn into an off-screen TBits and blitted over a saved
// copy of the screen, so that it can be rubbed out again without the
// views underneath being redrawn; when there is not enough memory for
// either, it is drawn straight on to the screen instead and simply left
// there until the invalidation at the end repaints it.
//
// The stroke is then handed to the view under it as an aeGesture2f
// command, which is what turns it into a selection (TEditView::
// AddHiliter).
//
// DEVIATION: the ROM brackets this with BusyBoxSend(0x35)/(0x36) to hold
// the busy box off while the pen is down.  The busy box lives above the
// view system (newt/NewtWorld.h), which this library does not link, and
// the same bracket is missing from TView::DragAndDrop for that reason.
void
TRootView::Hiliter(TUnitPublic* unit, TView* view)
{
	TStrokePublic* stroke = unit->Stroke();
	stroke->InkOff(true, false);
	if (fDirtyFlag)
	{
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveAllHilites, view, kNoParameter)));
		SetFrameSlot(RefVar(FGetGlobals(RefVar())), RSSYMlasttextchanged, RefVar());
	}
	fDirtyFlag = false;
	FPlaySound(RefVar(), RefVar(Rhilitesound));

	Rect dirty;
	SetEmptyRect(&dirty);
	GrafPort* port;
	GetPort(&port);
	Rect* screen = &port->portRect;
	TBits bits;
	TSaveScreenBits saved;
	Boolean offscreen = false;
	if (bits.Constructor(*screen))
	{
		offscreen = saved.AllocateBuffers(screen);
		if (!offscreen)
			bits.Cleanup();
		else
		{
			Point origin;
			origin.v = 0;
			origin.h = 0;
			bits.BeginDrawing(origin);
			bits.RestorePort();
			saved.SaveScreenBits();
		}
	}

	Point last = stroke->FirstPoint();
	last.h++;							// so that the first point is never "where we already are"
	Boolean first = true;
	while (!stroke->Done())
	{
		Point now = stroke->FinalPoint();
		if (now.v == last.v && now.h == last.h)
		{
			Wait(1);
			continue;
		}
		if (offscreen)
		{
			bits.SetPort();
			DrawHiliteLine(last, now, GetStdPattern(blackPat), first);
			bits.RestorePort();
		}
		Rect segment;
		Pt2Rect(now, last, &segment);
		InsetRect(&segment, -8, -8);
		SectRect(screen, &segment, &segment);
		StartDrawing(nil, nil);
		if (!offscreen)
			DrawHiliteLine(last, now, GetStdPattern(blackPat), first);
		else
		{
			saved.RestoreScreenBits(&segment, nil);
			bits.Draw(segment, segment, srcXor, nil);
		}
		StopDrawing(nil, nil);
		UnionRect(&dirty, &segment, &dirty);
		last = now;
		first = false;
	}

	gApplication->DispatchCommand(RefVar(MakeCommand(aeGesture2f, view, (Long) unit)));
	SmartInvalidate(dirty);
}


/*------------------------------------------------------------------------------
	T h e   c l i p b o a r d s

	A clipping is two views on the root - the clipboard that holds the
	dragged items and the icon the pen picks it up by - kept as two
	parallel arrays of their contexts, front first.  They are put on and
	taken off by the ordinary aeAddData/aeRemoveData commands, so that
	both go on the undo stack; RealDoCommand is what maintains the
	arrays.  `clipboardDepth` (a preference, one by default) says how
	many clippings are kept: adding one past the depth removes the last.
------------------------------------------------------------------------------*/

// ROM 0x001b37fc AddClipboard__9TRootViewFRC6RefVarT1
// The clipping put on the root: one aeAddData command dispatched twice,
// with the clipboard's template as its frame parameter and then the
// icon's.  Each addition posts its own undo.
void
TRootView::AddClipboard(RefArg clipboard, RefArg icon)
{
	RefVar cmd(MakeCommand(aeAddData, this, kNoParameter));
	CommandSetFrameParameter(cmd, clipboard);
	gApplication->DispatchCommand(cmd);
	CommandSetFrameParameter(cmd, icon);
	gApplication->DispatchCommand(cmd);
}


// ROM 0x001b3870 RemoveClipboard__9TRootViewFv
// The front clipping thrown away: the clipboard first, then its icon.
void
TRootView::RemoveClipboard(void)
{
	if (NOTNIL(fClipboards) && Length(fClipboards) > 0)
	{
		TView* view = GetView(RefVar(GetArraySlotRef(fClipboards, 0)));
		if (view != nil)
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, view->fId)));
	}
	if (NOTNIL(fClipboardIcons) && Length(fClipboardIcons) > 0)
	{
		TView* view = GetView(RefVar(GetArraySlotRef(fClipboardIcons, 0)));
		if (view != nil)
			gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, this, view->fId)));
	}
}


// ROM 0x001b58bc GetClipboard__9TRootViewFv
TView*
TRootView::GetClipboard(void)
{
	if (NOTNIL(fClipboards) && Length(fClipboards) > 0)
		return GetView(RefVar(GetArraySlotRef(fClipboards, 0)));
	return nil;
}


// ROM 0x001b5928 GetClipboardIcon__9TRootViewFv
TView*
TRootView::GetClipboardIcon(void)
{
	if (NOTNIL(fClipboardIcons) && Length(fClipboardIcons) > 0)
		return GetView(RefVar(GetArraySlotRef(fClipboardIcons, 0)));
	return nil;
}


// ROM 0x001b5994 GetClipboard__9TRootViewFP5TView
// The clipboard whose icon this view is: the icons are searched for its
// context and the clipboard at the same index answered.
TView*
TRootView::GetClipboard(TView* icon)
{
	if (NOTNIL(fClipboardIcons) && Length(fClipboardIcons) > 0)
	{
		RefVar start(MAKEINT(0));
		long index = LSearch(fClipboardIcons, icon->fContext, start, RefVar(RSSYM_3D), RefVar());
		if (index >= 0)
			return GetView(RefVar(GetArraySlotRef(fClipboards, index)));
	}
	return nil;
}


// ROM 0x001b5a64 GetClipboardIcon__9TRootViewFP10TClipboard
// And the other way round: the icon of this clipboard.
TView*
TRootView::GetClipboardIcon(TClipboard* clipboard)
{
	if (NOTNIL(fClipboards) && Length(fClipboards) > 0)
	{
		RefVar start(MAKEINT(0));
		long index = LSearch(fClipboards, clipboard->fContext, start, RefVar(RSSYM_3D), RefVar());
		if (index >= 0)
			return GetView(RefVar(GetArraySlotRef(fClipboardIcons, index)));
	}
	return nil;
}


// ROM 0x001b5b54 GetClipboardIcons__9TRootViewFv
Ref
TRootView::GetClipboardIcons(void)
{
	return gRootView->fClipboardIcons;
}


// ROM 0x001b584c GetFrontmostModalView__9TRootViewFv
// The frontmost of the root's visible children that has a modalState -
// the dialog that is modal now.
TView*
TRootView::GetFrontmostModalView(void)
{
	TBackwardViewListLoop loop(fChildren);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
		if ((view->fFlags & vVisible) != 0 && NOTNIL(view->GetProto(RSSYMmodalstate)))
			return view;
	return nil;
}


/*------------------------------------------------------------------------------
	I d l e r s
------------------------------------------------------------------------------*/

// ROM 0x001b2688 MoveLow__FP13CDynamicArray
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


// ROM 0x001b2bf8 GetIdlingView__9TRootViewFP5TView
// The idling record of a view whose Idle is running, nil when it is not.
IdlingView*
TRootView::GetIdlingView(TView* view)
{
	for (IdlingView* idling = fIdlingViews; idling != nil; idling = idling->fNext)
		if (idling->fView == view)
			return idling;
	return nil;
}


// ROM 0x001b2c28 UnlinkIdleView__9TRootViewFP5TView
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


// ROM 0x001b2ab4 AddIdler__9TRootViewFP5TViewUll
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


// ROM 0x001b2c4c RemoveIdler__9TRootViewFP5TViewl
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


// ROM 0x001b2d60 RemoveAllIdlers__9TRootViewFP5TView
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


// ROM 0x001b271c IdleViews__9TRootViewFv
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


/*------------------------------------------------------------------------------
	M e s s a g e s   t o   t h e   r o o t

	The system's own code reaches the scripts at the top of the view
	hierarchy by sending to the root view's context - the frame every
	other view's context has as its ultimate _parent.  GetRoot() answers
	the same frame to a script.
------------------------------------------------------------------------------*/

// ROM 0x001b1fe4 NSSendRootMessage__FRC6RefVar
Ref
NSSendRootMessage(RefArg message)
{
	RefVar context(gRootView->fContext);
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(context, message));
	return DoSend(context, implementor, message, 0);
}


// ROM 0x001b2a94 NSSendRootMessage__FRC6RefVarT1
Ref
NSSendRootMessage(RefArg message, RefArg a1)
{
	RefVar context(gRootView->fContext);
	if (!IsSymbol(message))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, message);
	RefVar implementor(FindImplementor(context, message));
	gInterpreter->PushValue(a1);
	return DoSend(context, implementor, message, 1);
}


/*------------------------------------------------------------------------------
	S h o w i n g   a   v i e w   w h i l e   a   m o d a l   d i a l o g
	i s   u p
------------------------------------------------------------------------------*/

// ROM 0x0c101944 gDelayedShowList - the views waiting to be shown
static CDynamicArray*	gDelayedShowList = nil;

// ROM 0x001b1a8c ModalSafeShow__FP5TView
// A view to be shown once the modal dialog has gone: put at the front of
// the list; a view holding the caret gives it up meanwhile, the caret's
// place kept to be given back (HoldPendingKeyView).
void
ModalSafeShow(TView* view)
{
	if (gDelayedShowList == nil)
		gDelayedShowList = new CDynamicArray(sizeof(TView*), 4);		// DEVIATION: the ROM's default elements are four bytes, a host pointer is wider
	gDelayedShowList->InsertElementsBefore(0, &view, 1);
	if (!gRootView->ViewContainsCaretView(view))
		return;
	TView* caret = gRootView->fCaretView;
	RefVar selection(caret->GetSelection());
	gRootView->HoldPendingKeyView(RefVar(caret->fContext), selection);
	gRootView->SetKeyView(nil, 0, 0, false);
}


// ROM 0x001b1b34 ModalSafeShowRelease__Fv
// The modal dialog gone (RealExitModalDialog): each waiting view shown
// (with an aeShow that does not ask again) and the caret given back.
void
ModalSafeShowRelease(void)
{
	if (gDelayedShowList == nil)
		return;
	CArrayIterator iter(gDelayedShowList);
	for (ArrayIndex index = iter.FirstIndex(); iter.More(); index = iter.NextIndex())
	{
		TView* view = *(TView**) gDelayedShowList->SafeElementPtrAt(index);
		gApplication->DispatchCommand(RefVar(MakeCommand(aeShow, view, kNoParameter)));
	}
	delete gDelayedShowList;
	gDelayedShowList = nil;
	gRootView->ActivatePendingKeyView();
}


// ROM 0x001b1c1c RemoveModalSafeView__FP5TView
void
RemoveModalSafeView(TView* view)
{
	if (gDelayedShowList == nil)
		return;
	CArrayIterator iter(gDelayedShowList);
	for (ArrayIndex index = iter.FirstIndex(); iter.More(); index = iter.NextIndex())
	{
		if (*(TView**) gDelayedShowList->SafeElementPtrAt(index) == view)
		{
			gDelayedShowList->RemoveElementsAt(index, 1);
			return;
		}
	}
}
