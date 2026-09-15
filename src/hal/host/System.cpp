/*
	File:		hal/host/System.cpp

	Contains:	Whole-machine control for a host build: a reset is recorded (and
				reported) rather than performed, so tests can observe it.
*/

#include "hal/System.h"

#include <stdio.h>

ULong	gHostResetCount = 0;
void	(*gHostResetHook)(void) = nil;
Boolean	gHostPoweredOff = false;

extern "C" NewtonErr
Reset(void)
{
	gHostResetCount++;
	fprintf(stderr, "[host] machine reset requested\n");
	if (gHostResetHook != nil)
		gHostResetHook();
	return 0;
}

extern "C" void
DisableAllInterrupts(void)
{
}

extern "C" void
IOPowerOffAll(void)
{
	gHostPoweredOff = true;
	fprintf(stderr, "[host] power off requested\n");
}

// Every call on the host takes the user-mode path (the system-call stubs),
// so the dual-mode ROM routines exercise it.
extern "C" Boolean
IsSuperMode(void)
{
	return false;
}

// A MessagePad 2100 has 4 MB of DRAM.
extern "C" ULong
GetRamSize(void)
{
	return 4 * 1024 * 1024;
}

// The host runs everything as a user-mode task.
extern "C" ULong
GetCPUMode(void)
{
	return 0x10;
}
