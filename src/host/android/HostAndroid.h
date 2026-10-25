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
#include <jni.h>

// android_main has run: the process is the activity's, so there can be a window
void	HostAndroidSetActive(bool active);
bool	HostAndroidActive(void);

// the activity's window came (or, nil, went)
void	HostAndroidSetWindow(ANativeWindow* window);
// a motion event of the activity ==> 1 when taken
int		HostAndroidInput(AInputEvent* event);
// the activity is being destroyed: the run ends
void	HostAndroidDestroyed(void);

// the activity's Java side (the Viwoods fast ink): its VM and the activity, from android_main's thread
void	HostAndroidSetActivity(JavaVM* vm, JNIEnv* env, jobject activity);
// the activity went to the background (true) or came back
void	HostAndroidPaused(bool paused);

// the display report (Diag.cpp, an APK made with build_apk.py --diag): drawn on the window, shown for 100 s
void	HostAndroidDiagnose(JavaVM* vm, ANativeWindow* window);
void	HostAndroidSetDiag(bool on);

#endif	/* __HOSTANDROID_H */
