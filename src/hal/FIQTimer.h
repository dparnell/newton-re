/*
	File:		hal/FIQTimer.h

	Contains:	TFIQTimer, the machine's fast timers: four software timers
				(each a handler, its argument, and a delay in hardware time
				units) multiplexed over one match register of the free-
				running timer, whose interrupt runs the ones that have come
				due.  The serial tools use one for the carrier-detect timer.
				gFIQTimer is the one the machine makes at boot
				(InitializeCommHardware, 0x000ea0b4) and GetFIQTimerObject
				answers it.

				The hardware - the timer's counter (0x0f181800), the match
				register (0x0f182000) and the match interrupt - is the
				host's (hal/host/HostFIQTimer.cpp: FIQTimerCounter,
				FIQTimerSetMatch, FIQTimerEnable/Disable, and an interrupt
				source that runs FIQTimerInterrupt when the match comes).

	Reconstructed from the MP2x00 US ROM (0x000b1a5c-0x000b1f00); each
	function cites its origin.
*/

#ifndef __HAL_FIQTIMER_H
#define __HAL_FIQTIMER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

typedef void (*FIQTimerProcPtr)(void* refCon, ULong arg);

struct FIQTimer
{
	FIQTimerProcPtr		fProc;			// +0x00  (nil: free)
	void*				fRefCon;		// +0x04
	ULong				fArg;			// +0x08
	ULong				fDelay;			// +0x0c  hardware time units left
	ULong				fActive;		// +0x10  1: counting down
};

#define kFIQTimers		4
#define kFIQTimerMinDelay	0xb4		// the shortest delay (and the slack DecrementFIQTimers allows)

class TFIQTimer
{
public:
					TFIQTimer(ULong* counterRegister);
	NewtonErr		Init(void);

	FIQTimer*		AcquireFIQTimer(FIQTimerProcPtr proc, void* refCon);
	void			ReleaseFIQTimer(FIQTimer* timer);
	void			ReleaseFIQTimers(void* refCon);
	void			SetFIQTimer(FIQTimer* timer, ULong delay, ULong arg);
	void			ResetFIQTimer(FIQTimer* timer);

	void			FIQTimerInterrupt(void);

private:
	void			InitFIQTimer(FIQTimer* timer);
	void			InitializeFIQTimers(void);
	FIQTimer*		DecrementFIQTimers(ULong elapsed);
	void			SetFIQTimerForShortestDelay(void);
	void			ServiceFIQTimers(void);

	ULong			fLastCount;			// +0x00  the counter when the timers were last decremented
	ULong*			fCounter;			// +0x04  (the host's: see FIQTimerCounter)
	FIQTimer		fTimers[kFIQTimers];	// +0x08
	Boolean			fInitialised;		// +0x58
	Boolean			fArmed;				// +0x59  the match interrupt is on
	Boolean			fServicing;			// +0x5a
};

TFIQTimer*	GetFIQTimerObject(void);
extern TFIQTimer*	gFIQTimer;

// the timer made and initialised (the part of InitializeCommHardware that does)
NewtonErr	InitFIQTimer(void);

// the hardware, provided by the port (hal/host/HostFIQTimer.cpp)
ULong		FIQTimerCounter(void);			// the free-running counter
void		FIQTimerSetMatch(ULong value);	// the match register
void		FIQTimerEnable(void);			// the match interrupt on
void		FIQTimerDisable(void);			//  and off
void		FIQTimerInstallHardware(TFIQTimer* timer);	// the match interrupt runs timer's FIQTimerInterrupt

#endif
