/*
	File:		host/HostViews.cpp

	Contains:	The view system started on the host over the host display.
*/

#include "HostViews.h"
#include <time.h>
#include "HostStores.h"
#include "HostNatives.h"
#include "HostScreen.h"
#include "Screen.h"
#include "Rects.h"
#include "Ports.h"
#include "Fonts.h"
#include "Text.h"
#include "RootView.h"
#include "Keyboard.h"
#include "Commands.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "Recognizer.h"
#include "StrokeCentral.h"
#include "UnitPublic.h"
#include "CardInfo.h"
#include "HostTablet.h"
#include "hal/host/Host.h"
#include "ROMImport.h"
#include <string.h>

static THostScreenDriver*	gHostDisplay = nil;


// ScreenSnapshot(path): the display written to the file - a PBM for a
// path ending in .pbm, else a PGM; ==> whether it could be
static Ref
FScreenSnapshot(RefArg /*rcvr*/, RefArg path)
{
	if (gHostDisplay == nil || !IsString(path))
		return NILREF;
	char name[1024];
	UniChar* text = (UniChar*) BinaryData(path);
	long length = (Length(path) / 2) - 1;
	if (length >= (long) sizeof(name))
		length = sizeof(name) - 1;
	for (long i = 0; i < length; i++)
		name[i] = (char) text[i];
	name[length] = 0;
	Boolean pbm = length > 4 && strcmp(name + length - 4, ".pbm") == 0;
	UpdateHardwareScreen();
	return MAKEBOOLEAN(pbm ? gHostDisplay->WritePBM(name) : gHostDisplay->WritePGM(name));
}


static Ref
FScreenWidth(RefArg /*rcvr*/)
{
	return MAKEINT(screenWidth);
}


static Ref
FScreenHeight(RefArg /*rcvr*/)
{
	return MAKEINT(screenHeight);
}


// KeyEvent(keyCode, isDown): a key of the hardware keyboard, as the
// keyboard tool would send it (the keyboard becomes connected)
static Ref
FKeyEvent(RefArg /*rcvr*/, RefArg keyCode, RefArg isDown)
{
	KeyboardEvent event(NOTNIL(isDown) ? aeKeyDown : aeKeyUp, RINT(keyCode));
	HandleKeyEvent(&event);
	return NILREF;
}


// KeyboardConnect(connected): a keyboard plugged in or pulled out
static Ref
FKeyboardConnect(RefArg /*rcvr*/, RefArg connected)
{
	KeyboardEvent event(aeKeyboardConnected, NOTNIL(connected) ? 1 : 0);
	HandleKeyEvent(&event);
	return NILREF;
}


// PenDown(x, y), PenMove(x, y), PenUp(): the pen on the tablet (queued,
// a record per tick of the waits the views' tracking loops make);
// IdleStrokes(): the stroke world run, as the application's idle would -
// the queued pen records go in as it waits, the clicks and taps reach
// the views under them
static Ref
FPenDown(RefArg /*rcvr*/, RefArg x, RefArg y)
{
	HostTabletQueuePenDown(RINT(x), RINT(y), 0);
	return NILREF;
}


static Ref
FPenMove(RefArg /*rcvr*/, RefArg x, RefArg y)
{
	HostTabletQueuePenMove(RINT(x), RINT(y));
	return NILREF;
}


static Ref
FPenUp(RefArg /*rcvr*/)
{
	HostTabletQueuePenUp(0);
	return NILREF;
}


static Ref
FIdleStrokes(RefArg /*rcvr*/)
{
	while (HostTabletQueued() > 0)
	{
		HostTabletPump();
		IdleStrokes();
	}
	IdleStrokes();
	return NILREF;
}


void
HostRegisterViewFunctions(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyEvent")), RefVar(MakeCFunction((void*) FKeyEvent, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyboardConnect")), RefVar(MakeCFunction((void*) FKeyboardConnect, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenSnapshot")), RefVar(MakeCFunction((void*) FScreenSnapshot, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenWidth")), RefVar(MakeCFunction((void*) FScreenWidth, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenHeight")), RefVar(MakeCFunction((void*) FScreenHeight, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenDown")), RefVar(MakeCFunction((void*) FPenDown, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenMove")), RefVar(MakeCFunction((void*) FPenMove, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenUp")), RefVar(MakeCFunction((void*) FPenUp, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "IdleStrokes")), RefVar(MakeCFunction((void*) FIdleStrokes, 0, nil)));
}


// The display made the screen: QuickDraw started, the host screen driver
// made and configured, InitScreen.
THostScreenDriver*
HostStartDisplay(long width, long height, long depth)
{
	InitGraf();
	gHostDisplay = new THostScreenDriver;
	gHostDisplay->New();
	gHostDisplay->Configure(width, height, depth, 100);
	InitScreen(gHostDisplay);
	return gHostDisplay;
}


// What the ROM's boot puts in the globals for the views and the fonts:
// the fonts (vars.fonts: the ROM font list's families by their symbols,
// as the ROM's globals template has them), an empty userConfiguration
// (with the userPenSize the ink is let out by), vars.international from
// the globals template (the locale and the keyboard mapping),
// vars.displayParams (the application area: the screen), the text, view,
// unit and host functions.
void
HostInitViewToolbox(void)
{
	InitFonts();
	RegisterTextNatives();
	RegisterViewNatives();
	InstallHostNatives();
	RefVar vars(gVarFrame);
	SetFrameSlot(vars, RSSYMvars, vars);
	SetFrameSlot(vars, RSSYMfunctions, RefVar(gFunctionFrame));
	if (ISNIL(GetFrameSlotRef(vars, RSSYMfonts)))
	{
		RefVar fonts(AllocateFrame());
		RefVar list(Rromfontlist);
		for (long i = 0; NOTNIL(list) && i < Length(list); i++)
			SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
		SetFrameSlot(vars, RSSYMfonts, fonts);
	}
	if (ISNIL(GetFrameSlotRef(vars, RSSYMuserconfiguration)))
		SetFrameSlot(vars, RSSYMuserconfiguration, RefVar(AllocateFrame()));
	RefVar config(GetFrameSlotRef(vars, RSSYMuserconfiguration));
	if (ISNIL(GetFrameSlotRef(config, RSSYMuserpensize)))
		SetFrameSlot(config, RSSYMuserpensize, RefVar(MAKEINT(1)));
	if (ISNIL(GetFrameSlotRef(vars, RSSYMinternational)) && NOTNIL(Rglobalheapvarwannabes))
	{
		// the ROM's globals template has the international frame the boot
		// makes a global of: the locale bundles, the current one, the
		// keyboard mapping (the German 'kchr) the keyboard translates with
		RefVar intl(GetFrameSlotRef(RefVar(Rglobalheapvarwannabes), RSSYMinternational));
		if (NOTNIL(intl))
			SetFrameSlot(vars, RSSYMinternational, RefVar(Clone(intl)));
	}
	if (ISNIL(GetFrameSlotRef(vars, RSSYMdisplayparams)))
	{
		// what the ROM's CreateDisplayParams (a root script) makes for the
		// screen: here the application area is the whole screen, with no
		// button bar (NOT YET RECONSTRUCTED: the button bar, the raw
		// display parameters of the display driver)
		RefVar params(AllocateFrame());
		SetFrameSlot(params, RSSYMappareagloballeft, RefVar(MAKEINT(0)));
		SetFrameSlot(params, RSSYMappareaglobaltop, RefVar(MAKEINT(0)));
		SetFrameSlot(params, RSSYMappareawidth, RefVar(MAKEINT(screenWidth)));
		SetFrameSlot(params, RSSYMappareaheight, RefVar(MAKEINT(screenHeight)));
		Rect root;
		SetRect(&root, 0, 0, screenWidth, screenHeight);
		SetFrameSlot(params, RefVar(Intern((char*) "rootBounds")), RefVar(ToObject(root)));
		SetFrameSlot(params, RefVar(Intern((char*) "buttonBarThickness")), RefVar(MAKEINT(0)));
		SetFrameSlot(vars, RSSYMdisplayparams, params);
	}
	HostRegisterViewFunctions();
	RegisterUnitNatives();
}


// The display made and the view system started over it, for a program
// with no event loop (newtonscript --display): the display, the toolbox,
// the root view, the recognition system at the clicks level, the stroke
// world and the host's pen (a minute put on the clock: the ROM boots for
// longer, and a click in the first half second is dropped as a tap after
// writing).
THostScreenDriver*
HostStartViews(long width, long height, long depth)
{
	HostStartDisplay(width, height, depth);
	HostInitViewToolbox();
	InitViewSystem();
	gNewtIsAliveAndWell = true;		// (no boot: the root draws no splash)
	gRecognition.Init(1);
	gStrokeWorld.Init();
	HostTabletInit();
	HostAdvanceClock(60 * 60 * 0xf000);		// (0xf000 clock ticks a Mac tick)
	return gHostDisplay;
}


THostScreenDriver*
HostDisplay(void)
{
	return gHostDisplay;
}


// the seconds between the two epochs: the Newton counts from 1904 and a
// host from 1970
const unsigned long long kSecondsFrom1904To1970 = 2082844800ULL;

// the display the newt world is to boot over (HostBootNewtWorld)
static long	gNewtDisplayWidth = 320;
static long	gNewtDisplayHeight = 480;
static long	gNewtDisplayDepth = 4;
static const char*	gNewtROMImage = nil;
static long	gNewtHeapSize = 0x400000;

void
HostConfigureNewtWorld(const char* romImage, long heapSize, long width, long height, long depth)
{
	gNewtROMImage = romImage;
	gNewtHeapSize = heapSize;
	gNewtDisplayWidth = width;
	gNewtDisplayHeight = height;
	gNewtDisplayDepth = depth;
}


// The newt world's host boot (newt/NewtWorld.h gNewtHostBoot: what the
// world's MainConstructor runs in place of the ROM's InitObjects, InitGraf
// and InitFonts): the ROM image read in and the object system started,
// the display and the toolbox, a minute on the clock.
void
HostBootNewtWorld(void)
{
	if (gNewtROMImage != nil && ImportROMObjectsFromFile(gNewtROMImage) != noErr)
		printf("cannot import %s\n", gNewtROMImage);
	gObjectHeapSize = gNewtHeapSize;
	InitObjects();
	RegisterAllNatives();
	HostMountStores();
	HostStartDisplay(gNewtDisplayWidth, gNewtDisplayHeight, gNewtDisplayDepth);
	HostInitViewToolbox();
	HostAdvanceClock(60 * 60 * 0xf000);
	// DEVIATION: the machine reads the date out of a battery-backed clock
	// chip, which the host has not got: its own clock stood at midnight on
	// 1 January 1904 until something set it, so every note was stamped
	// with that and the status bar said so.  The host's clock is the
	// nearest thing to a battery-backed one, in seconds from 1904 as the
	// Newton counts them.
	SetRealClockSeconds((ULong) ((unsigned long long) time(nil) + kSecondsFrom1904To1970));
}
