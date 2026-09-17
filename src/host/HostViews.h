/*
	File:		host/HostViews.h

	Contains:	The view system on the host: a display (the host screen
				driver, hal/host/HostScreen.h) made the screen, QuickDraw and
				the fonts started, what the ROM's boot puts in the globals
				for the views (vars.fonts from the ROM font list,
				vars.userConfiguration, vars.international - the locale and the
				keyboard mapping) and the root view
				made over the screen; the NewtonScript functions
				ScreenSnapshot(path) (the display written as a PGM, or a PBM
				for a .pbm path), ScreenWidth(), ScreenHeight(),
				KeyEvent(keyCode, isDown) and KeyboardConnect(connected) (the
				hardware keyboard's events, as the keyboard tool sends them).  What
				TNotebook::InitToolbox and the boot's NewtonScript do on the
				MessagePad.  The newt world (newt/NewtWorld.h) boots over
				HostBootNewtWorld and runs the event loop.
*/

#ifndef __HOSTVIEWS_H
#define __HOSTVIEWS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class THostScreenDriver;

// after InitObjects (with the ROM's objects imported for the fonts)
THostScreenDriver*	HostStartViews(long width, long height, long depth);	// the display, the toolbox, the root view, the recognition system and the pen: for a program with no event loop
THostScreenDriver*	HostStartDisplay(long width, long height, long depth);	// QuickDraw and the host screen driver made the screen
void				HostInitViewToolbox(void);				// the fonts, the globals and the natives the views need
void				HostRegisterViewFunctions(void);		// the NewtonScript functions above (HostInitViewToolbox does it)

// the newt world on the host (newt/NewtWorld.h): what its MainConstructor
// boots when gNewtHostBoot is HostBootNewtWorld - the ROM image read in,
// the object system, the display (HostConfigureNewtWorld says which),
// the toolbox
void				HostConfigureNewtWorld(const char* romImage, long heapSize, long width, long height, long depth);
void				HostBootNewtWorld(void);

#endif	/* __HOSTVIEWS_H */
