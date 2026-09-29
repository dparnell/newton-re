/*
	File:		hal/FIQTimer.cpp

	Contains:	TFIQTimer (FIQTimer.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "FIQTimer.h"
#include "NewtErrors.h"

TFIQTimer*	gFIQTimer = nil;			// ROM 0x0c100d28 gFIQTimer


// ROM 0x000b1a5c GetFIQTimerObject__Fv
TFIQTimer*
GetFIQTimerObject(void)
{
	return gFIQTimer;
}


// The match register set for the shortest active timer, and armed (again,
// if the counter got past it meanwhile).  (Inline in SetFIQTimer,
// SetFIQTimerForShortestDelay and FIQTimerInterrupt.)
static inline void
ArmForShortest(TFIQTimer* self, FIQTimer* timers, ULong* lastCount, Boolean* armed)
{
	FIQTimer* shortest = nil;
	ULong delay = 0xffffffff;
	for (long i = 0; i < kFIQTimers; i++)
		if (timers[i].fProc != nil && timers[i].fActive == 1 && timers[i].fDelay < delay)
		{
			shortest = &timers[i];
			delay = timers[i].fDelay;
		}
	if (shortest == nil)
		return;
	ULong match;
	do
	{
		*lastCount = FIQTimerCounter();
		match = *lastCount + delay;
		FIQTimerSetMatch(match);
		FIQTimerEnable();
	} while (delay < match - FIQTimerCounter());
	*armed = true;
}


// ROM 0x000b1acc SetFIQTimer__9TFIQTimerFP8FIQTimerUlT2
// The timer started (never shorter than kFIQTimerMinDelay); the others
// brought up to date first, and the match set for the shortest unless the
// timers are being serviced (which sets it when it is done).
void
TFIQTimer::SetFIQTimer(FIQTimer* timer, ULong delay, ULong arg)
{
	if (delay < kFIQTimerMinDelay)
		delay = kFIQTimerMinDelay;
	if (fArmed)
	{
		FIQTimerDisable();
		fArmed = false;
		ServiceFIQTimers();
	}
	timer->fArg = arg;
	timer->fDelay = delay;
	timer->fActive = 1;
	if (fServicing)
		return;
	ArmForShortest(this, fTimers, &fLastCount, &fArmed);
}


// ROM 0x000b1b3c ResetFIQTimer__9TFIQTimerFP8FIQTimer
void
TFIQTimer::ResetFIQTimer(FIQTimer* timer)
{
	timer->fActive = 0;
}


// ROM 0x000b1b48 InitFIQTimer__9TFIQTimerFP8FIQTimer
void
TFIQTimer::InitFIQTimer(FIQTimer* timer)
{
	timer->fProc = nil;
	timer->fRefCon = nil;
	timer->fArg = 0;
	timer->fDelay = 0;
	timer->fActive = 0;
}


// ROM 0x000b1b64 InitializeFIQTimers__9TFIQTimerFv
void
TFIQTimer::InitializeFIQTimers(void)
{
	for (long i = 0; i < kFIQTimers; i++)
		InitFIQTimer(&fTimers[i]);
	fArmed = false;
	fServicing = false;
	fLastCount = 0;
}


// ROM 0x000b1bac DecrementFIQTimers__9TFIQTimerFUl
// Each active timer's delay less the time gone by; one within
// kFIQTimerMinDelay of its end is due (its delay nought).  ==> the last of
// those found, or nil.
FIQTimer*
TFIQTimer::DecrementFIQTimers(ULong elapsed)
{
	FIQTimer* due = nil;
	for (long i = 0; i < kFIQTimers; i++)
		if (fTimers[i].fProc != nil && fTimers[i].fActive == 1)
		{
			if (fTimers[i].fDelay < elapsed + kFIQTimerMinDelay)
			{
				fTimers[i].fDelay = 0;
				due = &fTimers[i];
			}
			else
				fTimers[i].fDelay -= elapsed;
		}
	return due;
}


// ROM 0x000b1c0c SetFIQTimerForShortestDelay__9TFIQTimerFv
void
TFIQTimer::SetFIQTimerForShortestDelay(void)
{
	ArmForShortest(this, fTimers, &fLastCount, &fArmed);
}


// ROM 0x000b1cac ServiceFIQTimers__9TFIQTimerFv
// The timers brought up to date and the due ones run, one at a time, until
// none is due.
void
TFIQTimer::ServiceFIQTimers(void)
{
	fServicing = true;
	for (;;)
	{
		ULong now = FIQTimerCounter();
		ULong last = fLastCount;
		fLastCount = now;
		FIQTimer* due = DecrementFIQTimers(now - last);
		if (due == nil)
			break;
		due->fActive = 0;
		due->fProc(due->fRefCon, due->fArg);
	}
	fServicing = false;
}


// ROM 0x000b1d0c FIQTimerInterrupt__9TFIQTimerFv
void
TFIQTimer::FIQTimerInterrupt(void)
{
	FIQTimerDisable();
	fArmed = false;
	ServiceFIQTimers();
	ArmForShortest(this, fTimers, &fLastCount, &fArmed);
}


// ROM 0x000b1d90 __ct__9TFIQTimerFPUl
TFIQTimer::TFIQTimer(ULong* counterRegister)
{
	fCounter = counterRegister;
	InitializeFIQTimers();
	fInitialised = false;
}


// ROM 0x000b1dd4 Init__9TFIQTimerFv
// DEVIATION (hardware): the ROM wires the object and registers the match
// interrupt (8) at FIQ priority; the port's FIQTimerInstallHardware does
// what stands for that.
NewtonErr
TFIQTimer::Init(void)
{
	FIQTimerInstallHardware(this);
	gFIQTimer = this;
	fInitialised = true;
	return noErr;
}


// ROM 0x000b1e5c AcquireFIQTimer__9TFIQTimerFPFPvUl_vPv
FIQTimer*
TFIQTimer::AcquireFIQTimer(FIQTimerProcPtr proc, void* refCon)
{
	for (long i = 0; i < kFIQTimers; i++)
		if (fTimers[i].fProc == nil)
		{
			fTimers[i].fProc = proc;
			fTimers[i].fRefCon = refCon;
			return &fTimers[i];
		}
	return nil;
}


// ROM 0x000b1ea8 ReleaseFIQTimer__9TFIQTimerFP8FIQTimer
void
TFIQTimer::ReleaseFIQTimer(FIQTimer* timer)
{
	for (long i = 0; i < kFIQTimers; i++)
		if (&fTimers[i] == timer)
		{
			InitFIQTimer(timer);
			return;
		}
}


// ROM 0x000b1ed0 ReleaseFIQTimers__9TFIQTimerFPv
// Every timer of refCon's.
void
TFIQTimer::ReleaseFIQTimers(void* refCon)
{
	for (long i = 0; i < kFIQTimers; i++)
		if (fTimers[i].fRefCon == refCon)
			InitFIQTimer(&fTimers[i]);
}


// The part of InitializeCommHardware (ROM 0x000ea0b4) that makes the fast
// timers: over the timer's counter (0x0f181800 in the ROM; the port's
// FIQTimerCounter here).
NewtonErr
InitFIQTimer(void)
{
	if (gFIQTimer != nil)
		return noErr;
	TFIQTimer* timer = new TFIQTimer(nil);
	if (timer == nil)
		return -10007;
	return timer->Init();
}
