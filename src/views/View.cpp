/*
	File:		views/View.cpp

	Contains:	TView: the bases, the child list and its loops, the clipper,
				the structure (making, adding, removing, ordering, finding),
				the context and the slot cache, the scripts, the flags and
				values, and the bounds (justification against the parent).
				The drawing side is ViewDraw.cpp, the making of contexts and
				views BuildView.cpp.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "View.h"
#include "ClipboardView.h"
#include "Bits.h"
#include "Hilites.h"
#include "UnitPublic.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "Keyboard.h"
#include "Rects.h"
#include "Shapes.h"
#include "Draw.h"
#include "RegionVars.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "DragDrop.h"
#include "Stroke.h"
#include "ROMConstants.h"
#include "REPTranslators.h"
#include "Fonts.h"
#include "Locale.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "Screen.h"		// StartDrawing
#include "PickView.h"		// GetAppAreaBounds
#include "PolygonView.h"	// AlignPtToGrid
#include "Animate.h"		// TAnimate (SyncScroll)
#include "Cursors.h"		// CursorNext etc. (SyncScrollSoup)
#include "Bits.h"
#include "SoundSettings.h"	// FPlaySound (SoundEffect)
#include <string.h>

TViewList*	TView::gEmptyViewList = nil;		// ROM 0x0c101930 gEmptyViewList__5TView
long		TView::gViewIdCounter = 0;			// ROM 0x0c102050
TRootView*	gRootView = nil;					// ROM 0x0c101934 gRootView
// ROM 0x0c105524 gModalCount
// How many modal dialogs are up (FModalDialog and FFilterDialog raise it,
// RealExitModalDialog lowers it).
long		gModalCount = 0;
RefStruct*	gSlotCacheTable = nil;				// ROM 0x0c104f58 slotCacheRefs (the array, not a pointer into it: the host's heap compacts)
Boolean		gSkipVisRegions = false;			// ROM 0x0c104f60 gSkipVisRegions
Boolean		gDontDrawHilites = false;			// ROM 0x0c100cbc gDontDrawHilites
Boolean		gOutlineViews = false;				// ROM 0x0c10193c gOutlineViews
long		gSlowMotion = 0;					// ROM 0x0c101940 gSlowMotion

// the ROM's exception for a bounds frame that is not one
static char kBadBoundsFrame[] = "bad bounds frame";


/*------------------------------------------------------------------------------
	T x O b j e c t,  T R e s p o n d e r
------------------------------------------------------------------------------*/

// ROM 0x00143738 __nw__8TxObjectSFUi
// A cleared block (NewtPtrClear), or exOutOfMemory.
void*
TxObject::operator new(size_t size)
{
	void* p = NewPtrClear(size);
	if (p == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return p;
}


// ROM 0x00143774 __dl__8TxObjectSFPv
void
TxObject::operator delete(void* p)
{
	DisposPtr((Ptr) p);
}


// ROM 0x00143790 ClassID__8TxObjectCFv
long
TxObject::ClassID(void) const
{
	return 0;
}


// ROM 0x00143798 DerivedFrom__8TxObjectCFl
Boolean
TxObject::DerivedFrom(long /*id*/) const
{
	return false;
}


// ROM 0x00143778 __dt__8TxObjectFv
TxObject::~TxObject()
{ }


// ROM 0x001437ac Key__8TxObjectCFv
ULong
TxObject::Key(void) const
{
	return 0;
}


// ROM 0x001a9354 ClassID__10TResponderCFv
long
TResponder::ClassID(void) const
{
	return 0;
}


// ROM 0x001a935c DerivedFrom__10TResponderCFl
Boolean
TResponder::DerivedFrom(long id) const
{
	return TxObject::DerivedFrom(id);
}


// ROM 0x001a9390 DoCommand__10TResponderFRC6RefVar
Boolean
TResponder::DoCommand(RefArg cmd)
{
	return RealDoCommand(cmd);
}


Boolean
TResponder::RealDoCommand(RefArg /*cmd*/)
{
	return false;
}


/*------------------------------------------------------------------------------
	T L i s t L o o p,  T B a c k w a r d L o o p
------------------------------------------------------------------------------*/

// ROM 0x00141380 __ct__9TListLoopFP5CList
TListLoop::TListLoop(CList* list)
{
	fList = list;
	Reset();
}


// ROM 0x001413bc Reset__9TListLoopFv
void
TListLoop::Reset(void)
{
	fIndex = -1;
	fCount = fList->GetArraySize();
}


// ROM 0x001413d4 Next__9TListLoopFv
void*
TListLoop::Next(void)
{
	fIndex++;
	if (fIndex < fCount)
	{
		void** slot = (void**) fList->SafeElementPtrAt(fIndex);
		return slot != nil ? *slot : nil;
	}
	return nil;
}


// ROM 0x001413e4 Current__9TListLoopFv
void*
TListLoop::Current(void)
{
	if (fIndex < fCount)
	{
		void** slot = (void**) fList->SafeElementPtrAt(fIndex);
		return slot != nil ? *slot : nil;
	}
	return nil;
}


// ROM 0x001413fc RemoveCurrent__9TListLoopFv
void
TListLoop::RemoveCurrent(void)
{
	fList->RemoveElementsAt(fIndex, 1);
	fIndex--;
	fCount--;
}


// ROM 0x00141438 __ct__13TBackwardLoopFP5CList
TBackwardLoop::TBackwardLoop(CList* list)
{
	fIndex = list->GetArraySize();
	fList = list;
}


// ROM 0x00141470 Next__13TBackwardLoopFv
void*
TBackwardLoop::Next(void)
{
	fIndex--;
	if (fIndex >= 0)
	{
		void** slot = (void**) fList->SafeElementPtrAt(fIndex);
		return slot != nil ? *slot : nil;
	}
	return nil;
}


// ROM 0x00141480 Current__13TBackwardLoopFv
void*
TBackwardLoop::Current(void)
{
	if (fIndex >= 0)
	{
		void** slot = (void**) fList->SafeElementPtrAt(fIndex);
		return slot != nil ? *slot : nil;
	}
	return nil;
}


// ROM 0x00260234 GetFirstNonFloater__FP9TViewList
// The index of the last view that does not float (the front-most
// non-floater: the walk from the back stops at it), -1 when all float.
static long
FirstNonFloaterIndex(TViewList* list)
{
	for (long i = (long) list->GetArraySize() - 1; i >= 0; i--)
		if ((list->At(i)->fFlags & vFloating) == 0)
			return i;
	return -1;
}


/*------------------------------------------------------------------------------
	T C l i p p e r
------------------------------------------------------------------------------*/

// ROM 0x00066258 __ct__8TClipperFv
TClipper::TClipper()
{
	fIsObscured = false;
}


// ROM 0x0006629c UpdateRegions__8TClipperFP5TView
// The full region: the view's outer bounds, with rounded corners when its
// format has them (the ROM records FrameRoundRect into a region; the host
// takes the shape from OvalRgn - the same pixels); the visible region the
// whole plane until RecalcVisible narrows it.
void
TClipper::UpdateRegions(TView* view)
{
	Rect bounds;
	view->OuterBounds(&bounds);
	long round = ((view->fViewFormat & vfRoundMask) >> vfRoundShift) * 2;
	if (round != 0)
	{
		RgnHandle shape = OvalRgn(&bounds, round, round);
		if (shape != nil)
		{
			CopyRgn(shape, fFullRgn);
			DisposeRgn(shape);
		}
		else
			RectRgn(fFullRgn, &bounds);
	}
	else
		RectRgn(fFullRgn, &bounds);
	Rect everything;
	SetRect(&everything, -32767, -32767, 32766, 32766);
	RectRgn(fVisRgn, &everything);
}


// ROM 0x0006635c Offset__8TClipperF6TPoint
void
TClipper::Offset(Point delta)
{
	OffsetRgn(fFullRgn, delta.h, delta.v);
}


// ROM 0x00066388 RecalcVisible__8TClipperF11TBaseRegion
// The visible region is the full one less what is in front.
void
TClipper::RecalcVisible(RgnHandle inFront)
{
	DiffRgn(fFullRgn, inFront, fVisRgn);
	fIsObscured = !EqualRgn(fVisRgn, fFullRgn);
}


/*------------------------------------------------------------------------------
	T V i e w :  c l a s s
------------------------------------------------------------------------------*/

// ROM 0x0025f290 ClassID__5TViewCFv
long
TView::ClassID(void) const
{
	return clView;
}


// ROM 0x002635c0 DerivedFrom__5TViewCFl
Boolean
TView::DerivedFrom(long id) const
{
	return id == clView || TResponder::DerivedFrom(id);
}


// ROM 0x00268af4 __dt__5TViewFv
// The context's RefStruct goes; the children were removed by Delete.
TView::~TView()
{ }


// ROM 0x00268bcc DoCommand__5TViewFRC6RefVar
// The view answers the command itself, and a command it does not take
// goes on to its parent - which is how a command posted to the view
// under the pen reaches the root view (the hilite stroke) or the
// application (the editing commands).  The root is its own parent, which
// is where the walk stops.
//
// A RealDoCommand that answers 2 stops the walk and yet answers `not
// taken` to the caller, which is how a view swallows a command without
// letting anything else have it.
Boolean
TView::DoCommand(RefArg cmd)
{
	long result = RealDoCommand(cmd);
	if (result == 0 && fParent != this)
		result = fParent->DoCommand(cmd);
	if (result == 2)
		result = 0;
	return result != 0;
}


// ROM 0x00266368 Constructor__5TViewFRC6RefVarP5TView
// The view built from its context under the parent: the context is
// linked to the parent's (_parent) and to the object (viewCObject), the
// allocateContext/stepAllocateContext templates get contexts of their own
// under the named slots, the viewSetupFormScript runs (with vIsInSetupForm
// set: a script that closes the view makes -8501), the flags and format
// are read, a child of the root view gets its clipper, the parent takes
// the view, the bounds are set from viewBounds (an application whose
// bottom hangs below the screen is moved up to fit), declareSelf declares
// the context in itself, the children are added, a lassoing view is sized
// to enclose them, and the viewSetupDoneScript runs.  A Throw on the way
// takes the view out of the parent again.
void
TView::Constructor(RefArg context, TView* parent)
{
	fChildren = gEmptyViewList;
	fParent = parent;
	fContext = context;
	fSlotCache = 0xffffffff;
	fSlotCache2 = 0xffff;
	fId = ++gViewIdCounter;
	if (parent != this)
		SetFrameSlot(context, RSSYM_parent, parent->fContext);
	SetFrameSlot(context, RSSYMviewcobject, RefVar(AddressToRef(this)));

	RefVar allocs(GetCacheProto(kIndexAllocateContext));
	if (NOTNIL(allocs))
		for (long i = 0, count = Length(allocs); i < count; i += 2)
		{
			RefVar ctx(BuildContext(RefVar(GetArraySlotRef(allocs, i + 1)), true));
			SetFrameSlot(ctx, RSSYM_parent, context);
			SetContextSlot(RefVar(GetArraySlotRef(allocs, i)), ctx);
		}
	allocs = GetCacheProto(kIndexStepAllocateContext);
	if (NOTNIL(allocs))
		for (long i = 0, count = Length(allocs); i < count; i += 2)
		{
			RefVar ctx(BuildContext(RefVar(GetArraySlotRef(allocs, i + 1)), true));
			SetFrameSlot(ctx, RSSYM_parent, context);
			SetContextSlot(RefVar(GetArraySlotRef(allocs, i)), ctx);
		}

	SetFlags(vIsInSetupForm);
	SetupForm();
	if ((fFlags & vIsBeingDeleted) == vIsBeingDeleted)
	{
		ClearFlags(vIsBeingDeleted | vVisible);
		Throw(exRootException, (void*) kViewErrCouldNotCreate, nil);
	}
	ClearFlags(vIsInSetupForm);
	if (parent->fFlags & vWriteProtected)
		SetFlags(vWriteProtected);
	fFlags |= RINT(GetCacheProto(kIndexViewFlags)) & vViewFlagsMask;
	RefVar format(GetProto(RSSYMviewformat));
	if (NOTNIL(format))
		fViewFormat = RINT(format) & vfFormatMask;
	if (parent == gRootView && this != gRootView)
		SetFrameSlot(context, RSSYMviewclipper, RefVar(AddressToRef(new TClipper)));
	if (parent != this)
		parent->AddView(this);

	newton_try
	{
		RefVar bounds(GetProto(RSSYMviewbounds));
		if (ISNIL(bounds))
			Throw(exRootException, (void*) kViewErrNoViewBounds, nil);
		Rect r;
		if (!FromObject(bounds, r))
			ThrowMsg(kBadBoundsFrame);
		SetBounds(r);
		if ((fFlags & vApplication) && this != gRootView && (fViewJustify & vjParentVMask) == 0)
		{
			long overflow = viewBounds.bottom - gRootView->ScreenHeight();
			if (overflow > 0)
			{
				r.top -= overflow;
				if (r.top < 0)
					r.top = 0;
				r.bottom = gRootView->ScreenHeight();
				OffsetRect(&r, -parent->viewBounds.left, -parent->viewBounds.top);
				SetBounds(r);
			}
		}
		if ((fFlags & vVisible) && HasVisRgn())
			parent->ViewVisibleChanged(this, false);
		RefVar declareSelf(GetProto(RSSYMdeclareself));
		if (NOTNIL(declareSelf))
			SetFrameSlot(context, declareSelf, context);
		AddViews(false);
		if (fViewJustify & vjChildrenLasso)
		{
			// the union of the children (an empty extent counts as one pixel)
			Rect lasso;
			Boolean first = true;
			TViewLoop loop(fChildren);
			for (TView* child = loop.Next(); child != nil; child = loop.Next())
			{
				Rect r = child->viewBounds;
				if (r.left == r.right)
					r.right++;
				if (r.top == r.bottom)
					r.bottom++;
				if (first)
				{
					lasso = r;
					first = false;
				}
				else
					JoinRect(&lasso, &r, &lasso);
			}
			if (!first)
			{
				long dh;
				switch (fViewJustify & vjParentHMask)
				{
				case vjParentCenterH:
					dh = ((viewBounds.left + viewBounds.right) - (lasso.left + lasso.right)) / 2;
					break;
				case vjParentRightH:
					dh = viewBounds.right - lasso.right;
					break;
				default:
					dh = viewBounds.left - lasso.left;
					break;
				}
				OffsetRect(&lasso, dh, 0);
				long dv;
				switch (fViewJustify & vjParentVMask)
				{
				case vjParentCenterV:
					dv = ((viewBounds.top + viewBounds.bottom) - (lasso.top + lasso.bottom)) / 2;
					break;
				case vjParentBottomV:
					dv = viewBounds.bottom - lasso.bottom;
					break;
				default:
					dv = viewBounds.top - lasso.top;
					break;
				}
				OffsetRect(&lasso, 0, dv);
				viewBounds = lasso;
				Rect relative = lasso;
				DejustifyBounds(&relative);
				SetDataSlot(RSSYMviewbounds, RefVar(ToObject(relative)));
				RecalcBounds();
			}
		}
		SetupDone();
	}
	newton_catch_all
	{
		if ((fFlags & vVisible) && HasVisRgn())
			parent->ViewVisibleChanged(this, false);
		parent->fChildren->Remove(this);
		NextHandler(&_info);
	}
	end_try;
}


// ROM 0x00267584 Delete__5TViewFv
// The view and its children go: the viewQuitScript runs (answering
// 'postQuit asks for the viewPostQuitScript after the children are gone),
// the context forgets the object, the children are removed, the
// viewChildren slot of a context whose parent is not itself going is
// emptied, the allocated contexts are dropped, the clipper freed, the
// root view told, and the object deleted.
void
TView::Delete(void)
{
	SetFlags(vIsBeingDeleted);
	RemoveAllHilites();
	Boolean postQuit = false;
	newton_try
	{
		RefVar result(RunScript(RSSYMviewquitscript, RefVar(NILREF)));
		postQuit = EQRef(result, RSSYMpostquit);
	}
	newton_catch(exRootException)
	{ }
	end_try;
	SetContextSlot(RSSYMviewcobject, RefVar(NILREF));
	Boolean hadChildren = fChildren != gEmptyViewList;
	if (hadChildren)
		RemoveAllViews();
	if (postQuit)
	{
		newton_try
		{
			RunScript(RSSYMviewpostquitscript, RefVar(NILREF));
		}
		newton_catch(exRootException)
		{ }
		end_try;
	}
	if (hadChildren && (fParent->fFlags & vIsBeingDeleted) != vIsBeingDeleted)
	{
		if (FrameHasSlotRef(fContext, RSSYMviewchildren))
			SetContextSlot(RSSYMviewchildren, RefVar(NILREF));
	}
	RefVar allocs(GetCacheProto(kIndexAllocateContext));
	if (NOTNIL(allocs))
		for (long i = 0, count = Length(allocs); i < count; i += 2)
			SetContextSlot(RefVar(GetArraySlotRef(allocs, i)), RefVar(NILREF));
	allocs = GetCacheProto(kIndexStepAllocateContext);
	if (NOTNIL(allocs))
		for (long i = 0, count = Length(allocs); i < count; i += 2)
			SetContextSlot(RefVar(GetArraySlotRef(allocs, i)), RefVar(NILREF));
	TClipper* clipper = Clipper();
	if (clipper != nil)
	{
		delete clipper;
		SetFrameSlot(fContext, RSSYMviewclipper, RefVar(NILREF));
	}
	gRootView->ForgetAboutView(this);
	fSlotCache2 |= 0x10000;
	delete this;
}


// the script commands' arguments: [the unit (the parameter as a Ref)]
static Ref
UnitArgs(RefArg cmd)
{
	RefVar args(MakeArray(1));
	Long unit = CommandParameter(cmd);
	SetArraySlotRef(args, 0, unit != 0 ? AddressToRef((void*) unit) : NILREF);	// (host: no unit is nil - the ROM's units are never 0)
	return args;
}


// a script's result as a command result: handled unless it answered nil
static Boolean
ScriptHandled(RefArg cmd, Ref result)
{
	Boolean handled = NOTNIL(result);
	CommandSetResult(cmd, handled);
	return handled;
}


// ROM 0x00268d38 RealDoCommand__5TViewFRC6RefVar
// The commands a view answers (their ids Commands.h): the scripts -
// aeClick runs viewClickScript(unit) on a clickable view ('skip: the
// result 0, the click passed on), aeStroke viewStrokeScript(unit),
// aeScrub/aeCaret/aeLine and the other gestures viewGestureScript(unit,
// kind) (under a handler: a script that throws has handled the gesture),
// aeWord viewWordScript(unit), aeRawInk viewRawInkScript(strokes),
// aeInkWord viewInkWordScript(strokes) (these three the view's own
// scripts, not its parents'), aeScrollUp/Down and aeOverview
// their scripts (vars.lastTextChanged cleared after) - each handled
// unless the script answered nil; the key events (HandleKeyEvent: NOT
// YET RECONSTRUCTED); the structure - aeAddChild adds the frame
// parameter's view and shows it (aeShow with the parameter, dispatched),
// aeDropChild hides and removes the parameter's view, aeHide hides,
// aeShow shows (under a modal dialog a view outside it is shown
// ModalSafeShow), aeAddData puts the frame parameter in
// the soup as a child (posting aeRemoveData as its undo), aeRemoveData
// takes the child of the parameter's id out (posting aeAddData with its
// data), aeMoveData moves by params[0], [1] (posting the reverse as
// aeMoveChild to the parent), aeScaleData scales by params[0..3]
// (Scale, NOT YET), aeAddHilite appends the frame parameter (a THilite as
// a pointer Ref) to hilites, aeRemoveHilite removes one,
// aeRemoveAllHilites all, aeToChildren sends the command to every child
// and aeToHilitedChildren to the hilited ones, aeMoveChild sends
// aeMoveData to the child of the parameter's id.  ==> whether handled.
Boolean
TView::RealDoCommand(RefArg cmd)
{
	Boolean handled = false;
	long id = CommandID(cmd);
	switch (id)
	{
	case aeClick:
		if (fFlags & vClickable)
		{
			RefVar result(RunCacheScript(kIndexViewClickScript, RefVar(UnitArgs(cmd)), true));
			if (EQRef(result, RSSYMskip))
			{
				CommandSetResult(cmd, 0);
				handled = 2;		// taken, and the caller told nobody took it
			}
			else
				handled = ScriptHandled(cmd, result);
		}
		break;

	case aeStroke:
		handled = ScriptHandled(cmd, RunScript(RSSYMviewstrokescript, RefVar(UnitArgs(cmd)), true));
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		break;

	case aeScrub:
	case aeCaret:
	case aeLine:
	case aeGesture2f:
	case aeTap:
	case aeDoubleTap:
		{
			// The script runs under a handler: a script that throws is
			// taken to have handled the gesture (the result 1), so that
			// the gesture is not passed on to be written with.
			newton_try
			{
				RefVar args(MakeArray(2));
				Long unit = CommandParameter(cmd);
				SetArraySlotRef(args, 0, unit != 0 ? AddressToRef((void*) unit) : NILREF);
				SetArraySlotRef(args, 1, MAKEINT(id));
				handled = ScriptHandled(cmd, RunScript(RSSYMviewgesturescript, args, true));
				if (handled)
					gRootView->fDirtyFlag = true;
			}
			newton_catch_all
			{
				CommandSetResult(cmd, 1);
				handled = true;
			}
			end_try;
		}
		break;

	case aeWord:
		// (the view's own viewWordScript only: not its parents')
		handled = ScriptHandled(cmd, RunScript(RSSYMviewwordscript, RefVar(UnitArgs(cmd)), false));
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		break;

	case aeRawInk:
	case aeInkWord:
		{
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, GetStrokeBundleFromCommand(cmd));
			// (the view's own script only: not its parents')
			handled = ScriptHandled(cmd, RunCacheScript(id == aeRawInk ? kIndexViewRawInkScript : kIndexViewInkWordScript, args, false));
		}
		break;

	case aeKeyUp:
	case aeKeyDown:
	case aeKeyString:
	case aeKeyRepeat:
		HandleKeyEvent(cmd, id, nil);
		handled = true;
		break;

	case aeAddChild:
		{
			TView* child = AddChild(RefVar(CommandFrameParameter(cmd)));
			RefVar show(MakeCommand(aeShow, child, CommandParameter(cmd)));
			gApplication->DispatchCommand(show);
			handled = true;
		}
		break;

	case aeDropChild:
		{
			TView* child = (TView*) CommandParameter(cmd);
			child->Hide();
			RemoveChildView(child);
			handled = true;
		}
		break;

	case aeHide:
		Hide();
		handled = true;
		break;

	case aeShow:
		// while a modal dialog is up, a view put straight on the root is
		// not shown until it goes (ModalSafeShow) - unless it is marked as
		// safe to show over one (viewJustify 0x40000000), or the command
		// says not to ask (kNoModalCheck)
		if (gModalCount != 0 && CommandParameter(cmd) != kNoModalCheck)
		{
			Boolean safe = false;
			if (fParent != gRootView)
				safe = true;
			else
			{
				for (TView* view = this; view != gRootView; view = view->fParent)
					if ((view->fViewJustify & 0x40000000) != 0)
					{
						safe = true;
						break;
					}
			}
			if (!safe)
			{
				ModalSafeShow(this);
				handled = true;
				break;
			}
		}
		Show();
		handled = true;
		break;

	case aeScrollUp:
		RunScript(RSSYMviewscrollupscript, RefVar(NILREF), true);
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		handled = true;
		break;

	case aeScrollDown:
		RunScript(RSSYMviewscrolldownscript, RefVar(NILREF), true);
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		handled = true;
		break;

	case aeOverview:
		RunScript(RSSYMviewoverviewscript, RefVar(NILREF), true);
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		handled = true;
		break;

	case aeRemoveAllHilites:
		RemoveAllHilites();
		handled = true;
		break;

	case aeAddData:
		{
			TView* child = AddToSoup(RefVar(CommandFrameParameter(cmd)));
			if (child != nil)
			{
				long wantedId = CommandParameter(cmd);
				if (wantedId != kNoParameter)
					child->fId = wantedId;
				CommandSetParameter(cmd, (Long) child);
				gApplication->PostUndoCommand(aeRemoveData, this, child->fId);
				child->Dirty(nil);
			}
			else
				CommandSetParameter(cmd, 0);
			SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
			handled = true;
		}
		break;

	case aeRemoveData:
		{
			TView* child = FindID(CommandParameter(cmd));
			if (child != nil)
			{
				long childId = child->fId;
				RefVar data(child->DataFrame());
				RemoveFromSoup(child);
				RefVar undo(MakeCommand(aeAddData, this, childId));
				CommandSetFrameParameter(undo, data);
				gApplication->PostUndoCommand(undo);
			}
			SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
			handled = true;
		}
		break;

	case aeMoveData:
		{
			Point delta;
			delta.h = (short) CommandIndexParameter(cmd, 0);
			delta.v = (short) CommandIndexParameter(cmd, 1);
			Move(delta);
			RefVar undo(MakeCommand(aeMoveChild, fParent, fId));
			CommandSetIndexParameter(undo, 0, -delta.h);
			CommandSetIndexParameter(undo, 1, -delta.v);
			gApplication->PostUndoCommand(undo);
			SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
			handled = true;
		}
		break;

	case aeScaleData:
		// the view scaled as the rectangle in params 0-1 maps onto the one
		// in 2-3; the undo maps them back
		handled = true;			// (read-only or not: taken)
		if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
		{
			Rect src, dst;
			CommandIndexRect(cmd, 0, &src);
			CommandIndexRect(cmd, 2, &dst);
			Scale(src, dst);
			RefVar undo(MakeCommand(aeScaleData, this, kNoParameter));
			CommandSetIndexRect(undo, 0, dst);
			CommandSetIndexRect(undo, 2, src);
			gApplication->PostUndoCommand(undo);
			fParent->Dirty(nil);
		}
		break;

	case aeAddHilite:
		{
			RefVar hilite(CommandFrameParameter(cmd));
			// a frame carries the hilite in its 'hilite slot
			if (IsFrame(hilite))
				hilite = GetFrameSlotRef(hilite, RSSYMhilite);
			RefVar hilites(GetFrameSlotRef(fContext, RSSYMhilites));
			if (ISNIL(hilites))
			{
				hilites = MakeArray(0);
				SetFrameSlot(fContext, RSSYMhilites, hilites);
			}
			AddArraySlot(hilites, hilite);
			Dirty(nil);
			handled = true;
		}
		break;

	case aeRemoveHilite:
		RemoveHilite(RefVar(CommandFrameParameter(cmd)));
		handled = true;
		break;

	case aeToChildren:
		{
			TViewLoop loop(fChildren);
			for (TView* child = loop.Next(); child != nil; child = loop.Next())
				child->RealDoCommand(cmd);
			gRootView->fDirtyFlag = true;
			handled = true;
		}
		break;

	case aeToHilitedChildren:
		{
			CList* hilited = CList::Make();
			if (hilited != nil)
			{
				TViewLoop loop(fChildren);
				for (TView* child = loop.Next(); child != nil; child = loop.Next())
					if (child->Hilited())
						hilited->Insert(child);
				TListLoop hilitedLoop(hilited);
				for (TView* child = (TView*) hilitedLoop.Next(); child != nil; child = (TView*) hilitedLoop.Next())
					child->RealDoCommand(cmd);
				delete hilited;
			}
			gRootView->fDirtyFlag = true;
			handled = true;
		}
		break;

	case aeMoveChild:
		{
			TView* child = FindID(CommandParameter(cmd));
			if (child != nil)
			{
				CommandSetID(cmd, aeMoveData);
				child->RealDoCommand(cmd);
			}
			SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
			handled = true;
		}
		break;

	default:
		break;
	}
	return handled;
}


// ROM 0x0025fbdc TextFlags__5TViewCFv
// The textFlags slot, 0 when none.
long
TView::TextFlags(void) const
{
	Ref flags = GetProto(RSSYMtextflags);
	return ISINT(flags) ? RVALUE(flags) : 0;
}


// ROM 0x0025fc34 InsideView__5TViewFR6TPoint
// Whether the point is within the outer bounds.
Boolean
TView::InsideView(Point& pt)
{
	Rect bounds;
	OuterBounds(&bounds);
	return PtInRect(pt, &bounds);
}


// ROM 0x002657cc ChildBoundsChanged__5TViewFP5TViewR5TRect
void
TView::ChildBoundsChanged(TView* /*child*/, Rect& /*bounds*/)
{ }


// ROM 0x002657d0 SetupForm__5TViewFv
void
TView::SetupForm(void)
{
	RunScript(RSSYMviewsetupformscript, RefVar(NILREF));
}


// ROM 0x002658c0 SetupDone__5TViewFv
void
TView::SetupDone(void)
{
	RunScript(RSSYMviewsetupdonescript, RefVar(NILREF));
}


// ROM 0x0026a800 GetRangeText__5TViewFlT1
Ref
TView::GetRangeText(long /*start*/, long /*end*/)
{
	return NILREF;
}


// ROM 0x0026a808 GetValue__5TViewFRC6RefVarT1
// The slot's value, as the type asks: viewFlags is the view's own word,
// 'hilites with 'offset NOT YET (nil), otherwise the variable; a type the
// value is not a subclass of converts it: 'string prints it, 'int takes a
// char or a boolean (1 for true) as its number, else nil.
Ref
TView::GetValue(RefArg slot, RefArg type)
{
	RefVar value;
	if (EQRef(slot, RSSYMhilites) && EQRef(type, RSSYMoffset))
		return NILREF;		// NOT YET RECONSTRUCTED: the hilite offsets
	if (EQRef(slot, RSSYMviewflags))
		value = MAKEINT(fFlags & vViewFlagsMask);
	if (ISNIL(value))
		value = GetVar(slot);
	if (ISNIL(type))
		return value;
	RefVar valueClass(ClassOf(value));
	if (IsSubclassRef(valueClass, type))
		return value;
	if (EQRef(type, RSSYMstring))
		return SPrintObject(value);
	if (EQRef(type, RSSYMint))
	{
		if (EQRef(valueClass, RSSYMchar))
			return MAKEINT(RCHAR(value));
		if (EQRef(valueClass, RSSYMboolean))
			return MAKEINT(NOTNIL(value) ? 1 : 0);
	}
	return NILREF;
}


// ROM 0x0026a9ec SetValue__5TViewFRC6RefVarT1
// The slot set in the context (viewFlags and viewFormat in the view too;
// recConfig and dictionaries NOT YET: the recognition area cache), the
// view synced when the slot is viewBounds, viewFormat, viewJustify or
// viewFont, and Changed sent.
void
TView::SetValue(RefArg slot, RefArg value)
{
	if (EQRef(slot, RSSYMviewflags))
		fFlags = (RINT(value) & vViewFlagsMask) | (fFlags & ~vViewFlagsMask);
	else if (EQRef(slot, RSSYMviewformat))
		fViewFormat = RINT(value);
	SetContextSlot(slot, value);
	if (EQRef(slot, RSSYMviewbounds) || EQRef(slot, RSSYMviewformat) || EQRef(slot, RSSYMviewfont))
		Sync();
	else if (EQRef(slot, RSSYMviewjustify))
	{
		InvalidateSlotCache(kIndexViewJustify);
		Sync();
	}
	Changed(slot);
}


// ROM 0x0026ac34 Changed__5TViewFRC6RefVar
void
TView::Changed(RefArg slot)
{
	Changed(slot, fContext);
}


// ROM 0x0026ac40 Changed__5TViewFRC6RefVarT1
// The tied views (viewTie: [view, message, ...] pairs) are sent their
// messages with [context, slot]; the viewChangedScript runs (even for a
// vNoScripts view) with [slot, context]; the view is dirtied.
void
TView::Changed(RefArg slot, RefArg context)
{
	if (FrameHasSlotRef(fContext, RSSYMviewtie))
	{
		RefVar ties(GetFrameSlotRef(fContext, RSSYMviewtie));
		long count = Length(ties);
		RefVar args(MakeArray(2));
		SetArraySlotRef(args, 0, fContext);
		SetArraySlotRef(args, 1, slot);
		for (long i = 0; i < count; i += 2)
		{
			RefVar message(GetArraySlotRef(ties, i + 1));
			RefVar receiver(GetArraySlotRef(ties, i));
			DoMessage(receiver, message, args);
		}
	}
	RefVar args(MakeArray(2));
	SetArraySlotRef(args, 0, slot);
	SetArraySlotRef(args, 1, context);
	ULong flags = fFlags;
	ClearFlags(vNoScripts);
	RunCacheScript(kIndexViewChangedScript, args, true);
	if (flags & vNoScripts)
		SetFlags(vNoScripts);
	Dirty(nil);
}


// ROM 0x002660c4 Hilite__5TViewFUc
// The view shown hilited (or not): nothing for a view that is not
// visible; otherwise, within the view's visible region, the
// viewHiliteScript run with [on] - a non-nil result means it did the
// hiliting - else the bounds, let out by the format's inset, inverted
// (as a round rectangle of the format's radius less the pen, when there
// is a radius).  The caret is hidden while the view is drawn on when
// its rectangle overlaps the view's (NOT YET: the caret).
void
TView::Hilite(Boolean on)
{
	if (!VisibleDeep())
		return;
	TRegion savedRgn(SetupVisRgn());
	TRegionVar savedVisRgn(savedRgn);
	// NOT YET RECONSTRUCTED: gRootView->GetCaretRect / HideCaret when it overlaps OuterBounds
	unwind_protect
	{
		Boolean done = false;
		if (NOTNIL(GetCacheProto(kIndexViewHiliteScript)))
		{
			RefVar args(MakeArray(1));
			if (on)
				SetArraySlot(args, 0, RefVar(TRUEREF));
			done = NOTNIL(RunCacheScript(kIndexViewHiliteScript, args));
		}
		if (!done)
		{
			Rect bounds = viewBounds;
			long inset = (fViewFormat & vfInsetMask) >> vfInsetShift;
			long round = ((fViewFormat & vfRoundMask) >> vfRoundShift) * 2;
			InsetRect(&bounds, -inset, -inset);
			if (round != 0)
			{
				long pen = (fViewFormat & vfPenMask) >> vfPenShift;
				if (pen > 1)
				{
					round -= (pen - 1) * 2;
					if (round < 0)
						round = 0;
				}
			}
			if (round == 0)
				InvertRect(&bounds);
			else
				InvertRoundRect(&bounds, round, round);
		}
	}
	on_unwind
	{
		// NOT YET RECONSTRUCTED: gRootView->ShowCaret()
		GrafPort* port;
		GetPort(&port);
		CopyRgn(savedVisRgn, port->visRgn);
	}
	end_unwind;
}


// ROM 0x00269c38 HandleKeyEvent__5TViewFRC6RefVarUlPUc
// A key command to the view: aeKeyString runs the viewKeyStringScript
// with [string].  For the others the parameter's character, key code
// and modifiers make the arguments [char, int]: the int is the key
// code's unmodified character (TranslateKey without modifiers; a
// function key's own character or escape kept) with the key code and
// modifiers above it.  A repeat runs the viewKeyRepeatScript, or the
// viewKeyDownScript when there is none; a key up the viewKeyUpScript.
// A key down nobody took then looks for a key command (FindKeyCommand:
// unless the view takes its own keys, TextFlags 0x1000, and it is not
// a command keystroke) and sends its keyMessage (a repeat only when the
// command's modifiers have bit 2); and caps lock, when nobody took it,
// clicks and tells the _infoButtons' contexts :SetCapsLock(on).
// isCommandKey answers whether the keystroke was one.  ==> handled.
Boolean
TView::HandleKeyEvent(RefArg cmd, ULong id, Boolean* isCommandKey)
{
	RefVar args;
	if (id == aeKeyString)
	{
		args = MakeArray(1);
		SetArraySlot(args, 0, RefVar(CommandFrameParameter(cmd)));
		return NOTNIL(RunCacheScript(kIndexViewKeyStringScript, args));
	}
	ULong parameter = (ULong) CommandParameter(cmd);
	ULong ch = KeyEventChar(parameter);
	ULong keyCode = KeyEventKeyCode(parameter);
	Boolean isDown = id == aeKeyDown || id == aeKeyRepeat;
	ULong translated = ch;
	Boolean translate = true;
	if (keyCode == 0 && isDown && ch >= kFunctionKeyCharFirst && ch <= kFunctionKeyCharLast)
		translate = false;
	if (translate && ch != kEscapeChar)
	{
		ULong deadState = 0;
		translated = TranslateKey(keyCode, isDown, 0, &deadState);
	}
	args = MakeArray(2);
	SetArraySlot(args, 0, RefVar(MAKECHAR(ch)));
	SetArraySlot(args, 1, RefVar(MAKEINT(translated | (parameter & 0xffff0000))));
	Boolean ran = false;
	RefVar result;
	if (id == aeKeyRepeat)
		result = RunCacheScript(kIndexViewKeyRepeatScript, args, false, &ran);
	if (!ran)
		result = RunCacheScript(isDown ? kIndexViewKeyDownScript : kIndexViewKeyUpScript, args);
	Boolean handled = NOTNIL(result);
	Boolean commandKeystroke = isDown && IsCommandKeystroke((UniChar) translated, parameter);
	if (isCommandKey != nil)
		*isCommandKey = commandKeystroke;
	if (isDown && !handled && ch != 0)
	{
		if ((TextFlags() & 0x1000) == 0 || commandKeystroke)
		{
			RefVar keyCommand(FindKeyCommand(this, (UniChar) translated, parameter & 0x3e000000));
			if (NOTNIL(keyCommand))
			{
				RefVar message(GetFrameSlotRef(keyCommand, RSSYMkeymessage));
				if (NOTNIL(message))
				{
					Boolean send = true;
					if (id == aeKeyRepeat)
					{
						RefVar modifiers(GetFrameSlotRef(keyCommand, RSSYMmodifiers));
						if (ISNIL(modifiers) || (RINT(modifiers) & 4) == 0)
							send = false;
						else
							gInRepeatedKeyCommand = true;
					}
					if (send)
						SendKeyMessage(this, message);
					handled = true;
					gInRepeatedKeyCommand = false;
				}
			}
		}
	}
	if (!handled && isDown && keyCode == kCapsLockKey && (parameter & 0x1000000) == 0)
	{
		// NOT YET RECONSTRUCTED: FClicker
		RefVar buttons(GetFrameSlotRef(gVarFrame, RSSYM_infobuttons));
		if (IsArray(buttons))
		{
			for (long i = 0; i < Length(buttons); i++)
			{
				RefVar button(GetArraySlotRef(buttons, i));
				NSSend(button, RSSYMsetcapslock, RefVar(MAKEBOOLEAN(gHardCapsLock)));
			}
		}
		handled = true;
	}
	return handled;
}


// ROM 0x00266b6c Select__5TViewFUcT1
// The view selected (hilited) or not: unique first deselects the
// parent's selected child; then the vSelected flag set or cleared, and
// Hilite told, when it changes.
void
TView::Select(Boolean on, Boolean unique)
{
	if (unique)
		fParent->SelectNone();
	if (on)
	{
		if ((fFlags & vSelected) == 0)
		{
			SetFlags(vSelected);
			Hilite(true);
		}
	}
	else if (fFlags & vSelected)
	{
		ClearFlags(vSelected);
		Hilite(false);
	}
}


// ROM 0x002662fc SelectNone__5TViewFv
// The first selected child un-hilited (its flag stays as it is: Select
// clears it for its own view).
void
TView::SelectNone(void)
{
	TListLoop loop(fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
	{
		if (child->fFlags & vSelected)
		{
			child->Hilite(false);
			break;
		}
	}
}


// ROM 0x0026a6a0 SetCaretOffset__5TViewFPlT1
void
TView::SetCaretOffset(long* /*offset*/, long* /*length*/)
{ }


// ROM 0x0026a69c SetSelection__5TViewFRC6RefVarPlT2
void
TView::SetSelection(RefArg /*selection*/, long* /*start*/, long* /*end*/)
{ }


// ROM 0x0026a688 GetSelection__5TViewFv
Ref
TView::GetSelection(void)
{
	return NILREF;
}


// ROM 0x0026a6a4 ActivateSelection__5TViewFUc
// The view gains or loses the caret: its viewCaretActivateScript(on).
void
TView::ActivateSelection(Boolean on)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(args, 0, MAKEBOOLEAN(on));
	RunCacheScript(kIndexViewCaretActivateScript, args);
}


// ROM 0x0009e7ec DoEditCommand__5TViewFl
// The editing commands, all of them made of the drag and drop's parts:
//
//   0 cut    - a copy and then a clear;
//   1 copy   - the view's drag items made a clipping (TClipboard::
//              NewClipboard), laid out in the first item's `bounds`, or
//              where the selection is;
//   2 paste  - the front clipping delivered to the view under the caret
//              (or, with no caret, under the middle of the view; on a
//              page, at its top left one line down) as a drop would be
//              - the selection cleared first unless the view's
//              dragOptions say not to (clearOnPaste nil) - and the
//              clipping thrown away;
//   3 paste, keeping the clipping;
//   4 clear  - the view's drag items taken out of it (DropRemove).
//
// The screen is brought up to date afterwards.  ==> true.
Boolean
TView::DoEditCommand(long command)
{
	TDragInfo dragInfo(0L);
	switch (command)
	{
	case 0:
		DoEditCommand(1);
		DoEditCommand(4);
		break;

	case 1:
		{
			Rect bounds;
			bounds.top = bounds.bottom = -32768;
			AddDragInfo(&dragInfo);
			RefVar items(dragInfo.GetItems());
			if (Length(items) > 0)
			{
				RefVar item(GetArraySlotRef(items, 0));
				RefVar itemBounds(GetProtoVariable(item, RSSYMbounds, nil));
				if (NOTNIL(itemBounds))
					FromObject(itemBounds, bounds);
				if (bounds.top == -32768)
					GlobalHilitePinnedBounds(&bounds);
				Dirty(nil);
				TClipboard::NewClipboard(dragInfo, this, bounds, nil);
			}
		}
		break;

	case 2:
	case 3:
		{
			TClipboard* clipboard = (TClipboard*) gRootView->GetClipboard();
			TView* icon = gRootView->GetClipboardIcon();
			if (clipboard == nil || icon == nil)
				break;
			Boolean clearOnPaste = true;
			RefVar options(GetProto(RSSYMdragoptions));
			if (NOTNIL(options))
				clearOnPaste = NOTNIL(GetProtoVariable(options, RSSYMclearonpaste, nil));
			Point pt;
			pt.v = -32768;
			pt.h = 0;
			if (gRootView->CaretEnabled())
				gRootView->GetCaretPoint(&pt);
			Boolean atCaret = true;
			if (pt.v == -32768)
			{
				pt = MidPoint(viewBounds);
				atCaret = false;
			}
			TDragInfo pasted(0L);
			clipboard->GetClipboardDataInfo(&pasted);
			TView* target = TargetDrop(pasted, pt);
			if (!atCaret && target != nil && target->DerivedFrom(clEditView))
			{
				// on a page: its top left, a line down
				RefVar spacing(GetVariable(RefVar(target->fContext), RSSYMviewlinespacing, nil, 0));
				short lineSpacing = NOTNIL(spacing) ? (short) RINT(spacing) : 0;
				pt.v = viewBounds.top;
				pt.h = (short) (viewBounds.left + 5);
				pt.v = (short) (pt.v + lineSpacing);
			}
			if (target != nil && DropApprove(target))
			{
				if (clearOnPaste)
				{
					DoEditCommand(4);
					if (!atCaret)
					{
						Point caret;
						caret.v = -32768;
						caret.h = 0;
						if (gRootView->CaretEnabled())
							gRootView->GetCaretPoint(&caret);
						if (caret.v != -32768)
							pt = caret;
					}
				}
				// the clipping's data lands with its top left at the point
				Point iconOrigin;
				iconOrigin.v = icon->viewBounds.top;
				iconOrigin.h = icon->viewBounds.left;
				Rect data;
				data.top = data.bottom = iconOrigin.v;
				data.left = data.right = iconOrigin.h;
				clipboard->CalcDataBitsBounds(&data);
				Point dragPt;
				dragPt.h = (short) (pt.h + (short) (iconOrigin.h - data.left));
				dragPt.v = (short) (pt.v + (short) (iconOrigin.v - data.top));
				clipboard->EndDrag(pasted, target, iconOrigin, pt, dragPt, true);
				if (command == 2)
					gRootView->RemoveClipboard();
			}
		}
		break;

	case 4:
		{
			TView* view = nil;
			if (DerivedFrom(clParagraphView))
				view = ((TDataView*) this)->GetEnclosingEditView();
			if (view == nil)
				view = this;
			view->Dirty(nil);
			if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
			{
				AddDragInfo(&dragInfo);
				for (long i = Length(RefVar(dragInfo.GetItems())); --i >= 0; )
					DropRemove(RefVar(dragInfo.GetItemDragRef(i)));
			}
		}
		break;
	}
	gRootView->fDirtyFlag = true;
	gRootView->Update(nil);
	return true;
}


// ROM 0x0026a748 OffsetToCaret__5TViewFlP5TRect
// A plain view has no caret: the rect's top and bottom -32768, the
// caret's "nowhere" (left and right are not touched).
void
TView::OffsetToCaret(long /*offset*/, Rect* caret)
{
	caret->top = -32768;
	caret->bottom = -32768;
}


// ROM 0x0026a728 PointToCaret__5TViewFR6TPointP5TRectT2
// Nowhere, as OffsetToCaret.
void
TView::PointToCaret(Point& /*pt*/, Rect* caret, Rect* /*bounds*/)
{
	caret->top = -32768;
	caret->bottom = -32768;
}


// ROM 0x0025fa5c RemoveAllViews__5TViewFv
// Every child deleted (the view marked as going while they are, so that
// the children do not update the viewChildren slot), the list freed.
void
TView::RemoveAllViews(void)
{
	if (fChildren == gEmptyViewList)
		return;
	Boolean marked = (fFlags & vIsBeingDeleted) != vIsBeingDeleted;
	if (marked)
		SetFlags(vIsBeingDeleted);
	while (fChildren->GetArraySize() != 0)
	{
		TView* child = fChildren->At(0);
		fChildren->RemoveElementsAt(0, 1);
		child->Delete();
	}
	if (marked)
		ClearFlags(vIsBeingDeleted);
	if (fChildren != gEmptyViewList && fChildren != nil)
		delete fChildren;
	fChildren = gEmptyViewList;
}


// ROM 0x00268b3c Idle__5TViewFl
// The viewIdleScript's answer: the delay until the next idle, 0 for none.
long
TView::Idle(long /*arg*/)
{
	RefVar result(RunCacheScript(kIndexViewIdleScript, RefVar(NILREF)));
	return ISINT(result) ? RVALUE(result) : 0;
}


/*------------------------------------------------------------------------------
	T h e   d a t a   h i l i t e s
	What is selected *inside* a view, as against TView::Hilite, which
	inverts the whole of one because it is being pressed.  Each hilite is a
	C++ THilite (Hilites.h) whose address sits in the view's `hilites` array
	as a pointer Ref; the base class only keeps them, a data view being the
	one that knows what its items are and draws them.
------------------------------------------------------------------------------*/

// ROM 0x00267188 DrawHiliting__5TViewFv
// The base draws no hiliting of its own.
void	TView::DrawHiliting(void)									{ }


// ROM 0x0026715c DrawHilitedData__5TViewFv
// The whole view, drawn again - a data view that hilites part of itself
// overrides this with something smaller.
void
TView::DrawHilitedData(void)
{
	Draw(viewBounds, false);
}


// the pen-driven hiliting: NOT YET RECONSTRUCTED (the recogniser's units)
// ROM 0x00262150 HandleHilite__5TViewFP11TUnitPubliclUc
// A stroke over the view selects the whole of it: the unit's box, grown by
// eight pixels, has to cover more than 60 per cent of the view.  A stroke
// that is long and thin counts by its long axis alone - a line drawn across
// a one-line view covers little of it, so the short axis is taken out of
// both rectangles and the test made on the other one.  Only the hilite and
// un-hilite kinds (1 and -1) are looked at, and a false `doIt` asks
// whether the stroke would count without acting on it.  ==> the kind
// taken, or 0.
long
TView::HandleHilite(TUnitPublic* unit, long gesture, Boolean doIt)
{
	if (gesture != 1 && gesture != -1)
		return false;
	Rect strokeBounds;
	unit->Bounds(&strokeBounds);
	InsetRect(&strokeBounds, -8, -8);
	Rect mine = viewBounds;
	Boolean covered = CoveredBy(&mine, &strokeBounds) > 60;
	if (!covered)
	{
		long width = strokeBounds.right - strokeBounds.left;
		long height = strokeBounds.bottom - strokeBounds.top;
		if (width >= 2 * height
			&& strokeBounds.left >= mine.left && strokeBounds.right <= mine.right)
		{
			// a flat stroke lying within the view: judge it by its height alone
			strokeBounds.left = 0;  strokeBounds.right = 1;
			mine.left = 0;          mine.right = 1;
		}
		else if (height >= 2 * width
			&& strokeBounds.top >= mine.top && strokeBounds.bottom <= mine.bottom)
		{
			// and a tall one by its width
			strokeBounds.top = 0;   strokeBounds.bottom = 1;
			mine.top = 0;           mine.bottom = 1;
		}
		else
			return false;
		covered = CoveredBy(&mine, &strokeBounds) > 60;
	}
	if (!covered)
		return false;
	if (doIt)
		HiliteAll();
	return true;
}


// ROM 0x00262528 HandleScrub__5TViewFRC5TRectlP11TUnitPublicUc
// A scrub over the view: the answer is the gesture the view takes (5) when
// the scrub covers more than 75 per cent of it.  A read-only or
// write-protected view takes none.  The base only answers - the caller is
// what acts - so the unit and `doIt` go unused here.
long
TView::HandleScrub(const Rect& bounds, long gesture, TUnitPublic* /*unit*/, Boolean /*doIt*/)
{
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return false;
	if (gesture != 5 && gesture != -1)
		return false;
	return CoveredBy(&viewBounds, &bounds) > 75 ? 5 : 0;
}


// ROM 0x00261de4 Hilited__5TViewFv
// Whether anything in the view is selected.
Boolean
TView::Hilited(void)
{
	RefVar hilites(Hilites());
	return NOTNIL(hilites) && Length(hilites) != 0;
}


// ROM 0x00261d74 Hilites__5TViewFv
// The view's selections: the `hilites` slot of the context.
Ref		TView::Hilites(void)		{ return GetProto(RSSYMhilites); }


// ROM 0x00261e30 FirstHilite__5TViewFv
// The first hilite, or nil.
Ref
TView::FirstHilite(void)
{
	RefVar hilites(Hilites());
	if (NOTNIL(hilites) && Length(hilites) != 0)
		return GetArraySlotRef(hilites, 0);
	return NILREF;
}


// ROM 0x00261e94 DrawHilites__5TViewFUc
// The base draws none; a data view (TParagraphView) overrides it.
void	TView::DrawHilites(Boolean)									{ }


// ROM 0x00262000 IsCompletelyHilited__5TViewFRC6RefVar
// Whether the hilite covers a whole item: true for a view with no items of
// its own to be partly selected.
Boolean	TView::IsCompletelyHilited(RefArg)							{ return true; }


// ROM 0x00262008 HiliteAll__5TViewFv
// One hilite over the whole view, in the view's own coordinates.  It goes
// in through the command, so that it can be undone like any other.
void
TView::HiliteAll(void)
{
	RemoveAllHilites();
	THilite* hilite = new THilite;
	if (hilite == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	SetRect(&hilite->fBounds, 0, 0,
			viewBounds.right - viewBounds.left, viewBounds.bottom - viewBounds.top);
	RefVar cmd(MakeCommand(aeAddHilite, this, 0x8000000));
	CommandSetFrameParameter(cmd, RefVar(AddressToRef(hilite)));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x00262104 DeleteHilited__5TViewFRC6RefVar
// The parent is asked to delete what is selected here - a data view keeps
// the items, so the child that holds the hilite is not the one that owns
// them.  The hilite the caller names is not looked at.
void
TView::DeleteHilited(RefArg)
{
	RefVar cmd(MakeCommand(aeRemoveData, fParent, fId));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x00261e98 RemoveHilite__5TViewFRC6RefVar
// Out of the array, the C++ object disposed of, and the parent invalidated
// over where it was (the hilite's bounds are the view's own coordinates);
// when nothing is selected any more the root forgets us as its hiliter.
void
TView::RemoveHilite(RefArg hilite)
{
	RefVar hilites(GetFrameSlotRef(fContext, RSSYMhilites));
	if (NOTNIL(hilites))
		ArrayRemove(hilites, hilite);
	THilite* object = (THilite*) RefToAddress(hilite);
	Rect bounds;
	SetEmptyRect(&bounds);
	if (object != nil)
	{
		bounds = object->fBounds;
		delete object;
	}
	OffsetRect(&bounds, viewBounds.left, viewBounds.top);
	if (fParent != nil)
		fParent->Dirty(&bounds);
	if (!Hilited() && gRootView->fHiliter == this)
		gRootView->fHiliter = nil;
}


// ROM 0x00261f64 RemoveAllHilites__5TViewFv
// Each one in turn.  Removing shortens the array under the loop, so the
// loop's index and count are stepped back with it.
void
TView::RemoveAllHilites(void)
{
	HiliteLoop loop(this);
	while (loop.Next())
	{
		RemoveHilite(loop.fHilite);
		loop.fIndex--;
		loop.fCount--;
	}
	if (gRootView->fHiliter == this)
		gRootView->fHiliter = nil;
}


// ROM 0x002622d8 GlobalHiliteBounds__5TViewFP5TRect
// The union of the hilites' bounds, in the parent's coordinates, added to
// whatever the caller had in bounds already (an empty rect, usually), and
// the answer is the click options that go with the selection - bit 1 says
// it can be resized, and TEditView::DrawHiliting draws a resize border
// round it.  A leaf view answers its own ClickOptions; a view with nothing
// selected leaves the bounds alone and answers 0.
long
TView::GlobalHiliteBounds(Rect* bounds)
{
	HiliteLoop loop(this);
	if (!loop.Next())
		return 0;
	do
	{
		Rect r = loop.fCurrent != nil ? loop.fCurrent->fBounds : viewBounds;
		OffsetRect(&r, viewBounds.left, viewBounds.top);
		UnionRect(bounds, &r, bounds);
	}
	while (loop.Next());
	return ClickOptions();
}


// ROM 0x00262414 GlobalHiliteResizeBounds__5TViewFP5TRect
// The bounds a selection may be resized within: the view's own, unioned
// with what the caller had.  Nothing selected here, nothing to add.
void
TView::GlobalHiliteResizeBounds(Rect* bounds)
{
	if (!Hilited())
		return;
	if (bounds->top == -32768 || EmptyRect(bounds))
	{
		*bounds = viewBounds;
		return;
	}
	if (EmptyRect(&viewBounds))
		return;
	if (viewBounds.top < bounds->top)
		bounds->top = viewBounds.top;
	if (viewBounds.left < bounds->left)
		bounds->left = viewBounds.left;
	if (viewBounds.bottom > bounds->bottom)
		bounds->bottom = viewBounds.bottom;
	if (viewBounds.right > bounds->right)
		bounds->right = viewBounds.right;
}


// ROM 0x0026244c GlobalHilitePinnedBounds__5TViewFP5TRect
// The base pins a selection to where the hilites are.
void
TView::GlobalHilitePinnedBounds(Rect* bounds)
{
	GlobalHiliteBounds(bounds);
}


// ROM 0x00262a20 IsGridded__5TViewFRC6RefVarP6TPoint
// Whether the view's viewGrid is the kind asked about, and how far apart
// the grid is: viewLineSpacing both ways, except that a line grid has no
// horizontal step at all.  A view with no viewLineSpacing is on a grid of
// one.
Boolean
TView::IsGridded(RefArg gridKind, Point* spacing)
{
	if (!EQRef(GetProto(RSSYMviewgrid), gridKind))
		return false;
	if (spacing != nil)
	{
		RefVar lineSpacing(GetProto(RSSYMviewlinespacing));
		short apart = (short) (ISNIL(lineSpacing) ? 1 : RINT(lineSpacing));
		spacing->v = apart;
		spacing->h = EQRef(gridKind, RSSYMlinegrid) ? 0 : apart;
	}
	return true;
}


// ROM 0x00262454 PointInHilite__5TViewFR6TPoint
// Whether the point - in the parent's coordinates - falls on any of the
// hilites, whose bounds are the view's own.
Boolean
TView::PointInHilite(Point& pt)
{
	Point local;
	local.v = (short) (pt.v - viewBounds.top);
	local.h = (short) (pt.h - viewBounds.left);
	HiliteLoop loop(this);
	while (loop.Next())
	{
		if (loop.fCurrent != nil && loop.fCurrent->Encloses(local))
			return true;
	}
	return false;
}
long	TView::ClickOptions(void)									{ return 1; }		// ROM 0x00262568 ClickOptions__5TViewFv (a selection may be dragged)

/*------------------------------------------------------------------------------
	D r a g   a n d   d r o p
	The pen-tracked drag of a view's data onto another; the source offers
	drag items (AddDragInfo), the target under the pen is found and asked
	which types it takes (GetSupportedDropTypes/AcceptDrop), given the data
	(GetDropData from the source) and told to drop (Drop -> viewDropScript).
	Each step runs the matching view script.
------------------------------------------------------------------------------*/

// ROM 0x0009e648 AddDragInfo__5TViewFP9TDragInfo
// The view's drag items added through its viewAddDragInfoScript([items]);
// ==> whether it added any.
Boolean
TView::AddDragInfo(TDragInfo* dragInfo)
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, RefVar(dragInfo->GetItems()));
	Boolean ran = false;
	RefVar result(RunScript(RSSYMviewadddraginfoscript, args, true, &ran));
	return ran && NOTNIL(result);
}


// ROM 0x000a15c0 GetDropData__5TViewFRC6RefVarT1
// The data for a drop type, from the source's viewGetDropDataScript
// ([type, dragRef]).
Ref
TView::GetDropData(RefArg dragType, RefArg dragRef)
{
	RefVar args(MakeArray(2));
	SetArraySlot(args, 0, dragType);
	SetArraySlot(args, 1, dragRef);
	return RunScript(RSSYMviewgetdropdatascript, args, true);
}


// ROM 0x000a14fc GetSupportedDropTypes__5TViewFRC6TPoint
// The drag types the view accepts at the point, from its
// viewGetDropTypesScript([pt]); nil for none.
Ref
TView::GetSupportedDropTypes(const Point& pt)
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, RefVar(PointToFrame(pt)));
	Boolean ran = false;
	RefVar result(RunScript(RSSYMviewgetdroptypesscript, args, true, &ran));
	return (ran && ISNIL(result)) ? NILREF : (Ref) result;
}


// ROM 0x000a12c0 AcceptDrop__5TViewFRC9TDragInfoRC6TPoint
// Whether the view takes the drag at the point: its supported types
// checked against the drag's items.
Boolean
TView::AcceptDrop(const TDragInfo& dragInfo, const Point& pt)
{
	RefVar types(GetSupportedDropTypes(pt));
	if (!IsArray(types))
		return false;
	return ((TDragInfo&) dragInfo).CheckTypes(types);
}


// ROM 0x0009cbc4 Drop__5TViewFRC6RefVarT1P6TPoint
// The drop delivered to the view: viewDropScript([type, data, pt]).
Boolean
TView::Drop(RefArg dropTypes, RefArg dropData, Point* dropPt)
{
	RefVar args(MakeArray(3));
	SetArraySlot(args, 0, dropTypes);
	SetArraySlot(args, 1, dropData);
	SetArraySlot(args, 2, RefVar(PointToFrame(*dropPt)));
	return NOTNIL(RunScript(RSSYMviewdropscript, args, true));
}


// ROM 0x000a13e4 DropMove__5TViewFRC6RefVarRC6TPointT2Uc
// A drag moved within the view: viewDropMoveScript([dragRef, oldPt,
// newPt, copy]).
Boolean
TView::DropMove(RefArg dragRef, const Point& oldPt, const Point& newPt, Boolean copy)
{
	RefVar args(MakeArray(4));
	SetArraySlot(args, 0, dragRef);
	SetArraySlot(args, 1, RefVar(PointToFrame(oldPt)));
	SetArraySlot(args, 2, RefVar(PointToFrame(newPt)));
	SetArraySlot(args, 3, RefVar(copy ? TRUEREF : NILREF));
	return NOTNIL(RunScript(RSSYMviewdropmovescript, args, true));
}


// ROM 0x0009cc90 DropRemove__5TViewFRC6RefVar
// The dragged item removed from the source after a move:
// viewDropRemoveScript([dragRef]).
Boolean
TView::DropRemove(RefArg dragRef)
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, dragRef);
	return NOTNIL(RunScript(RSSYMviewdropremovescript, args, true));
}


// ROM 0x0009cd10 DropApprove__5TViewFP5TView
// Whether the source approves dropping on the target:
// viewDropApproveScript([targetContext]).  With no script the drop is
// approved.
Boolean
TView::DropApprove(TView* target)
{
	RefVar args(MakeArray(1));
	if (target != nil)
		SetArraySlot(args, 0, RefVar(target->fContext));
	Boolean ran = false;
	RefVar result(RunScript(RSSYMviewdropapprovescript, args, true, &ran));
	if (!ran)
		return true;			// no script: approved
	return NOTNIL(result);
}


// ROM 0x0009e6f4 DragFeedback__5TViewFRC9TDragInfoRC6TPointUc
// The target showing where a drag would go, or taking the showing away:
// viewDragFeedbackScript(items, pt, show).  ==> whether the script
// answered something, which is what tells a view with feedback of its own
// that the script has done it instead.
Boolean
TView::DragFeedback(const TDragInfo& dragInfo, const Point& pt, Boolean show)
{
	RefVar args(MakeArray(3));
	SetArraySlot(args, 0, RefVar(dragInfo.GetItems()));
	SetArraySlot(args, 1, RefVar(PointToFrame(pt)));
	SetArraySlot(args, 2, RefVar(show ? TRUEREF : NILREF));
	return NOTNIL(RunScript(RSSYMviewdragfeedbackscript, args, true));
}


// ROM 0x002625f4 Scale__5TViewFRC5TRectT1
// The view's bounds mapped from one rectangle onto another, its
// selection made the size of the view, and the bounds written back in
// its parent's coordinates.
void
TView::Scale(const Rect& src, const Rect& dst)
{
	TTransform transform;
	transform.Setup(&src, &dst, false);
	Rect r = viewBounds;
	::Scale(&r, transform);
	RefVar hilite(FirstHilite());
	if (NOTNIL(hilite))
	{
		THilite* h = (THilite*) RefToAddress(hilite);
		h->fBounds.bottom = (short) (r.bottom - r.top);
		h->fBounds.right = (short) (r.right - r.left);
	}
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&r, -origin.h, -origin.v);
	WriteBounds(r);
}


// ROM 0x00262570 DrawScaledData__5TViewFRC5TRectT1P5TRect
// A selected view's bounds as a resize from `src` to `dst` would leave
// them - all a plain view shows of itself while the selection is being
// resized; nothing (a top of -32768) for one that is not selected.
void
TView::DrawScaledData(const Rect& src, const Rect& dst, Rect* bounds)
{
	if (Hilited())
	{
		TTransform transform;
		transform.Setup(&src, &dst, false);
		*bounds = viewBounds;
		::Scale(bounds, transform);
		return;
	}
	bounds->top = bounds->bottom = -32768;
}


// ROM 0x0009d134 DropDone__5TViewFv
Boolean
TView::DropDone(void)
{
	return NOTNIL(RunScript(RSSYMviewdropdonescript, RefVar(NILREF), true));
}


// ROM 0x0009d5c8 TargetDrop__5TViewFRC9TDragInfoRC6TPoint
// The view that would take the drag at the point: the deepest view there
// with the drop flags, walked up to one that accepts the drag
// (FindDropViewDeep), then its FindDropView, then its viewFindTargetScript
// gets a chance to redirect.  Nil for none.
TView*
TView::TargetDrop(const TDragInfo& dragInfo, const Point& pt)
{
	TView* under = gRootView->FindView(pt, 0x1fffe00, nil);
	if (under == nil)
		return nil;
	TView* accepting = FindDropViewDeep(under, dragInfo, pt);
	if (accepting == nil)
		return nil;
	TView* target = accepting->FindDropView(dragInfo, pt);
	if (target == nil)
		return nil;
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, RefVar(((TDragInfo&) dragInfo).GetItems()));
	Boolean ran = false;
	RefVar redirect(target->RunScript(RSSYMviewfindtargetscript, args, true, &ran));
	if (ran)
		target = ISNIL(redirect) ? nil : GetView(redirect);
	return target;
}


// ROM 0x0026718c CopyProtection__5TViewCFv
// The view's copyProtection variable: bit 0 set, its data may not be
// copied (nor dragged at all); 0 when it has none.
long
TView::CopyProtection(void) const
{
	RefVar protection(GetVar(RSSYMcopyprotection));
	return NOTNIL(protection) ? RINT(protection) : 0;
}


// ROM 0x0009cdb4 EndDrag__5TViewFRC9TDragInfoP5TViewRC6TPointN23Uc
// The drag delivered.  `startPt` is where the pen went down, `dragPt`
// where the dragged image ended up (the pen, pinned to the limits and
// snapped to the target's grid) and `dropPt` the pen itself; the
// difference between the first two is how far the data moved.
//
// Dropped on the view it came from, each item is moved (DropMove, told
// the distance).  Anywhere else the target's supported types pick the
// item's type, the source view (failing that, the item's own view) gives
// the data, whose viewBounds are then moved by the distance and into the
// target's coordinates (its ContentsOrigin), and the target is told to
// Drop it.  A drop that was taken removes the item from the source
// unless it was a copy or the source may not be changed, and a target
// whose selection can be resized is redrawn, the border having moved.
// The target's DropDone ends it.
void
TView::EndDrag(const TDragInfo& info, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy)
{
	TDragInfo& dragInfo = (TDragInfo&) info;
	Point delta;
	delta.h = (short) (dragPt.h - startPt.h);
	delta.v = (short) (dragPt.v - startPt.v);
	long count = Length(RefVar(dragInfo.GetItems()));
	Point pt = dropPt;
	for (long i = 0; i < count; i++)
	{
		RefVar dragRef(dragInfo.GetItemDragRef(i));
		if (this == target)
		{
			DropMove(dragRef, delta, dropPt, copy);
			continue;
		}
		RefVar types(target->GetSupportedDropTypes(dropPt));
		RefVar type(dragInfo.FindType(i, types));
		TView* source = this;
		RefVar data(GetDropData(type, dragRef));
		if (ISNIL(data))
		{
			source = dragInfo.GetItemView(i);
			if (source == nil)
				continue;
			data = source->GetDropData(type, dragRef);
			if (ISNIL(data))
				continue;
		}
		CheckViewBounds(type, data);
		Point moved;
		moved.h = (short) (source->viewBounds.left + delta.h);
		moved.v = (short) (source->viewBounds.top + delta.v);
		Point origin = target->ContentsOrigin();
		Point by;
		by.h = (short) (moved.h - origin.h);
		by.v = (short) (moved.v - origin.v);
		OffsetBoundsRef(data, by);
		if (target->Drop(type, data, &pt))
		{
			if (!copy && (fFlags & (vReadOnly | vWriteProtected)) == 0)
				DropRemove(dragRef);
			Rect bounds;
			bounds.top = bounds.bottom = -32768;
			if ((target->GlobalHiliteBounds(&bounds) & 2) != 0)
				target->Dirty(nil);
		}
	}
	target->DropDone();
}


// ROM 0x0009d194 DragAndDrop__5TViewFP13TStrokePublicRC5TRectPC5TRectT3UcRC9TDragInfoT3
// The view's data dragged with the pen.  `bounds` is what is dragged,
// `pinBounds` the part of it that must stay inside `limitBounds` (the
// application area when there is none), `clipBounds` what a clipping
// made of it covers.  Data whose copyProtection forbids it is not
// dragged at all.
//
// Drag follows the pen; the screen under where the image was is then
// redrawn.  A drag that went somewhere and that the source approves ends
// one of three ways: on a target, which EndDrag delivers the data to;
// let go on the edge of the screen where clippings are kept, which makes
// a clipping of it (the data taken from the view unless it was a copy);
// or, for a clipping itself, the clipping moved.  The view's selection is
// taken away afterwards when the drag made a clipping or involved text
// or ran from one page to another.
//
// ==> 0 the pen did not go far enough to be a drag, 1 it was a drag
// that went nowhere, 2 the data was dropped.
//
// (the ROM first tells the busy box a drag is being tracked - BusyBoxSend
//  0x37 - which the views layer cannot reach from here)
long
TView::DragAndDrop(TStrokePublic* stroke, const Rect& bounds, const Rect* pinBounds, const Rect* clipBounds, Boolean copy, const TDragInfo& dragInfo, const Rect* limitBounds)
{
	stroke->InkOff(true);
	if ((CopyProtection() & 1) != 0)
		return 0;
	Point dropPt, dragPt;
	Boolean moved, onClipboard;
	TView* target = Drag(dragInfo, stroke, bounds, pinBounds, limitBounds, copy, &dropPt, &dragPt, &moved, &onClipboard);
	gRootView->Invalidate(TRectangularRegion(bounds), fParent);
	long result = moved;
	if ((target != nil || onClipboard) && DropApprove(target))
	{
		RefVar context(fContext);
		Boolean preserve = gRootView->SetPreserveHilites(true);
		result = 2;
		gRootView->fDirtyFlag = true;
		if (!onClipboard)
		{
			Point start = stroke->FirstPoint();
			EndDrag(dragInfo, target, start, dropPt, dragPt, copy);
		}
		else if ((fFlags & vClipboard) == 0)
		{
			TClipboard::NewClipboard(dragInfo, this, clipBounds != nil ? *clipBounds : bounds, &dropPt);
			if (!copy && (fFlags & (vReadOnly | vWriteProtected)) == 0)
				for (long i = Length(RefVar(dragInfo.GetItems())) - 1; i >= 0; i--)
					DropRemove(RefVar(dragInfo.GetItemDragRef(i)));
		}
		else
			((TClipboard*) this)->MoveIcon(dropPt);
		gRootView->SetPreserveHilites(preserve);
		if (GetView(context) == this
			&& (onClipboard
				|| DerivedFrom(clParagraphView)
				|| (this != target && target->DerivedFrom(clParagraphView))
				|| (this != target && target->DerivedFrom(clEditView) && DerivedFrom(clEditView))))
			RemoveAllHilites();
	}
	return result;
}


// ROM 0x0009d6f4 Drag__5TViewFRC9TDragInfoP13TStrokePublicRC5TRectPC5TRectT4UcP6TPointT7PUcT9
// The pen followed until it lifts, the dragged image going with it.
//
// The pen is kept to where `pinBounds` (the point it went down at when
// there is none) stays inside `limitBounds` (the application area); what
// comes back in `dragPt` is the pen so kept, and in `dropPt` the pen
// itself.  Nothing moves until the pen has gone further than the items'
// smallest minDragDistance (four pixels at most): `moved` says whether it
// did.  The image is the data drawn by DragBits, put down through a
// one-bit mask of itself over the screen it was taken from, and moved at
// most every three ticks; with no memory for it, a gray outline of the
// rectangle is dragged instead.  Each time it moves over a target (the
// pen over the limits and not over the clipboard's edge), the target is
// asked to show where the data would go (DragFeedback), snapping the
// image to its grid.  `onClipboard` says whether the pen was let go over
// the edge of the application area where clippings are kept.
//
// ==> the target it was let go over.  A drag that ends back within the
// minimum distance of where it started has no target and did not move;
// one on the clipboard's edge has no target either, and its drop point
// is taken to just outside the edge.
TView*
TView::Drag(const TDragInfo& dragInfo, TStrokePublic* stroke, const Rect& bounds, const Rect* pinBounds, const Rect* limitBounds, Boolean copy,
			Point* dropPt, Point* dragPt, Boolean* moved, Boolean* onClipboard)
{
	// the application area inset by five: the pen outside it is on the
	// clipboard's edge
	RefVar displayParams(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdisplayparams));
	Point appOrigin;
	appOrigin.h = (short) RINT(GetProtoVariable(displayParams, RSSYMappareagloballeft, nil));
	appOrigin.v = (short) RINT(GetProtoVariable(displayParams, RSSYMappareaglobaltop, nil));
	Rect clipboardArea;
	clipboardArea.top = (short) (appOrigin.v + 5);
	clipboardArea.left = (short) (appOrigin.h + 5);
	clipboardArea.right = (short) (RINT(GetProtoVariable(displayParams, RSSYMappareawidth, nil)) - 10 + clipboardArea.left);
	clipboardArea.bottom = (short) (RINT(GetProtoVariable(displayParams, RSSYMappareaheight, nil)) - 10 + clipboardArea.top);
	RefVar buttonBarPosition(GetProtoVariable(displayParams, RSSYMbuttonbarposition, nil));
	*moved = false;
	*onClipboard = false;

	long minDistance = 4;
	RefVar item;
	long count = Length(RefVar(dragInfo.GetItems()));
	for (long i = 0; i < count; i++)
	{
		item = GetArraySlotRef(RefVar(dragInfo.GetItems()), i);
		RefVar distance(GetProtoVariable(item, RSSYMmindragdistance, nil));
		if (ISINT(distance))
		{
			long d = RVALUE(distance);
			if (d < minDistance)
				minDistance = d;
		}
	}

	Point start = stroke->FirstPoint();
	Rect lastBounds = bounds;
	Rect limit;
	if (limitBounds != nil)
		limit = *limitBounds;
	else
		GetAppAreaBounds(&limit);
	Rect pin;
	if (pinBounds != nil)
		pin = *pinBounds;
	else
	{
		pin.top = pin.bottom = start.v;
		pin.left = pin.right = start.h;
	}
	long minH = (short) (limit.left - pin.left + start.h);
	long maxH = (short) (limit.right - pin.right + start.h);
	long minV = (short) (limit.top - pin.top + start.v);
	long maxV = (short) (limit.bottom - pin.bottom + start.v);
	Point lastPt;
	lastPt.v = -32768;
	lastPt.h = 0;
	Boolean haveMask = false;
	Boolean haveBits = false;
	Boolean noBits = false;

	PixelMap mask;
	DragBits bits;
	newton_try
	{
		haveMask = InitBitMap(&mask, bounds, 1, kDefaultDPI, kDefaultDPI);
		if ((fFlags & vClipboard) != 0)
		{
			bits.Constructor(this, &bounds, copy);
			haveBits = true;
			bits.fDataBits.Draw(bounds, bounds, srcXor, nil);
			bits.fDataBits.CopyIntoBitmap(&mask, srcCopy, nil);
		}
	}
	newton_catch(exOutOfMemory)
	{
		noBits = true;			// (the ROM makes the bits afresh)
	}
	end_try;

	TView* target = nil;
	ULong lastTicks = 0;
	Boolean feedbackShown = false;
	Point feedbackPt;
	Boolean firstOutline = true;
	Point pt = start;			// (the ROM's is not set until the loop runs)
	while (!stroke->Done())
	{
		pt = stroke->FinalPoint();
		*onClipboard = PointOnClipboard(pt, clipboardArea, buttonBarPosition);
		*dragPt = pt;
		*dropPt = pt;
		if (dragPt->h < minH)
			dragPt->h = (short) minH;
		else if (dragPt->h > maxH)
			dragPt->h = (short) maxH;
		if (dragPt->v < minV)
			dragPt->v = (short) minV;
		else if (dragPt->v > maxV)
			dragPt->v = (short) maxV;
		if (!*moved)
			*moved = CheapDistance(*dropPt, start) > minDistance;
		if (*moved && (dropPt->h != lastPt.h || dropPt->v != lastPt.v))
		{
			if (!haveBits && !noBits)
			{
				newton_try
				{
					bits.Constructor(this, &bounds, copy);
					bits.fDataBits.CopyIntoBitmap(&mask, srcCopy, nil);
					haveBits = true;
				}
				newton_catch(exOutOfMemory)
				{
					noBits = true;
				}
				end_try;
			}
			Point delta;
			delta.h = (short) (dragPt->h - start.h);
			delta.v = (short) (dragPt->v - start.v);
			if (target != nil)
				target->AlignDragPtToGrid(dragInfo, &delta);
			Rect newBounds = bounds;
			OffsetRect(&newBounds, delta.h, delta.v);
			if (Ticks() - lastTicks > 2)
			{
				StartDrawing(nil, nil);
				if (feedbackShown)
				{
					target->DragFeedback(dragInfo, feedbackPt, false);
					feedbackShown = false;
				}
				if (!noBits)
				{
					// the screen put back where the image was, the screen
					// where it goes taken, a hole the image's shape cut
					// there and the image drawn into it
					bits.fBackground.Draw(bounds, lastBounds, srcCopy, nil);
					bits.fBackground.CopyFromScreen(newBounds, bounds, srcCopy, nil);
					GrafPort* port;
					GetPort(&port);
					CopyBits(&mask, &port->portBits, &mask.bounds, &newBounds, srcBic, nil);
					bits.fDataBits.Draw(bounds, newBounds, srcOr, nil);
				}
				else
				{
					PenState state;
					GetPenState(&state);
					PenNormal();
					SetFgPattern(GetStdPattern(grayPat));
					PenSize(2, 2);
					PenMode(patXor);
					if (!firstOutline)
						FrameRect(&lastBounds);
					else
						firstOutline = false;
					FrameRect(&newBounds);
					SetPenState(&state);
				}
				if (!*onClipboard
					&& limit.left <= dropPt->h && dropPt->h <= limit.right
					&& limit.top <= dropPt->v && dropPt->v <= limit.bottom)
				{
					target = TargetDrop(dragInfo, *dropPt);
					if (target != nil)
					{
						feedbackShown = target->DragFeedback(dragInfo, *dropPt, true);
						feedbackPt = *dropPt;
					}
				}
				StopDrawing(nil, nil);
				lastBounds = newBounds;
				lastTicks = Ticks();
			}
		}
		else
			Wait(1);
		lastPt = *dropPt;
	}
	if (haveMask)
		DisposHandle((Handle) mask.baseAddr);
	if (feedbackShown)
		target->DragFeedback(dragInfo, feedbackPt, false);
	if (*moved)
		gRootView->SmartInvalidate(lastBounds);
	PenNormal();

	long dh = start.h - dragPt->h;
	if (dh < 0)
		dh = -dh;
	long dv = start.v - dragPt->v;
	if (dv < 0)
		dv = -dv;
	if (dh <= (short) minDistance && dv <= (short) minDistance)
	{
		// back where it started: no drag at all
		target = nil;
		*moved = false;
	}
	else if (!*onClipboard)
	{
		if (target != nil)
		{
			Point delta;
			delta.h = (short) (dragPt->h - start.h);
			delta.v = (short) (dragPt->v - start.v);
			target->AlignDragPtToGrid(dragInfo, &delta);
			dragPt->h = (short) (start.h + delta.h);
			dragPt->v = (short) (start.v + delta.v);
		}
	}
	else
	{
		// on the clipboard's edge: the drop point taken to just outside
		// the edge it is over
		target = nil;
		if (pt.v <= clipboardArea.top)
			dropPt->v = (short) (clipboardArea.top - 5);
		else if (pt.h <= clipboardArea.left)
			dropPt->h = (short) (clipboardArea.left - 5);
		else if (clipboardArea.bottom > pt.v)
			dropPt->h = (short) (clipboardArea.right + 5);
		else
			dropPt->v = (short) (clipboardArea.bottom + 5);
		*dragPt = *dropPt;
	}
	return target;
}


// ROM 0x0009d498 AlignDragPtToGrid__5TViewFRC9TDragInfoP6TPoint
// A drag's offset snapped to the view's grid: a square grid snaps
// anything, a line grid only text.
void
TView::AlignDragPtToGrid(const TDragInfo& dragInfo, Point* pt)
{
	Point spacing;
	if (!IsGridded(RefVar(RSSYMsquaregrid), &spacing))
	{
		if (!IsGridded(RefVar(RSSYMlinegrid), &spacing))
			return;
		Boolean text = false;
		for (long i = Length(RefVar(dragInfo.GetItems())); --i >= 0; )
			if (NOTNIL(dragInfo.FindType(i, RefVar(RSSYMtext))))
			{
				text = true;
				break;
			}
		if (!text)
			return;
	}
	AlignPtToGrid(pt, spacing);
}


// ROM 0x0009e3bc DrawDragBackground__5TViewFRC5TRectUc
// The screen under a dragged image, as the view's script would have it
// drawn: the rectangle erased and viewDrawDragBackgroundScript(bounds,
// copy) run.  ==> whether the script answered something - failing that
// DragBits draws the background itself.
Boolean
TView::DrawDragBackground(const Rect& bounds, Boolean copy)
{
	EraseRect(&bounds);
	RefVar args(MakeArray(2));
	SetArraySlot(args, 0, RefVar(ToObject(bounds)));
	SetArraySlot(args, 1, RefVar(copy ? TRUEREF : NILREF));
	return NOTNIL(RunScript(RSSYMviewdrawdragbackgroundscript, args, true));
}


// ROM 0x0009e48c DrawDragData__5TViewFRC5TRect
// The dragged data drawn for its image: viewDrawDragDataScript(bounds),
// and failing that the view's selected data (DrawHilitedData).
void
TView::DrawDragData(const Rect& bounds)
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, RefVar(ToObject(bounds)));
	if (ISNIL(RunScript(RSSYMviewdrawdragdatascript, args, true)))
		DrawHilitedData();
}

// the default drop hooks a view without its own behaviour uses
TView*	TView::FindDropView(const TDragInfo&, const Point&)			{ return this; }		// ROM 0x000a0df4 FindDropView__5TViewFRC9TDragInfoRC6TPoint (a view is its own drop target)


/*------------------------------------------------------------------------------
	T h e   h i l i t e   s t r o k e

	A hilite click is the pen held still on a view for three quarters of
	a second and then drawn across it (`recognition/StrokeQueue.cpp`).
	The root view draws the line following the pen (TRootView::Hiliter)
	and then hands the unit to the view under it as an aeGesture2f
	command; TEditView answers that by asking its children what the
	stroke selects.
------------------------------------------------------------------------------*/

// ROM 0x00262708 AddHiliter__5TViewFP11TUnitPublic +0x14
// Whether a hilite stroke goes *round* something rather than through it.
// Its box has to be more than 24 by 16, and, from the fifth point on, it
// has to have gone more than twenty pixels from where it started and
// then come back to within twenty of it.  The stroke's length is asked
// again on every turn of the loop because the pen may still be moving.
//
// (The ROM has this inline in both AddHiliters, word for word.)
Boolean
IsLassoStroke(TUnitPublic* unit)
{
	Rect box;
	unit->Bounds(&box);
	if (box.right - box.left <= 24 || box.bottom - box.top <= 16)
		return false;
	TStrokePublic* stroke = unit->Stroke();
	long closest = 0x270f;
	Boolean wentAway = false;
	ULong i = 4;
	if (i >= (ULong) stroke->Size())
		return false;
	do
	{
		Point pt = stroke->GetPoint(i);
		Point first = stroke->FirstPoint();
		long distance = CheapDistance(first, pt);
		if (distance > 20)
			wentAway = true;
		if (wentAway && distance < closest)
			closest = distance;
		i++;
	}
	while (i < (ULong) stroke->Size());
	return closest < 20;
}


// ROM 0x00262708 AddHiliter__5TViewFP11TUnitPublic
// A hilite stroke offered to the view's children.  A lasso asks for a
// whole-object hilite (kind 1); anything else asks the children what
// they would take (-1).  They are asked twice: once to find the
// strongest claim, and once to carry that claim out, so that every child
// that can take the same kind of hilite takes it.
Boolean
TView::AddHiliter(TUnitPublic* unit)
{
	Boolean lasso = IsLassoStroke(unit);
	long kind = 0;
	TViewLoop loop(fChildren);
	TView* child;
	while ((child = loop.Next()) != nil)
	{
		long claim = child->HandleHilite(unit, lasso ? 1 : -1, false);
		if (claim > kind)
			kind = claim;
	}
	if (kind != 0)
	{
		TViewLoop again(fChildren);
		while ((child = again.Next()) != nil)
			child->HandleHilite(unit, kind, true);
	}
	return true;
}


// ROM 0x000a37ec DrawHiliteLine__FRC6TPointT1PP8PixelMapUc
// A segment of the hilite line, drawn a pixel at a time along whichever
// axis it moves further in.  Only the first four and the last four steps
// are filled ovals; everything between them is one straight line from
// the fourth oval to the fifth-from-last, which is what makes a long
// stroke cheap to draw on a twenty megahertz machine.  The first segment
// of a stroke is drawn with a pen four pixels fatter, so that the stroke
// starts with a blob where the pen was held still.
void
DrawHiliteLine(const Point& from, const Point& to, PatternHandle pattern, Boolean first)
{
	long dh = to.h - from.h;
	long dv = to.v - from.v;
	long steps = dh < 0 ? -dh : dh;
	long vSpan = dv < 0 ? -dv : dv;
	if (vSpan > steps)
		steps = vSpan;
	if (steps == 0)
		return;
	long size = first ? 12 : 8;
	PenNormal();
	PenSize(size, size);
	SetFgPattern(pattern);
	Fixed hStep = ToFixed(dh) / steps;
	Fixed vStep = ToFixed(dv) / steps;
	Fixed h = 0;
	Fixed v = 0;
	Boolean pending = false;
	Point corner;
	corner.v = 0;
	corner.h = 0;
	for (long i = 0; i < steps; i++)
	{
		Rect oval;
		oval.left = (short) (from.h + (h >> 16) - size / 2);
		oval.top = (short) (from.v + (v >> 16) - size / 2);
		oval.right = (short) (oval.left + size);
		oval.bottom = (short) (oval.top + size);
		if (i <= 3)
		{
			FillOval(&oval, pattern);
			corner.v = oval.top;
			corner.h = oval.left;
		}
		else if (steps - 4 > i)
			pending = true;
		else
		{
			if (pending)
			{
				MoveTo(corner.h, corner.v);
				LineTo(oval.left, oval.top);
				pending = false;
			}
			FillOval(&oval, pattern);
		}
		h += hStep;
		v += vStep;
		if (first)
		{
			size = 8;
			PenSize(8, 8);
			first = false;
		}
	}
}


// ROM 0x0009e528 GetClipboardDataBits__5TViewFP5TRect
// The picture a clipping keeps of what was dragged out of the view: a
// 'bits binary the size of the rectangle, with the view drawn into it.
// A heap too full for the binary answers nil, and the clipping does
// without a picture.
Ref
TView::GetClipboardDataBits(Rect* bounds)
{
	PixelMap map;
	RefVar bits(TClipboard::AllocateClipboardBits(*bounds, &map));
	if (NOTNIL(bits))
	{
		LockRefArg(bits);
		map.baseAddr = (Ptr) BinaryData(bits);
		TBits into;
		newton_try
		{
			into.Constructor(map);
			into.BeginDrawing(*(Point*) bounds);
			DrawDragData(*bounds);
		}
		newton_catch_all
		{
			into.RestorePort();
			UnlockRefArg(bits);
			rethrow;
		}
		end_try;
		into.RestorePort();
		UnlockRefArg(bits);
	}
	return bits;
}


// ROM 0x002673e8 DoMoveCommand__5TViewF6TPoint
// The view moved by the point, through the application so that the move
// can be undone (aeMoveData's two index parameters are the offsets).
void
TView::DoMoveCommand(Point by)
{
	RefVar cmd(MakeCommand(aeMoveData, this, kNoParameter));
	CommandSetIndexParameter(cmd, 0, by.h);
	CommandSetIndexParameter(cmd, 1, by.v);
	gApplication->DispatchCommand(cmd);
}


// ROM 0x0026a1c8 BuildKeyChildList__5TViewFP9TViewListlT2
// The key views under this one, front to back, appended to the list: each
// visible child asked to add its own (recursing), then the child itself
// added when it is not read-only - for the plain tab order (kind 0), when
// its textFlags say it wants keys (bit 0x8000) or it is a protoInputLine;
// for the command-key order (kind 1), when it is a paragraph that is not
// protoStaticText.
void
TView::BuildKeyChildList(TViewList* list, long direction, long kind)
{
	TListLoop loop(fChildren);
	for (TView* child = (TView*) loop.Next(); child != nil; child = (TView*) loop.Next())
	{
		if ((child->fFlags & vVisible) == 0)
			continue;
		child->BuildKeyChildList(list, direction, kind);
		if (child->fFlags & vReadOnly)
			continue;
		if (kind == 0)
		{
			if ((child->TextFlags() & 0x8000) != 0 || child->ProtoedFrom(Rprotoinputline))
				list->InsertLast(child);
		}
		else if (kind == 1)
		{
			if (child->DerivedFrom(clParagraphView) && !child->ProtoedFrom(Rprotostatictext))
				list->InsertLast(child);
		}
	}
}


// ROM 0x0026a2d8 NextKeyView__5TViewFP5TViewlT2
// The key view that follows (direction 1) or precedes (-1) the focus in
// the tab order, for the given kind.  An explicit order comes first: from
// this view up to the one that holds a _tabChildren array (a plain view
// with no _tabParent starts the search one level up), whose entries are
// frame paths from that view's context to its key views; the focus's path
// is found and the entry `direction` further on (wrapping) is the answer
// (a _tabChildren that does not name the focus throws kViewErrNoKeyView).
// Failing that, the automatic order: from this view up to the container
// (a vApplication view, a protoContainerView, or one with a _tabParent),
// whose BuildKeyChildList gives the key views front to back; the one
// after the focus is the next (wrapping to the first), the one before it
// the previous (nil when the focus is not in the list).
TView*
TView::NextKeyView(TView* focus, long direction, long kind)
{
	TView* start = this;
	if ((fFlags & vApplication) == 0 && !ProtoedFrom(Rprotocontainerview) && ISNIL(GetProto(RSSYM_tabparent)))
	{
		if (this != gRootView && fParent != gRootView)
			start = fParent;
	}

	// the explicit _tabChildren order
	if (kind != 1)
	{
		TView* holder = start;
		while (ISNIL(holder->GetProto(RSSYM_tabchildren)))
		{
			if (holder == gRootView)
				goto automatic;
			holder = holder->fParent;
		}
		RefVar tabChildren(holder->GetProto(RSSYM_tabchildren));
		long count = Length(tabChildren);
		RefVar focusContext(focus->fContext);
		RefVar holderContext(holder->fContext);
		long i = 0;
		for ( ; i < count; i++)
		{
			RefVar path(GetArraySlotRef(tabChildren, i));
			if (EQRef(focusContext, RefVar(GetFramePath(holderContext, path))))
				break;
		}
		if (i >= count)
			Throw(exRootException, (void*) kViewErrNoKeyView, nil);
		long slot = (count + i + direction) % count;
		RefVar path(GetArraySlotRef(tabChildren, slot));
		return GetView(RefVar(GetFramePath(holderContext, path)));
	}

automatic:
	TView* container = start;
	while ((container->fFlags & vApplication) == 0 && !container->ProtoedFrom(Rprotocontainerview)
		&& ISNIL(container->GetProto(RSSYM_tabparent)) && container != gRootView)
		container = container->fParent;

	TViewList* list = TViewList::Make();
	container->BuildKeyChildList(list, direction, kind);
	TView* after = nil;			// the first key view seen (the forward-wrap target)
	TView* prev = (list->Count() > 1) ? list->At(list->Count() - 1) : nil;	// the view before the current (backward answer), pre-seeded with the last for the first-view wrap
	Boolean sawFocus = false;
	TListLoop loop(list);
	TView* cur = (TView*) loop.Next();
	for (;;)
	{
		TView* thisCur = cur;
		cur = after;
		if (thisCur == nil)
			break;
		if (thisCur == focus)
		{
			if (direction == -1)
				break;
			sawFocus = true;
			thisCur = prev;
		}
		else
		{
			cur = thisCur;
			if (sawFocus)
				break;
			if (after == nil)
				after = thisCur;
		}
		cur = (TView*) loop.Next();
		prev = thisCur;
	}
	if (direction >= 0)
		prev = cur;
	delete list;
	return prev;
}


/*------------------------------------------------------------------------------
	T V i e w :  s t r u c t u r e
------------------------------------------------------------------------------*/

// ROM 0x0025f1ac AddView__5TViewFRC6RefVar
// A child made from the template: its context built (or, for a template
// naming a preallocatedContext, taken from the view's variable of that
// name - when its viewFlags say visible), the view built.  ==> nil when
// no view was made.
TView*
TView::AddView(RefArg templ)
{
	RefVar context(GetProtoVariable(templ, RSSYMpreallocatedcontext, nil));
	if (ISNIL(context))
		context = BuildContext(templ, false);
	else
	{
		context = GetVar(context);
		if ((RINT(GetProtoVariable(context, RSSYMviewflags, nil)) & vVisible) == 0)
			return nil;
	}
	if (ISNIL(context))
		return nil;
	return BuildView(this, context);
}


// ROM 0x0025f8cc AddView__5TViewFP5TView
// The child appended to the list - in front of the floaters when it does
// not float itself (the ROM's ReorderView with the last index).
void
TView::AddView(TView* child)
{
	if (fChildren == gEmptyViewList)
		fChildren = TViewList::Make();
	if ((child->fFlags & vFloating) == 0)
	{
		TView* last = fChildren->GetArraySize() != 0 ? fChildren->Last() : nil;
		if (last != nil && (last->fFlags & vFloating))
		{
			fChildren->InsertAt(0, child);
			ReorderView(child, fChildren->GetArraySize() - 1);
			return;
		}
	}
	fChildren->InsertElementsBefore(fChildren->GetArraySize(), &child, 1);
}


// ROM 0x00265e4c AddChild__5TViewFRC6RefVar
// The context's view when it is already a child, else a new one.  This is
// what aeAddChild - the command Open dispatches - comes to, and it goes
// straight to BuildView: unlike AddView, it does not ask the context
// whether it is vVisible.  That is how a view that was preallocated
// invisible (the root's allocateContext children: the button bar's context
// has viewFlags 2560, the Notepad's 4) is opened at all, and why the same
// context is refused when it turns up in a parent's viewChildren.
TView*
TView::AddChild(RefArg templ)
{
	TView* child = Exists(fChildren, templ);
	if (child == nil)
		child = BuildView(this, templ);
	return child;
}


// ROM 0x00262c0c AddViews__5TViewFUc
// The children from viewChildren (Children) and stepChildren made, after
// the viewSetupChildrenScript.  Syncing (SyncChildren): the children that
// exist are kept and marked, the rest dropped (RemoveUnmarked), and every
// child synced.  A vjReflow view stops at the first child hanging below
// its bottom.
void
TView::AddViews(Boolean sync)
{
	Boolean wasInSetup = (fFlags & vIsInSetupForm) != 0;
	if (!wasInSetup)
		SetFlags(vIsInSetupForm);
	RunCacheScript(kIndexViewSetupChildrenScript, RefVar(NILREF));
	RefVar children(Children());
	long count = NOTNIL(children) ? Length(children) : 0;
	RefVar stepChildren(GetProto(RSSYMstepchildren));
	long total = count;
	if (NOTNIL(stepChildren))
		total += Length(stepChildren);
	if (total > 0)
	{
		long top = viewBounds.top;
		if (fChildren == gEmptyViewList)
			fChildren = TViewList::Make(total);
		RefVar templ;
		for (long i = 0; i < total; i++)
		{
			templ = i < count ? GetArraySlotRef(children, i) : GetArraySlotRef(stepChildren, i - count);
			TView* child = nil;
			if (sync)
				child = Exists(fChildren, templ);
			if (child == nil)
				child = AddView(templ);
			if (child != nil && sync)
				child->SetFlags(vIsMarked);
			if (i > 0 && (fViewJustify & vjReflow) && child != nil
			 && child->viewBounds.bottom - top > viewBounds.bottom - viewBounds.top)
			{
				// the child hangs below: dropped, and the stepChildren cut there
				if (i >= count)
					SetLength(stepChildren, i - count);
				RemoveChildView(child);
				break;
			}
		}
		if (sync)
		{
			RemoveUnmarked();
			TViewLoop loop(fChildren);
			for (TView* child = loop.Next(); child != nil; child = loop.Next())
				child->Sync();
		}
	}
	if (!wasInSetup)
		ClearFlags(vIsInSetupForm);
}


// ROM 0x0025f960 RemoveView__5TViewFv
// The view leaves its parent: hidden if it was visible and being set up
// is over, taken out of the list (freed when empty) and deleted.
void
TView::RemoveView(void)
{
	TView* parent = fParent;
	if ((fFlags & vVisible) && (fFlags & vIsInSetupForm) == 0)
	{
		if (HasVisRgn())
			Hide();
		Rect bounds;
		OuterBounds(&bounds);
		parent->Dirty(&bounds);
	}
	long index = parent->fChildren->GetIdentityIndex(this);
	if (index != (long) kEmptyIndex)
	{
		parent->fChildren->RemoveElementsAt(index, 1);
		if (parent->fChildren->GetArraySize() == 0)
		{
			delete parent->fChildren;
			parent->fChildren = gEmptyViewList;
		}
	}
	Delete();
}


// ROM 0x0025f96c RemoveChildView__5TViewFP5TView
void
TView::RemoveChildView(TView* child)
{
	if ((child->fFlags & vVisible) && (child->fFlags & vIsInSetupForm) == 0)
	{
		if (child->HasVisRgn())
			child->Hide();
		Rect bounds;
		child->OuterBounds(&bounds);
		Dirty(&bounds);
	}
	long index = fChildren->GetIdentityIndex(child);
	if (index != (long) kEmptyIndex)
	{
		fChildren->RemoveElementsAt(index, 1);
		if (fChildren->GetArraySize() == 0)
		{
			delete fChildren;
			fChildren = gEmptyViewList;
		}
	}
	child->Delete();
}


// ROM 0x002635f4 SyncScroll__5TViewFRC6RefVarN21
// A roll (protoRoll's viewScrollUpScript/viewScrollDownScript) scrolled a
// step through its items - an array of item templates, each with its
// `height` (a collapsed one, or every one when the roll's allCollapsed is
// set, taking the roll's collapsedHeight instead).  The roll's
// viewOriginY is how far the first item showing is scrolled into; index
// is that item.
//
// Down (direction >= 0): further into the item showing when it is taller
// than the roll, else on to the next item (nothing when there is none).
// Up: back up the item showing, else back to the one before - scrolled to
// its last whole view's height, h - h % height, when it is taller than the
// roll.  The items from the lower of the old and new index are then
// walked while they fill the roll: those from the new index on are the
// children, made where they do not exist yet (going up, put in front) and
// marked, the rest removed (RemoveUnmarked); the ones scrolled over add
// their height to how far the roll slides.  The slide is animated with
// the scroll sound over the roll less its bottom 5 pixels, or with no
// slide the roll is simply redrawn.  ==> the items now showing, nil for
// nothing to do.
Ref
TView::SyncScroll(RefArg items, RefArg indexRef, RefArg directionRef)
{
	RefVar scratch;
	long height = (short) (viewBounds.bottom - viewBounds.top);
	long direction = RINT(directionRef);
	long oldIndex = RINT(indexRef);
	long index = oldIndex;
	long count = Length(items);
	scratch = GetCacheProto(kIndexViewOriginY);
	long scrolled = ISNIL(scratch) ? 0 : RINT(scratch);
	long slide = 0;
	if (direction < 0)
	{
		if (oldIndex == 0 && scrolled == 0)
			return NILREF;
		if (scrolled == 0)
		{
			index += direction;
			scratch = GetArraySlotRef(items, index);
			long h = RINT(RefVar(GetVariable(scratch, RSSYMheight, nil, 0)));
			if (h > height)
				slide = h - h % height;
		}
		else
		{
			slide = scrolled - height;
			if (slide < 0)
				slide = 0;
		}
	}
	else
	{
		scratch = GetArraySlotRef(items, oldIndex);
		long h = RINT(RefVar(GetVariable(scratch, RSSYMheight, nil, 0)));
		if (h - scrolled > height)
			slide = scrolled + height;
		else
		{
			index += direction;
			if (index >= count)
				return NILREF;
		}
	}
	if (slide != scrolled)
	{
		InvalidateSlotCache(kIndexViewOriginY);
		SetContextSlot(RSSYMvieworiginy, RefVar(MAKEINT(slide)));
	}
	SetVariable(fContext, RSSYMindex, RefVar(MAKEINT(index)));
	Boolean allCollapsed = NOTNIL(RefVar(GetProto(RSSYMallcollapsed)));
	scratch = GetVar(RSSYMcollapsedheight);
	long collapsedHeight = ISNIL(scratch) ? 0 : RINT(scratch);
	RefVar showing(MakeArray(0));
	long i = oldIndex >= index ? index : oldIndex;
	for (long y = -slide; y < height && i < count; i++)
	{
		scratch = GetArraySlotRef(items, i);
		Boolean collapsed = allCollapsed;
		if (!collapsed)
			collapsed = NOTNIL(RefVar(GetProtoVariable(scratch, RSSYMcollapsed, nil)));
		long h = collapsed ? collapsedHeight : RINT(RefVar(GetVariable(scratch, RSSYMheight, nil, 0)));
		if (i < index || i < oldIndex)
			slide += h;
		if (i >= index)
		{
			AddArraySlot(showing, scratch);
			y += h;
		}
	}
	TAnimate anim;
	if (slide != 0)
	{
		Rect bounds = viewBounds;
		bounds.bottom -= 5;
		anim.SetupSlideEffect(this, bounds, direction >= 0 ? -slide : slide, 0);
	}
	long kids = Length(showing);
	long front = 0;
	for (long k = 0; k < kids; k++)
	{
		scratch = GetArraySlotRef(showing, k);
		TView* child = Exists(fChildren, scratch);
		if (child == nil)
		{
			child = AddView(scratch);
			if (direction < 0)
			{
				// made at the end: going up, it belongs in front
				fChildren->RemoveElementsAt(fChildren->GetArraySize() - 1, 1);
				fChildren->InsertAt(front++, child);
			}
		}
		child->SetFlags(vIsMarked);
	}
	RemoveUnmarked();
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->RecalcBounds();
	if (slide != 0)
	{
		anim.DoEffect(direction >= 0 ? RefVar(RSSYMscrolldownsound) : RefVar(RSSYMscrollupsound));
		Rect bounds = viewBounds;
		bounds.bottom -= 5;
		gRootView->Invalidate(TRectangularRegion(bounds), this);
	}
	else
	{
		Dirty(nil);
		gRootView->Update(nil);
	}
	return showing;
}


// ROM 0x00262ff4 SoundEffect__5TViewFRC6RefVar
// The sound the view's slot names played (PlaySound in its context), when
// it has one.
void
TView::SoundEffect(RefArg slot)
{
	RefVar sound(GetVar(slot));
	if (NOTNIL(sound))
		FPlaySound(fContext, sound);
}


// ROM 0x00263034 SyncScrollSoup__5TViewFRC6RefVarT1
// A roll over a soup cursor scrolled a step: the cursor's entries are the
// items, each with its `height`, and the roll's viewOriginY is how far
// into the entry at the cursor it is scrolled.  A step is the roll's
// height less an overlap (its overlapScrollAmount; else twice its
// viewLineSpacing; else 16).
//
// Down: further into an entry taller than the roll (or the roll's
// lastItem), a step; else on to the next entry (nothing when there is
// none - the cursor put back to its start), sliding the rest of this one
// away.  Up: back up the entry scrolled into, a step (not past its top);
// else back to the entry before (none: the cursor reset, nothing done) -
// into it as far as whole steps go when it is taller than the roll.  Then
// the roll's viewSetupChildrenScript makes its children afresh from the
// cursor, the views of the ones already showing are kept (by their data),
// the others made (going up, put in front) and the rest removed, and the
// slide is animated with the scroll sound over the roll less its bottom 5
// pixels, or with no slide the roll is simply redrawn.  ==> nil.
Ref
TView::SyncScrollSoup(RefArg cursor, RefArg directionRef)
{
	RefVar scratch(GetProto(RSSYMoverlapscrollamount));
	long overlap = 16;
	if (NOTNIL(scratch))
		overlap = RINT(scratch);
	else
	{
		scratch = GetVar(RSSYMviewlinespacing);
		if (NOTNIL(scratch))
			overlap = RINT(scratch) * 2;
	}
	long height = (short) (viewBounds.bottom - viewBounds.top);
	long direction = RINT(directionRef);
	scratch = GetCacheProto(kIndexViewOriginY);
	long scrolled = ISNIL(scratch) ? 0 : RINT(scratch);
	long origin = 0;
	long slide;
	if (direction < 0)
	{
		if (scrolled == 0)
		{
			scratch = CursorPrev(cursor);
			if (ISNIL(scratch))
			{
				CursorReset(cursor);
				return NILREF;
			}
			long h = RINT(RefVar(GetVariable(scratch, RSSYMheight, nil, 0)));
			slide = -h;
			if (h > height)
			{
				origin = h - h % (height - overlap);
				slide = origin - h;
			}
		}
		else
		{
			origin = scrolled - (height - overlap);
			slide = -(height - overlap);
			if (origin < 0)
				origin = 0;
		}
	}
	else
	{
		scratch = CursorEntry(cursor);
		long left = RINT(RefVar(GetVariable(scratch, RSSYMheight, nil, 0))) - scrolled;
		if (left > height || EQ(scratch, RefVar(GetProto(RSSYMlastitem))))
		{
			origin = scrolled + height - overlap;
			slide = height - overlap;
		}
		else
		{
			slide = left;
			if (ISNIL(RefVar(CursorNext(cursor))))
			{
				CursorReset(cursor);
				return NILREF;
			}
		}
	}
	if (origin != scrolled)
	{
		InvalidateSlotCache(kIndexViewOriginY);
		SetContextSlot(RSSYMvieworiginy, RefVar(MAKEINT(origin)));
	}
	TAnimate anim;
	if (slide != 0)
	{
		Rect bounds = viewBounds;
		bounds.bottom -= 5;
		anim.SetupSlideEffect(this, bounds, -slide, 0);
	}
	RunCacheScript(kIndexViewSetupChildrenScript, RefVar(NILREF));
	RefVar children(Children());
	long count = ISNIL(children) ? 0 : Length(children);
	RefVar stepChildren(GetProto(RSSYMstepchildren));
	long total = count;
	if (NOTNIL(stepChildren))
		total += Length(stepChildren);
	long front = 0;
	for (long i = 0; i < total; i++)
	{
		scratch = i < count ? GetArraySlotRef(children, i) : GetArraySlotRef(stepChildren, i - count);
		TView* child = DataExists(fChildren, scratch);
		if (child == nil)
		{
			child = AddView(scratch);
			if (direction < 0)
			{
				fChildren->RemoveElementsAt(fChildren->GetArraySize() - 1, 1);
				fChildren->InsertAt(front++, child);
			}
		}
		child->SetFlags(vIsMarked);
	}
	RemoveUnmarked();
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->RecalcBounds();
	if (slide != 0)
	{
		anim.DoEffect(direction >= 0 ? RefVar(RSSYMscrolldownsound) : RefVar(RSSYMscrollupsound));
		Rect bounds = viewBounds;
		bounds.bottom -= 5;
		gRootView->Invalidate(TRectangularRegion(bounds), this);
	}
	else
	{
		Dirty(nil);
		gRootView->Update(nil);
	}
	return NILREF;
}


// ROM 0x002623a0 RemoveUnmarked__5TViewFv
// The children AddViews did not mark are removed; the marks are cleared.
void
TView::RemoveUnmarked(void)
{
	if (fChildren == gEmptyViewList)
		return;
	for (long i = 0; i < (long) fChildren->GetArraySize(); )
	{
		TView* child = fChildren->At(i);
		if (child->fFlags & vIsMarked)
		{
			child->ClearFlags(vIsMarked);
			i++;
		}
		else
			RemoveChildView(child);
	}
}


// ROM 0x00260cd8 ReorderView__5TViewFP5TViewl
// The child moved to the index: a non-floater no further than the first
// floater, a floater no nearer the front than the non-floaters (behind the
// clipboards of the root view); the view's visibility recomputed when it
// moved.
void
TView::ReorderView(TView* child, long index)
{
	long current = fChildren->GetIdentityIndex(child);
	long firstFloater = FirstNonFloaterIndex(fChildren);
	if ((child->fFlags & vFloating) == 0 && (child->fViewJustify & vjIsModal) == 0)
	{
		if (index < 0)
			index = 0;
		if (index > firstFloater)
			index = firstFloater;
	}
	else
	{
		long last = fChildren->GetArraySize() - 1;
		if (index < firstFloater + 1)
			index = firstFloater + 1;
		if (index > last)
			index = last;
		if (this == gRootView)
		{
			// behind the clipboards
			long i = last;
			if (i == current && i > 0)
				i--;
			while (i >= 0 && gRootView->GetClipboard(fChildren->At(i)) != nil && i > 0)
			{
				index--;
				i--;
			}
		}
	}
	if (index == current)
		return;
	fChildren->Remove(child);
	fChildren->InsertAt(index, child);
	if ((child->fFlags & vVisible) == 0)
		return;
	// what changed: the child's outer bounds where the siblings between
	// the two places overlap it, less the views in front of both; the
	// clippers from the front down to the lower place are recomputed
	Rect outer;
	child->OuterBounds(&outer);
	long hi = index > current ? index : current;
	long lo = index < current ? index : current;
	TRegionVar inFront;			// the running mask for the clippers
	SetEmptyRgn(inFront);
	TRegionVar above;			// what is in front of both places
	SetEmptyRgn(above);
	TRegionVar changed;
	SetEmptyRgn(changed);
	Boolean isRoot = this == gRootView;
	long i = fChildren->GetArraySize();
	TBackwardViewListLoop loop(fChildren);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
	{
		i--;
		if ((view->fFlags & vVisible) == 0)
			continue;
		Rect bounds;
		view->OuterBounds(&bounds);
		Rect overlap;
		Boolean intersects = SectRect(&outer, &bounds, &overlap);
		TClipper* clipper = isRoot ? view->Clipper() : nil;
		if (i > hi)
		{
			if (clipper != nil)
			{
				UnionRgn(inFront, clipper->fFullRgn, inFront);
				UnionRgn(above, clipper->fFullRgn, above);
			}
			else if (!isRoot || intersects)
			{
				TRectangularRegion r(bounds);
				UnionRgn(inFront, r, inFront);
				UnionRgn(above, r, above);
			}
		}
		else if (i >= lo)
		{
			if (clipper != nil)
			{
				if (intersects)
					clipper->RecalcVisible(inFront);
				UnionRgn(inFront, clipper->fFullRgn, inFront);
				if (i != index && intersects)
					UnionRgn(changed, clipper->fFullRgn, changed);
			}
			else
			{
				TRectangularRegion r(bounds);
				if (!isRoot)
					UnionRgn(inFront, r, inFront);
				if (i != index && intersects)
					UnionRgn(changed, r, changed);
			}
		}
	}
	TRectangularRegion outerRgn(outer);
	SectRgn(changed, outerRgn, changed);
	DiffRgn(changed, above, changed);
	gRootView->Invalidate(changed, this);
}


// ROM 0x002611e8 BringToFront__5TViewFv
void
TView::BringToFront(void)
{
	fParent->ReorderView(this, fParent->fChildren->GetArraySize());
}


// ROM 0x002611fc MoveChildBehind__5TViewFP5TViewT1
// The child moved to just behind the other (to the back for nil).
void
TView::MoveChildBehind(TView* child, TView* behind)
{
	long current = fChildren->GetIdentityIndex(child);
	long index = behind != nil ? fChildren->GetIdentityIndex(behind) : 0;
	if (current <= index)
		index--;
	if (index != current)
		ReorderView(child, index);
}


// ROM 0x0025f43c AddToSoup__5TViewFRC6RefVar
// A child added from its template: the viewAddChildScript is asked first
// (answering a frame names the template, anything else means done: the
// view is looked up); else the template joins the data frame's
// viewChildren (made when there is none) and the view is made.  ==> the
// view.
TView*
TView::AddToSoup(RefArg templ)
{
	RefVar args(MakeArray(1));
	SetArraySlotRef(args, 0, templ);
	RefVar result(RunScript(RSSYMviewaddchildscript, args));
	if (NOTNIL(result))
		return FindView(IsFrame(result) ? (RefArg) result : templ);
	RefVar children(Children());
	if (ISNIL(children))
	{
		children = MakeArray(1);
		SetArraySlotRef(children, 0, templ);
		RefVar data(DataFrame());
		SetFrameSlot(data, RSSYMviewchildren, children);
	}
	else
		AddArraySlot(children, templ);
	return AddView(templ);
}


// ROM 0x0025f5a4 RemoveFromSoup__5TViewFP5TView
// The child removed, and its data frame taken out of viewChildren unless
// the viewDropChildScript (given the data frame) does it.
void
TView::RemoveFromSoup(TView* child)
{
	RefVar args(MakeArray(1));
	RefVar data(child->DataFrame());
	SetArraySlotRef(args, 0, data);
	child->RemoveView();
	if (ISNIL(RunScript(RSSYMviewdropchildscript, args)))
	{
		RefVar children(Children());
		if (NOTNIL(children))
			ArrayRemove(children, data);
	}
}


// ROM 0x0025fb34 FindView__5TViewFRC6RefVar
// The view (this or a descendant) whose data is the frame (SoupEQ).
TView*
TView::FindView(RefArg data)
{
	if (SoupEQ(RefVar(DataFrame()), data))
		return this;
	if ((fFlags & vVisible) == 0)
		return nil;
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
	{
		TView* found = child->FindView(data);
		if (found != nil)
			return found;
	}
	return nil;
}


// ROM 0x0025fc70 Distance__5TViewF6TPointP6TPoint
// How far the point is outside the view: 0 inside its visible region (or
// its outer bounds when it has no clipper), else 0x10000... the ROM answers
// 0x10000 for an invisible view, 0 for a point inside, and the distance
// InsideView measures otherwise (the host: 0x10000).
long
TView::Distance(Point pt, Point* /*delta*/)
{
	if ((fFlags & vVisible) == 0)
		return 0x10000;
	TClipper* clipper = Clipper();
	if (clipper != nil)
		return PtInRgn(pt, clipper->fVisRgn) ? 0 : 0x10000;
	return InsideView(pt) ? 0 : 0x10000;
}


// ROM 0x0025fd78 FindClosestView__5TViewF6TPointUlPlP6TPointPUc
// The deepest (front-most) view under the point whose flags match the
// mask (any view for 0), with its distance; clipped says a vClipping view
// was met on the way.
TView*
TView::FindClosestView(Point pt, ULong flags, long* distance, Point* delta, Boolean* clipped)
{
	*clipped = false;
	TView* found = nil;
	long d = Distance(pt, delta);
	if (d != 0x10000)
	{
		if (fFlags & vClipping)
			*clipped = true;
		if (flags == 0 || (fFlags & vRecognitionAllowed & flags) != 0)
		{
			*distance = d;
			found = this;
		}
		long best = 0x10000;
		TBackwardViewListLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
		{
			long childDistance = 0x10000;
			Boolean childClipped;
			TView* candidate = child->FindClosestView(pt, flags, &childDistance, delta, &childClipped);
			if (childDistance < best)
			{
				*distance = childDistance;
				found = candidate;
				best = childDistance;
			}
			if (childClipped)
				*clipped = true;
		}
	}
	return found;
}


// ROM 0x0025fe94 FindView__5TViewF6TPointUlP6TPoint
TView*
TView::FindView(Point pt, ULong flags, Point* delta)
{
	long distance;
	Boolean clipped;
	return FindClosestView(pt, flags, &distance, delta, &clipped);
}


// ROM 0x00267398 FindID__5TViewFl
// The child with the id.
TView*
TView::FindID(long id)
{
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		if (child->fId == id)
			return child;
	return nil;
}


// ROM 0x0026130c FrontMost__5TViewFv
// The front-most visible application view under this one (this one when
// no child is).
TView*
TView::FrontMost(void)
{
	if ((fFlags & vVisible) == 0 || (fFlags & vApplication) == 0)
		return nil;
	TBackwardViewListLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
	{
		TView* found = child->FrontMost();
		if (found != nil)
			return found;
	}
	return this;
}


// ROM 0x00261380 FrontMostApp__5TViewFv
// The same, not counting floaters.
TView*
TView::FrontMostApp(void)
{
	if ((fFlags & vVisible) == 0 || (fFlags & vApplication) == 0 || (fFlags & vFloating) != 0)
		return nil;
	TBackwardViewListLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
	{
		TView* found = child->FrontMostApp();
		if (found != nil)
			return found;
	}
	return this;
}


// ROM 0x00261260 ChildViewFrames__5TViewFv
// The children's contexts, as an array.
Ref
TView::ChildViewFrames(void)
{
	long count = fChildren->GetArraySize();
	RefVar frames(MakeArray(count));
	long i = 0;
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		SetArraySlotRef(frames, i++, child->fContext);
	return frames;
}


// ROM 0x0026a690 Children__5TViewFv
// The viewChildren array.
Ref
TView::Children(void)
{
	return GetProto(RSSYMviewchildren);
}


// ROM 0x00265c48 GetWindowView__5TViewFv
// The ancestor that is a child of the root view.
TView*
TView::GetWindowView(void)
{
	TView* view = this;
	while (!view->HasVisRgn())
		view = view->fParent;
	return view;
}


// ROM 0x0026a138 ProtoedFrom__5TViewFRC6RefVar
// Whether the frame is in the context's proto chain.
Boolean
TView::ProtoedFrom(RefArg proto)
{
	RefVar frame(fContext);
	while (NOTNIL(frame))
	{
		if (EQRef(frame, proto))
			return true;
		frame = GetFrameSlotRef(frame, RSSYM_proto);
	}
	return false;
}


// ROM 0x0026a768 Clipper__5TViewCFv
// The clipper of a child of the root view, from its context's viewclipper.
TClipper*
TView::Clipper(void) const
{
	if (!HasVisRgn())
		return nil;
	Ref clipper = GetFrameSlotRef(fContext, RSSYMviewclipper);
	return NOTNIL(clipper) ? (TClipper*) RefToAddress(clipper) : nil;
}


// ROM 0x0026a7d0 HasVisRgn__5TViewCFv
// The children of the root view have visible regions of their own.
Boolean
TView::HasVisRgn(void) const
{
	return fParent == gRootView && this != gRootView;
}


// ROM 0x00262950 VisibleDeep__5TViewCFv
// Visible, and so are all the ancestors.
Boolean
TView::VisibleDeep(void) const
{
	const TView* view = this;
	for ( ; ; )
	{
		if ((view->fFlags & vVisible) == 0)
			return false;
		if (view == gRootView)
			return true;
		view = view->fParent;
	}
}


/*------------------------------------------------------------------------------
	T V i e w :  t h e   c o n t e x t
------------------------------------------------------------------------------*/

// ROM 0x0026af70 DataFrame__5TViewFv
// The realData of a data view, else the context.
Ref
TView::DataFrame(void)
{
	Ref data = GetCacheProto(kIndexRealData);
	return NOTNIL(data) ? data : (Ref) fContext;
}


// ROM 0x0026afac GetProto__5TViewCFRC6RefVar
// The slot along the context's proto chain (GetProtoVariable, with the
// nil-context error).
Ref
TView::GetProto(RefArg slot) const
{
	if (ISNIL(fContext))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, slot);
	return GetProtoVariable(fContext, slot, nil);
}


// ROM 0x0026afb8 GetVar__5TViewCFRC6RefVar
// The slot along the context's proto and parent chains (GetVariable).
Ref
TView::GetVar(RefArg slot) const
{
	if (ISNIL(fContext))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, slot);
	return GetVariable(fContext, slot, nil, 0);
}


// ROM 0x0026afc8 GetWriteableProtoVariable__5TViewFRC6RefVar
// The slot in the data frame itself: an inherited value is cloned into it.
Ref
TView::GetWriteableProtoVariable(RefArg slot)
{
	RefVar data(DataFrame());
	RefVar value(GetFrameSlotRef(data, slot));
	if (ISNIL(value))
	{
		value = GetProto(slot);
		if (NOTNIL(value))
		{
			value = Clone(value);
			SetDataSlot(slot, value);
		}
	}
	return value;
}


// ROM 0x0026b06c GetWriteableVariable__5TViewFRC6RefVar
Ref
TView::GetWriteableVariable(RefArg slot)
{
	RefVar value(GetWriteableProtoVariable(slot));
	if (ISNIL(value))
	{
		value = GetVar(slot);
		if (NOTNIL(value))
		{
			value = Clone(value);
			SetDataSlot(slot, value);
		}
	}
	return value;
}


// ROM 0x0026b0ec SetContextSlot__5TViewFRC6RefVarT1
// The slot set in the context itself (SetFrameSlot: the frame must be
// writeable; the lookup caches are cleared).
void
TView::SetContextSlot(RefArg slot, RefArg value)
{
	SetFrameSlot(fContext, slot, value);
}


// ROM 0x0026b0f4 SetDataSlot__5TViewFRC6RefVarT1
void
TView::SetDataSlot(RefArg slot, RefArg value)
{
	RefVar data(DataFrame());
	SetFrameSlot(data, slot, value);
}


// the slot symbol of a cache index (the ROM indexes slotCacheRefs)
Ref
SlotCacheRef(long index)
{
	return GetArraySlotRef(*gSlotCacheTable, index);
}


// ROM 0x0025f3ac GetCacheProto__5TViewFl
// The cached slot along the proto chain: nil at once when the view's mask
// says the slot is not there; the bit is cleared when the lookup finds
// nil, set when it finds a value.
Ref
TView::GetCacheProto(long index)
{
	ULong* mask = index < 32 ? &fSlotCache : &fSlotCache2;
	ULong bit = 1UL << (index < 32 ? index : index - 32);
	if ((*mask & bit) == 0)
		return NILREF;
	Ref value = GetProto(RefVar(SlotCacheRef(index)));
	if (ISNIL(value))
		*mask &= ~bit;
	else
		*mask |= bit;
	return value;
}


// ROM 0x0025f31c GetCacheVariable__5TViewFl
// The same along the proto and parent chains.
Ref
TView::GetCacheVariable(long index)
{
	ULong* mask = index < 32 ? &fSlotCache : &fSlotCache2;
	ULong bit = 1UL << (index < 32 ? index : index - 32);
	if ((*mask & bit) == 0)
		return NILREF;
	Ref value = GetVar(RefVar(SlotCacheRef(index)));
	if (ISNIL(value))
		*mask &= ~bit;
	else
		*mask |= bit;
	return value;
}


// ROM 0x00263ccc InvalidateSlotCache__5TViewFl
// The slot may be there again.
void
TView::InvalidateSlotCache(long index)
{
	if (index < 32)
		fSlotCache |= 1UL << index;
	else
		fSlotCache2 |= 1UL << (index - 32);
}


// ROM 0x00263d00 RunScript__5TViewFRC6RefVarT1UcPUc
// The script (a slot of the context's proto chain, or of the parent
// chain too when lookupVars) sent to the context with the args, unless
// the view runs no scripts; ran says whether there was one.
Ref
TView::RunScript(RefArg tag, RefArg args, Boolean lookupVars, Boolean* ran)
{
	RefVar result;
	Boolean did = false;
	if ((fFlags & vNoScripts) == 0)
	{
		Ref script = lookupVars ? GetVar(tag) : GetProto(tag);
		if (NOTNIL(script))
		{
			result = lookupVars ? DoMessage(fContext, tag, args) : DoProtoMessage(fContext, tag, args);
			did = true;
		}
	}
	if (ran != nil)
		*ran = did;
	return result;
}


// ROM 0x00263bbc RunCacheScript__5TViewFlRC6RefVarUcPUc
// The same for a slot of the cache: nothing when the view's mask says the
// script is not there.
Ref
TView::RunCacheScript(long index, RefArg args, Boolean lookupVars, Boolean* ran)
{
	RefVar result;
	Boolean did = false;
	if ((fFlags & vNoScripts) == 0)
	{
		ULong mask = index < 32 ? fSlotCache : fSlotCache2;
		ULong bit = 1UL << (index < 32 ? index : index - 32);
		if (mask & bit)
		{
			Ref script = lookupVars ? GetCacheVariable(index) : GetCacheProto(index);
			if (NOTNIL(script))
			{
				RefVar tag(SlotCacheRef(index));
				result = lookupVars ? DoMessage(fContext, tag, args) : DoProtoMessage(fContext, tag, args);
				did = true;
			}
		}
	}
	if (ran != nil)
		*ran = did;
	return result;
}


// ROM 0x0025f668 Sync__5TViewFv
// The view brought up to date with its context: the viewSetupFormScript
// run again (for a view that is set up), the bounds recomputed from
// viewBounds and viewJustify - a view of the same size is moved (Offset),
// one of another size dirtied, re-set and dirtied again - and the
// children's bounds recalculated.
void
TView::Sync(void)
{
	if ((fFlags & vIsInSetupForm) == 0)
		SetupForm();
	RefVar bounds(GetProto(RSSYMviewbounds));
	Rect r;
	if (!FromObject(bounds, r))
		ThrowMsg(kBadBoundsFrame);
	Rect old = viewBounds;
	InvalidateSlotCache(kIndexViewJustify);
	Ref justify = GetCacheProto(kIndexViewJustify);
	fViewJustify = ISNIL(justify) ? 0 : ((RINT(justify) & vjJustifyMask) | (fViewJustify & ~vjJustifyMask));
	Rect justified = r;
	JustifyBounds(&justified);
	if (!EqualRect(&justified, &old))
	{
		if (justified.bottom - justified.top == old.bottom - old.top
		 && justified.right - justified.left == old.right - old.left)
			Offset(MakePoint(justified.left - old.left, justified.top - old.top));
		else
		{
			Rect outer;
			OuterBounds(&outer);
			fParent->Dirty(&outer);
			SetBounds(r);
			if (fViewJustify & vjIsModal)
				SetModalView(this);
			Dirty(nil);
		}
		TViewLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
			child->RecalcBounds();
	}
}


// ROM 0x0025f298 SetFlags__5TViewFUl
// The bits set; a change of vVisible or vSelected goes to the context's
// viewFlags slot too.
void
TView::SetFlags(ULong flags)
{
	fFlags |= flags;
	if (flags & (vVisible | vSelected))
	{
		ULong slotFlags = RINT(GetCacheProto(kIndexViewFlags));
		SetContextSlot(RSSYMviewflags, RefVar(MAKEINT(slotFlags | flags)));
	}
}


// ROM 0x0026abb0 ClearFlags__5TViewFUl
void
TView::ClearFlags(ULong flags)
{
	fFlags &= ~flags;
	if (flags & (vVisible | vSelected))
	{
		ULong slotFlags = RINT(GetCacheProto(kIndexViewFlags));
		SetContextSlot(RSSYMviewflags, RefVar(MAKEINT(slotFlags & ~flags)));
	}
}


// ROM 0x0026180c GetTextStyle__5TViewFv
// The viewFont, the user's font when none.
Ref
TView::GetTextStyle(void)
{
	Ref font = GetCacheVariable(kIndexViewFont);
	if (ISNIL(font))
		font = GetPreference(RSSYMuserfont);
	return font;
}


// ROM 0x00261880 GetTextStyleRecord__5TViewFP11StyleRecord
void
TView::GetTextStyleRecord(StyleRecord* style)
{
	CreateTextStyleRecord(RefVar(GetTextStyle()), style);
}


// ROM 0x00261d44 Printing__5TViewFv
// Whether the view is in a print view (NOT YET: no print views).
Boolean
TView::Printing(void)
{
	return false;
}


/*------------------------------------------------------------------------------
	T V i e w :  b o u n d s
------------------------------------------------------------------------------*/

// ROM 0x0026404c OuterBounds1__FP5TRectUl
// The bounds grown by the frame's pen (when there is a frame) and the
// inset, with the shadow added at the right and bottom.
void
OuterBounds1(Rect* bounds, ULong viewFormat)
{
	long pen = (viewFormat & vfFrameMask) != 0 ? (viewFormat & vfPenMask) >> vfPenShift : 0;
	long grow = pen + ((viewFormat & vfInsetMask) >> vfInsetShift);
	if (grow != 0)
		InsetRect(bounds, -grow, -grow);
	long shadow = (viewFormat & vfShadowMask) >> vfShadowShift;
	if (shadow != 0)
	{
		bounds->right += shadow;
		bounds->bottom += shadow;
	}
}


// ROM 0x002640d0 OuterBounds__5TViewFP5TRect
// The bounds with what the format draws outside them; the popup view gets
// three pixels more above and below.
void
TView::OuterBounds(Rect* bounds)
{
	*bounds = viewBounds;
	if ((fViewFormat & (vfPenMask | vfInsetMask | vfShadowMask)) == 0)
		return;
	OuterBounds1(bounds, fViewFormat);
	if (gRootView->fDefaultButton == this)
	{
		bounds->top -= 3;
		bounds->bottom += 3;
	}
}


// ROM 0x0026555c SetBounds__5TViewFRC5TRect
// The bounds from the template's viewBounds: justified against the parent
// by viewJustify (read from the context into fViewJustify); a child of
// the root view has its clipper's regions recomputed and the visibility
// of the views around it.
void
TView::SetBounds(const Rect& bounds)
{
	viewBounds = bounds;
	Ref justify = GetCacheProto(kIndexViewJustify);
	fViewJustify = ISNIL(justify) ? 0 : ((RINT(justify) & vjJustifyMask) | (fViewJustify & ~vjJustifyMask));
	JustifyBounds(&viewBounds);
	TClipper* clipper = Clipper();
	if (clipper != nil)
	{
		clipper->UpdateRegions(this);
		fParent->ViewVisibleChanged(this, false);
	}
}


// ROM 0x00267458 GetChildOrigin__5TViewFP6TPoint
// The scroll origin of the contents: viewOriginX, viewOriginY.
void
TView::GetChildOrigin(Point* origin)
{
	Ref x = GetCacheProto(kIndexViewOriginX);
	origin->h = (short) (ISNIL(x) ? 0 : RINT(x));
	Ref y = GetCacheProto(kIndexViewOriginY);
	origin->v = (short) (ISNIL(y) ? 0 : RINT(y));
}


// ROM 0x00267504 ContentsOrigin__5TViewFv
// Where the contents' (0, 0) is: the bounds' top left less the scroll
// origin.
Point
TView::ContentsOrigin(void)
{
	Point origin;
	GetChildOrigin(&origin);
	return MakePoint(viewBounds.left - origin.h, viewBounds.top - origin.v);
}


// ROM 0x00263da4 LocalOrigin__5TViewCFv
// The bounds' top left relative to the parent's contents' origin - where
// the view is in the coordinates its viewBounds slot is written in.
Point
TView::LocalOrigin(void) const
{
	Point origin = fParent->ContentsOrigin();
	return MakePoint(viewBounds.left - origin.h, viewBounds.top - origin.v);
}


// ROM 0x00265334 SetOrigin__5TViewFR6TPoint
// The contents scrolled to the origin: the view dirtied, the children
// shifted by the old origin less the new (a window child through Offset,
// the rest simply), and viewOriginX/Y set.
void
TView::SetOrigin(Point& origin)
{
	Ref x = GetCacheProto(kIndexViewOriginX);
	long oldX = ISNIL(x) ? 0 : RINT(x);
	Ref y = GetCacheProto(kIndexViewOriginY);
	long oldY = ISNIL(y) ? 0 : RINT(y);
	Dirty(nil);
	if (fChildren->GetArraySize() != 0)
	{
		Point delta = MakePoint(oldX - origin.h, oldY - origin.v);
		TViewLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
		{
			if (child->HasVisRgn())
				child->Offset(delta);
			else
				child->SimpleOffset(delta, false);
		}
	}
	InvalidateSlotCache(kIndexViewOriginX);
	SetContextSlot(RSSYMvieworiginx, RefVar(MAKEINT(origin.h)));
	InvalidateSlotCache(kIndexViewOriginY);
	SetContextSlot(RSSYMvieworiginy, RefVar(MAKEINT(origin.v)));
}


// the bounds of the parent this view is placed in: the parent's bounds,
// or the application area (vars.displayParams) for a child of the root
static void
ParentBoundsFor(TView* view, TView* parent, Rect* parentBounds)
{
	if (parent == gRootView && view != gRootView)
	{
		RefVar params(GetFrameSlotRef(gVarFrame, RSSYMdisplayparams));
		long left = RINT(GetProtoVariable(params, RSSYMappareagloballeft, nil));
		long top = RINT(GetProtoVariable(params, RSSYMappareaglobaltop, nil));
		long width = RINT(GetProtoVariable(params, RSSYMappareawidth, nil));
		long height = RINT(GetProtoVariable(params, RSSYMappareaheight, nil));
		SetRect(parentBounds, left, top, left + width, top + height);
	}
	else
		*parentBounds = parent->viewBounds;
}


// ROM 0x0026415c JustifyBounds__5TViewFP5TRect
// The template's viewBounds made global.  The base is the parent's
// contents origin (its top left less the scroll origin; the top left
// itself for vjParentClip); a parent still being set up whose bounds are
// empty has its own viewBounds justified first.  The sibling bits place
// the view against the previous sibling instead: centred on it, after
// it (below/right), before it (top/left), or full (the bounds added to
// the sibling's, no base); the ratio bits scale the bounds by a hundredth
// of the sibling's (else the parent's) size.  The parent bits then
// centre, or place at the far edge, or (full) add the bounds to the
// parent's.  A child of the root view is placed in the application area.
void
TView::JustifyBounds(Rect* bounds)
{
	TView* parent = fParent;
	if (this == parent)
		return;
	Rect parentBounds = parent->viewBounds;
	if (EmptyRect(&parentBounds) && (parent->fFlags & vIsInSetupForm))
	{
		RefVar pb(parent->GetProto(RSSYMviewbounds));
		if (!FromObject(pb, parentBounds))
			ThrowMsg(kBadBoundsFrame);
		parent->JustifyBounds(&parentBounds);
	}
	Point origin;
	parent->GetChildOrigin(&origin);
	Point base = MakePoint(parentBounds.left - origin.h, parentBounds.top - origin.v);
	ULong justify;
	if ((fFlags & vIsInSetupForm) && (fFlags & vIsBeingDeleted) != vIsBeingDeleted)
	{
		Ref j = GetCacheProto(kIndexViewJustify);
		justify = ISNIL(j) ? 0 : (RINT(j) & vjJustifyMask);
	}
	else
		justify = fViewJustify & vjJustifyMask;
	if (justify & vjParentClip)
	{
		base.v += origin.v;
		base.h += origin.h;
	}
	Boolean noSiblingV = true;
	Boolean noSiblingH = true;
	if (justify & vjSiblingMask)
	{
		long index = parent->fChildren->GetIdentityIndex(this);
		if (index == (long) kEmptyIndex)
			index = parent->fChildren->GetArraySize();
		if (index > 0)
		{
			TView* sibling = parent->fChildren->At(index - 1);
			ULong sv = justify & vjSiblingVMask;
			if (sv != 0)
			{
				noSiblingV = false;
				if (justify & (vjTopRatio | vjBottomRatio))
				{
					long height = sibling->viewBounds.bottom - sibling->viewBounds.top;
					if (justify & vjTopRatio)
						bounds->top = (short) (bounds->top * height / 100);
					if (justify & vjBottomRatio)
						bounds->bottom = (short) (bounds->bottom * height / 100);
				}
				switch (sv)
				{
				case vjSiblingCenterV:
					base.v = sibling->viewBounds.top + ((sibling->viewBounds.bottom - sibling->viewBounds.top) - (bounds->bottom - bounds->top)) / 2;
					break;
				case vjSiblingBottomV:
					base.v = sibling->viewBounds.bottom;
					break;
				case vjSiblingFullV:
					base.v = 0;
					bounds->top += sibling->viewBounds.top;
					bounds->bottom += sibling->viewBounds.bottom;
					break;
				case vjSiblingTopV:
					base.v = sibling->viewBounds.top;
					break;
				}
			}
			ULong sh = justify & vjSiblingHMask;
			if (sh != 0)
			{
				noSiblingH = false;
				if (justify & (vjLeftRatio | vjRightRatio))
				{
					long width = sibling->viewBounds.right - sibling->viewBounds.left;
					if (justify & vjLeftRatio)
						bounds->left = (short) (bounds->left * width / 100);
					if (justify & vjRightRatio)
						bounds->right = (short) (bounds->right * width / 100);
				}
				switch (sh)
				{
				case vjSiblingCenterH:
					base.h = sibling->viewBounds.left + ((sibling->viewBounds.right - sibling->viewBounds.left) - (bounds->right - bounds->left)) / 2;
					break;
				case vjSiblingRightH:
					base.h = sibling->viewBounds.right;
					break;
				case vjSiblingFullH:
					base.h = 0;
					bounds->left += sibling->viewBounds.left;
					bounds->right += sibling->viewBounds.right;
					break;
				case vjSiblingLeftH:
					base.h = sibling->viewBounds.left;
					break;
				}
			}
		}
	}
	if (parent == gRootView && this != gRootView)
	{
		ParentBoundsFor(this, parent, &parentBounds);
		base = MakePoint(parentBounds.left, parentBounds.top);
	}
	if (justify & vjRatioMask)
	{
		if (noSiblingH)
		{
			long width = parentBounds.right - parentBounds.left;
			if (justify & vjLeftRatio)
				bounds->left = (short) (bounds->left * width / 100);
			if (justify & vjRightRatio)
				bounds->right = (short) (bounds->right * width / 100);
		}
		if (noSiblingV)
		{
			long height = parentBounds.bottom - parentBounds.top;
			if (justify & vjTopRatio)
				bounds->top = (short) (bounds->top * height / 100);
			if (justify & vjBottomRatio)
				bounds->bottom = (short) (bounds->bottom * height / 100);
		}
	}
	if (noSiblingV)
	{
		switch (justify & vjParentVMask)
		{
		case vjParentCenterV:
			base.v += ((parentBounds.bottom - parentBounds.top) - (bounds->bottom - bounds->top)) / 2;
			break;
		case vjParentBottomV:
			base.v += parentBounds.bottom - parentBounds.top;
			break;
		case vjParentFullV:
			base.v = 0;
			bounds->top += parentBounds.top;
			bounds->bottom += parentBounds.bottom;
			break;
		}
	}
	if (noSiblingH)
	{
		switch (justify & vjParentHMask)
		{
		case vjParentCenterH:
			base.h += ((parentBounds.right - parentBounds.left) - (bounds->right - bounds->left)) / 2;
			break;
		case vjParentRightH:
			base.h += parentBounds.right - parentBounds.left;
			break;
		case vjParentFullH:
			base.h = 0;
			bounds->left += parentBounds.left;
			bounds->right += parentBounds.right;
			break;
		}
	}
	OffsetRect(bounds, base.h, base.v);
}


// ROM 0x00264a54 DejustifyBounds__5TViewFP5TRect
// The inverse: the global bounds made a template's viewBounds under the
// view's viewJustify (the full alignments and the sibling ones are undone
// as the ROM does, after the base is taken off; ratios become hundredths
// of the parent's or sibling's size).
void
TView::DejustifyBounds(Rect* bounds)
{
	*bounds = viewBounds;
	TView* parent = fParent;
	if (this == parent)
		return;
	ULong justify = fViewJustify & vjJustifyMask;
	Rect parentBounds;
	ParentBoundsFor(this, parent, &parentBounds);
	Point base = MakePoint(parentBounds.left, parentBounds.top);
	if ((justify & vjParentClip) == 0)
	{
		Point origin;
		parent->GetChildOrigin(&origin);
		base.v -= origin.v;
		base.h -= origin.h;
	}
	OffsetRect(bounds, -base.h, -base.v);
	Boolean noSiblingV = true;
	Boolean noSiblingH = true;
	long height = viewBounds.bottom - viewBounds.top;
	long width = viewBounds.right - viewBounds.left;
	if (justify & vjSiblingMask)
	{
		long index = parent->fChildren->GetIdentityIndex(this);
		if (index != 0 && index != (long) kEmptyIndex)
		{
			TView* sibling = parent->fChildren->At(index - 1);
			ULong sv = justify & vjSiblingVMask;
			if (sv != 0)
			{
				noSiblingV = false;
				switch (sv)
				{
				case vjSiblingCenterV:
					bounds->top = (short) (((viewBounds.top - sibling->viewBounds.top) + (viewBounds.bottom - sibling->viewBounds.bottom)) / 2);
					bounds->bottom = (short) (bounds->top + height);
					break;
				case vjSiblingBottomV:
					bounds->top = viewBounds.top - sibling->viewBounds.bottom;
					bounds->bottom = (short) (bounds->top + height);
					break;
				case vjSiblingFullV:
					bounds->top -= sibling->viewBounds.top;
					bounds->bottom -= sibling->viewBounds.bottom;
					break;
				case vjSiblingTopV:
					bounds->top = viewBounds.top - sibling->viewBounds.top;
					bounds->bottom = (short) (bounds->top + height);
					break;
				}
				if (justify & (vjTopRatio | vjBottomRatio))
				{
					long siblingHeight = sibling->viewBounds.bottom - sibling->viewBounds.top;
					if (justify & vjTopRatio)
						bounds->top = (short) (bounds->top * 100 / siblingHeight);
					if (justify & vjBottomRatio)
						bounds->bottom = (short) (bounds->bottom * 100 / siblingHeight);
				}
			}
			ULong sh = justify & vjSiblingHMask;
			if (sh != 0)
			{
				noSiblingH = false;
				switch (sh)
				{
				case vjSiblingCenterH:
					bounds->left = (short) (((viewBounds.left - sibling->viewBounds.left) + (viewBounds.right - sibling->viewBounds.right)) / 2);
					bounds->right = (short) (bounds->left + width);
					break;
				case vjSiblingRightH:
					bounds->left = viewBounds.left - sibling->viewBounds.right;
					bounds->right = (short) (bounds->left + width);
					break;
				case vjSiblingFullH:
					bounds->left -= sibling->viewBounds.left;
					bounds->right -= sibling->viewBounds.right;
					break;
				case vjSiblingLeftH:
					bounds->left = viewBounds.left - sibling->viewBounds.left;
					bounds->right = (short) (bounds->left + width);
					break;
				}
				if (justify & (vjLeftRatio | vjRightRatio))
				{
					long siblingWidth = sibling->viewBounds.right - sibling->viewBounds.left;
					if (justify & vjLeftRatio)
						bounds->left = (short) (bounds->left * 100 / siblingWidth);
					if (justify & vjRightRatio)
						bounds->right = (short) (bounds->right * 100 / siblingWidth);
				}
			}
		}
	}
	long parentHeight = parentBounds.bottom - parentBounds.top;
	long parentWidth = parentBounds.right - parentBounds.left;
	long belowBottom = parentHeight - bounds->bottom;
	long rightOfRight = parentWidth - bounds->right;
	if (noSiblingV)
	{
		switch (justify & vjParentVMask)
		{
		case vjParentCenterV:
			bounds->top = (short) ((bounds->top - belowBottom) / 2);
			bounds->bottom = (short) (bounds->top + height);
			break;
		case vjParentBottomV:
			bounds->top -= parentHeight;
			bounds->bottom -= parentHeight;
			break;
		case vjParentFullV:
			bounds->bottom = (short) -belowBottom;
			break;
		}
	}
	if (noSiblingH)
	{
		switch (justify & vjParentHMask)
		{
		case vjParentCenterH:
			bounds->left = (short) ((bounds->left - rightOfRight) / 2);
			bounds->right = (short) (bounds->left + width);
			break;
		case vjParentRightH:
			bounds->left -= parentWidth;
			bounds->right -= parentWidth;
			break;
		case vjParentFullH:
			bounds->right = (short) -rightOfRight;
			break;
		}
	}
	if (justify & vjRatioMask)
	{
		if (noSiblingV)
		{
			if (justify & vjTopRatio)
				bounds->top = (short) (bounds->top * 100 / parentHeight);
			if (justify & vjBottomRatio)
				bounds->bottom = (short) (bounds->bottom * 100 / parentHeight);
		}
		if (noSiblingH)
		{
			if (justify & vjLeftRatio)
				bounds->left = (short) (bounds->left * 100 / parentWidth);
			if (justify & vjRightRatio)
				bounds->right = (short) (bounds->right * 100 / parentWidth);
		}
	}
}


// ROM 0x002652a4 RecalcBounds__5TViewFv
// The bounds set again from the template's viewBounds (after the parent
// moved or changed size) - when it converts to a rectangle - and then the
// children's recalculated in their turn, whatever the view's own did: a
// view moved by its parent carries its whole subtree with it (Mahjongg's
// tiles-remaining box and its digits, after the screen is turned).
void
TView::RecalcBounds(void)
{
	RefVar bounds(GetProto(RSSYMviewbounds));
	Rect r;
	if (FromObject(bounds, r))
		SetBounds(r);
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->RecalcBounds();
}


// ROM 0x00263f28 WriteBounds__5TViewFRC5TRect
// The bounds (relative to the parent's contents origin) written to the
// data frame's viewBounds when they changed - not for a read-only or
// write-protected view - and set, with Changed sent.
void
TView::WriteBounds(const Rect& bounds)
{
	Rect current = viewBounds;
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&current, -origin.h, -origin.v);
	if (EqualRect(&current, &bounds))
		return;
	if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
		SetDataSlot(RSSYMviewbounds, RefVar(ToObject(bounds)));
	SetBounds(bounds);
	if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
		Changed(RSSYMviewbounds);
}


// ROM 0x00263e1c Move__5TViewFRC6TPoint
// The view moved by the delta: the bounds written, the children
// recalculated, the parent dirtied.
void
TView::Move(const Point& delta)
{
	Rect bounds = viewBounds;
	Point origin = fParent->ContentsOrigin();
	OffsetRect(&bounds, -origin.h, -origin.v);
	OffsetRect(&bounds, delta.h, delta.v);
	WriteBounds(bounds);
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->RecalcBounds();
	fParent->Dirty(nil);
}


// ROM 0x0025fec4 Offset__5TViewF6TPoint
// The view and its children shifted by the delta without re-justifying:
// a view without a clipper has its old place dirtied in the parent and
// its new one dirtied; a child of the root view has the parent told
// (ChildViewMoved: the regions moved, the visibility recomputed); the
// modal view is set again.
void
TView::Offset(Point delta)
{
	if (delta.h == 0 && delta.v == 0)
		return;
	Boolean noVisRgn = !HasVisRgn();
	if (noVisRgn)
	{
		Rect bounds;
		OuterBounds(&bounds);
		fParent->Dirty(&bounds);
	}
	SimpleOffset(delta, false);
	if (noVisRgn)
		Dirty(nil);
	else
		fParent->ChildViewMoved(this, delta);
	if (fViewJustify & vjIsModal)
		SetModalView(this);
}


// ROM 0x0025ff9c SimpleOffset__5TViewF6TPointl
// The bounds and the children's moved by the delta; a vjParentClip view
// stays where it is unless the move is its parent's (inChildren).
void
TView::SimpleOffset(Point delta, Boolean inChildren)
{
	if (!inChildren && (fViewJustify & vjParentClip))
		return;
	OffsetRect(&viewBounds, delta.h, delta.v);
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->SimpleOffset(delta, true);
}


// ROM 0x00260028 ChildViewMoved__5TViewFP5TView6TPoint
// A child of the root view moved by the delta: the views from the front
// down to it have their visible regions recomputed, the old and new
// places of the child invalidated less what shows through.
void
TView::ChildViewMoved(TView* child, Point delta)
{
	if (!child->HasVisRgn())
		return;
	TRegionVar inFront;
	SetEmptyRgn(inFront);
	TRegionVar dirty;
	SetEmptyRgn(dirty);
	Boolean passed = false;
	TBackwardViewListLoop loop(fChildren);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
	{
		if (view->fFlags & vVisible)
		{
			TClipper* clipper = view->Clipper();
			if (view == child)
			{
				TRectangularRegion old((*clipper->fFullRgn)->rgnBBox);
				UnionRgn(dirty, old, dirty);
				clipper->Offset(delta);
				passed = true;
			}
			if (passed)
				clipper->RecalcVisible(inFront);
			if (view == child)
			{
				TRectangularRegion now((*clipper->fFullRgn)->rgnBBox);
				UnionRgn(dirty, now, dirty);
				DiffRgn(dirty, inFront, dirty);
				gRootView->Invalidate(dirty, nil);
			}
			UnionRgn(inFront, clipper->fFullRgn, inFront);
		}
	}
}


// ROM 0x0026561c ChildrenHeight__5TViewFPl
// The children's heights added; count answers how many, plus one.
long
TView::ChildrenHeight(long* count)
{
	long n = 1;
	long height = 0;
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
	{
		height += child->viewBounds.bottom - child->viewBounds.top;
		n++;
	}
	if (count != nil)
		*count = n;
	return height;
}


// ROM 0x00265698 SetChildrenVertical__5TViewFlT1
// The children stacked from top, spacing apart (NOT YET RECONSTRUCTED
// beyond the first: the ROM's loop over the rest is lost after the first
// child's SetBounds); ==> the bottom reached.
long
TView::SetChildrenVertical(long top, long spacing)
{
	Point origin = ContentsOrigin();
	long bottom = top;
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
	{
		Rect bounds = child->viewBounds;
		OffsetRect(&bounds, -origin.h, -origin.v);
		long height = bounds.bottom - bounds.top;
		if (bounds.top < bottom)
			bounds.top = (short) bottom;
		bounds.bottom = (short) (bounds.top + height);
		child->SetBounds(bounds);
		bottom = bounds.bottom + spacing;
	}
	return bottom;
}


// ROM 0x00260274 Dump__5TViewFl
// The view printed for the debugger: its debug name, class, context,
// bounds and flags by name, one line per depth.
void
TView::Dump(long depth)
{
	char name[64];
	name[0] = 0;
	newton_try
	{
		RefVar debug(GetProto(RSSYMdebug));
		if (NOTNIL(debug))
		{
			if (IsSymbol(debug))
				strncpy(name, SymbolName(debug), sizeof(name) - 1);
			else
				ConvertFromUnicode(GetCString(debug), name, kMacRomanEncoding, sizeof(name) - 1);
			name[sizeof(name) - 1] = 0;
		}
	}
	newton_catch(exRootException)
	{ }
	end_try;
	for (long i = 0; i < depth; i++)
		gREPout->Print("|");
	gREPout->Print("%-12.12s", name);
	for (long i = depth; i < 8; i++)
		gREPout->Print(" ");
	gREPout->Print("%4s #%lX", "", (unsigned long) (Ref) fContext);
	gREPout->Print(" [%3ld,%3ld,%3ld,%3ld] %8lX", (long) viewBounds.left, (long) viewBounds.top, (long) viewBounds.right, (long) viewBounds.bottom, (unsigned long) fFlags);
	static const struct { ULong bit; const char* name; } kFlagNames[] = {
		{ vVisible, " vVisible" }, { vWriteProtected, " vWriteProtected" }, { vReadOnly, " vReadOnly" },
		{ vApplication, " vApplication" }, { vCalculateBounds, " vCalculateBounds" }, { vClipping, " vClipping" },
		{ vFloating, " vFloating" } };
	for (unsigned i = 0; i < sizeof(kFlagNames) / sizeof(kFlagNames[0]); i++)
		if (fFlags & kFlagNames[i].bit)
			gREPout->Print(kFlagNames[i].name);
	if ((fFlags & vAnythingAllowed) == vAnythingAllowed)
		gREPout->Print(" vAnythingAllowed");
	else
	{
		static const struct { ULong bit; const char* name; } kAllowed[] = {
			{ vClickable, " vClickable" }, { vStrokesAllowed, " vStrokesAllowed" }, { vGesturesAllowed, " vGesturesAllowed" },
			{ vCharsAllowed, " vCharsAllowed" }, { vNumbersAllowed, " vNumbersAllowed" }, { vLettersAllowed, " vLettersAllowed" },
			{ vPunctuationAllowed, " vPunctuationAllowed" }, { vShapesAllowed, " vShapesAllowed" }, { vMathAllowed, " vMathAllowed" },
			{ vPhoneField, " vPhoneField" }, { vDateField, " vDateField" }, { vTimeField, " vTimeField" },
			{ vAddressField, " vAddressField" }, { vNameField, " vNameField" }, { vCapsRequired, " vCapsRequired" },
			{ vCustomDictionaries, " vCustomDictionaries" } };
		for (unsigned i = 0; i < sizeof(kAllowed) / sizeof(kAllowed[0]); i++)
			if (fFlags & kAllowed[i].bit)
				gREPout->Print(kAllowed[i].name);
	}
	if (fFlags & vSelected)
		gREPout->Print(" vSelected");
	if (fFlags & vClipboard)
		gREPout->Print(" vClipboard");
	if (fFlags & vNoScripts)
		gREPout->Print(" vNoScripts");
	if (fFlags & vHasIdlerHint)
		gREPout->Print(" vHasIdlerHint");
	gREPout->Print("\n");
	TViewLoop loop(fChildren);
	for (TView* child = loop.Next(); child != nil; child = loop.Next())
		child->Dump(depth + 1);
}
