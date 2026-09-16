/*
	File:		views/BuildView.cpp

	Contains:	Making views from templates: the context (BuildContext), the
				C++ object of the template's class (BuildView), finding a
				view from a context (GetView), the bounds frames (ToObject,
				FromObject), and the host's setup of the view system (the
				slot cache symbols, the prototype frames, the root view).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "RootView.h"
#include "TextView.h"
#include "Rects.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"


// ROM 0x001f18dc BadWickedNaughtyNoot__Fl
// A warning to the developer: the global BadWickedNaughtyNoot (a
// NewtonScript function) is called with the number, when there is one.
void
BadWickedNaughtyNoot(long which)
{
	if (NOTNIL(GetFrameSlotRef(gFunctionFrame, RSSYMbadwickednaughtynoot)))
		NSCallGlobalFn(RSSYMbadwickednaughtynoot, RefVar(MAKEINT(which)), RefVar(NILREF));
}


/*------------------------------------------------------------------------------
	C o n t e x t s
------------------------------------------------------------------------------*/

// ROM 0x0025c598 GetCacheContext__FRC6RefVar
// A template's _cacheContext (a context kept for it) cloned, protoed to
// the template and parented to the root's context; nil when it has none.
Ref
GetCacheContext(RefArg templ)
{
	RefVar context(GetFrameSlotRef(templ, RSSYM_cachecontext));
	if (NOTNIL(context))
	{
		context = Clone(context);
		SetFrameSlot(context, RSSYM_proto, templ);
		SetFrameSlot(context, RSSYM_parent, gRootView->fContext);
	}
	return context;
}


// ROM 0x0025c634 BuildContext__5TViewFRC6RefVarUc
// The context a view runs in, from its template.  A template without a
// viewClass names a viewStationery (an ink slot makes it 'poly): the
// stationery's form in vars.stdForms supplies the class; a class with
// bit 16 makes the template the form's realData (the template, cloned if
// read-only, protoed to the form), else the form itself is the template.
// Unless forced, a template whose viewFlags lack vVisible makes no
// context (nil).  The context is the template's _cacheContext or a clone
// of canonicalContext (canonicalDataContext for data), with the template
// as _proto and the root's context as _parent (the Constructor sets the
// real parent), realData when there is data, viewStationery 'poly for ink.
Ref
TView::BuildContext(RefArg templ, Boolean forceVisible)
{
	RefVar viewClass(GetProtoVariable(templ, RSSYMviewclass, nil));
	RefVar form(templ);
	RefVar data;
	Boolean isInk = false;
	if (ISNIL(viewClass))
	{
		viewClass = GetProtoVariable(templ, RSSYMviewstationery, nil);
		if (ISNIL(viewClass) && NOTNIL(GetProtoVariable(templ, RSSYMink, nil)))
		{
			isInk = true;
			viewClass = RSSYMpoly;
		}
		if (ISNIL(viewClass))
			Throw(exRootException, (void*) kViewErrNoViewClass, nil);
		RefVar stdForms(GetFrameSlotRef(gVarFrame, RSSYMstdforms));
		RefVar stationery(GetProtoVariable(stdForms, viewClass, nil));
		if (ISNIL(stationery))
			Throw(exRootException, (void*) kViewErrNoStationery, nil);
		viewClass = GetVariable(stationery, RSSYMviewclass, nil, 0);
		data = templ;
		if ((RINT(viewClass) & clDataTemplate) == 0)
			form = stationery;
		else
		{
			if (ObjectFlags(form) & kObjReadOnly)
				form = Clone(form);
			SetFrameSlot(form, RSSYM_proto, stationery);
		}
	}
	if (!forceVisible)
	{
		RefVar flags(GetProtoVariable(form, RSSYMviewflags, nil));
		if (ISNIL(flags))
			Throw(exRootException, (void*) kViewErrNoViewFlags, nil);
		if ((RINT(flags) & vVisible) == 0)
			return NILREF;
	}
	RefVar context(GetCacheContext(form));
	if ((RINT(viewClass) & clDataTemplate) != 0)
	{
		data = templ;
		if (ISNIL(context))
			context = Clone(RefVar(Rcanonicaldatacontext));
	}
	if (ISNIL(context))
		context = Clone(RefVar(Rcanonicalcontext));
	if (isInk)
		SetFrameSlot(context, RSSYMviewstationery, RSSYMpoly);
	SetFrameSlot(context, RSSYM_proto, form);
	SetFrameSlot(context, RSSYM_parent, gRootView->fContext);
	if (NOTNIL(data))
		SetFrameSlot(context, RSSYMrealdata, data);
	return context;
}


// ROM 0x0025ca18 BuildView__FP5TViewRC6RefVar
// The C++ object for the context's viewClass, constructed under the
// parent; a Throw in the Constructor removes the view again.  NOT YET
// RECONSTRUCTED: the subclasses - every class gets a TView (the ROM makes
// TPictureView, TEditView, TKeyboardView, TMonthView, TParagraphView,
// TPolygonView, TMathExpView, TMathOpView, TMathLineView, TRemoteView,
// TPickView, TGaugeView, TPrintView, TMeetingView, TSliderView,
// TListView, TClipboard, TOutline, THelpOutline, TXView for classes
// 75-108, and -8501 for any other); TTextView (97, 98) is here.
TView*
BuildView(TView* parent, RefArg context)
{
	long viewClass = RINT(GetProtoVariable(context, RSSYMviewclass, nil)) & 0xffff;
	TView* view = nil;
	switch (viewClass)
	{
	case clView:
	case clPictureView - 1: case clPictureView:
	case clEditView:
	case clContainerView: case clKeyboardView:
	case clMonthView:
	case clParagraphView:
	case clPolygonView:
	case clDataView: case clMathExpView:
	case clMathOpView:
	case clMathLineView:
	case clRemoteView - 1: case clRemoteView:
	case clPickView - 2: case clPickView - 1: case clPickView:
	case clGaugeView:
	case clPrintView - 1: case clPrintView:
	case clMeetingView:
	case clSliderView:
	case clListView:
	case clClipboard - 1: case clClipboard:
	case clOutline - 3: case clOutline - 2: case clOutline - 1: case clOutline:
	case clHelpOutline - 1: case clHelpOutline:
	case clTXView:
		view = new TView;
		break;
	case clTextView - 1: case clTextView:
		view = new TTextView;
		break;
	default:
		break;
	}
	if (view == nil)
		Throw(exRootException, (void*) kViewErrCouldNotCreate, nil);
	newton_try
	{
		view->Constructor(context, parent);
	}
	newton_catch_all
	{
		view->RemoveView();
		NextHandler(&_info);
	}
	end_try;
	return view;
}


/*------------------------------------------------------------------------------
	F i n d i n g   v i e w s
------------------------------------------------------------------------------*/

// ROM 0x0025f4c4 GetView__FRC6RefVar
// The view of a context (its viewCObject, found through the proto and
// parent chains), nil when none.
TView*
GetView(RefArg context)
{
	if (ISNIL(context))
		return nil;
	RefVar object(GetVariable(context, RSSYMviewcobject, nil, 0));
	if (ISNIL(object))
		return nil;
	return (TView*) RefToAddress(object);
}


// ROM 0x0025f524 GetFrontCommandKeyView__Fv
// NOT YET RECONSTRUCTED: the front-most visible child of the root view's
// command-key view (TextFlags).
static TView*
GetFrontCommandKeyView(void)
{
	return nil;
}


// ROM 0x0025f5a8 GetView__FRC6RefVarT1
// The view a name means from a context: nil the context's own view (via
// its preallocatedContext); a frame with a viewCObject its view, another
// frame the view whose data it is (SoupEQ, from the root down); the
// symbols 'viewFrontMost (the front-most application view),
// 'viewFrontMostApp (not counting floaters), 'viewFrontKey and
// 'viewFrontCommandKey (the caret view, else NOT YET: the root view).
TView*
GetView(RefArg context, RefArg name)
{
	if (gRootView == nil)
		return nil;
	if (!IsSymbol(name))
	{
		if (ISNIL(name))
		{
			RefVar ctx(context);
			RefVar preallocated;
			if (NOTNIL(ctx))
				preallocated = GetProtoVariable(ctx, RSSYMpreallocatedcontext, nil);
			if (NOTNIL(preallocated))
				ctx = GetVariable(ctx, preallocated, nil, 0);
			return GetView(ctx);
		}
		if (FrameHasSlotRef(name, RSSYMviewcobject))
			return GetView(name);
		if (SoupEQ(RefVar(gRootView->DataFrame()), name))
			return gRootView;
		if ((gRootView->fFlags & vVisible) == 0)
			return nil;
		TViewLoop loop(gRootView->fChildren);
		for (TView* child = loop.Next(); child != nil; child = loop.Next())
		{
			TView* found = child->FindView(name);
			if (found != nil)
				return found;
		}
		return nil;
	}
	if (EQRef(name, RSSYMviewfrontmost))
		return gRootView->FrontMost();
	if (EQRef(name, RSSYMviewfrontmostapp))
		return gRootView->FrontMostApp();
	if (EQRef(name, RSSYMviewfrontkey))
	{
		TView* view = gRootView->fCaretView;
		if (view == nil)
			view = GetFrontCommandKeyView();
		return view != nil ? view : gRootView;
	}
	if (EQRef(name, RSSYMviewfrontcommandkey))
	{
		TView* view = gRootView->fCaretView;
		if (view == nil)
			view = GetFrontCommandKeyView();
		return view != nil ? view : gRootView;
	}
	return nil;
}


// ROM 0x001efe1c FailGetView__FRC6RefVar
TView*
FailGetView(RefArg context)
{
	TView* view = GetView(context);
	if (view == nil)
		ThrowMsg((char*) "nil view");
	return view;
}


// ROM 0x001f0258 FailGetView__FRC6RefVarT1
TView*
FailGetView(RefArg context, RefArg name)
{
	TView* view = GetView(context, name);
	if (view == nil)
		ThrowMsg((char*) "nil view");
	return view;
}


// ROM 0x00262a98 ProtoEQ__FRC6RefVarT1
// Whether the frame is the other or in its proto chain.
Boolean
ProtoEQ(RefArg a, RefArg b)
{
	RefVar frame(a);
	while (NOTNIL(frame))
	{
		if (EQRef(frame, b))
			return true;
		frame = GetFrameSlotRef(frame, RSSYM_proto);
	}
	return false;
}


// ROM 0x002638e4 SoupEQ__FRC6RefVarT1
// The same soup entry (both have a _uniqueId and they match), else
// ProtoEQ.
Boolean
SoupEQ(RefArg a, RefArg b)
{
	RefVar idA(GetProtoVariable(a, RSSYM_uniqueid, nil));
	if (NOTNIL(idA))
	{
		RefVar idB(GetProtoVariable(b, RSSYM_uniqueid, nil));
		if (NOTNIL(idB))
			return EQRef(idA, idB);
	}
	return ProtoEQ(a, b);
}


// ROM 0x0025fe48 Exists__FP9TViewListRC6RefVar
// The view in the list whose context is protoed to the template.
TView*
Exists(TViewList* list, RefArg templ)
{
	TViewLoop loop(list);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
		if (ProtoEQ(view->fContext, templ))
			return view;
	return nil;
}


// ROM 0x0025f84c DataExists__FP9TViewListRC6RefVar
// The view in the list whose data frame is the frame.
TView*
DataExists(TViewList* list, RefArg data)
{
	TViewLoop loop(list);
	for (TView* view = loop.Next(); view != nil; view = loop.Next())
		if (EQRef(view->DataFrame(), data))
			return view;
	return nil;
}


/*------------------------------------------------------------------------------
	S e t u p
------------------------------------------------------------------------------*/

// the frames the ROM keeps as constants, made when no ROM is imported
void
InitViewPrototypes(void)
{
	if (ISNIL(Rcanonicalcontext))
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYM_parent, RefVar(NILREF));
		SetFrameSlot(frame, RSSYM_proto, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMviewcobject, RefVar(NILREF));
		Rcanonicalcontext = frame;
	}
	if (ISNIL(Rcanonicaldatacontext))
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYM_proto, RefVar(NILREF));
		SetFrameSlot(frame, RSSYM_parent, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMviewcobject, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMrealdata, RefVar(NILREF));
		Rcanonicaldatacontext = frame;
	}
	if (ISNIL(Rcanonicalrect))
	{
		RefVar frame(AllocateFrame());
		SetFrameSlot(frame, RSSYMleft, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMtop, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMright, RefVar(NILREF));
		SetFrameSlot(frame, RSSYMbottom, RefVar(NILREF));
		Rcanonicalrect = frame;
	}
	if (ISNIL(Rrootcontext))
		Rrootcontext = AllocateFrame();
	if (ISNIL(Rslotcachetable))
	{
		static const char* const kSlotNames[kSlotCacheCount] = {
			"viewQuitScript", "styles", "tabs", "realData", "text", "viewTransferMode", "viewFont",
			"viewOriginY", "viewOriginX", "viewJustify", "viewFlags", "viewDrawScript", "viewIdleScript",
			"viewSetupChildrenScript", "viewChildren", "buttonClickScript", "viewClickScript", "viewFormat",
			"viewBounds", "viewShowScript", "viewStrokeScript", "viewGestureScript", "viewHiliteScript",
			"allocateContext", "keyPressScript", "viewKeyUpScript", "viewKeyDownScript", "viewKeyRepeatScript",
			"stepAllocateContext", "viewChangedScript", "viewRawInkScript", "viewInkWordScript",
			"viewKeyStringScript", "viewCaretActivateScript" };
		RefVar table(MakeArray(kSlotCacheCount));
		for (long i = 0; i < kSlotCacheCount; i++)
			SetArraySlotRef(table, i, MakeSymbol((char*) kSlotNames[i]));
		Rslotcachetable = table;
	}
}


// ROM 0x001f3c40 InitScriptGlobals__Fv (the part that keeps the slot cache table)
// and the root view's making (the ROM's boot: the root template is the
// ROM's Rviewroot, whose viewSetupFormScript 0x00438a65 makes
// vars.displayParams and takes its viewBounds from the params'
// rootBounds).  The view system needs the object system, QuickDraw and a
// current port (the screen); the host's root template is {viewClass 75,
// viewFlags vVisible + vApplication, viewFormat vfFillWhite, the same
// setup form script as source, _proto the view methods} and its display
// params are the port's rectangle (the application area the whole of it).
void
InitViewSystem(void)
{
	InitViewPrototypes();
	if (gSlotCacheTable == nil)
		gSlotCacheTable = new RefStruct(Rslotcachetable);
	if (gRootView != nil)
		return;
	GrafPort* port = GetCurrentPort();
	if (ISNIL(GetFrameSlotRef(gVarFrame, RSSYMdisplayparams)))
	{
		RefVar params(AllocateFrame());
		SetFrameSlot(params, RefVar(Intern((char*) "rootBounds")), RefVar(ToObject(port->portRect)));	// (no RSSYM: the ROM interns it at boot)
		SetFrameSlot(params, RSSYMappareagloballeft, RefVar(MAKEINT(port->portRect.left)));
		SetFrameSlot(params, RSSYMappareaglobaltop, RefVar(MAKEINT(port->portRect.top)));
		SetFrameSlot(params, RSSYMappareawidth, RefVar(MAKEINT(port->portRect.right - port->portRect.left)));
		SetFrameSlot(params, RSSYMappareaheight, RefVar(MAKEINT(port->portRect.bottom - port->portRect.top)));
		SetFrameSlot(RefVar(gVarFrame), RSSYMdisplayparams, params);
	}
	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clRootView)));
	SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible | vApplication)));
	SetFrameSlot(templ, RSSYMviewformat, RefVar(MAKEINT(vfFillWhite)));
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(port->portRect)));
	// ROM 0x00438a65 (object) Rviewroot.viewSetupFormScript (the display params made already)
	SetFrameSlot(templ, RSSYMviewsetupformscript, RefVar(CompileScriptFunction("func() self.viewBounds := displayParams.rootBounds")));
	SetFrameSlot(templ, RSSYM_proto, RefVar(MakeViewMethods()));
	TRootView* root = new TRootView;
	gRootView = root;
	root->Constructor(templ);
}
