/*
	File:		hal/host/System.cpp

	Contains:	Whole-machine control for a host build: a reset is recorded (and
				reported) rather than performed, so tests can observe it.
*/

#include "hal/System.h"

#include <stdio.h>

ULong	gHostResetCount = 0;
Boolean	gHostPoweredOff = false;

extern "C" NewtonErr
Reset(void)
{
	gHostResetCount++;
	fprintf(stderr, "[host] machine reset requested\n");
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
