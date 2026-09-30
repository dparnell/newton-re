/*
	File:		recognition/TabletDriver.h

	Contains:	The tablet driver: the protocol the pen's hardware is reached
				through (TTabletDriver), the one in use (gTabletDriver, made
				by TabInitialize), and the calls the system makes of it - the
				calibration that turns the panel's readings into the
				screen's coordinates, the sleep and the wake, the bypass the
				journal plays through, the sampling interval.

				A driver samples the pen from its interrupts and puts each
				sample into the tablet buffer (TabletBuffer.h) already in the
				screen's coordinates - x and y in eighths of a pixel - having
				applied the calibration: a scale and an offset each way,
				16.16 (Calibration).  While the inker calibrates
				(SetDoingCalibration) it hands over the panel's raw readings
				instead, which the calibration screen averages at each target
				(Inker.h: TInker::Calibrate).

				TabInitialize looks for "TMainTabletDriver" first - which is
				how a machine's own driver takes the place of the ROM's - and
				only then makes the MP2x00's own, TResistiveTablet (the
				resistive panel read through the Voyager's ADC, its sampling
				a state machine over the pen-down interrupt and a timer: NOT
				YET RECONSTRUCTED, being hardware).  The host's driver is
				hal/host/HostTablet.h's, a TMainTabletDriver.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM (the
				protocol's glue 0x00385b8c-0x00385cb8; 0x0025065c-0x00250860).
*/

#ifndef __TABLETDRIVER_H
#define __TABLETDRIVER_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

// What turns a raw reading into a screen coordinate (0x14 bytes): the
// screen's x is fXScale * the raw x + fXOffset, and so for y, all 16.16
// (the raw reading being the ADC's 12 bits).  Which raw axis goes with
// which screen axis depends on how the panel sits against the screen
// (TInker::Calibrate works it out from the orientations).
struct Calibration
{
	Fixed		fXScale;		// +00
	Fixed		fYScale;		// +04
	Fixed		fXOffset;		// +08
	Fixed		fYOffset;		// +0C
	UChar		fField10;		// +10 (Calibrate sets it 1; TResistiveTablet::Init 200)
	UChar		fField11;		// +11 (1; 230)
	UChar		fPad[2];
};

// what the driver is doing (GetTabletState)
enum
{
	kTabletStateIdle		= 0,		// the pen up, waiting for it
	kTabletStatePenDown		= 1,		// sampling (TResistiveTablet's 1-7 are its sampling steps)
	kTabletStateBypassed	= 8,		// the journal playing (StartBypassTablet)
	kTabletStateShutDown	= 9			// the machine asleep
};

PROTOCOL TTabletDriver : public TProtocol
{
public:
	static TTabletDriver*	New(char* implementation);					// ROM 0x00385b8c New__13TTabletDriverSFPc
	void			Delete();												// ROM 0x00385bb8 Delete__13TTabletDriverFv

	VIRTUAL long	Init(const Rect& screen) ENDVIRTUAL;					// ROM 0x00385bd4 Init__13TTabletDriverFRC4Rect
	VIRTUAL void	WakeUp(void) ENDVIRTUAL;								// ROM 0x00385be0 WakeUp__13TTabletDriverFv
	VIRTUAL long	ShutDown(void) ENDVIRTUAL;								// ROM 0x00385bec ShutDown__13TTabletDriverFv
	VIRTUAL long	TabletIdle(void) ENDVIRTUAL;							// ROM 0x00385bf8 TabletIdle__13TTabletDriverFv
	VIRTUAL ULong	GetSampleRate(void) ENDVIRTUAL;							// ROM 0x00385c04 GetSampleRate__13TTabletDriverFv - the interval, 3.6864 MHz ticks
	VIRTUAL void	SetSampleRate(ULong rate) ENDVIRTUAL;					// ROM 0x00385c10 SetSampleRate__13TTabletDriverFUl
	VIRTUAL void	GetTabletCalibration(Calibration* calibration) ENDVIRTUAL;			// ROM 0x00385c1c GetTabletCalibration__13TTabletDriverFP11Calibration
	VIRTUAL void	SetTabletCalibration(const Calibration& calibration) ENDVIRTUAL;	// ROM 0x00385c28 SetTabletCalibration__13TTabletDriverFRC11Calibration
	VIRTUAL void	SetDoingCalibration(UChar doing, ULong* orientation) ENDVIRTUAL;	// ROM 0x00385c34 SetDoingCalibration__13TTabletDriverFUcPUl - raw readings while doing; ==> the orientation the panel reads in
	VIRTUAL void	GetTabletResolution(long* x, long* y) ENDVIRTUAL;		// ROM 0x00385c40 GetTabletResolution__13TTabletDriverFPlT1 - samples an inch, 16.16
	VIRTUAL void	TabSetOrientation(long orientation) ENDVIRTUAL;			// ROM 0x00385c4c TabSetOrientation__13TTabletDriverFl
	VIRTUAL long	GetTabletState(void) ENDVIRTUAL;						// ROM 0x00385c58 GetTabletState__13TTabletDriverFv - a kTabletState...
	VIRTUAL long	GetFingerInputState(UChar* state) ENDVIRTUAL;			// ROM 0x00385c64 GetFingerInputState__13TTabletDriverFPUc
	VIRTUAL long	SetFingerInputState(UChar state) ENDVIRTUAL;			// ROM 0x00385c70 SetFingerInputState__13TTabletDriverFUc
	VIRTUAL long	RecalibrateTabletAfterRotate(void) ENDVIRTUAL;			// ROM 0x00385c7c RecalibrateTabletAfterRotate__13TTabletDriverFv
	VIRTUAL Boolean	TabletNeedsRecalibration(void) ENDVIRTUAL;				// ROM 0x00385c88 TabletNeedsRecalibration__13TTabletDriverFv
	VIRTUAL long	StartBypassTablet(void) ENDVIRTUAL;						// ROM 0x00385c94 StartBypassTablet__13TTabletDriverFv - ==> 0, or -1 while the pen is down
	VIRTUAL long	StopBypassTablet(void) ENDVIRTUAL;						// ROM 0x00385ca0 StopBypassTablet__13TTabletDriverFv - ==> 0, or -1 when it was not bypassed
	VIRTUAL void	ReturnTabletToConsciousness(ULong a, ULong b, ULong c) ENDVIRTUAL;	// ROM 0x00385cac ReturnTabletToConsciousness__13TTabletDriverFUlN21
};

extern TTabletDriver*	gTabletDriver;			// ROM 0x0c104d34 gTabletDriver

class TUPort;

long	TabInitialize(const Rect& screen, TUPort* inkerPort);				// ROM 0x0025065c TabInitialize__FRC4RectP6TUPort - the buffer, the driver found, made and started
void	TabBoot(void);														// ROM 0x00250098 TabBoot
void	TabSetOrientation(long orientation);								// ROM 0x00250718 TabSetOrientation__Fl
Boolean	TabletNeedsRecalibration(void);										// ROM 0x0025072c TabletNeedsRecalibration__Fv
long	GetTabletState(void);												// ROM 0x002507a0 GetTabletState__Fv
long	TabletIdle(void);													// ROM 0x002507f0 TabletIdle__Fv
void	SetSampleRate(ULong rate);											// ROM 0x0025080c SetSampleRate__FUl
void	GetTabletCalibration(Calibration* calibration);						// ROM 0x00250820 GetTabletCalibration__FP11Calibration
void	SetTabletCalibration(const Calibration& calibration);				// ROM 0x00250834 SetTabletCalibration__FRC11Calibration
void	SetDoingCalibration(UChar doing, ULong* orientation);				// ROM 0x00250848 SetDoingCalibration__FUcPUl

#endif	/* __TABLETDRIVER_H */
