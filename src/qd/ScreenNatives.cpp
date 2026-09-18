/*
	File:		qd/ScreenNatives.cpp

	Contains:	What a script can ask of the screen: its contrast and which
				way up it is.  Thin wrappers over GetGrafInfo/SetGrafInfo
				(Screen.h), which is where the screen driver answers.

				The NewtonScript boot asks for the contrast while it is
				setting its globals up, so these have to answer before the
				system is running.  Without a screen driver GetGrafInfo
				answers a contrast of 0 and an orientation of 1, which is
				what a host that has not started a display gets.

	Reconstructed from the MP2100 D ROM (0x0020030c-0x00200660); each
	function cites its origin.
*/

#include "Screen.h"
#include "Frames.h"
#include "NativeFunctions.h"


// ROM 0x00200634 FGetLCDContrast__FRC6RefVar
Ref
FGetLCDContrast(RefArg /*rcvr*/)
{
	long contrast = 0;
	GetGrafInfo(kGrafInfoContrast, &contrast);
	return MAKEINT(contrast);
}


// ROM 0x0020030c FSetLCDContrast__FRC6RefVarT1
Ref
FSetLCDContrast(RefArg /*rcvr*/, RefArg contrast)
{
	SetGrafInfo(kGrafInfoContrast, RINT(contrast));
	return NILREF;
}


// ROM 0x002003b4 FGetOrientation
Ref
FGetOrientation(RefArg /*rcvr*/)
{
	long orientation = 0;
	GetGrafInfo(kGrafInfoOrientation, &orientation);
	return MAKEINT(orientation);
}


// ROM 0x002003dc FSetOrientation
// SetOrientation, not SetGrafInfo: turning the screen moves the root view
// and the ports with it.
Ref
FSetOrientation(RefArg /*rcvr*/, RefArg orientation)
{
	SetOrientation(RINT(orientation));
	return NILREF;
}


// ROM 0x001f009c FLockScreen
// The screen held while a script draws a lot: the drawing bracket taken
// and given back, so what is drawn in between reaches the display in one
// go.  nil unlocks.
Ref
FLockScreen(RefArg /*rcvr*/, RefArg lock)
{
	if (ISNIL(lock))
		StopDrawing(nil, nil);
	else
		StartDrawing(nil, nil);
	return NILREF;
}


void
RegisterScreenNatives(void)
{
	RegisterNativeFunction("FLockScreen", (void*) FLockScreen, 1);
	RegisterNativeFunction("FGetLCDContrast__FRC6RefVar", (void*) FGetLCDContrast, 0);
	RegisterNativeFunction("FSetLCDContrast__FRC6RefVarT1", (void*) FSetLCDContrast, 1);
	RegisterNativeFunction("FGetOrientation", (void*) FGetOrientation, 0);
	RegisterNativeFunction("FSetOrientation", (void*) FSetOrientation, 1);
}
