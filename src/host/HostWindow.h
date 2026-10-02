/*
	File:		host/HostWindow.h

	Contains:	A window on the host's display: the display's grays shown
				(refreshed thirty times a second), the mouse as the pen (a
				press is a pen-down, the moves samples, the release a
				pen-up: into the tablet buffer through hal/host/HostTablet.h)
				and the keys as the keyboard's (HostKeyboardPush).  The
				window runs on a thread of its own, started before the OS
				boots; closing it ends the run (HostKeyboardQuit).

				One implementation per host answers these two calls, and
				the host's CMakeLists picks it: host/win32/HostWindow.cpp
				over the Win32 message loop, host/x11/HostWindow.cpp over
				X11 (a Linux or BSD host, and a Wayland one through
				XWayland).  A host with neither - or one whose display
				cannot show the grays - has HostWindowStart answer false,
				and the world runs without a window.

				A key is handed over as its Windows virtual key code
				whichever host it came from, so that host/HostKeyboard.cpp
				keeps the one map from a key to the Newton's (ADB) code.
*/

#ifndef __HOSTWINDOW_H
#define __HOSTWINDOW_H

// (no Newton headers here: the platform's headers and they do not mix - the
// display comes as its size and its bytes)
bool	HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale);	// the window opened on its thread over the display's grays (0 white .. 255 black, width per row); ==> whether it could be
void	HostWindowStop(void);								// the window closed and its thread joined
// The display's size (--display) made the shape the window will show it:
// unchanged on a desktop; on a reMarkable landscape when the tablet is
// (its type folio attached) - its AppLoad shows newton's picture upright
// whichever way it is turned, and the picture's shape is fixed when the
// window opens (docs/host-remarkable.md, "Rotation")
void	HostWindowPreferredDisplay(long* width, long* height);
// A press (what 0), a move (1) or a release (2) of the mouse at a point of
// the display, given to the window as the mouse's would be - posted to its
// message queue on Windows - so that a test drives the pen through the
// window itself (newton --window-pen).  With no window open, or on a host
// whose events cannot be posted (X11), the window's shims are called
// directly, as its event loop would call them.
void	HostWindowPostPen(long x, long y, int what);
// Where the window is on the host's screen (its top left, the frame's),
// kept as it closes - and where the next one opens (a restarted newton's
// window opening where the old one was): ==> false when there is none
bool	HostWindowPosition(long* x, long* y);
void	HostWindowSetPosition(long x, long y);

#endif	/* __HOSTWINDOW_H */
