/*
	File:		views/BuildView.cpp

	Contains:	Making views from templates: the context (BuildContext), the
				C++ object of the template's class (BuildView), finding a
				view from a context (GetView), the bounds frames (ToObject,
				FromObject), and the host's setup of the view system (the
				slot cache symbols, the prototype frames, the root view).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RootView.h"
#include "TextView.h"
#include "PictureView.h"
#include "ParagraphView.h"
#include "ContainerView.h"
#include "EditView.h"
#include "Application.h"
#include "GaugeView.h"
#include "PolygonView.h"
#include "KeyboardView.h"
#include "MonthView.h"
#include "ListView.h"
#include "MeetingView.h"
#include "PrintView.h"
#include "SliderView.h"
#include "ClipboardView.h"
#include "PickView.h"
#include "Rects.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "NewtonExceptions.h"
#include "TXView.h"
#include "Outline.h"
#include "RemoteView.h"


// ROM 0x001ef4c4 BadWickedNaughtyNoot__Fl
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

// ROM 0x0025e4d0 GetCacheContext__FRC6RefVar
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


// ROM 0x0025e56c BuildContext__5TViewFRC6RefVarUc
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


// ROM 0x0025e950 BuildView__FP5TViewRC6RefVar
// The C++ object for the context's viewClass, constructed under the
// parent; a Throw in the Constructor removes the view again, and a class
// the ROM does not know is -8501 (kViewErrCouldNotCreate).  Every class
// the ROM makes is here but the math views (TMathExpView, TMathOpView,
// TMathLineView), which are NOT YET RECONSTRUCTED and get a plain TView.
TView*
BuildView(TView* parent, RefArg context)
{
	long viewClass = RINT(GetProtoVariable(context, RSSYMviewclass, nil)) & 0xffff;
	TView* view = nil;
	switch (viewClass)
	{
	case clView:
	case clMathExpView:
	case clMathOpView:
	case clMathLineView:
	case clPrintView - 1:
		view = new TView;
		break;
	case clPrintView:
		view = new TPrintView;
		break;
	case clRemoteView - 1: case clRemoteView:
		view = new TRemoteView;
		break;
	case clOutline - 3: case clOutline - 2: case clOutline - 1: case clOutline:
		view = new TOutline;
		break;
	case clHelpOutline - 1: case clHelpOutline:
		view = new THelpOutline;
		break;
	case clTXView:
		view = new TXView;
		break;
	case clKeyboardView:
		view = new TKeyboardView;
		break;
	case clListView:
		view = new TListView;
		break;
	case clMeetingView:
		view = new TMeetingView;
		break;
	case clSliderView:
		view = new TSliderView;
		break;
	case clMonthView:
		view = new TMonthView;
		break;
	case clClipboard - 1: case clClipboard:
		view = new TClipboard;
		break;
	case clTextView - 1: case clTextView:
		view = new TTextView;
		break;
	case clPictureView - 1: case clPictureView:
		view = new TPictureView;
		break;
	case clParagraphView:
		view = new TParagraphView;
		break;
	case clDataView:
		view = new TDataView;
		break;
	case clPolygonView:
		view = new TPolygonView;
		break;
	case clContainerView:
		view = new TContainerView;
		break;
	case clEditView:
		view = new TEditView;
		break;
	case clGaugeView:
		view = new TGaugeView;
		break;
	case clPickView - 2: case clPickView - 1: case clPickView:
		view = new TPickView;
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

// ROM 0x002613fc GetView__FRC6RefVar
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


// ROM 0x0026145c GetFrontCommandKeyView__Fv
// The front-most visible child of the root that takes command keys (text
// flag 0x4000); nil for none.
TView*
GetFrontCommandKeyView(void)
{
	TViewList* children = gRootView->fChildren;
	for (long index = children->Count() - 1; index >= 0; index--)
	{
		TView* child = children->At(index);
		if ((child->fFlags & vVisible) != 0 && (child->TextFlags() & 0x4000) != 0)
			return child;
	}
	return nil;
}


// ROM 0x002614e0 GetView__FRC6RefVarT1
// The view a name means from a context: nil the context's own view (via
// its preallocatedContext); a frame with a viewCObject its view, another
// frame the view whose data it is (SoupEQ, from the root down); the
// symbols 'viewFrontMost (the front-most application view),
// 'viewFrontMostApp (not counting floaters), 'viewFrontKey (the caret
// view; else the front command-key view when it takes keys - text flag
// 0x8000 - or the view it would restore the caret to does; else the next
// key view of the front-most view, then of the root; else the root) and
// 'viewFrontCommandKey (the caret view, else the front command-key view,
// else the root).
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
		{
			TView* front = GetFrontCommandKeyView();
			if (front != nil)
			{
				if ((front->TextFlags() & 0x8000) == 0)
				{
					front = gRootView->FindRestorableKeyView(front, nil);
					if (front != nil && (front->TextFlags() & 0x8000) == 0)
						front = nil;
				}
				view = front;
			}
			if (view == nil)
			{
				TView* frontMost = gRootView->FrontMost();
				if (frontMost != nil)
					view = frontMost->NextKeyView(nil, 0, 0);
			}
		}
		if (view == nil)
			view = gRootView->NextKeyView(nil, 0, 0);
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


// ROM 0x001eda04 FailGetView__FRC6RefVar
TView*
FailGetView(RefArg context)
{
	TView* view = GetView(context);
	if (view == nil)
		ThrowMsg((char*) "nil view");
	return view;
}


// ROM 0x001ede40 FailGetView__FRC6RefVarT1
TView*
FailGetView(RefArg context, RefArg name)
{
	TView* view = GetView(context, name);
	if (view == nil)
		ThrowMsg((char*) "nil view");
	return view;
}


// ROM 0x002649d0 ProtoEQ__FRC6RefVarT1
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


// ROM 0x0026581c SoupEQ__FRC6RefVarT1
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


// ROM 0x00261d80 Exists__FP9TViewListRC6RefVar
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


// ROM 0x00261784 DataExists__FP9TViewListRC6RefVar
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


// ROM 0x001f1828 InitScriptGlobals__Fv (the part that keeps the slot cache table)
// and the root view's making (the ROM's boot: the root template is the
// ROM's Rviewroot, whose viewSetupFormScript 0x00438a65 makes
// vars.displayParams and takes its viewBounds from the params'
// rootBounds).  The view system needs the object system, QuickDraw and a
// current port (the screen); the host's root template is {viewClass 75,
// viewFlags vVisible + vApplication, viewFormat vfFillWhite, the same
// setup form script as source, _proto the view methods} and its display
// params are the port's rectangle (the application area the whole of it);
// the application (gApplication, a plain TApplication) is made first.
// The root's template is the ROM's own Rviewroot, handed over as it lies:
// 263 slots - the methods every application sends to the root (Notify,
// BlessApp, _BlessedOpen, CloseSlips, GotoSleep, ...), its setup scripts,
// and the fifty-nine children that are the system's views, none of them
// visible, each opened when something asks for it.  Without the ROM's
// objects there is the host's stand-in instead: a plain root that fills
// white and takes its bounds from the display params, with the C view
// methods as its _proto.
Ref
MakeRootTemplate(void)
{
	GrafPort* port = GetCurrentPort();
	if (NOTNIL(Rviewroot))
		return Rviewroot;			// the ROM's own template, fifty-nine children and all
	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clRootView)));
	SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible | vApplication)));
	SetFrameSlot(templ, RSSYMviewformat, RefVar(MAKEINT(vfFillWhite)));
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(port->portRect)));
	// ROM 0x0041a52d (object) Rviewroot.viewSetupFormScript (the display params made already)
	SetFrameSlot(templ, RSSYMviewsetupformscript, RefVar(CompileScriptFunction("func() self.viewBounds := displayParams.rootBounds")));
	SetFrameSlot(templ, RSSYM_proto, RefVar(MakeViewMethods()));
	return templ;
}


void
InitViewSystem(void)
{
	InitViewSystem(RefVar(NILREF));
}


void
InitViewSystem(RefArg rootTemplate)
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
	RefVar templ(rootTemplate);
	if (ISNIL(templ))
	{
		templ = AllocateFrame();
		SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clRootView)));
		SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible | vApplication)));
		SetFrameSlot(templ, RSSYMviewformat, RefVar(MAKEINT(vfFillWhite)));
		SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(port->portRect)));
		// ROM 0x0041a52d (object) Rviewroot.viewSetupFormScript (the display params made already)
		SetFrameSlot(templ, RSSYMviewsetupformscript, RefVar(CompileScriptFunction("func() self.viewBounds := displayParams.rootBounds")));
		SetFrameSlot(templ, RSSYM_proto, RefVar(MakeViewMethods()));
	}
	if (gApplication == nil)
	{
		gApplication = new TApplication;
		gApplication->Constructor();
	}
	TRootView* root = new TRootView;
	gRootView = root;
	root->Constructor(templ);
	// DEVIATION: the root's viewChildren is one of the ROM's own objects
	// and so read-only, and FAddView appends to whatever the proto chain
	// answers - so a script that adds a view to the root writes a read-only
	// object and gets kNSErrObjectReadOnly.  The ROM never adds to the root
	// that way (an application is installed by putting its context in a
	// slot of the root, InstallFormPart, and opened from there), but the
	// host's demos and tests do, so the root's own context is given a copy
	// of the array to append to.  The template itself is left alone.
	RefVar children(root->GetProto(RSSYMviewchildren));
	if (NOTNIL(children))
		root->SetContextSlot(RSSYMviewchildren, RefVar(Clone(children)));
}
