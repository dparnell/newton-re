/*
	File:		host/HostOrientation.h

	Contains:	The Newton's screen turned to follow the host device.

				A MessagePad's screen turns when its owner asks (the
				Rotate button, SetScreenOrientation); a tablet turns its
				screen itself when it is held the other way or a keyboard
				cover is folded back - the reMarkable Paper Pro goes to
				landscape with its type folio.  The host's window says
				how the device is held (HostAskOrientation, from any
				thread: AppLoad's rotation, docs/host-remarkable.md
				"Rotation"); the kernel services task - the keyboard
				tool's task - sends it to the newt world as a 'scpt event
				(TRunScriptEvent) naming the root view's variable
				`hostDisplay` and its method `Turn`, as host/HostPackages.h
				sends a package; `Turn` calls the ROM's own
				SetScreenOrientation when the Newton's screen is the other
				shape from the device's, so the root view and the
				applications are laid out again as the Rotate button lays
				them out.  The window then draws the turned display onto
				its panel turned to match (host/remarkable/PanelTurn.h).

				A device held portrait asks for the portrait orientation
				the Newton had before it was first turned (2 on the
				MP2x00 as it boots); landscape is 1 for a device turned
				to the left and 3 to the right.  The two portraits and the
				two landscapes look the same on a host - its screen buffer
				only changes shape - so which of each is chosen matters
				only to scripts that ask GetOrientation.

				HostDeviceRotation(rotation), a NewtonScript function,
				asks the same from a script (demo/rotation.ns).
*/

#ifndef __HOSTORIENTATION_H
#define __HOSTORIENTATION_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

// how the device is held, as AppLoad's qtfb says it (ROTATION_* in its
// common.h): the window's picture turned that way on the panel
enum
{
	kHostRotationUpright = 0,
	kHostRotationLeft = 1,			// turned 90 degrees to the left: landscape
	kHostRotationRight = 2,			// turned 90 degrees to the right: landscape
	kHostRotationUpsideDown = 3
};

void	HostAskOrientation(long rotation);			// from any thread: the device held so; the Newton's screen turned to match
void	HostInstallOrientationGlobal(void);			// in the newt world, once its globals are built: `hostDisplay`, HostDeviceRotation
void	HostSendAskedOrientation(void);				// from the kernel services task: the last rotation asked sent to the world

extern "C" {
void	HostWindowDeviceRotation(long rotation);	// the window's shim: HostAskOrientation
void	HostWindowDisplayShape(long* width, long* height, long* orientation);	// the window's shim: the display's size as it is turned now, and the Newton's orientation (plain reads)
}

#endif	/* __HOSTORIENTATION_H */
