/*
	File:		newt/Notebook.cpp

	Contains:	TNotebook, TARMNotebook and the notifiers.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ParseUtter.h"
#include "Dates.h"
#include "Notebook.h"
#include "Inker.h"
#include "KernelGlobals.h"
#include "print/Printer.h"
#include "CICCodec.h"
#include "InkFont.h"
#include "FramePartHandler.h"		// InitFontLoader
#include "ScriptBoot.h"
#include "RootView.h"
#include "Recognizer.h"
#include "InkRecognizer.h"
#include "RosRecognizer.h"
#include "WordEngines.h"
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
#include "Librarian.h"
#include "Draw.h"
#include "SplashScreen.h"
#include "Text.h"
#include "Fonts.h"
#include "Pictures.h"
#include "SoundSettings.h"
#include "NewtonGestalt.h"
#include "Unicode.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
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
// the root's copperfield: books/Librarian.h).
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
	InitLibrarian();
}


// the splash for the root view to draw (views/RootView.h: the views are
// below the application here)
static void
DrawNotebookSplashScreen(void)
{
	((TNotebook*) gApplication)->DrawSplashScreen();
}


// The display parameters of a screen the ROM does not know.
//
// DEVIATION (host): the ROM describes its display in GetAllRawDisplayParams
// (a NewtonScript built-in, ROM 0x004186cd), which answers the global
// AllRawDisplayParams when there is one and otherwise its own literal - the
// MessagePad's 320 x 480, a button bar 46 pixels thick at the bottom in
// portrait and at the right in landscape.  CreateDisplayParams makes
// vars.displayParams (the root's bounds, the application area, the button
// bar) out of it, and everything that lays the screen out reads that.  The
// global is the hook a machine with another screen was to fill in; a host
// display of any other size fills it in here, the four orientations shaped
// as the ROM's are (the display's own width and height in 0 and 2, the two
// swapped in 1 and 3) and the button bar as thick as the ROM's.
static void
DefineHostDisplayParams(void)
{
	PixelMap screen;
	GetGrafInfo(kGrafInfoScreenPixelMap, &screen);
	long orientation = 0;
	GetGrafInfo(kGrafInfoOrientation, &orientation);
	long width = screen.bounds.right - screen.bounds.left;
	long height = screen.bounds.bottom - screen.bounds.top;
	if (orientation == 1 || orientation == 3)
	{
		long w = width;
		width = height;
		height = w;
	}
	if (width == 320 && height == 480)
		return;					// the ROM's own table describes it
	long depth = screen.pixMapFlags & 0xff;
	RefVar make(CompileScriptFunction(
		"func(w, h, depth) begin "
		"local thick := 46; "
		"local land := {pixelDepth: depth, orientation: 1, scrTop: 0, scrLeft: 0, scrWidth: h, scrHeight: w, "
		"appAreaGlobalTop: 0, appAreaGlobalLeft: 0, appAreaTop: 0, appAreaLeft: 0, appAreaWidth: h - thick, appAreaHeight: w, "
		"rootBounds: {left: 0, top: 0, right: h, bottom: w}, buttonBarBounds: {left: h - thick, top: 0, right: h, bottom: w}, "
		"buttonBarPosition: 'right, buttonBarControlsPosition: 'bottom, bellyButtonPosition: 'inside, "
		"buttonBarVThickness: thick, buttonBarHThickness: thick, appAreaBounds: {left: 0, top: 0, right: h - thick, bottom: w}}; "
		"local port := {_proto: land, orientation: 0, scrWidth: w, scrHeight: h, appAreaWidth: w, appAreaHeight: h - thick, "
		"rootBounds: {left: 0, top: 0, right: w, bottom: h}, buttonBarPosition: 'bottom, buttonBarControlsPosition: 'right, "
		"buttonBarBounds: {left: 0, top: h - thick, right: w, bottom: h}, appAreaBounds: {left: 0, top: 0, right: w, bottom: h - thick}}; "
		"[port, land, {_proto: port, orientation: 2, appAreaBounds: port.appAreaBounds}, "
		"{_proto: land, orientation: 3, appAreaBounds: land.appAreaBounds}] "
		"end"));
	RefVar args(MakeArray(3));
	SetArraySlot(args, 0, RefVar(MAKEINT(width)));
	SetArraySlot(args, 1, RefVar(MAKEINT(height)));
	SetArraySlot(args, 2, RefVar(MAKEINT(depth)));
	RefVar params(DoBlock(make, args));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "AllRawDisplayParams")), params);
}


// ROM 0x00146b28 InitToolbox__9TNotebookFv
// The toolbox: the offscreen bitmaps and the port, the script globals,
// the inker, the screen orientation from the preference (else the
// screen's own), the splash screen and the boot sound, the print
// drivers, the font loader, the international utilities, the recognition
// system at level 2, the init scripts, DarkStar.
// (The screen orientation comes before the splash so that it is drawn
// the right way round.)
void
TNotebook::InitToolbox(void)
{
	TApplication::InitToolbox();
	InitOffscreenBitmaps();
	InitScriptGlobals();
	DefineHostDisplayParams();		// DEVIATION (host): a display of another size
	InstallFix2010();		// DEVIATION: the year-2010 fix (intl/Dates.h)
	InitInker();
	RefVar orientation(GetPreference(RSSYMscreenorientation));
	long current;
	GetGrafInfo(kGrafInfoOrientation, &current);
	if (ISNIL(orientation))
		SetOrientation(current);
	else if ((current == 1 || current == 3) && (RINT(orientation) == 0 || RINT(orientation) == 2))
		// DEVIATION (host): a display the host made wider than it is tall
		// starts the way round it was made (hal/host/HostScreen.cpp's
		// Configure: landscape), whatever the preference says - the ROM's
		// default is portrait, the MessagePad's own way round
		SetOrientation(current);
	else
		SetOrientation(RINT(orientation));
	gDrawSplashScreenProc = DrawNotebookSplashScreen;
	DrawSplashScreen();
	FPlaySoundIrregardless(RefVar(), RefVar(Rbootsound));
	InitPrintDrivers();
	InitFontLoader();
	InitInternationalUtils();
	// the ROM starts it at 2 - clicks and strokes, and the shapes and
	// words above them.
	// (DEVIATION: the ROM's TRecognitionManager::Init starts the stroke
	// compression itself.  The ink area sits above the recogniser here -
	// it reaches the strokes through it - so the two are started from
	// outside instead, and InitializeInkCodecs registers the CIC codec
	// with the seam the ROM does not have.)
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	// the ROM's own handwriting engine, Rosetta, reads the writing
	// (recognition/RosRecognizer.h); what it cannot read is kept as
	// ink.  (The letter set chooses between it and the cursive
	// recogniser, ParaGraph's: ReadCursiveOptions, under gRecognition.Init.)
	RegisterRosettaWRec();
	// ... and the host's own engines beside it, which the Handwriting
	// Recognition slip offers (recognition/WordEngines.h)
	RegisterHostWordEngines();
	gRecognition.Init(2);		// (which puts the letter set's word recogniser in use: ReadCursiveOptions)
	RunInitScripts();
	InitDarkStar(RefVar(), RefVar());
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
// The inker (recognition/Inker.h, the 'inkr world) started, waking the
// Newt port as strokes change.  Host: the tablet's host side made ready
// first - the wait hook for the tests without the OS, the host's tablet
// driver registered for the inker's TabInitialize to find
// (hal/host/HostTablet.h).
void
TNotebook::InitInker(void)
{
	HostTabletInit();
	if (gOSIsRunning)		// (host: a test of the application without the OS has no tasks)
		StartInker(gNewtPort);
}


// ROM 0x0014602c DrawSplashScreen__9TNotebookFv
// The boot's splash: the screen painted black, the maker's picture
// (DrawSplashGraphic) or else the ROM's bootLogoBitmap centred above the
// bottom 140 lines, then in white System 9 bold, centred across the
// screen: up to three lines of the maker's text 30 above that, and the
// four lines "Newton <version>", the copyright, "Apple Computer, Inc." and
// "All rights reserved.", ten apart.  TRootView::RealDraw draws it too,
// until the system is up.
void
TNotebook::DrawSplashScreen(void)
{
	static const char* const kSplashLines[4] =		// (the ROM: a table of char* at 0x0037413c)
	{
		"Newton ",
		"\xA9" "1993-1997",				// (Mac Roman: the copyright sign)
		"Apple Computer, Inc.",
		"All rights reserved."
	};
	Rect box;
	SetRect(&box, 0, 0, (short) screenWidth, (short) screenHeight);
	PaintRect(&box);
	short bottom = (short) (screenHeight - 140);
	SetRect(&box, 0, 0, (short) screenWidth, bottom);
	UChar drawn;
	TSplashScreenInfo* info = DrawSplashGraphic(&drawn, box);
	if (!drawn)
		DrawPicture(RefVar(Rbootlogobitmap), box, 6, 0);

	TextOptions options;
	memset(&options, 0, sizeof(options));
	options.fAlignment = 0x8000;					// centred
	options.fWidth = ToFixed(screenWidth);
	options.fTransferMode = 3;						// srcBic: white on the black
	StyleRecord style;
	StyleRecord* styles = &style;
	CreateTextStyleRecord(RefVar(Rfontsystem9bold), &style);
	FPoint where;
	where.x = 0;
	short textTop = (short) (bottom - 30);
	UniChar text[256];
	if (info != nil)
	{
		// the maker's lines, one after another in the buffer, an empty
		// one ending them
		if (info->GetText(text) != 0)
		{
			UniChar* line = text;
			for (long i = 0; i < 3; i++)
			{
				if (i > 0)
				{
					line += Ustrlen(line) + 1;
					if (*line == 0)
						break;
				}
				where.y = ToFixed(textTop + i * 10);
				DrawTextOnce(line, Ustrlen(line), &styles, nil, where, &options, nil);
			}
		}
		info->Delete();
	}
	ConvertToUnicode(kSplashLines[0], text, kMacRomanEncoding, 0x7fffffff);
	TUGestalt gestalt;
	TGestaltSystemInfo system;
	if (gestalt.Gestalt(kGestalt_SystemInfo, &system, sizeof(system)) == noErr)
		VersionString(&system, text + Ustrlen(text));
	for (long i = 1; i <= 4; i++)
	{
		where.y = ToFixed(bottom + i * 10);
		DrawTextOnce(text, Ustrlen(text), &styles, nil, where, &options, nil);
		if (i < 4)
			ConvertToUnicode(kSplashLines[i], text, kMacRomanEncoding, 0x7fffffff);
	}
	DisposeStyleRecord(&style);
}



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
