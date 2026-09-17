/*
	File:		host/HostViews.h

	Contains:	The view system on the host: a display (the host screen
				driver, hal/host/HostScreen.h) made the screen, QuickDraw and
				the fonts started, what the ROM's boot puts in the globals
				for the views (vars.fonts from the ROM font list,
				vars.userConfiguration, vars.displayParams) and the root view
				made over the screen; the NewtonScript functions
				ScreenSnapshot(path) (the display written as a PGM, or a PBM
				for a .pbm path), ScreenWidth(), ScreenHeight(),
				KeyEvent(keyCode, isDown) and KeyboardConnect(connected) (the
				hardware keyboard's events, as the keyboard tool sends them).  What
				TNotebook::InitToolbox and the boot's NewtonScript do on the
				MessagePad; the event loop is NOT YET.
*/

#ifndef __HOSTVIEWS_H
#define __HOSTVIEWS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class THostScreenDriver;

// after InitObjects (with the ROM's objects imported for the fonts)
THostScreenDriver*	HostStartViews(long width, long height, long depth);
void				HostRegisterViewFunctions(void);		// the NewtonScript functions above (HostStartViews does it)

#endif	/* __HOSTVIEWS_H */
