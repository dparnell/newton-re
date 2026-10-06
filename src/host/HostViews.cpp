/*
	File:		host/HostViews.cpp

	Contains:	The view system started on the host over the host display.
*/

#include "HostViews.h"
#include <time.h>
#include "LongTime.h"
#include "HostStores.h"
#include "Soups.h"
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
#include "InkRecognizer.h"
#include "RosRecognizer.h"
#include "WordEngines.h"
#include "StrokeCentral.h"
#include "UnitPublic.h"
#include "CardInfo.h"
#include "HostCards.h"
#include "HostInterconnect.h"
#include "HostTablet.h"
#include "hal/host/Host.h"
#include "ROMImport.h"
#include "REPTranslators.h"
#include <string.h>

static THostScreenDriver*	gHostDisplay = nil;


// Disasm(fn): a NewtonScript function's bytecode printed through the REP
// (frames/REPTranslators.h's Disassemble, the ROM's own 0x002c1e68) - for
// reading a function of the machine's own that is not where
// analysis/nsfunctions.py can find it (one out of a package part, or one
// reached only at run time, e.g. GetRoot().calendar.CalculateScrollRects)
static Ref
FDisasm(RefArg /*rcvr*/, RefArg fn)
{
	Disassemble(fn);
	return NILREF;
}


// ScreenColour(h, v): the colour the display shows at the point, as
// 0xRRGGBB (a gray's three alike), nil off it - a colour screen's
// (--colour, qd/Colour.h) palette entry
static Ref
FScreenColour(RefArg /*rcvr*/, RefArg h, RefArg v)
{
	if (gHostDisplay == nil || !ISINT((Ref) h) || !ISINT((Ref) v))
		return NILREF;
	Long x = RINT(h), y = RINT(v);
	if (x < 0 || y < 0 || x >= gHostDisplay->Width() || y >= gHostDisplay->Height())
		return NILREF;
	UpdateHardwareScreen();
	return MAKEINT((Long) gHostDisplay->Colour(x, y));
}


// ScreenSnapshot(path): the display written to the file - a PBM for a
// path ending in .pbm, a PPM (in colour on a colour screen) for .ppm,
// else a PGM; ==> whether it could be
// ScreenPixel(h, v): the gray the display shows at the point (0 black,
// 255 white), nil off it - what a test looks at to see what was drawn
static Ref
FScreenPixel(RefArg /*rcvr*/, RefArg h, RefArg v)
{
	if (gHostDisplay == nil || !ISINT((Ref) h) || !ISINT((Ref) v))
		return NILREF;
	Long x = RINT(h), y = RINT(v);
	if (x < 0 || y < 0 || x >= gHostDisplay->Width() || y >= gHostDisplay->Height())
		return NILREF;
	UpdateHardwareScreen();
	return MAKEINT(255 - gHostDisplay->Gray(x, y));
}


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
	Boolean ppm = length > 4 && strcmp(name + length - 4, ".ppm") == 0;		// (in colour on a colour screen)
	UpdateHardwareScreen();
	return MAKEBOOLEAN(pbm ? gHostDisplay->WritePBM(name) : ppm ? gHostDisplay->WritePPM(name) : gHostDisplay->WritePGM(name));
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


// PacePen(on): with the OS running, the pen's records fed one per tick
// by the inker, as a real pen's arrive, rather than straight into the
// tablet at once - so that a view tracking the pen (a drag) sees the pen
// still down while it moves.
static Ref
FPacePen(RefArg /*rcvr*/, RefArg on)
{
	HostTabletSetPaced(NOTNIL(on));
	return NILREF;
}


// The panel's pen (hal/host/HostTablet.h), which goes through the host's
// tablet driver and its calibration where PenDown/PenUp's test pen does
// not.  HostTabletTap(x, y, milliseconds): the pen held on the panel at
// (x, y) for so long.  HostTabletSkew(dx, dy, sx, sy): the panel put
// askew, reading the point (x*sx+dx, y*sy+dy).  HostTabletAutoCalibrate
// (on): the calibration screen's targets tapped as they appear.
// HostTabletCalibrationTarget(): [h, v] of the last target it showed, or
// nil.  HostTabletShutDowns(): how many sleeps the driver has been shut
// down for.
static Ref
FHostTabletShutDowns(RefArg /*rcvr*/)
{
	return MAKEINT(HostTabletShutDowns());
}

static Ref
FHostTabletTap(RefArg /*rcvr*/, RefArg x, RefArg y, RefArg milliseconds)
{
	HostTabletRawTap(RINT(x), RINT(y), (ULong) RINT(milliseconds));
	return NILREF;
}

static Ref
FHostTabletSkew(RefArg /*rcvr*/, RefArg dx, RefArg dy, RefArg sx, RefArg sy)
{
	HostTabletSetSkew(CoerceToDouble(dx), CoerceToDouble(dy), CoerceToDouble(sx), CoerceToDouble(sy));
	return NILREF;
}

static Ref
FHostTabletAutoCalibrate(RefArg /*rcvr*/, RefArg on)
{
	HostTabletAutoCalibrate(NOTNIL(on));
	return NILREF;
}

static Ref
FHostTabletCalibrationTarget(RefArg /*rcvr*/)
{
	long h, v;
	if (!HostTabletCalibrationTargetAt(&h, &v))
		return NILREF;
	RefVar target(MakeArray(2));
	SetArraySlot(target, 0, MAKEINT(h));
	SetArraySlot(target, 1, MAKEINT(v));
	return target;
}


static Ref
FIdleStrokes(RefArg /*rcvr*/)
{
	// (the whole recogniser's idle, as the application's would be: the
	// strokes, the ink and the controller's four passes - a tap reaches
	// its view through the arbitration, not out of IdleStrokes itself)
	while (HostTabletQueued() > 0)
	{
		HostTabletPump();
		gRecognition.Idle();
	}
	HostTabletSettle();
	gRecognition.Idle();
	return NILREF;
}


void
HostRegisterViewFunctions(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyEvent")), RefVar(MakeCFunction((void*) FKeyEvent, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyboardConnect")), RefVar(MakeCFunction((void*) FKeyboardConnect, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenSnapshot")), RefVar(MakeCFunction((void*) FScreenSnapshot, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "Disasm")), RefVar(MakeCFunction((void*) FDisasm, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenPixel")), RefVar(MakeCFunction((void*) FScreenPixel, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenColour")), RefVar(MakeCFunction((void*) FScreenColour, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenWidth")), RefVar(MakeCFunction((void*) FScreenWidth, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenHeight")), RefVar(MakeCFunction((void*) FScreenHeight, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenDown")), RefVar(MakeCFunction((void*) FPenDown, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenMove")), RefVar(MakeCFunction((void*) FPenMove, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PenUp")), RefVar(MakeCFunction((void*) FPenUp, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "IdleStrokes")), RefVar(MakeCFunction((void*) FIdleStrokes, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "PacePen")), RefVar(MakeCFunction((void*) FPacePen, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostTabletTap")), RefVar(MakeCFunction((void*) FHostTabletTap, 3, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostTabletSkew")), RefVar(MakeCFunction((void*) FHostTabletSkew, 4, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostTabletAutoCalibrate")), RefVar(MakeCFunction((void*) FHostTabletAutoCalibrate, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostTabletShutDowns")), RefVar(MakeCFunction((void*) FHostTabletShutDowns, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostTabletCalibrationTarget")), RefVar(MakeCFunction((void*) FHostTabletCalibrationTarget, 0, nil)));
	HostRegisterCardFunctions();
	HostRegisterInterconnectFunctions();
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
	// (the letter set, which the cursive recogniser's installation reads
	// before ReadCursiveOptions writes its default: 2, printed)
	if (ISNIL(GetFrameSlotRef(config, RSSYMlettersetselection)))
		SetFrameSlot(config, RSSYMlettersetselection, RefVar(MAKEINT(2)));
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
	// the System soup, which the ROM's boot block gets or creates on the
	// internal store before anything reads a preference out of it (the
	// cursive recogniser's installation reads the saved letter weights)
	RefVar stores(GetStores());
	if (NOTNIL(stores) && Length(stores) > 0)
	{
		RefVar store(GetArraySlotRef(stores, 0));
		RefVar name(Rsystemsoupname);
		if (ISNIL(StoreHasSoup(store, name)) && NOTNIL(Rsystemsoupindexes))
			StoreCreateSoup(store, name, RefVar(Rsystemsoupindexes));
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
	// the ROM starts it at 2 - clicks and strokes, and the shapes and
	// words above them.  The shape and word recognisers themselves are
	// NOT YET, so level 2 here means only that the dictionaries are built
	// and the word half of the system is meant to be on.
	// the ROM's own handwriting engine, as the Notebook's toolbox
	// registers it (recognition/RosRecognizer.h)
	RegisterRosettaWRec();
	RegisterHostWordEngines();	// (and the host's own engines: recognition/WordEngines.h)
	gRecognition.Init(2);		// (which puts the letter set's word recogniser in use: ReadCursiveOptions)
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
	// Newton counts them - GMT, as the Newton's clock chip keeps it: the
	// time a script sees is that plus the home city's offset
	// (RealClockSeconds), so the Newton shows the time in the city Setup
	// or Time Zones names, as the device does.  (Past 2010 the ROM's
	// script seconds overflow: intl/Dates.h's year-2010 fix.)
	TTime gmt((ULong) ((unsigned long long) time(nil) + kSecondsFrom1904To1970), kSeconds);
	TURealTimeAlarm::SetTime(gmt);
}
