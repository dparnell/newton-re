/*
	File:		host/HostViews.cpp

	Contains:	The view system started on the host over the host display.
*/

#include "HostViews.h"
#include "HostScreen.h"
#include "Screen.h"
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


void
HostRegisterViewFunctions(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyEvent")), RefVar(MakeCFunction((void*) FKeyEvent, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "KeyboardConnect")), RefVar(MakeCFunction((void*) FKeyboardConnect, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenSnapshot")), RefVar(MakeCFunction((void*) FScreenSnapshot, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenWidth")), RefVar(MakeCFunction((void*) FScreenWidth, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "ScreenHeight")), RefVar(MakeCFunction((void*) FScreenHeight, 0, nil)));
}


// The display made and the view system started over it: QuickDraw, the
// screen, the fonts (vars.fonts: the ROM font list's families by their
// symbols, as the ROM's globals template has them), an empty
// userConfiguration, the text, view and host functions, the root view.
THostScreenDriver*
HostStartViews(long width, long height, long depth)
{
	InitGraf();
	gHostDisplay = new THostScreenDriver;
	gHostDisplay->New();
	gHostDisplay->Configure(width, height, depth, 100);
	InitScreen(gHostDisplay);
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
	HostRegisterViewFunctions();
	InitViewSystem();
	return gHostDisplay;
}
