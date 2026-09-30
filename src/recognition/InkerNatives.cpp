/*
	File:		recognition/InkerNatives.cpp

	Contains:	The script's side of the inker (recognition/Inker.h): its
				port, found by name, and the tablet's calibration -
				GetCalibration and SetCalibration (a 'calibration binary of
				0x14 bytes), CalibrateTablet (the calibration screen, and
				the result put in the System soup by the ROM's
				savecalibration block), IsTabletCalibrationNeeded, and
				LoadInkerCalibration, which gives the inker back what the
				System soup kept (the ROM's loadcalibration block) when the
				machine starts or wakes - and shows the calibration screen
				when there is nothing kept.

				The calibration binary is kept big-endian, as the Newton
				wrote it: four 16.16 words (the x and y scales, the x and y
				offsets) and two bytes (DEVIATION: the ROM copies the
				struct as it lies in memory; a host of the other byte order
				swaps the words so a store means the same thing everywhere).

				Reconstructed from the MP2x00 US ROM (0x0013fb0c-0x0013fea0,
				0x00141098-0x0014137c); each function cites its origin.
*/

#include "Inker.h"
#include "Stroke.h"
#include "Frames.h"
#include "Objects.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "RootView.h"
#include "View.h"
#include "NameServer.h"
#include "UserPorts.h"
#include "toolbox/ByteOrder.h"
#include "intl/Locale.h"
#include "newt/NewtWorld.h"		// TBusyBoxEvent

#include <stdio.h>
#include <string.h>

// ROM 0x0c101654 gInkerCalibrated
long	gInkerCalibrated = 0;

// ROM 0x0c101658 gTheInkerPort (declared by newt/NewtWorld.h): the busy
// box is sent to it once it has been looked up
TUPort*	gTheInkerPort = nil;


// ROM 0x0030dd60 BusyBoxSend__Fl
// The command sent to the inker as a 'newt/'inkr event; nothing at all
// when the inker is not there.
void
BusyBoxSend(long command)
{
	if (gTheInkerPort == nil)
		return;
	TBusyBoxEvent event;
	event.fAEventClass = kNewtEventClass;
	event.fAEventID = kNewtInkerEvent;
	event.fCommand = (ULong) command;
	event.fUnused0c = 0;
	gTheInkerPort->Send(&event, sizeof(event), kBusyBoxSendTimeout);
}


// a calibration as the binary keeps it, and back
static void
PackCalibration(const Calibration& calibration, UChar* data)
{
	PutBigEndianWord(data + 0x00, (ULong32) calibration.fXScale);
	PutBigEndianWord(data + 0x04, (ULong32) calibration.fYScale);
	PutBigEndianWord(data + 0x08, (ULong32) calibration.fXOffset);
	PutBigEndianWord(data + 0x0c, (ULong32) calibration.fYOffset);
	data[0x10] = calibration.fField10;
	data[0x11] = calibration.fField11;
	data[0x12] = calibration.fPad[0];
	data[0x13] = calibration.fPad[1];
}

static void
UnpackCalibration(const UChar* data, Calibration* calibration)
{
	calibration->fXScale = (Long32) GetBigEndianWord(data + 0x00);
	calibration->fYScale = (Long32) GetBigEndianWord(data + 0x04);
	calibration->fXOffset = (Long32) GetBigEndianWord(data + 0x08);
	calibration->fYOffset = (Long32) GetBigEndianWord(data + 0x0c);
	calibration->fField10 = data[0x10];
	calibration->fField11 = data[0x11];
	calibration->fPad[0] = data[0x12];
	calibration->fPad[1] = data[0x13];
}


// ROM 0x0013fb0c InkerPort__Fv
// The inker's port, looked up by its name ('inkr, a "TUPort") the first
// time it is asked for and kept in gTheInkerPort.
TUPort*
InkerPort(void)
{
	if (gTheInkerPort == nil)
	{
		TUNameServer nameServer;
		ULong thing = 0, spec;
		nameServer.Lookup((char*) "inkr", (char*) "TUPort", &thing, &spec);
		gTheInkerPort = new TUPort((TObjectId) thing);
	}
	return gTheInkerPort;
}


// an RPC to the inker: the event sent, the reply read back into it
static long
InkerRPC(TInkerEvent* event, ULong size, ULong* replySize, TTimeout timeout = kNoTimeout)
{
	event->fAEventClass = kNewtEventClass;
	event->fAEventID = kInkerID;
	TInkerEvent reply;
	memset(&reply, 0, sizeof(reply));
	ULong got = 0;
	long err = InkerPort()->SendRPC(&got, event, size, &reply, sizeof(reply), timeout);
	if (replySize != nil)
		*replySize = got;
	if (err == noErr && got != 0)
		memcpy(event, &reply, got < sizeof(reply) ? got : sizeof(reply));
	return err;
}


// ROM 0x0013fb98 HobbleTablet__Fv
// The inker sent its 0x1d command ('newt/'inkr), which slows the tablet
// down.  (Host: only while the inker runs - a program with no OS has no
// inker to ask, and the name server would not find one.)
void
HobbleTablet(void)
{
	if (gInker == nil)
		return;
	struct { TAEvent fEvent; ULong fCommand; } command;
	command.fEvent.fAEventClass = kNewtEventClass;
	command.fEvent.fAEventID = 'inkr';
	command.fCommand = 0x1d;
	TAEvent reply[3];
	ULong replySize;
	InkerPort()->SendRPC(&replySize, &command, sizeof(command), reply, sizeof(reply));
}


// ROM 0x0014078c InkerOffUnHobbled__FP5TRect
// The inker told to stop inking the stroke under way (its pen mode set to
// nought, command 7), and what its live ink covered so far answered.
// (Host: only while the inker runs, as HobbleTablet.)
void
InkerOffUnHobbled(Rect* inked)
{
	if (gInker == nil)
		return;
	TInkerEvent event;
	memset(&event, 0, sizeof(event));
	event.fCommand = 7;
	if (InkerRPC(&event, kInkerReplySize, nil) == noErr)
		*inked = event.fInkedBounds;
}


// ROM 0x00140dcc InkerOff__FP5TRect
// The same with the tablet slowed down first.
void
InkerOff(Rect* inked)
{
	HobbleTablet();
	InkerOffUnHobbled(inked);
}


// ROM 0x0013fc2c LoadInkerCalibration__Fv
// The calibration the System soup kept given the inker (the ROM's
// loadcalibration block, answering true when there was one), and the
// calibration screen shown when there was none - or when the tablet asks
// for it.  (A file named bootNoCalibrate, which the ROM looks for through
// its debugging file system, skips the screen; the host looks for it in
// its own working directory.)
void
LoadInkerCalibration(void)
{
	RefVar found(DoBlock(RefVar(Rloadcalibration), RefVar(NILREF)));
	if (NOTNIL(found))
		gInkerCalibrated = 1;
	FILE* f = fopen("bootNoCalibrate", "r");
	if (f == nil)
		CheckTabletCalibration();
	else
	{
		gInkerCalibrated = 1;
		fclose(f);
	}
}


// ROM 0x0013fcb8 FSetCalibration__FRC6RefVarT1
// SetCalibration(binary): the inker given a calibration, a binary of any
// other length (or nil) ignored.  ==> nil either way.
static Ref
FSetCalibration(RefArg /*rcvr*/, RefArg calibration)
{
	if (NOTNIL(calibration) && Length(calibration) == 0x14)
	{
		TInkerEvent event;
		memset(&event, 0, sizeof(event));
		event.fCommand = kInkerSetCalibration;
		UnpackCalibration((const UChar*) BinaryData(calibration), &event.fCalibration);
		InkerRPC(&event, kInkerCalibrationReplySize, nil);
	}
	return NILREF;
}


// ROM 0x0013fda4 FGetCalibration__FRC6RefVar
// GetCalibration(): the inker's calibration as a 'calibration binary, or
// nil when it did not answer with one.
static Ref
FGetCalibration(RefArg /*rcvr*/)
{
	RefVar result;
	TInkerEvent event;
	memset(&event, 0, sizeof(event));
	event.fCommand = kInkerGetCalibration;
	ULong replySize = 0;
	InkerRPC(&event, kInkerCalibrationReplySize, &replySize);
	if (replySize == kInkerCalibrationReplySize)
	{
		result = AllocateBinary(RSSYMcalibration, 0x14);
		PackCalibration(event.fCalibration, (UChar*) BinaryData(result));
	}
	return result;
}


// ROM 0x00141098 CalibrateInker__Fv
// The inker asked to show the calibration screen (command 5), given the
// sleep time - at most ten minutes - to wait at each target, so the
// machine does not fall asleep in the middle.  When it worked the tablet
// is marked calibrated and the ROM's savecalibration block puts the new
// calibration in the System soup (the "Calibration" entry, under the
// screen's orientation).  ==> 0, or the error.
long
CalibrateInker(void)
{
	TInkerEvent event;
	memset(&event, 0, sizeof(event));
	event.fCommand = kInkerCalibrate;
	ULong limit = 0;
	RefVar sleepTime(GetPreference(RSSYMsleeptime));
	if (NOTNIL(sleepTime))
	{
		ULong seconds = (ULong) RINT(sleepTime);
		if (seconds > 600)
			seconds = 600;
		limit = seconds * 0x384000;
	}
	event.fArg = limit;
	InkerRPC(&event, kInkerReplySize, nil);
	long err = (long) (Long32) event.fArg;
	if (err == 0)
	{
		gInkerCalibrated = 1;
		DoBlock(RefVar(Rsavecalibration), RefVar(NILREF));
		err = 0;
	}
	return err;
}


// ROM 0x001411dc FCalibrateTablet__FRC6RefVar
// CalibrateTablet(): the calibration screen, and the whole screen redrawn
// over it.  ==> nil when it worked, the error as an integer when not.
static Ref
FCalibrateTablet(RefArg /*rcvr*/)
{
	long err = CalibrateInker();
	gRootView->Dirty(nil);
	return err == 0 ? NILREF : MAKEINT(err);
}


// ROM 0x0014121c CheckTabletHWCalibration__Fv
// Whether the tablet says it wants calibrating (the inker's command 0x21).
Boolean
CheckTabletHWCalibration(void)
{
	TInkerEvent event;
	memset(&event, 0, sizeof(event));
	event.fAEventClass = kNewtEventClass;
	event.fAEventID = kInkerID;
	event.fCommand = kInkerCalibrationNeeded;
	TInkerEvent reply;
	memset(&reply, 0, sizeof(reply));
	ULong got;
	long err = gTheInkerPort->SendRPC(&got, &event, kInkerReplySize, &reply, sizeof(reply));
	return err == noErr && (long) (Long32) reply.fArg > 0;
}


// ROM 0x001412dc CheckTabletCalibration__Fv
// The calibration screen, when the tablet has not been calibrated or asks
// to be.
void
CheckTabletCalibration(void)
{
	if (!gInkerCalibrated || CheckTabletHWCalibration())
		FCalibrateTablet(RefVar(NILREF));
}


// ROM 0x0014132c FIsTabletCalibrationNeeded
// IsTabletCalibrationNeeded(): whether the tablet asks to be calibrated;
// nil when the inker has not been looked up yet.
static Ref
FIsTabletCalibrationNeeded(RefArg /*rcvr*/)
{
	Ref result = NILREF;
	if (gTheInkerPort != nil)
		result = CheckTabletHWCalibration() ? TRUEREF : NILREF;
	return result;
}


void
RegisterInkerNatives(void)
{
	RegisterNativeFunction("FGetCalibration__FRC6RefVar", (void*) FGetCalibration, 0);
	RegisterNativeFunction("FSetCalibration__FRC6RefVarT1", (void*) FSetCalibration, 1);
	RegisterNativeFunction("FIsTabletCalibrationNeeded", (void*) FIsTabletCalibrationNeeded, 0);
	RegisterNativeFunction("FCalibrateTablet__FRC6RefVar", (void*) FCalibrateTablet, 0);
}
