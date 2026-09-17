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
#include "NewtonTime.h"
#include "CompMath.h"
#include "Rects.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"


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
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "IdleViews")), RefVar(MakeCFunction((void*) FIdleViews, 0, nil)));
	RegisterShapeNatives();
	RegisterApplicationNatives();
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
		{ "DrawShape", (void*) FDrawShape, 2 }, { "AddUndoAction", (void*) FAddUndoAction, 2 }, { "SetupIdle", (void*) FSetupIdleX, 1 },
		{ nil, nil, 0 } };
	RefVar methods(AllocateFrame());
	for (long i = 0; kMethods[i].fName != nil; i++)
		SetFrameSlot(methods, RefVar(Intern((char*) kMethods[i].fName)), RefVar(MakeCFunction(kMethods[i].fFn, kMethods[i].fArgs, nil)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Open")), RefVar(CompileScriptFunction(kOpenSource)));
	SetFrameSlot(methods, RefVar(Intern((char*) "Toggle")), RefVar(CompileScriptFunction(kToggleSource)));
	return methods;
}
