/*
	File:		views/View.cpp

	Contains:	TView: the bases, the child list and its loops, the clipper,
				the structure (making, adding, removing, ordering, finding),
				the context and the slot cache, the scripts, the flags and
				values, and the bounds (justification against the parent).
				The drawing side is ViewDraw.cpp, the making of contexts and
				views BuildView.cpp.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "View.h"
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
#include <string.h>

TViewList*	TView::gEmptyViewList = nil;		// ROM 0x0c101a1c gEmptyViewList__5TView
long		TView::gViewIdCounter = 0;			// ROM 0x0c102050
TRootView*	gRootView = nil;					// ROM 0x0c101a20 gRootView
// ROM 0x0c102618 gModalCount
// How many modal dialogs are up.  NOT YET RECONSTRUCTED: the modal
// dialog code that raises and lowers it, so nothing is ever modal.
long		gModalCount = 0;
RefStruct*	gSlotCacheTable = nil;				// ROM 0x0c10204c slotCacheRefs (the array, not a pointer into it: the host's heap compacts)
Boolean		gSkipVisRegions = false;			// ROM 0x0c102054 gSkipVisRegions
Boolean		gDontDrawHilites = false;			// ROM 0x0c100cb8 gDontDrawHilites
Boolean		gOutlineViews = false;				// ROM 0x0c101a28 gOutlineViews
long		gSlowMotion = 0;					// ROM 0x0c101a2c gSlowMotion

// the ROM's exception for a bounds frame that is not one
static char kBadBoundsFrame[] = "bad bounds frame";


/*------------------------------------------------------------------------------
	T x O b j e c t,  T R e s p o n d e r
------------------------------------------------------------------------------*/

// ROM 0x0014528c __nw__8TxObjectSFUi
// A cleared block (NewtPtrClear), or exOutOfMemory.
void*
TxObject::operator new(size_t size)
{
	void* p = NewPtrClear(size);
	if (p == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	return p;
}


// ROM 0x001452c8 __dl__8TxObjectSFPv
void
TxObject::operator delete(void* p)
{
	DisposPtr((Ptr) p);
}


// ROM 0x001452e4 ClassID__8TxObjectCFv
long
TxObject::ClassID(void) const
{
	return 0;
}


// ROM 0x001452ec DerivedFrom__8TxObjectCFl
Boolean
TxObject::DerivedFrom(long /*id*/) const
{
	return false;
}


// ROM 0x001452cc __dt__8TxObjectFv
TxObject::~TxObject()
{ }


// ROM 0x00145300 Key__8TxObjectCFv
ULong
TxObject::Key(void) const
{
	return 0;
}


// ROM 0x001ab8f0 ClassID__10TResponderCFv
long
TResponder::ClassID(void) const
{
	return 0;
}


// ROM 0x001ab8f8 DerivedFrom__10TResponderCFl
Boolean
TResponder::DerivedFrom(long id) const
{
	return TxObject::DerivedFrom(id);
}


// ROM 0x001ab92c DoCommand__10TResponderFRC6RefVar
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

// ROM 0x00142ed4 __ct__9TListLoopFP5CList
TListLoop::TListLoop(CList* list)
{
	fList = list;
	Reset();
}


// ROM 0x00142f10 Reset__9TListLoopFv
void
TListLoop::Reset(void)
{
	fIndex = -1;
	fCount = fList->GetArraySize();
}


// ROM 0x00142f28 Next__9TListLoopFv
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


// ROM 0x00142f38 Current__9TListLoopFv
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


// ROM 0x00142f50 RemoveCurrent__9TListLoopFv
void
TListLoop::RemoveCurrent(void)
{
	fList->RemoveElementsAt(fIndex, 1);
	fIndex--;
	fCount--;
}


// ROM 0x00142f8c __ct__13TBackwardLoopFP5CList
TBackwardLoop::TBackwardLoop(CList* list)
{
	fIndex = list->GetArraySize();
	fList = list;
}


// ROM 0x00142fc4 Next__13TBackwardLoopFv
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


// ROM 0x00142fd4 Current__13TBackwardLoopFv
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


// ROM 0x0025e2fc GetFirstNonFloater__FP9TViewList
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

// ROM 0x00066bf8 __ct__8TClipperFv
TClipper::TClipper()
{
	fIsObscured = false;
}


// ROM 0x00066c3c UpdateRegions__8TClipperFP5TView
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


// ROM 0x00066cfc Offset__8TClipperF6TPoint
void
TClipper::Offset(Point delta)
{
	OffsetRgn(fFullRgn, delta.h, delta.v);
}


// ROM 0x00066d28 RecalcVisible__8TClipperF11TBaseRegion
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

// ROM 0x0025d358 ClassID__5TViewCFv
long
TView::ClassID(void) const
{
	return clView;
}


// ROM 0x00261688 DerivedFrom__5TViewCFl
Boolean
TView::DerivedFrom(long id) const
{
	return id == clView || TResponder::DerivedFrom(id);
}


// ROM 0x00266bbc __dt__5TViewFv
// The context's RefStruct goes; the children were removed by Delete.
TView::~TView()
{ }


// ROM 0x00266c94 DoCommand__5TViewFRC6RefVar
Boolean
TView::DoCommand(RefArg cmd)
{
	return RealDoCommand(cmd);
}


// ROM 0x00264430 Constructor__5TViewFRC6RefVarP5TView
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


// ROM 0x0026564c Delete__5TViewFv
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


// ROM 0x00266e00 RealDoCommand__5TViewFRC6RefVar
// The commands a view answers (their ids Commands.h): the scripts -
// aeClick runs viewClickScript(unit) on a clickable view ('skip: the
// result 0, the click passed on), aeStroke viewStrokeScript(unit),
// aeScrub/aeCaret/aeLine and the other gestures viewGestureScript(unit,
// kind), aeWord viewWordScript(unit), aeRawInk viewRawInkScript(strokes),
// aeInkWord viewInkWordScript(strokes), aeScrollUp/Down and aeOverview
// their scripts (vars.lastTextChanged cleared after) - each handled
// unless the script answered nil; the key events (HandleKeyEvent: NOT
// YET RECONSTRUCTED); the structure - aeAddChild adds the frame
// parameter's view and shows it (aeShow with the parameter, dispatched),
// aeDropChild hides and removes the parameter's view, aeHide hides,
// aeShow shows (under a modal dialog a view outside it is shown
// ModalSafeShow - NOT YET: shown), aeAddData puts the frame parameter in
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
				handled = true;		// (the ROM's 2: taken, but passed on)
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
			RefVar args(MakeArray(2));
			Long unit = CommandParameter(cmd);
			SetArraySlotRef(args, 0, unit != 0 ? AddressToRef((void*) unit) : NILREF);
			SetArraySlotRef(args, 1, MAKEINT(id));
			handled = ScriptHandled(cmd, RunScript(RSSYMviewgesturescript, args, true));
			if (handled)
				gRootView->fDirtyFlag = true;
		}
		break;

	case aeWord:
		handled = ScriptHandled(cmd, RunScript(RSSYMviewwordscript, RefVar(UnitArgs(cmd)), true));
		SetFrameSlot(RefVar(gVarFrame), RSSYMlasttextchanged, RefVar(NILREF));
		break;

	case aeRawInk:
	case aeInkWord:
		{
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, GetStrokeBundleFromCommand(cmd));
			handled = ScriptHandled(cmd, RunCacheScript(id == aeRawInk ? kIndexViewRawInkScript : kIndexViewInkWordScript, args, true));
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
		}
		break;

	case aeHide:
		Hide();
		break;

	case aeShow:
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
		if ((fFlags & (vReadOnly | vWriteProtected)) == 0)
		{
			Rect src, dst;
			src.left = (short) CommandIndexParameter(cmd, 0);
			src.top = (short) CommandIndexParameter(cmd, 1);
			dst.left = (short) CommandIndexParameter(cmd, 2);
			dst.top = (short) CommandIndexParameter(cmd, 3);
			src.right = dst.right = 0;
			src.bottom = dst.bottom = 0;
			Scale(src, dst);
			RefVar undo(MakeCommand(aeScaleData, this, kNoParameter));
			CommandSetIndexParameter(undo, 0, dst.left);
			CommandSetIndexParameter(undo, 1, dst.top);
			CommandSetIndexParameter(undo, 2, src.left);
			CommandSetIndexParameter(undo, 3, src.top);
			gApplication->PostUndoCommand(undo);
			fParent->Dirty(nil);
		}
		break;

	case aeAddHilite:
		{
			RefVar hilite(CommandFrameParameter(cmd));
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
		}
		break;

	default:
		break;
	}
	return handled;
}


// ROM 0x0025dca4 TextFlags__5TViewCFv
// The textFlags slot, 0 when none.
long
TView::TextFlags(void) const
{
	Ref flags = GetProto(RSSYMtextflags);
	return ISINT(flags) ? RVALUE(flags) : 0;
}


// ROM 0x0025dcfc InsideView__5TViewFR6TPoint
// Whether the point is within the outer bounds.
Boolean
TView::InsideView(Point& pt)
{
	Rect bounds;
	OuterBounds(&bounds);
	return PtInRect(pt, &bounds);
}


// ROM 0x00263894 ChildBoundsChanged__5TViewFP5TViewR5TRect
void
TView::ChildBoundsChanged(TView* /*child*/, Rect& /*bounds*/)
{ }


// ROM 0x00263898 SetupForm__5TViewFv
void
TView::SetupForm(void)
{
	RunScript(RSSYMviewsetupformscript, RefVar(NILREF));
}


// ROM 0x00263988 SetupDone__5TViewFv
void
TView::SetupDone(void)
{
	RunScript(RSSYMviewsetupdonescript, RefVar(NILREF));
}


// ROM 0x002688c8 GetRangeText__5TViewFlT1
Ref
TView::GetRangeText(long /*start*/, long /*end*/)
{
	return NILREF;
}


// ROM 0x002688d0 GetValue__5TViewFRC6RefVarT1
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


// ROM 0x00268ab4 SetValue__5TViewFRC6RefVarT1
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


// ROM 0x00268cfc Changed__5TViewFRC6RefVar
void
TView::Changed(RefArg slot)
{
	Changed(slot, fContext);
}


// ROM 0x00268d08 Changed__5TViewFRC6RefVarT1
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


// ROM 0x0026418c Hilite__5TViewFUc
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


// ROM 0x00267d00 HandleKeyEvent__5TViewFRC6RefVarUlPUc
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


// ROM 0x00264c34 Select__5TViewFUcT1
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


// ROM 0x002643c4 SelectNone__5TViewFv
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


// ROM 0x00268768 SetCaretOffset__5TViewFPlT1
void
TView::SetCaretOffset(long* /*offset*/, long* /*length*/)
{ }


// ROM 0x00268764 SetSelection__5TViewFRC6RefVarPlT2
void
TView::SetSelection(RefArg /*selection*/, long* /*start*/, long* /*end*/)
{ }


// ROM 0x00268750 GetSelection__5TViewFv
Ref
TView::GetSelection(void)
{
	return NILREF;
}


// ROM 0x0026876c ActivateSelection__5TViewFUc
// The view gains or loses the caret: its viewCaretActivateScript(on).
void
TView::ActivateSelection(Boolean on)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(args, 0, MAKEBOOLEAN(on));
	RunCacheScript(kIndexViewCaretActivateScript, args);
}


// ROM 0x0009f9ec DoEditCommand__5TViewFl
Boolean
TView::DoEditCommand(long /*command*/)
{
	return false;
}


// ROM 0x00268810 OffsetToCaret__5TViewFlP5TRect
void
TView::OffsetToCaret(long /*offset*/, Rect* caret)
{
	SetEmptyRect(caret);
}


// ROM 0x002687f0 PointToCaret__5TViewFR6TPointP5TRectT2
void
TView::PointToCaret(Point& /*pt*/, Rect* caret, Rect* /*bounds*/)
{
	SetEmptyRect(caret);
}


// ROM 0x0025db24 RemoveAllViews__5TViewFv
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


// ROM 0x00266c04 Idle__5TViewFl
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

// ROM 0x00265250 DrawHiliting__5TViewFv
// The base draws no hiliting of its own.
void	TView::DrawHiliting(void)									{ }


// ROM 0x00265224 DrawHilitedData__5TViewFv
// The whole view, drawn again - a data view that hilites part of itself
// overrides this with something smaller.
void
TView::DrawHilitedData(void)
{
	Draw(viewBounds, false);
}


// the pen-driven hiliting: NOT YET RECONSTRUCTED (the recogniser's units)
// ROM 0x00260218 HandleHilite__5TViewFP11TUnitPubliclUc
// A stroke over the view selects the whole of it: the unit's box, grown by
// eight pixels, has to cover more than 60 per cent of the view.  A stroke
// that is long and thin counts by its long axis alone - a line drawn across
// a one-line view covers little of it, so the short axis is taken out of
// both rectangles and the test made on the other one.  Only the hilite and
// un-hilite gestures (1 and -1) are looked at, and a false `doIt` asks
// whether the stroke would count without acting on it.
Boolean
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


// ROM 0x002605f0 HandleScrub__5TViewFRC5TRectlP11TUnitPublicUc
// A scrub over the view: the answer is the gesture the view takes (5) when
// the scrub covers more than 75 per cent of it.  A read-only or
// write-protected view takes none.  The base only answers - the caller is
// what acts - so the unit and `doIt` go unused here.
Boolean
TView::HandleScrub(const Rect& bounds, long gesture, TUnitPublic* /*unit*/, Boolean /*doIt*/)
{
	if ((fFlags & (vReadOnly | vWriteProtected)) != 0)
		return false;
	if (gesture != 5 && gesture != -1)
		return false;
	return CoveredBy(&viewBounds, &bounds) > 75 ? 5 : 0;
}


// ROM 0x0025feac Hilited__5TViewFv
// Whether anything in the view is selected.
Boolean
TView::Hilited(void)
{
	RefVar hilites(Hilites());
	return NOTNIL(hilites) && Length(hilites) != 0;
}


// ROM 0x0025fe3c Hilites__5TViewFv
// The view's selections: the `hilites` slot of the context.
Ref		TView::Hilites(void)		{ return GetProto(RSSYMhilites); }


// ROM 0x0025fef8 FirstHilite__5TViewFv
// The first hilite, or nil.
Ref
TView::FirstHilite(void)
{
	RefVar hilites(Hilites());
	if (NOTNIL(hilites) && Length(hilites) != 0)
		return GetArraySlotRef(hilites, 0);
	return NILREF;
}


// ROM 0x0025ff5c DrawHilites__5TViewFUc
// The base draws none; a data view (TParagraphView) overrides it.
void	TView::DrawHilites(Boolean)									{ }


// ROM 0x002600c8 IsCompletelyHilited__5TViewFRC6RefVar
// Whether the hilite covers a whole item: true for a view with no items of
// its own to be partly selected.
Boolean	TView::IsCompletelyHilited(RefArg)							{ return true; }


// ROM 0x002600d0 HiliteAll__5TViewFv
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


// ROM 0x002601cc DeleteHilited__5TViewFRC6RefVar
// The parent is asked to delete what is selected here - a data view keeps
// the items, so the child that holds the hilite is not the one that owns
// them.  The hilite the caller names is not looked at.
void
TView::DeleteHilited(RefArg)
{
	RefVar cmd(MakeCommand(aeRemoveData, fParent, fId));
	gApplication->DispatchCommand(cmd);
}


// ROM 0x0025ff60 RemoveHilite__5TViewFRC6RefVar
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


// ROM 0x0026002c RemoveAllHilites__5TViewFv
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


// ROM 0x002603a0 GlobalHiliteBounds__5TViewFP5TRect
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


// ROM 0x002604dc GlobalHiliteResizeBounds__5TViewFP5TRect
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


// ROM 0x00260514 GlobalHilitePinnedBounds__5TViewFP5TRect
// The base pins a selection to where the hilites are.
void
TView::GlobalHilitePinnedBounds(Rect* bounds)
{
	GlobalHiliteBounds(bounds);
}


// ROM 0x00260ae8 IsGridded__5TViewFRC6RefVarP6TPoint
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


// ROM 0x0026051c PointInHilite__5TViewFR6TPoint
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
long	TView::ClickOptions(void)									{ return 0; }		// ROM 0x00260630 ClickOptions__5TViewFv
void	TView::DrawScaledData(const Rect&, const Rect&, Rect*)		{ }		// ROM 0x00260638 DrawScaledData__5TViewFRC5TRectT1P5TRect
void	TView::Scale(const Rect&, const Rect&)						{ }		// ROM 0x002606bc Scale__5TViewFRC5TRectT1

/*------------------------------------------------------------------------------
	D r a g   a n d   d r o p
	The pen-tracked drag of a view's data onto another; the source offers
	drag items (AddDragInfo), the target under the pen is found and asked
	which types it takes (GetSupportedDropTypes/AcceptDrop), given the data
	(GetDropData from the source) and told to drop (Drop -> viewDropScript).
	Each step runs the matching view script.
------------------------------------------------------------------------------*/

// ROM 0x0009f848 AddDragInfo__5TViewFP9TDragInfo
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


// ROM 0x000a27c0 GetDropData__5TViewFRC6RefVarT1
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


// ROM 0x000a26fc GetSupportedDropTypes__5TViewFRC6TPoint
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


// ROM 0x000a24c0 AcceptDrop__5TViewFRC9TDragInfoRC6TPoint
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


// ROM 0x0009ddc4 Drop__5TViewFRC6RefVarT1P6TPoint
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


// ROM 0x000a25e4 DropMove__5TViewFRC6RefVarRC6TPointT2Uc
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


// ROM 0x0009de90 DropRemove__5TViewFRC6RefVar
// The dragged item removed from the source after a move:
// viewDropRemoveScript([dragRef]).
Boolean
TView::DropRemove(RefArg dragRef)
{
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, dragRef);
	return NOTNIL(RunScript(RSSYMviewdropremovescript, args, true));
}


// ROM 0x0009df10 DropApprove__5TViewFP5TView
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


// ROM 0x0009e334 DropDone__5TViewFv
Boolean
TView::DropDone(void)
{
	return NOTNIL(RunScript(RSSYMviewdropdonescript, RefVar(NILREF), true));
}


// ROM 0x0009e7c8 TargetDrop__5TViewFRC9TDragInfoRC6TPoint
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


// ROM 0x0009dfb4 EndDrag__5TViewFRC9TDragInfoP5TViewRC6TPointN23Uc
// The drag delivered: for each item, when it is dropped on this same view
// it is moved (DropMove); else the target's supported types pick the
// item's type, the data is fetched from the source (GetDropData) and the
// target told to Drop it - a successful non-copy drop then removes the
// item from the source (DropRemove).  The target's DropDone ends it.
void
TView::EndDrag(const TDragInfo& info, TView* target, const Point& startPt, const Point& dropPt, const Point& dragPt, Boolean copy)
{
	TDragInfo& dragInfo = (TDragInfo&) info;
	long count = dragInfo.Count();
	for (long i = 0; i < count; i++)
	{
		RefVar dragRef(dragInfo.GetItemDragRef(i));
		if (this == target)
		{
			DropMove(dragRef, startPt, dragPt, copy);
			continue;
		}
		RefVar types(target->GetSupportedDropTypes(dropPt));
		RefVar type(dragInfo.FindType(i, types));
		RefVar data(GetDropData(type, dragRef));
		if (ISNIL(data))
		{
			TView* itemView = dragInfo.GetItemView(i);
			if (itemView != nil)
				data = itemView->GetDropData(type, dragRef);
		}
		if (ISNIL(data))
			continue;
		Point pt = dropPt;
		if (target->Drop(type, data, &pt))
		{
			if (!copy && (fFlags & (vReadOnly | vWriteProtected)) == 0)
				DropRemove(dragRef);
		}
	}
	target->DropDone();
}


// ROM 0x0009e394 DragAndDrop__5TViewFP13TStrokePublicRC5TRectPC5TRectT3UcRC9TDragInfoT3 (NOT YET RECONSTRUCTED: the pen-tracked drag
// with the clipboard icon following the pen; the host's simplified drag
// tracks the pen and drops on the target under the release point)
Boolean
TView::DragAndDrop(TStrokePublic* stroke, const Rect& bounds, const Rect* /*limit*/, const Rect* /*slop*/, Boolean copy, const TDragInfo& info, const Rect* /*dragBounds*/)
{
	stroke->InkOff(true);
	TDragInfo& dragInfo = (TDragInfo&) info;
	if (dragInfo.Count() == 0)
		AddDragInfo(&dragInfo);
	Point start = stroke->FirstPoint();
	// follow the pen (a full drag draws the item as it moves - NOT YET);
	// the release point picks the drop target
	while (!stroke->Done())
		Wait(1);
	Point drop = stroke->FinalPoint();
	TView* target = TargetDrop(dragInfo, drop);
	if (target == nil)
		return false;
	if (!DropApprove(target))
		return false;
	EndDrag(dragInfo, target, start, drop, drop, copy);
	return true;
}


// the default drop hooks a view without its own behaviour uses
void	TView::DrawDragBackground(const Rect&, Boolean)				{ }
void	TView::DrawDragData(const Rect&)							{ }
Boolean	TView::GetClipboardDataBits(Rect*)							{ return false; }
void	TView::DragFeedback(const TDragInfo&, const Point&, Boolean)	{ }
TView*	TView::FindDropView(const TDragInfo&, const Point&)			{ return this; }		// ROM 0x000a1ff4 FindDropView__5TViewFRC9TDragInfoRC6TPoint (a view is its own drop target)


// ROM 0x00268290 BuildKeyChildList__5TViewFP9TViewListlT2
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


// ROM 0x002683a0 NextKeyView__5TViewFP5TViewlT2
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

// ROM 0x0025d274 AddView__5TViewFRC6RefVar
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


// ROM 0x0025d994 AddView__5TViewFP5TView
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


// ROM 0x00263f14 AddChild__5TViewFRC6RefVar
// The template's view when it is already a child, else a new one.
TView*
TView::AddChild(RefArg templ)
{
	TView* child = Exists(fChildren, templ);
	if (child == nil)
		child = AddView(templ);
	return child;
}


// ROM 0x00260cd4 AddViews__5TViewFUc
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


// ROM 0x0025da28 RemoveView__5TViewFv
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


// ROM 0x0025da34 RemoveChildView__5TViewFP5TView
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


// ROM 0x00260468 RemoveUnmarked__5TViewFv
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


// ROM 0x0025eda0 ReorderView__5TViewFP5TViewl
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


// ROM 0x0025f2b0 BringToFront__5TViewFv
void
TView::BringToFront(void)
{
	fParent->ReorderView(this, fParent->fChildren->GetArraySize());
}


// ROM 0x0025f2c4 MoveChildBehind__5TViewFP5TViewT1
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


// ROM 0x0025d504 AddToSoup__5TViewFRC6RefVar
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


// ROM 0x0025d66c RemoveFromSoup__5TViewFP5TView
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


// ROM 0x0025dbfc FindView__5TViewFRC6RefVar
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


// ROM 0x0025dd38 Distance__5TViewF6TPointP6TPoint
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


// ROM 0x0025de40 FindClosestView__5TViewF6TPointUlPlP6TPointPUc
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


// ROM 0x0025df5c FindView__5TViewF6TPointUlP6TPoint
TView*
TView::FindView(Point pt, ULong flags, Point* delta)
{
	long distance;
	Boolean clipped;
	return FindClosestView(pt, flags, &distance, delta, &clipped);
}


// ROM 0x00265460 FindID__5TViewFl
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


// ROM 0x0025f3d4 FrontMost__5TViewFv
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


// ROM 0x0025f448 FrontMostApp__5TViewFv
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


// ROM 0x0025f328 ChildViewFrames__5TViewFv
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


// ROM 0x00268758 Children__5TViewFv
// The viewChildren array.
Ref
TView::Children(void)
{
	return GetProto(RSSYMviewchildren);
}


// ROM 0x00263d10 GetWindowView__5TViewFv
// The ancestor that is a child of the root view.
TView*
TView::GetWindowView(void)
{
	TView* view = this;
	while (!view->HasVisRgn())
		view = view->fParent;
	return view;
}


// ROM 0x00268200 ProtoedFrom__5TViewFRC6RefVar
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


// ROM 0x00268830 Clipper__5TViewCFv
// The clipper of a child of the root view, from its context's viewclipper.
TClipper*
TView::Clipper(void) const
{
	if (!HasVisRgn())
		return nil;
	Ref clipper = GetFrameSlotRef(fContext, RSSYMviewclipper);
	return NOTNIL(clipper) ? (TClipper*) RefToAddress(clipper) : nil;
}


// ROM 0x00268898 HasVisRgn__5TViewCFv
// The children of the root view have visible regions of their own.
Boolean
TView::HasVisRgn(void) const
{
	return fParent == gRootView && this != gRootView;
}


// ROM 0x00260a18 VisibleDeep__5TViewCFv
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

// ROM 0x00269038 DataFrame__5TViewFv
// The realData of a data view, else the context.
Ref
TView::DataFrame(void)
{
	Ref data = GetCacheProto(kIndexRealData);
	return NOTNIL(data) ? data : (Ref) fContext;
}


// ROM 0x00269074 GetProto__5TViewCFRC6RefVar
// The slot along the context's proto chain (GetProtoVariable, with the
// nil-context error).
Ref
TView::GetProto(RefArg slot) const
{
	if (ISNIL(fContext))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, slot);
	return GetProtoVariable(fContext, slot, nil);
}


// ROM 0x00269080 GetVar__5TViewCFRC6RefVar
// The slot along the context's proto and parent chains (GetVariable).
Ref
TView::GetVar(RefArg slot) const
{
	if (ISNIL(fContext))
		ThrowExInterpreterWithSymbol(kNSErrNilContext, slot);
	return GetVariable(fContext, slot, nil, 0);
}


// ROM 0x00269090 GetWriteableProtoVariable__5TViewFRC6RefVar
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


// ROM 0x00269134 GetWriteableVariable__5TViewFRC6RefVar
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


// ROM 0x002691b4 SetContextSlot__5TViewFRC6RefVarT1
// The slot set in the context itself (SetFrameSlot: the frame must be
// writeable; the lookup caches are cleared).
void
TView::SetContextSlot(RefArg slot, RefArg value)
{
	SetFrameSlot(fContext, slot, value);
}


// ROM 0x002691bc SetDataSlot__5TViewFRC6RefVarT1
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


// ROM 0x0025d474 GetCacheProto__5TViewFl
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


// ROM 0x0025d3e4 GetCacheVariable__5TViewFl
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


// ROM 0x00261d94 InvalidateSlotCache__5TViewFl
// The slot may be there again.
void
TView::InvalidateSlotCache(long index)
{
	if (index < 32)
		fSlotCache |= 1UL << index;
	else
		fSlotCache2 |= 1UL << (index - 32);
}


// ROM 0x00261dc8 RunScript__5TViewFRC6RefVarT1UcPUc
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


// ROM 0x00261c84 RunCacheScript__5TViewFlRC6RefVarUcPUc
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


// ROM 0x0025d730 Sync__5TViewFv
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
				gRootView->SetModalView(this);
			Dirty(nil);
		}
		TViewLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
			child->RecalcBounds();
	}
}


// ROM 0x0025d360 SetFlags__5TViewFUl
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


// ROM 0x00268c78 ClearFlags__5TViewFUl
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


// ROM 0x0025f8d4 GetTextStyle__5TViewFv
// The viewFont, the user's font when none.
Ref
TView::GetTextStyle(void)
{
	Ref font = GetCacheVariable(kIndexViewFont);
	if (ISNIL(font))
		font = GetPreference(RSSYMuserfont);
	return font;
}


// ROM 0x0025f948 GetTextStyleRecord__5TViewFP11StyleRecord
void
TView::GetTextStyleRecord(StyleRecord* style)
{
	CreateTextStyleRecord(RefVar(GetTextStyle()), style);
}


// ROM 0x0025fe0c Printing__5TViewFv
// Whether the view is in a print view (NOT YET: no print views).
Boolean
TView::Printing(void)
{
	return false;
}


/*------------------------------------------------------------------------------
	T V i e w :  b o u n d s
------------------------------------------------------------------------------*/

// ROM 0x00262114 OuterBounds1__FP5TRectUl
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


// ROM 0x00262198 OuterBounds__5TViewFP5TRect
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


// ROM 0x00263624 SetBounds__5TViewFRC5TRect
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


// ROM 0x00265520 GetChildOrigin__5TViewFP6TPoint
// The scroll origin of the contents: viewOriginX, viewOriginY.
void
TView::GetChildOrigin(Point* origin)
{
	Ref x = GetCacheProto(kIndexViewOriginX);
	origin->h = (short) (ISNIL(x) ? 0 : RINT(x));
	Ref y = GetCacheProto(kIndexViewOriginY);
	origin->v = (short) (ISNIL(y) ? 0 : RINT(y));
}


// ROM 0x002655cc ContentsOrigin__5TViewFv
// Where the contents' (0, 0) is: the bounds' top left less the scroll
// origin.
Point
TView::ContentsOrigin(void)
{
	Point origin;
	GetChildOrigin(&origin);
	return MakePoint(viewBounds.left - origin.h, viewBounds.top - origin.v);
}


// ROM 0x00261e6c LocalOrigin__5TViewCFv
// The bounds' top left relative to the contents' origin.
Point
TView::LocalOrigin(void) const
{
	Point origin = ((TView*) this)->ContentsOrigin();
	return MakePoint(viewBounds.left - origin.h, viewBounds.top - origin.v);
}


// ROM 0x002633fc SetOrigin__5TViewFR6TPoint
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


// ROM 0x00262224 JustifyBounds__5TViewFP5TRect
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


// ROM 0x00262b1c DejustifyBounds__5TViewFP5TRect
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


// ROM 0x0026336c RecalcBounds__5TViewFv
// The bounds set again from the template's viewBounds (after the parent
// moved or changed size); a template without a proper viewBounds keeps
// the children's recalculated instead... the ROM recalculates the children
// when the frame does not convert, then sets the bounds regardless.
void
TView::RecalcBounds(void)
{
	RefVar bounds(GetProto(RSSYMviewbounds));
	Rect r;
	if (!FromObject(bounds, r))
	{
		TViewLoop loop(fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
			child->RecalcBounds();
		SetRect(&r, 0, 0, 0, 0);
	}
	SetBounds(r);
}


// ROM 0x00261ff0 WriteBounds__5TViewFRC5TRect
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


// ROM 0x00261ee4 Move__5TViewFRC6TPoint
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


// ROM 0x0025df8c Offset__5TViewF6TPoint
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
		gRootView->SetModalView(this);
}


// ROM 0x0025e064 SimpleOffset__5TViewF6TPointl
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


// ROM 0x0025e0f0 ChildViewMoved__5TViewFP5TView6TPoint
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


// ROM 0x002636e4 ChildrenHeight__5TViewFPl
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


// ROM 0x00263760 SetChildrenVertical__5TViewFlT1
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


// ROM 0x0025e33c Dump__5TViewFl
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
