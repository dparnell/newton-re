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

	Reconstructed from the MP2x00 US ROM (0x00202a3c-0x00202d90); each
	function cites its origin.
*/

#include "Screen.h"
#include "Frames.h"
#include "NativeFunctions.h"


// ROM 0x00202d64 FGetLCDContrast__FRC6RefVar
Ref
FGetLCDContrast(RefArg /*rcvr*/)
{
	long contrast = 0;
	GetGrafInfo(kGrafInfoContrast, &contrast);
	return MAKEINT(contrast);
}


// ROM 0x00202a3c FSetLCDContrast__FRC6RefVarT1
Ref
FSetLCDContrast(RefArg /*rcvr*/, RefArg contrast)
{
	SetGrafInfo(kGrafInfoContrast, RINT(contrast));
	return NILREF;
}


// ROM 0x00202ae4 FGetOrientation
Ref
FGetOrientation(RefArg /*rcvr*/)
{
	long orientation = 0;
	GetGrafInfo(kGrafInfoOrientation, &orientation);
	return MAKEINT(orientation);
}


// ROM 0x00202b0c FSetOrientation
// SetOrientation, not SetGrafInfo: turning the screen moves the root view
// and the ports with it.
Ref
FSetOrientation(RefArg /*rcvr*/, RefArg orientation)
{
	SetOrientation(RINT(orientation));
	return NILREF;
}


// ROM 0x001edc84 FLockScreen
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


// ROM 0x00201a0c FBackLightStatus
// BackLightStatus(): true when the backlight is on.  (Switching it on
// and off is FBackLight 0x00201a3c, NOT YET RECONSTRUCTED - it goes to
// the power manager.)
static Ref
FBackLightStatus(RefArg /*rcvr*/)
{
	long on = 0;
	GetGrafInfo(kGrafInfoBacklight, &on);
	return MAKEBOOLEAN(on == 1);
}

void
RegisterScreenNatives(void)
{
	RegisterNativeFunction("FLockScreen", (void*) FLockScreen, 1);
	RegisterNativeFunction("FGetLCDContrast__FRC6RefVar", (void*) FGetLCDContrast, 0);
	RegisterNativeFunction("FSetLCDContrast__FRC6RefVarT1", (void*) FSetLCDContrast, 1);
	RegisterNativeFunction("FBackLightStatus", (void*) FBackLightStatus, 0);
	RegisterNativeFunction("FGetOrientation", (void*) FGetOrientation, 0);
	RegisterNativeFunction("FSetOrientation", (void*) FSetOrientation, 1);
}
