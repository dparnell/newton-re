/*
	File:		hal/host/Interrupts.cpp

	Contains:	Interrupt control for a host build.  There is no interrupt
				controller; the calls record what the kernel asked for so tests
				(and, later, a host scheduler thread) can act on it.
*/

#include "hal/Interrupts.h"

Boolean	gHostInterruptEnabled = false;		// state of the (only) interrupt object
ULong	gHostTimeSliceAlarm = 0;			// last alarm programmed, in ticks

extern "C" void
DisableInterrupt(InterruptObject* /*interrupt*/)
{
	gHostInterruptEnabled = false;
}

extern "C" void
QuickEnableInterrupt(InterruptObject* /*interrupt*/)
{
	gHostInterruptEnabled = true;
}

extern "C" void
SetTimeSliceAlarm(ULong ticksFromNow)
{
	gHostTimeSliceAlarm = ticksFromNow;
}
