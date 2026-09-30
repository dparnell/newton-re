/*
	File:		hal/host/HostTabletDriver.cpp

	Contains:	The host's tablet driver (hal/host/HostTablet.h): a
				TTabletDriver (recognition/TabletDriver.h) registered as
				"TMainTabletDriver", the name TabInitialize looks for before
				it makes the MP2x00's resistive panel - so it stands where a
				machine's own driver would.  Host only (not in the ROM).

				Its panel is newton's window: the mouse held down is the pen
				on the glass, and the driver samples it as the MP2x00's does
				its ADC - every sampling interval (0xb400 ticks: 80 a
				second) while the pen is down, from an interrupt (a host
				interrupt source), each reading a raw pair of 12-bit numbers
				turned into the screen's coordinates by the calibration
				(Calibration: a scale and an offset each way, as
				TResistiveTablet::ConvertSample 0x0005b068 does it) and put
				in the tablet buffer; the pen going down puts a pen-down
				record first and the inker is woken, the pen going up a
				pen-up.  While the inker calibrates, the raw pair goes in
				instead.

				The panel reads eight to the pixel and sits square on the
				display, so its factory calibration (a scale of an eighth,
				no offset) is exact: a mouse is an accurate pen out of the
				box.  A test can put the panel askew (HostTabletSetSkew, or
				NEWTON_TABLET_SKEW="dx,dy,sx,sy": the raw reading is that of
				the point x*sx+dx, y*sy+dy) to show Align Pen correcting it.
				The panel turns with the window, so it always reads in the
				screen's own orientation.
*/

#include "HostTablet.h"
#include "TabletBuffer.h"
#include "TabletDriver.h"
#include "Inker.h"
#include "HostInterruptSources.h"
#include "hal/Timer.h"
#include "NewtonTime.h"
#include "OSErrors.h"
#include "Screen.h"
#include "Ports.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

PROTOCOL TMainTabletDriver : public TTabletDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TMainTabletDriver);

	TMainTabletDriver*	New();
	void			Delete();
	long			Init(const Rect& screen);
	void			WakeUp(void);
	long			ShutDown(void);
	long			TabletIdle(void);
	ULong			GetSampleRate(void);
	void			SetSampleRate(ULong rate);
	void			GetTabletCalibration(Calibration* calibration);
	void			SetTabletCalibration(const Calibration& calibration);
	void			SetDoingCalibration(UChar doing, ULong* orientation);
	void			GetTabletResolution(long* x, long* y);
	void			TabSetOrientation(long orientation);
	long			GetTabletState(void);
	long			GetFingerInputState(UChar* state);
	long			SetFingerInputState(UChar state);
	long			RecalibrateTabletAfterRotate(void);
	Boolean			TabletNeedsRecalibration(void);
	long			StartBypassTablet(void);
	long			StopBypassTablet(void);
	void			ReturnTabletToConsciousness(ULong a, ULong b, ULong c);

	ULong			Convert(long rawX, long rawY);		// a raw reading as a sample word
	void			Sample(void);						// the sampling interrupt

	Calibration		fCalibration;
	Boolean			fDoingCalibration;
	long			fState;
	long			fOrientation;
	ULong			fSampleRate;
	Rect			fScreen;
	Boolean			fPenInBuffer;			// a pen-down is in the buffer and its pen-up is not
	Int64			fNextSample;
};

PROTOCOL_IMPL_SOURCE_MACRO(TMainTabletDriver)
PROTOCOL_CLASSINFO(TMainTabletDriver, "TTabletDriver", "", 0, 0, nil)

static TMainTabletDriver*	gHostTabletDriver = nil;
static std::atomic<long>	gShutDowns(0);		// sleeps the driver has been shut down for

// the panel: where the pen is on the window (any thread writes, the
// sampling interrupt reads), and how the panel sits against the display
static std::atomic<bool>	gRawDown(false);
static std::atomic<long>	gRawX(0);			// pixels
static std::atomic<long>	gRawY(0);
static std::atomic<ULong>	gRawDowns(0);		// pen-downs so far (a tap quicker than a sample still counts)
static double				gSkewDX = 0, gSkewDY = 0, gSkewSX = 1, gSkewSY = 1;

// a scripted tap on the panel: down at (x, y) for so long, then up (the
// scripts' HostTabletRawTap, and the calibration's automatic taps)
static std::atomic<bool>	gTapPending(false);
static long					gTapX, gTapY;
static ULong				gTapMilliseconds;
static Int64				gTapUpAt;
static std::atomic<bool>	gTapDown(false);
static std::atomic<ULong>	gTapReads(0);		// the pen-down samples the calibration screen must read before it lifts (0: none)
static ULong				gTapReadsAtDown;
static Int64				gTapGiveUpAt;

// the calibration screen's targets (TInker::GetRawPoint tells them)
static std::atomic<bool>	gAutoCalibrate(false);
static std::atomic<long>	gTargetH(-1);
static std::atomic<long>	gTargetV(-1);


static ULong
Now(void)
{
	TTime now;
	GetClock(&now.time);
	return now.ConvertTo(kMacTicks) & 0x7fffffff;
}


// The panel's 12-bit reading of a point of the window: eight to the pixel,
// askew when a test has put it so.
static void
RawReading(long x, long y, long* rawX, long* rawY)
{
	*rawX = lround((x * gSkewSX + gSkewDX) * 8);
	*rawY = lround((y * gSkewSY + gSkewDY) * 8);
	if (*rawX < 0) *rawX = 0;
	if (*rawX > 0xfff) *rawX = 0xfff;
	if (*rawY < 0) *rawY = 0;
	if (*rawY > 0xfff) *rawY = 0xfff;
}


static Boolean
SampleDeadline(Int64* when)
{
	TMainTabletDriver* driver = gHostTabletDriver;
	if (driver == nil || driver->fState == kTabletStateShutDown)
		return false;
	if (!gRawDown.load() && !gTapPending.load() && !gTapDown.load() && !driver->fPenInBuffer)
		return false;
	*when = driver->fNextSample;
	return true;
}


static void
SampleDeliver(void)
{
	if (gHostTabletDriver != nil)
		gHostTabletDriver->Sample();
}


/*------------------------------------------------------------------------------
	T M a i n T a b l e t D r i v e r
------------------------------------------------------------------------------*/

TMainTabletDriver*
TMainTabletDriver::New()
{
	fCalibration.fXScale = 0x2000;			// the factory's: an eighth of a pixel a step, square on the display
	fCalibration.fYScale = 0x2000;
	fCalibration.fXOffset = 0;
	fCalibration.fYOffset = 0;
	fCalibration.fField10 = 1;
	fCalibration.fField11 = 1;
	fCalibration.fPad[0] = fCalibration.fPad[1] = 0;
	fDoingCalibration = false;
	fState = kTabletStateIdle;
	fOrientation = 0;
	fSampleRate = 0xb400;
	SetRect(&fScreen, 0, 0, 320, 480);
	fPenInBuffer = false;
	fNextSample.hi = fNextSample.lo = 0;
	const char* skew = getenv("NEWTON_TABLET_SKEW");
	double dx, dy, sx, sy;
	if (skew != nil && sscanf(skew, "%lf,%lf,%lf,%lf", &dx, &dy, &sx, &sy) == 4)
		HostTabletSetSkew(dx, dy, sx, sy);
	gHostTabletDriver = this;
	return this;
}


void
TMainTabletDriver::Delete()
{
	if (gHostTabletDriver == this)
		gHostTabletDriver = nil;
	HostUnregisterInterruptSource(SampleDeadline, SampleDeliver);
}


long
TMainTabletDriver::Init(const Rect& screen)
{
	fScreen = screen;
	fState = kTabletStateIdle;
	HostRegisterInterruptSource(SampleDeadline, SampleDeliver);
	return noErr;
}


void
TMainTabletDriver::WakeUp(void)
{
	if (fState == kTabletStateShutDown)
		fState = kTabletStateIdle;
}


// A stroke under way is ended (as TResistiveTablet's ErrorPenUp does) and
// the sampling stopped.
long
TMainTabletDriver::ShutDown(void)
{
	if (fState == kTabletStateShutDown)
		return kTabletStateShutDown;
	if (fPenInBuffer)
	{
		TBCInsertTabletSample(kTabletPenUp, Now());
		fPenInBuffer = false;
	}
	fState = kTabletStateShutDown;
	gShutDowns.fetch_add(1);
	return noErr;
}


long		TMainTabletDriver::TabletIdle(void)						{ return 0; }
ULong		TMainTabletDriver::GetSampleRate(void)					{ return fSampleRate; }
void		TMainTabletDriver::SetSampleRate(ULong rate)			{ fSampleRate = rate; }
void		TMainTabletDriver::GetTabletCalibration(Calibration* c)	{ *c = fCalibration; }
void		TMainTabletDriver::SetTabletCalibration(const Calibration& c)	{ fCalibration = c; }
void		TMainTabletDriver::TabSetOrientation(long orientation)	{ fOrientation = orientation; }
long		TMainTabletDriver::GetTabletState(void)					{ return fState; }
long		TMainTabletDriver::GetFingerInputState(UChar*)			{ return kError_Call_Not_Implemented; }
long		TMainTabletDriver::SetFingerInputState(UChar)			{ return kError_Call_Not_Implemented; }
long		TMainTabletDriver::RecalibrateTabletAfterRotate(void)	{ return 0; }
void		TMainTabletDriver::ReturnTabletToConsciousness(ULong, ULong, ULong)	{ }

// The panel keeps its factory calibration good: it never asks to be
// calibrated again of itself (the MP2x00's asks when the calibration its
// kernel keeps across a restart is not marked good).
Boolean		TMainTabletDriver::TabletNeedsRecalibration(void)		{ return false; }

// Eight readings to the pixel at 100 dots an inch, as the MP2x00's 800.
void
TMainTabletDriver::GetTabletResolution(long* x, long* y)
{
	*x = 800 << 16;
	*y = 800 << 16;
}


// Raw readings while doing; ==> the orientation the panel reads in, which
// is always the screen's own (it turns with the window) - so the
// calibration screen takes the panel's x to the screen's h and its y to v.
void
TMainTabletDriver::SetDoingCalibration(UChar doing, ULong* orientation)
{
	fDoingCalibration = doing;
	if (orientation != nil)
	{
		long current = 0;
		GetGrafInfo(kGrafInfoOrientation, &current);
		*orientation = (ULong) current;
	}
}


// Refused while the pen is down (TResistiveTablet::StartBypassTablet
// 0x0005ad04: from idle, or bypassed already).
long
TMainTabletDriver::StartBypassTablet(void)
{
	if (fState != kTabletStateIdle && fState != kTabletStateBypassed)
		return -1;
	fState = kTabletStateBypassed;
	return 0;
}


long
TMainTabletDriver::StopBypassTablet(void)
{
	if (fState != kTabletStateBypassed)
		return -1;
	fState = kTabletStateIdle;
	return 0;
}


// A raw reading as a sample word, as TResistiveTablet::ConvertSample does
// it in the screen's own orientation: while calibrating, the raw pair
// itself (x in the high field, each 12 bits); otherwise x and y worked
// out by the calibration, x kept on the screen (y only kept off its top -
// the ROM's), and packed in eighths of a pixel, the pressure 4.
ULong
TMainTabletDriver::Convert(long rawX, long rawY)
{
	ULong raw16X = (ULong) rawX << 4, raw16Y = (ULong) rawY << 4;
	if (fDoingCalibration)
		return (raw16Y & 0xfff0) | ((raw16X & 0xfff0) << 14) | 4;
	Long32 x = (Long32) ((ULong32) fCalibration.fXScale * (ULong32) (raw16X >> 4) + (ULong32) fCalibration.fXOffset);
	Long32 y = (Long32) ((ULong32) fCalibration.fYScale * (ULong32) (raw16Y >> 4) + (ULong32) fCalibration.fYOffset);
	Rect screen = qdGlobals.fScreenBits.bounds;
	Long32 left = (Long32) screen.left << 16, top = (Long32) screen.top << 16;
	Long32 right = ((Long32) screen.right - 1) << 16;
	if (x < left)
		x = left;
	else if (x > right)
		x = right;
	if (y < top)
		y = top;
	return (ULong32) (((((ULong32) x & 0x7ffe000) << 5) | (((ULong32) y & 0x7ffe0ff) >> 9)) | 4);
}


// The sampling interrupt: the pen's state read off the panel, a pen-down
// or pen-up put in the buffer as it changes (the inker woken for each),
// and a sample while it is down.
void
TMainTabletDriver::Sample(void)
{
	Int64 now;
	GetClock(&now);
	TTime interval(fSampleRate, kSystemTimeUnits);
	fNextSample = now;
	CompAdd(&interval.time, &fNextSample);

	// a scripted tap starts, or ends
	if (gTapPending.exchange(false))
	{
		gRawX.store(gTapX);
		gRawY.store(gTapY);
		gRawDown.store(true);
		gRawDowns.fetch_add(1);
		gTapDown.store(true);
		gTapReadsAtDown = TBCPolledPenDownSamples();
		TTime hold(gTapMilliseconds, kMilliseconds);
		gTapUpAt = now;
		CompAdd(&hold.time, &gTapUpAt);
		TTime giveUp(10, kSeconds);
		gTapGiveUpAt = now;
		CompAdd(&giveUp.time, &gTapGiveUpAt);
	}
	else if (gTapDown.load() && CompCompare(&now, &gTapUpAt) >= 0
			 && (TBCPolledPenDownSamples() - gTapReadsAtDown >= gTapReads.load()
				 || CompCompare(&now, &gTapGiveUpAt) >= 0))		// (a calibration that stopped reading)
	{
		gTapDown.store(false);
		gRawDown.store(false);
	}

	if (fState == kTabletStateBypassed || fState == kTabletStateShutDown)
		return;
	Boolean down = gRawDown.load();
	if (!down && gRawDowns.load() != 0 && !fPenInBuffer)
		down = true;			// (a tap that came and went between two samples still makes a stroke)
	if (down && !fPenInBuffer)
	{
		TBCInsertTabletSample(kTabletPenDown, Now());
		fPenInBuffer = true;
		fState = kTabletStatePenDown;
		TBCWakeUpInkerFromInterrupt(0);
	}
	if (fPenInBuffer)
	{
		long rawX, rawY;
		RawReading(gRawX.load(), gRawY.load(), &rawX, &rawY);
		TBCInsertTabletSample(Convert(rawX, rawY), 0);
	}
	if (!gRawDown.load() && fPenInBuffer)
	{
		gRawDowns.store(0);
		TBCInsertTabletSample(kTabletPenUp, Now());
		fPenInBuffer = false;
		fState = kTabletStateIdle;
		TBCWakeUpInkerFromInterrupt(0);
	}
}


/*------------------------------------------------------------------------------
	T h e   p a n e l
------------------------------------------------------------------------------*/

void
HostTabletRegisterDriver(void)
{
	if (gProtocolRegistry != nil)		// (no registry without the OS: HostTabletInit makes the driver itself)
		TMainTabletDriver::ClassInfo()->Register();
	gInkerCalibrationTargetHook = HostTabletCalibrationTarget;
}


TTabletDriver*
HostTabletMakeDriver(void)
{
	TMainTabletDriver* driver = new TMainTabletDriver;
	return driver != nil ? driver->New() : nil;
}


void
HostTabletRawPenDown(long x, long y)
{
	gRawX.store(x);
	gRawY.store(y);
	gRawDown.store(true);
	gRawDowns.fetch_add(1);
}


void
HostTabletRawPenMove(long x, long y)
{
	gRawX.store(x);
	gRawY.store(y);
}


void
HostTabletRawPenUp(void)
{
	gRawDown.store(false);
}


// a tap: down at (x, y) for at least so long, and until the calibration
// screen's polls have taken reads pen-down samples (0: no such wait)
static void
StartTap(long x, long y, ULong milliseconds, ULong reads)
{
	gTapX = x;
	gTapY = y;
	gTapMilliseconds = milliseconds;
	gTapReads.store(reads);
	gTapPending.store(true);
	if (gHostTabletDriver != nil)
		GetClock(&gHostTabletDriver->fNextSample);		// (due at once)
}


void
HostTabletRawTap(long x, long y, ULong milliseconds)
{
	StartTap(x, y, milliseconds, 0);
}


void
HostTabletSetSkew(double dx, double dy, double sx, double sy)
{
	gSkewDX = dx;
	gSkewDY = dy;
	gSkewSX = sx;
	gSkewSY = sy;
}


void
HostTabletAutoCalibrate(Boolean on)
{
	gAutoCalibrate.store(on);
}


// TInker::GetRawPoint's target (Inker.h's gInkerCalibrationTargetHook):
// kept for a script to ask, and tapped at once when the calibration is
// automatic - held until the target has had the twenty readings in a row it
// wants (a few more for good measure), and at least half a second.  A tap
// held for a fixed time alone was sometimes read fewer than twenty times
// when the inker was slow to run, and the target then waited for a pen
// that never came until the calibration timed out.
const ULong kCalibrationTapReads = 24;

void
HostTabletCalibrationTarget(short h, short v)
{
	gTargetH.store(h);
	gTargetV.store(v);
	if (gAutoCalibrate.load())
		StartTap(h, v, 500, kCalibrationTapReads);
}


Boolean
HostTabletCalibrationTargetAt(long* h, long* v)
{
	if (gTargetH.load() < 0)
		return false;
	*h = gTargetH.load();
	*v = gTargetV.load();
	return true;
}


Boolean
HostTabletDriverBypassed(void)
{
	return gHostTabletDriver != nil && gHostTabletDriver->fState == kTabletStateBypassed;
}


long
HostTabletDriverState(void)
{
	return gHostTabletDriver != nil ? gHostTabletDriver->fState : -1;
}


long
HostTabletShutDowns(void)
{
	return gShutDowns.load();
}
