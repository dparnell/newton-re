/*
	File:		host/HostOrientation.cpp

	Contains:	The Newton's screen turned to follow the host device
				(HostOrientation.h).
*/

#include <stdio.h>
#include "HostOrientation.h"
#include "HostViews.h"
#include "hal/host/HostScreen.h"
#include "NewtWorld.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "UserPorts.h"
#include "NameServer.h"
#include "RootView.h"
#include "Screen.h"
#include <atomic>

static std::atomic<long>	gAskedRotation(-1);		// the last rotation asked and not yet sent; -1 none
static std::atomic<bool>	gGlobalReady(false);
static long					gHomePortrait = -1;		// the portrait orientation the Newton had before it was first turned


void
HostAskOrientation(long rotation)
{
	if (rotation >= kHostRotationUpright && rotation <= kHostRotationUpsideDown)
		gAskedRotation.store(rotation);
}


// hostDisplay:Turn(data): the device held as data's one byte says (a
// kHostRotation); the screen turned with the ROM's SetScreenOrientation
// when it is the other shape.  ==> the orientation now
static Ref
FHostTurn(RefArg /*rcvr*/, RefArg data)
{
	long rotation = kHostRotationUpright;
	if (IsBinary(data) && Length(data) >= 1)
		rotation = ((const unsigned char*) BinaryData(data))[0];
	else if (ISINT(data))
		rotation = RINT(data);
	long current = 0;
	GetGrafInfo(kGrafInfoOrientation, &current);
	Boolean landscape = (current & 1) != 0;
	Boolean wanted = rotation == kHostRotationLeft || rotation == kHostRotationRight;
	if (!landscape && gHomePortrait < 0)
		gHomePortrait = current;
	if (wanted == landscape)
		return MAKEINT(current);
	long target = wanted ? (rotation == kHostRotationLeft ? 1 : 3) : (gHomePortrait >= 0 ? gHomePortrait : 2);
	NSCallGlobalFn(RefVar(Intern((char*) "SetScreenOrientation")), RefVar(MAKEINT(target)));
	return MAKEINT(target);
}


// HostDeviceRotation(rotation): a script's way to ask what the window asks
// (the turn is made by the kernel services task's next round, as the
// window's is)
static Ref
FHostDeviceRotation(RefArg /*rcvr*/, RefArg rotation)
{
	HostAskOrientation(RINT(rotation));
	return NILREF;
}


void
HostInstallOrientationGlobal(void)
{
	RefVar hostDisplay(AllocateFrame());
	SetFrameSlot(hostDisplay, RefVar(Intern((char*) "Turn")), RefVar(MakeCFunction((void*) FHostTurn, 1, nil)));
	SetFrameSlot(gRootView->fContext, RefVar(Intern((char*) "hostDisplay")), hostDisplay);	// (a 'scpt event's variable is looked for from the root view)
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostDeviceRotation")), RefVar(MakeCFunction((void*) FHostDeviceRotation, 1, nil)));
	gGlobalReady.store(true);
}


void
HostSendAskedOrientation(void)
{
	if (!gGlobalReady.load() || !gNewtIsAliveAndWell || gAskedRotation.load() < 0)
		return;
	static TObjectId portId = 0;
	if (portId == 0)
	{
		TUNameServer nameServer;
		ULong spec = 0;
		if (nameServer.Lookup((char*) "newt", (char*) "TUPort", &portId, &spec) != noErr)
		{
			portId = 0;
			return;
		}
	}
	unsigned char rotation = (unsigned char) gAskedRotation.exchange(-1);
	TRunScriptEvent event("hostDisplay", "Turn");
	event.fData = &rotation;
	event.fSize = 1;
	TUPort newtPort(portId);
	ULong replySize = 0;
	long err = newtPort.SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
	if (err == noErr)
		err = event.fError;
	if (err != noErr)
		fprintf(stderr, "[host] the screen not turned for rotation %d (error %ld)\n", rotation, err);
	else
		fprintf(stderr, "[host] device rotation %d: orientation %ld\n", rotation, event.fResult);
}


extern "C" {

void
HostWindowDeviceRotation(long rotation)
{
	HostAskOrientation(rotation);
}


void
HostWindowDisplayShape(long* width, long* height, long* orientation)
{
	THostScreenDriver* display = HostDisplay();
	if (display == nil)
	{
		*width = *height = *orientation = 0;
		return;
	}
	*width = display->Width();
	*height = display->Height();
	*orientation = display->GetFeature(kScreenFeatureOrientation);
}

}
