/*
	File:		hal/host/HostTablet.h

	Contains:	The host's tablet.  Two ways into the tablet buffer
				(recognition/TabletBuffer.h) that the inker reads:

				  - the panel: the host's tablet driver (HostTabletDriver.cpp),
				    a TTabletDriver registered as "TMainTabletDriver", samples
				    the pen on the window as the MP2x00's samples its ADC -
				    raw 12-bit readings, eight to the pixel, turned into the
				    screen's coordinates by the calibration (so Align Pen
				    works), put in from its interrupt every sampling interval
				    while the pen is down.  The window's mouse is this pen
				    (HostTabletRawPenDown/Move/Up), and so is a script's
				    HostTabletRawTap.  Out of the box the panel sits square
				    on the display and the factory calibration is exact; a
				    test can put it askew (HostTabletSetSkew, or
				    NEWTON_TABLET_SKEW="dx,dy,sx,sy") to see the calibration
				    correct it, and have the calibration screen's targets
				    tapped for it (HostTabletAutoCalibrate).
				  - the test pen: records in the screen's coordinates put in
				    straight away (HostTabletPenDown/Move/Up), or queued
				    (HostTabletQueue...) to be fed one at a time - a tick of
				    a Wait each when the OS is not running (HostTabletInit
				    installs the wait hook, os600/user/UserBoot.h
				    gHostWaitHook, which stands in for the inker there), an
				    idle of the inker each when it is and the pen is paced.
*/

#ifndef __HAL_HOST_TABLET_H
#define __HAL_HOST_TABLET_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	HostTabletInit(void);								// the tablet buffer emptied, the wait hook installed, the driver registered
void	HostTabletPenDown(long x, long y, ULong time);		// a pen-down record (time 0: now), then a sample at the point
void	HostTabletPenMove(long x, long y, ULong pressure = 3);	// a sample (pixels; the pressure 0-7)
void	HostTabletPenUp(ULong time);						// a pen-up record (time 0: now)
ULong	HostTabletSample(long x, long y, ULong pressure = 3);	// the sample word for a point
Boolean	HostTabletBypassed(void);							// the tablet bypassed (the journal playing): the window's pen is ignored

// the panel (HostTabletDriver.cpp)
class TTabletDriver;
void	HostTabletRegisterDriver(void);						// "TMainTabletDriver" registered for TabInitialize to find
TTabletDriver*	HostTabletMakeDriver(void);					// ... or one made (no OS)
void	HostTabletRawPenDown(long x, long y);				// the pen on the window (pixels; any thread)
void	HostTabletRawPenMove(long x, long y);
void	HostTabletRawPenUp(void);
void	HostTabletRawTap(long x, long y, ULong milliseconds);	// down at (x, y) for so long, then up
void	HostTabletSetSkew(double dx, double dy, double sx, double sy);	// the panel reads the point (x*sx+dx, y*sy+dy)
void	HostTabletAutoCalibrate(Boolean on);				// the calibration screen's targets tapped as they appear
void	HostTabletCalibrationTarget(short h, short v);		// (Inker.h's gInkerCalibrationTargetHook)
Boolean	HostTabletCalibrationTargetAt(long* h, long* v);	// the last target shown; ==> whether there has been one
Boolean	HostTabletDriverBypassed(void);
long	HostTabletDriverState(void);						// a kTabletState..., or -1 with no driver
long	HostTabletShutDowns(void);							// how many times the driver has been shut down (the machine's sleeps)

// queued records, fed one per tick of a Wait (or by HostTabletPump)
void	HostTabletQueuePenDown(long x, long y, ULong time);
void	HostTabletQueuePenMove(long x, long y, ULong pressure = 3);
void	HostTabletQueuePenUp(ULong time);
void	HostTabletQueueNothing(void);						// a tick with no record
Boolean	HostTabletPump(void);								// the next queued record fed and the strokes read; ==> whether there was one
long	HostTabletQueued(void);								// records still queued
void	HostTabletSetPaced(Boolean paced);					// with the inker running: queued records fed an idle at a time rather than at once
void	HostTabletWait(ULong ticks);						// the wait hook: ticks records pumped, then the strokes read

#endif	/* __HAL_HOST_TABLET_H */
