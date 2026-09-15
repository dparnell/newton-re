/*
	File:		hal/Interrupts.h

	Contains:	Interrupt sources and the scheduler's time-slice timer, as the
				kernel sees them.  An InterruptObject describes one interrupt
				source registered with the interrupt manager (the DDK only
				forward-declares it, CardSocket.h); the kernel keeps the
				scheduler's in gSchedulerIntObj.

	ROM:		DisableInterrupt (jump table 0x01bf8c4c), QuickEnableInterrupt
				(0x01c06300); the time slice is programmed straight into the
				Voyager timer by StartScheduler 0x001ce87c.
*/

#ifndef __HAL_INTERRUPTS_H
#define __HAL_INTERRUPTS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

struct InterruptObject;

// 0x12000 ticks of the 3.6864 MHz timer = 20 ms (kMilliseconds in NewtonTime.h is 3686 ticks)
const ULong kSchedulerTimeSlice = 0x12000;

extern "C" {
void	DisableInterrupt(InterruptObject* interrupt);
void	QuickEnableInterrupt(InterruptObject* interrupt);
void	SetTimeSliceAlarm(ULong ticksFromNow);		// the timer interrupt that pre-empts the running task
void	HInitInterrupts(void);						// boot: the interrupt controller (ROM 0x000e6e0c)
void	InitInterruptTables(void);					// boot: the handler tables (ROM 0x000e7ffc)
}

#endif	/* __HAL_INTERRUPTS_H */
