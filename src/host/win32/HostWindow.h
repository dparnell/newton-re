/*
	File:		host/win32/HostWindow.h

	Contains:	A window on the host's display: the display's grays shown
				(refreshed thirty times a second), the mouse as the pen (a
				press is a pen-down, the moves samples, the release a
				pen-up: into the tablet buffer through hal/host/HostTablet.h)
				and the keys as the keyboard's (HostKeyboardPush).  The
				window runs on a thread of its own (Win32 message loop),
				started before the OS boots; closing it ends the run
				(HostKeyboardQuit).  Only the Windows build has it; the
				others run the world without a window.
*/

#ifndef __HOSTWINDOW_H
#define __HOSTWINDOW_H

// (no Newton headers here: the Windows headers and they do not mix - the
// display comes as its size and its bytes)
bool	HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale);	// the window opened on its thread over the display's grays (0 white .. 255 black, width per row); ==> whether it could be
void	HostWindowStop(void);								// the window closed and its thread joined

#endif	/* __HOSTWINDOW_H */
