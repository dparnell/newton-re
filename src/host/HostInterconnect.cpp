/*
	File:		host/HostInterconnect.cpp

	Contains:	The interconnect pin for scripts (HostInterconnect.h).

	Host-only: there is nothing of the ROM's here.
*/

#include "HostInterconnect.h"
#include "CommManager.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"


// HostInterconnect(state): the comm manager told the pin's new state, as
// the interconnect handler tells it (its kCMNotify event, the state in
// the word the ROM's handler reads).  ==> nil, or the error.
static Ref
FHostInterconnect(RefArg /*rcvr*/, RefArg state)
{
	TCMPackageEvent message;
	TCMEvent reply;
	message.fEvent = kCMNotify;
	message.fPackageB = ISINT(state) ? RINT(state) : 0;
	message.fPackageA = 0;
	NewtonErr err = CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
	return err == noErr ? NILREF : MAKEINT(err);
}


static Ref
FourChars(ULong word)
{
	char text[5] = { (char) (word >> 24), (char) (word >> 16), (char) (word >> 8), (char) word, 0 };
	return MakeString(text);
}


// HostLastSerialDevice(): the device the comm manager last heard of on a
// serial port, as a frame {type, manufacturer, version} of four-character
// strings, or nil.
static Ref
FHostLastSerialDevice(RefArg /*rcvr*/)
{
	TConnectedDevice device;
	if (CMGetLastDevice(&device) != noErr || device.fDeviceType == 0)
		return NILREF;
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "type")), RefVar(FourChars(device.fDeviceType)));
	SetFrameSlot(frame, RefVar(Intern((char*) "manufacturer")), RefVar(FourChars(device.fManufacturer)));
	SetFrameSlot(frame, RefVar(Intern((char*) "version")), RefVar(FourChars(device.fVersion)));
	return frame;
}


void
HostRegisterInterconnectFunctions(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostInterconnect")), RefVar(MakeCFunction((void*) FHostInterconnect, 1, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostLastSerialDevice")), RefVar(MakeCFunction((void*) FHostLastSerialDevice, 0, nil)));
}
