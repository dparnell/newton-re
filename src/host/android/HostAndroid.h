/*
	File:		host/android/HostAndroid.h

	Contains:	What android/AndroidMain.cpp (the activity's event thread) and
				android/HostWindow.cpp (the display and the pen) say to each
				other.  Not part of host/HostWindow.h: only the Android build
				has an activity.
*/

#ifndef __HOSTANDROID_H
#define __HOSTANDROID_H

struct ANativeWindow;
struct AInputEvent;

// android_main has run: the process is the activity's, so there can be a window
void	HostAndroidSetActive(bool active);
bool	HostAndroidActive(void);

// the activity's window came (or, nil, went)
void	HostAndroidSetWindow(ANativeWindow* window);
// a motion event of the activity ==> 1 when taken
int		HostAndroidInput(AInputEvent* event);
// the activity is being destroyed: the run ends
void	HostAndroidDestroyed(void);

#endif	/* __HOSTANDROID_H */
