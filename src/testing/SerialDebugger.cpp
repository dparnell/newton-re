/*
	File:		testing/SerialDebugger.cpp

	Contains:	InitSerialDebugging and PreInitSerialDebugging - a script
				turning on the ROM's serial debugger, the remote debugging
				protocol a development Newton talks to a desktop debugger
				over the serial port - and the debugger's flag,
				gWantSerialDebugging, which says whether the package manager
				tells the debugger where packages went.

	The natives are reconstructed; the debugger itself is the kernel's and
	the hardware's (PreXInitSerialDebugger 0x00199ef8, InitSerialDebugging,
	TerminateSerialDebugging 0x0019a548, and a word read from the debug
	card at 0xe0000000), which the host has not got.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "OSErrors.h"

#include <string.h>


// whether the serial debugger is wanted (ROM: set by InitSerialDebugging;
// the package manager's RegisterPackageWithDebugger does nothing without it)
Boolean	gWantSerialDebugging = false;


/*------------------------------------------------------------------------------
	The debugger itself.

	DEVIATION: there is no serial debugger on the host - no debug card, no
	serial link to a desktop debugger - so nothing can be started: the two
	that would start it answer kError_Call_Not_Implemented, stopping one
	that never started is done, and the debug card's word reads as nought.
------------------------------------------------------------------------------*/

// ROM 0x00199ef8 PreXInitSerialDebugger
static NewtonErr
PreXInitSerialDebugger(ULong /*port*/, ULong /*speed*/, ULong /*options*/)
{
	return kError_Call_Not_Implemented;
}


// ROM 0x0019a064 InitSerialDebugging
static NewtonErr
InitSerialDebugging(ULong /*arg1*/, ULong /*arg2*/, ULong /*port*/, long /*mode*/)
{
	return kError_Call_Not_Implemented;
}


// ROM 0x0019a548 TerminateSerialDebugging
static NewtonErr
TerminateSerialDebugging(void)
{
	return noErr;
}


// The word the ROM reads from the debug card (0xe0000000).
static long
DebugCardWord(void)
{
	return 0;
}


// A port's name as the four characters the debugger is given ('extr').
static ULong
PortName(RefArg name)
{
	unsigned char chars[8];				// (the ROM's buffer: two words)
	memset(chars, 0, sizeof(chars));
	ConvertFromUnicode(GetCString(name), chars, kMacRomanEncoding, 4);
	return ((ULong) chars[0] << 24) | ((ULong) chars[1] << 16) | ((ULong) chars[2] << 8) | chars[3];
}


// ROM 0x0019a6d0 FInitSerialDebugging
// InitSerialDebugging(port, mode): the serial debugger over the port named
// by its four characters.  Mode 0 stops it; modes 1 to 9 leave the flag
// clear, 1 starting the early debugger at 57600 (0xe100) and 2 reading the
// debug card (3 to 9 doing nothing); any other mode starts the full
// debugger in that mode and keeps the flag set if it started.  ==> the
// error, nought when it went.
static Ref
FInitSerialDebugging(RefArg /*rcvr*/, RefArg port, RefArg mode)
{
	long err = 0;
	Long how = RINT(mode);
	gWantSerialDebugging = true;
	ULong name = PortName(port);
	if (how < 1 || how > 9)
	{
		if (how == 0)
			err = TerminateSerialDebugging();
		else
		{
			err = InitSerialDebugging(1, 1, name, how);
			if (err != noErr)
				gWantSerialDebugging = false;
		}
	}
	else
	{
		gWantSerialDebugging = false;
		if (how == 1)
			err = PreXInitSerialDebugger(name, 0xe100, 0);
		else if (how == 2)
			err = DebugCardWord();
	}
	return MAKEINT(err);
}


// ROM 0x0019a7ac FPreInitSerialDebugging
// PreInitSerialDebugging(port, speed, options): the early debugger started
// over the port; options -1 reads the debug card instead, and 0 or less does
// nothing.  ==> the error.
static Ref
FPreInitSerialDebugging(RefArg /*rcvr*/, RefArg port, RefArg speed, RefArg options)
{
	long err = 0;
	Long rate = RINT(speed);
	Long how = RINT(options);
	ULong name = PortName(port);
	if (how >= 1)
		err = PreXInitSerialDebugger(name, (ULong) rate, (ULong) how);
	else if (how == -1)
		err = DebugCardWord();
	return MAKEINT(err);
}


void
RegisterSerialDebuggerNatives(void)
{
	RegisterNativeFunction("FInitSerialDebugging", (void*) FInitSerialDebugging, 2);
	RegisterNativeFunction("FPreInitSerialDebugging", (void*) FPreInitSerialDebugging, 3);
}
