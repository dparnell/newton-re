/*
	File:		recognition/TabletDriver.cpp

	Contains:	The tablet driver's protocol glue and the calls made of the
				driver in use (recognition/TabletDriver.h).

				A host without the OS (a unit test, newtonscript) may have no
				driver at all; the calls then answer what the MP2x00's own
				would of an idle pen (DEVIATION: the ROM always has one).

				Reconstructed from the MP2x00 US ROM (0x00385b8c-0x00385cb8,
				0x0025065c-0x00250860); each function cites its origin.
*/

#include "TabletDriver.h"
#include "TabletBuffer.h"
#include "GestaltSources.h"
#include "hal/Atomic.h"
#include "OSErrors.h"

TTabletDriver*	gTabletDriver = nil;		// ROM 0x0c104d34 gTabletDriver


/*------------------------------------------------------------------------------
	T T a b l e t D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x00385b8c New__13TTabletDriverSFPc
TTabletDriver*
TTabletDriver::New(char* implementation)
{
	TTabletDriver* p = (TTabletDriver*) AllocInstanceByName("TTabletDriver", implementation);
	return p != nil ? (TTabletDriver*) p->GlueNew() : nil;
}


// ROM 0x00385bb8 Delete__13TTabletDriverFv
void
TTabletDriver::Delete()
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	T h e   d r i v e r   i n   u s e
------------------------------------------------------------------------------*/

// ROM 0x0025065c TabInitialize__FRC4RectP6TUPort
// The tablet buffer made ready for the inker (its port woken as samples
// come), the machine's own driver ("TMainTabletDriver") made if one has
// been registered, else the MP2x00's resistive panel, and the driver
// started over the screen's rectangle.  ==> 0, or the error (a byte).
// NOT YET RECONSTRUCTED: TResistiveTablet (0x0005ac60-0x0005b740, the
// panel read through the Voyager's ADC - hardware), so a machine with no
// driver of its own has none.
long
TabInitialize(const Rect& screen, TUPort* inkerPort)
{
	TBCTabletBufferInit(inkerPort);
	gTabletDriver = (TTabletDriver*) NewByName("TTabletDriver", "TMainTabletDriver");
	if (gTabletDriver == nil)
	{
		// NOT YET RECONSTRUCTED: TResistiveTablet::ClassInfo()->Register();
		gTabletDriver = TTabletDriver::New((char*) "TResistiveTablet");
	}
	long err = gTabletDriver != nil ? gTabletDriver->Init(screen) : kError_Call_Not_Implemented;
	return err & 0xff;
}


// ROM 0x00250098 TabBoot
// Nothing but an atomic stretch: the ROM's tablet needs nothing at boot.
void
TabBoot(void)
{
	EnterAtomic();
	ExitAtomic();
}


// ROM 0x0025074c TabWakeUp
// The driver woken after a sleep (CyclePower).
void
TabWakeUp(void)
{
	if (gTabletDriver != nil)
		gTabletDriver->WakeUp();
}


// ROM 0x002507c8 TabShutDown
// ... and shut down for one: a stroke under way is ended with a pen-up.
void
TabShutDown(void)
{
	if (gTabletDriver != nil)
		gTabletDriver->ShutDown();
}


// ROM 0x0025075c StartBypassTablet__Fv
// The tablet's own samples ignored while something else - the journal -
// puts samples in; refused (-1) while the pen is down.
long
StartBypassTablet(void)
{
	return gTabletDriver != nil ? gTabletDriver->StartBypassTablet() : -1;
}


// ROM 0x0025076c StopBypassTablet__Fv
long
StopBypassTablet(void)
{
	return gTabletDriver != nil ? gTabletDriver->StopBypassTablet() : -1;
}


// ROM 0x00250700 GetTabletResolution__FPlT1
// How finely the tablet reads, samples an inch each way, 16.16.  (With no
// driver the MP2x00's: TResistiveTablet::GetTabletResolution 0x0005ac9c
// answers 800 both ways.)
void
GetTabletResolution(long* x, long* y)
{
	if (gTabletDriver != nil)
	{
		gTabletDriver->GetTabletResolution(x, y);
		return;
	}
	*y = 800 << 16;
	*x = 800 << 16;
}

// host: the name server's way to the tablet (GestaltSources.h)
static struct TabletGestaltSource
{
	TabletGestaltSource()	{ gGestaltTabletResolution = GetTabletResolution; }
} sTabletGestaltSource;


// ROM 0x00250718 TabSetOrientation__Fl
void
TabSetOrientation(long orientation)
{
	if (gTabletDriver != nil)
		gTabletDriver->TabSetOrientation(orientation);
}


// ROM 0x0025072c TabletNeedsRecalibration__Fv
Boolean
TabletNeedsRecalibration(void)
{
	return gTabletDriver != nil && gTabletDriver->TabletNeedsRecalibration();
}


// ROM 0x002507a0 GetTabletState__Fv
long
GetTabletState(void)
{
	return gTabletDriver != nil ? gTabletDriver->GetTabletState() : kTabletStateIdle;
}


// ROM 0x002507f0 TabletIdle__Fv
// Nothing to do (the ROM does not ask the driver).
long
TabletIdle(void)
{
	return 0;
}


// ROM 0x002507fc GetSampleRate__Fv
// The tablet driver's sampling *interval*, despite the name: how many
// ticks of the 3.6864 MHz tablet timer (0x384000 a second) go between two
// samples, which is why everything that uses it divides 0x384000 by it.
// (With no driver the MP2x00's: TResistiveTablet keeps it at +0x64 and
// sets it to 0xb400 whenever the pen goes up, 80 samples a second.)
ULong
GetSampleRate(void)
{
	return gTabletDriver != nil ? gTabletDriver->GetSampleRate() : 0xb400;
}


// ROM 0x0025080c SetSampleRate__FUl
void
SetSampleRate(ULong rate)
{
	if (gTabletDriver != nil)
		gTabletDriver->SetSampleRate(rate);
}


// ROM 0x00250820 GetTabletCalibration__FP11Calibration
void
GetTabletCalibration(Calibration* calibration)
{
	if (gTabletDriver != nil)
		gTabletDriver->GetTabletCalibration(calibration);
}


// ROM 0x00250834 SetTabletCalibration__FRC11Calibration
void
SetTabletCalibration(const Calibration& calibration)
{
	if (gTabletDriver != nil)
		gTabletDriver->SetTabletCalibration(calibration);
}


// ROM 0x00250848 SetDoingCalibration__FUcPUl
void
SetDoingCalibration(UChar doing, ULong* orientation)
{
	if (gTabletDriver != nil)
		gTabletDriver->SetDoingCalibration(doing, orientation);
}
