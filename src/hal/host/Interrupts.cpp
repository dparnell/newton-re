/*
	File:		hal/host/Interrupts.cpp

	Contains:	Interrupt control for a host build.  There is no interrupt
				controller; the calls record what the kernel asked for, and the
				task runtime (os600/kernel/host/TaskRuntime.cpp) fires the
				time-slice handler when the deadline passes.
*/

#include "hal/Interrupts.h"
#include "hal/Timer.h"
#include "hal/host/Host.h"
#include "CompMath.h"

Boolean	gHostInterruptEnabled = false;		// state of the (only) interrupt object, the scheduler's
Boolean	gHostTimeSliceArmed = false;
Int64	gHostTimeSliceDeadline = {0, 0};

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
	Int64 delta = {0, ticksFromNow};
	GetClock(&gHostTimeSliceDeadline);
	CompAdd(&delta, &gHostTimeSliceDeadline);
	gHostTimeSliceArmed = true;
}

extern "C" void
HInitInterrupts(void)
{
}

extern "C" void
InitInterruptTables(void)
{
}
