/*
	File:		newt/Notebook.cpp

	Contains:	TNotebook, TARMNotebook and the notifiers.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Notebook.h"
#include "CICCodec.h"
#include "InkFont.h"
#include "ScriptBoot.h"
#include "RootView.h"
#include "Recognizer.h"
#include "InkRecognizer.h"
#include "StrokeCentral.h"
#include "Ports.h"
#include "Regions.h"
#include "Screen.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Locale.h"
#include "NewtonExceptions.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtWorld.h"
#include "hal/host/HostTablet.h"
#include <string.h>

RgnHandle	gScreenRgn = nil;			// (the ROM's word at 0x0c103abc)
RgnHandle	gWideRgn = nil;				// (0x0c103ac0)
static PatternHandle	gNotebookPattern = nil;	// (0x0c103ac4: a pattern the notebook disposes with the regions; NOT YET: what makes it)

void	InitViewSystem(RefArg rootTemplate);	// views/BuildView.cpp: the root view, from this template
Ref		MakeRootTemplate(void);			// views/BuildView.cpp: the ROM's Rviewroot, or the host's stand-in


/*------------------------------------------------------------------------------
	T N o t e b o o k
------------------------------------------------------------------------------*/

long
TNotebook::ClassID(void) const
{
	return clNotebook;
}


// ROM 0x00145fb4 DerivedFrom__9TNotebookCFl
Boolean
TNotebook::DerivedFrom(long id) const
{
	return id == clNotebook || TApplication::DerivedFrom(id);
}


// ROM 0x001467f8 Constructor__9TNotebookFv
// The application constructed, the root view made from the viewRoot
// template (gRootView), and the librarian with its library soup (from
// the root's copperfield: NOT YET RECONSTRUCTED - TLibrarian).
//
// The template is the ROM's own Rviewroot (MakeRootTemplate), 263 slots:
// the methods the applications send to the root - Notify, BlessApp,
// CloseSlips, GotoSleep - its setup scripts, and the viewChildren naming
// the fifty-nine views a Newton boots with.  Only the visible ones are
// built; the applications are opened later, and most of them have no
// vVisible until they are.  Without the ROM's objects MakeRootTemplate
// answers the host's stand-in instead.
void
TNotebook::Constructor(void)
{
	TApplication::Constructor();
	InitViewSystem(RefVar(MakeRootTemplate()));
}


// ROM 0x00146b28 InitToolbox__9TNotebookFv
// The toolbox: the offscreen bitmaps and the port, the script globals,
// the inker, the screen orientation from the preference (else the
// screen's own), the splash screen and the boot sound, the print
// drivers, the font loader, the international utilities, the recognition
// system at level 2, the init scripts, DarkStar.
// NOT YET RECONSTRUCTED: InitScriptGlobals (vars from varsMapStarter, the
// classes, the funky functions, bootInitNSGlobals), DrawSplashScreen,
// FPlaySoundIrregardless(bootSound), InitPrintDrivers, InitFontLoader,
// InitInternationalUtils, RunInitScripts, InitDarkStar; the recognition
// system starts at level 1 (the clicks) on the host.
void
TNotebook::InitToolbox(void)
{
	TApplication::InitToolbox();
	InitOffscreenBitmaps();
	InitScriptGlobals();
	InitInker();
	RefVar orientation(GetPreference(RSSYMscreenorientation));
	if (ISNIL(orientation))
	{
		long current;
		GetGrafInfo(kGrafInfoOrientation, &current);
		SetOrientation(current);
	}
	else
		SetOrientation(RINT(orientation));
	// the ROM starts it at 2 - clicks and strokes, and the shapes and
	// words above them.  The shape and word recognisers themselves are
	// NOT YET, so level 2 here means only that the dictionaries are built
	// and the word half of the system is meant to be on.
	// (DEVIATION: the ROM's TRecognitionManager::Init starts the stroke
	// compression itself.  The ink area sits above the recogniser here -
	// it reaches the strokes through it - so the two are started from
	// outside instead, and InitializeInkCodecs registers the CIC codec
	// with the seam the ROM does not have.)
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	// (DEVIATION: the ROM's own handwriting engine is the CIC
	// library, which is NOT YET.  The reconstruction's ink-only
	// engine stands in its place, so that writing becomes ink rather
	// than being dropped: recognition/InkRecognizer.h.)
	RegisterInkOnlyRecognizer();
	gRecognition.Init(2);
	// (NOT YET: on the Newton a script chooses which of the two word
	//  recognisers is in use, with UseWRec; with one engine here the
	//  host puts it in use itself, so that writing is read - or, as it
	//  is, left as ink - rather than dropped.)
	SetWordRecognizer(kWRecDomainType);
	RunInitScripts();
	gStrokeWorld.Init();
}


// ROM 0x00146c50 InitOffscreenBitmaps__9TNotebookFv
// The port of the task's globals copied into gGrafPort (the ROM: the
// fork's port, 0x54 bytes; the host has one port), the screen region
// made of the screen's rectangle and the wide-open region copied.
// ==> whether the copy could be made.
Boolean
TNotebook::InitOffscreenBitmaps(void)
{
	GrafPort* port = GetCurrentPort();
	if (port != nil && port != &gGrafPort)
		gGrafPort = *port;
	gScreenRgn = NewRgn();
	gWideRgn = NewRgn();
	Rect screen;
	SetRect(&screen, 0, 0, (short) screenWidth, (short) screenHeight);
	RectRgn(gScreenRgn, &screen);
	CopyRgn(wideHandle, gWideRgn);
	return true;
}


// ROM 0x00146ca8 InitInker__9TNotebookFv
// The inker (a TInker, 'inkr, over the Newt port) started as a fork.
// NOT YET RECONSTRUCTED: TInker.  Host: the tablet's stand-in - a task
// reading the tablet buffer into the stroke queue every tick when the OS
// runs (waking the world through the Newt port when a stroke changes, as
// the ROM's inker does), the wait hook otherwise (hal/host/HostTablet.h).
void
TNotebook::InitInker(void)
{
	HostTabletInit();
	HostInkerSetNewtPort(gNewtPort);
	HostInkerStart();
}


// ROM 0x0014602c DrawSplashScreen__9TNotebookFv
// NOT YET RECONSTRUCTED: the splash screen (the 'splash picture and the
// version string drawn in the screen's middle).
void
TNotebook::DrawSplashScreen(void)
{ }


// ROM 0x00146410 Run__9TNotebookFv
// Up to ten idle passes, the root view updated after each, while an idle
// is due.
void
TNotebook::Run(void)
{
	for (long pass = 10; ; )
	{
		Idle();
		if (gRootView->NeedsUpdate())
			gRootView->Update(nil);
		if (--pass == 0)
			break;
		if (!NeedsIdle())
			break;
	}
}


// ROM 0x00146478 Idle__9TNotebookFv
// The application idled, then the recognition system and the views; the
// next idle time is the earlier of the views' and the recogniser's.
void
TNotebook::Idle(void)
{
	TApplication::Idle();
	gRecognition.Idle();
	TTime next = gRootView->IdleViews();
	UpdateNextIdleTime(next);
	UpdateNextIdleTime(gRecognition.NextIdle());
}


// ROM 0x001464d0 NeedsIdle__9TNotebookFv
// Whether an idle time is set and has passed.
Boolean
TNotebook::NeedsIdle(void)
{
	static const Int64 zero = { 0, 0 };
	if (CompCompare(&fNextIdleTime.time, &zero) != 0)
	{
		TTime now = GetGlobalTime();
		if (CompCompare(&fNextIdleTime.time, &now.time) < 0)
			return true;
	}
	return false;
}


// ROM 0x00146b1c Quit__9TNotebookFv
// The screen regions and the pattern let go.
void
TNotebook::Quit(void)
{
	if (gScreenRgn != nil)
		DisposeRgn(gScreenRgn);
	if (gWideRgn != nil)
		DisposeRgn(gWideRgn);
	gScreenRgn = gWideRgn = nil;
	if (gNotebookPattern != nil)
		DisposePattern(gNotebookPattern);
	gNotebookPattern = nil;
}


/*------------------------------------------------------------------------------
	T A R M N o t e b o o k
------------------------------------------------------------------------------*/

// ROM 0x00145f78 ClassID__12TARMNotebookCFv
long
TARMNotebook::ClassID(void) const
{
	return clARMNotebook;
}


// ROM 0x00145f80 DerivedFrom__12TARMNotebookCFl
Boolean
TARMNotebook::DerivedFrom(long id) const
{
	return id == clARMNotebook || TNotebook::DerivedFrom(id);
}


/*------------------------------------------------------------------------------
	T h e   n o t i f i e r s
------------------------------------------------------------------------------*/

// ROM 0x00146528 SetActionDescription__Fl
// vars.actionDescription: the error string's code for what is going on.
void
SetActionDescription(long errorCode)
{
	SetFrameSlot(RefVar(gVarFrame), RSSYMactiondescription, RefVar(MAKEINT(errorCode)));
}


// ROM 0x00146584 Notify__FRC6RefVar
// The root view's notify method sent the arguments (the ROM: DoSend after
// FindImplementor - a throw when the root has none).  Host: nothing when
// the root has no notify (the ROM's root template has one).
Ref
Notify(RefArg args)
{
	long defined;
	return DoMessageIfDefined(gRootView->fContext, RSSYMnotify, args, &defined);
}


// ROM 0x00146648 ActionErrorNotify__FlT1
// actionNotify(kind, errorCode, nil): an error in what was going on.
void
ActionErrorNotify(long errorCode, long kind)
{
	RefVar args(MakeArray(3));
	SetArraySlotRef(args, 0, MAKEINT(kind));
	SetArraySlotRef(args, 1, MAKEINT(errorCode));
	long defined;
	DoMessageIfDefined(gRootView->fContext, RSSYMactionnotify, args, &defined);
	if (!defined)
		printf("action error %ld (%ld)\n", errorCode, kind);
}


// ROM 0x001466ec GetExceptionErr__FP9Exception
// The error code an exception carries: out of memory's data (kError_No_Memory
// when none), a frames exception's errorCode slot, a message exception's
// data; else kError_No_Memory... no: -8007 (an unknown exception).
long
GetExceptionErr(Exception* exception)
{
	long err = -8007;
	if (Subexception(exception->name, exOutOfMemory))
	{
		err = (long) (Long) exception->data;
		if (err == 0)
			err = kError_No_Memory;
	}
	else if (Subexception(exception->name, "type.ref"))
	{
		RefVar data(**(Ref**) exception->data);
		if (IsFrame(data))
		{
			RefVar code(GetFrameSlotRef(data, RSSYMerrorcode));
			if (ISINT(code))
				err = RINT(code);
		}
	}
	else if (Subexception(exception->name, "evt.ex.msg"))
		err = (long) (Long) exception->data;
	return err;
}


// ROM 0x001468d4 ExceptionNotify__FP9Exception
// An exception shown to the user: the memory is checked (an out-of-memory
// exception thrown when 1K cannot be had), vars.lastEx (the name),
// lastExMessage (a message exception's text, or a frames exception's
// message slot), lastExError (its code) and lastExData (the frame) set,
// the port made the default one and the screen lock released, and the
// error shown (ActionErrorNotify, kind 3).
void
ExceptionNotify(Exception* exception)
{
	Ptr probe = NewPtr(0x400);
	if (probe == nil)
		OutOfMemory();
	DisposPtr(probe);
	long err = GetExceptionErr(exception);
	RefVar message;
	RefVar data;
	if (Subexception(exception->name, "evt.ex.msg"))
		message = MakeString((const char*) exception->data);
	else if (Subexception(exception->name, "type.ref"))
	{
		data = **(Ref**) exception->data;
		if (IsFrame(data))
			message = GetFrameSlotRef(data, RSSYMmessage);
	}
	RefVar vars(gVarFrame);
	SetFrameSlot(vars, RSSYMlastex, RefVar(MakeString(exception->name)));
	SetFrameSlot(vars, RSSYMlastexmessage, message);
	SetFrameSlot(vars, RSSYMlastexerror, RefVar(MAKEINT(err)));
	SetFrameSlot(vars, RSSYMlastexdata, data);
	SetPort(&gGrafPort);
	ReleaseScreenLock();
	ActionErrorNotify(err, 3);
}
