/*
	File:		hal/host/HostTablet.h

	Contains:	The host's tablet: where the ROM's tablet driver
				(TResistiveTablet, NOT YET RECONSTRUCTED) samples the pen
				from its ADC interrupt and puts the records into the tablet
				buffer (recognition/TabletBuffer.h), the host puts them in
				from its window or a test - a pen-down at a time, samples
				(pixel coordinates, a pressure), a pen-up at a time - either
				straight away (HostTabletPenDown/Move/Up) or queued
				(HostTabletQueue...) to be fed in one record per tick as the
				OS waits: HostTabletInit installs the host's wait hook
				(os600/user/UserBoot.h gHostWaitHook), which stands in for
				the inker task - each tick of a Wait feeds a queued record
				and runs the stroke queue's StrokeTime, so the tracking
				loops of the views (TrackHilite, DoCaretClick), which wait a
				tick between looks at the stroke, see it grow.  With the OS
				running the waits are the kernel's, so an inker task of the
				host's (HostInkerStart) reads the buffer every tick instead.
*/

#ifndef __HAL_HOST_TABLET_H
#define __HAL_HOST_TABLET_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	HostTabletInit(void);								// the tablet buffer emptied, the wait hook installed
void	HostTabletPenDown(long x, long y, ULong time);		// a pen-down record (time 0: now), then a sample at the point
void	HostTabletPenMove(long x, long y, ULong pressure = 3);	// a sample (pixels; the pressure 0-7)
void	HostTabletPenUp(ULong time);						// a pen-up record (time 0: now)
ULong	HostTabletSample(long x, long y, ULong pressure = 3);	// the sample word for a point

// queued records, fed one per tick of a Wait (or by HostTabletPump)
void	HostTabletQueuePenDown(long x, long y, ULong time);
void	HostTabletQueuePenMove(long x, long y, ULong pressure = 3);
void	HostTabletQueuePenUp(ULong time);
void	HostTabletQueueNothing(void);						// a tick with no record
Boolean	HostTabletPump(void);								// the next queued record fed and the strokes read; ==> whether there was one
long	HostTabletQueued(void);								// records still queued
void	HostTabletWait(ULong ticks);						// the wait hook: ticks records pumped, then the strokes read

// the inker's stand-in when the OS runs: a task ('inkr) that reads the
// tablet buffer into the stroke queue every tick, as the ROM's inker task
// does (the wait hook cannot: a task's Wait sleeps in the kernel)
Boolean	HostInkerStart(void);								// the task started (none when the OS is not running); ==> whether it was
void	HostInkerStop(void);								// the task told to end

#endif	/* __HAL_HOST_TABLET_H */
