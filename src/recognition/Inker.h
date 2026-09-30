/*
	File:		recognition/Inker.h

	Contains:	The inker: the 'inkr world (TInker) between the tablet driver
				and the stroke world.  The driver puts the pen's samples into
				the tablet buffer (TabletBuffer.h) from its interrupt and
				wakes the inker ({'newt, 'inkr, 2}); the inker reads them into
				the stroke queue (RealStrokeTime), draws the live ink as the
				pen moves, and wakes the application ({'newt, 'idle, 'inkr}
				to the Newt port) whenever a stroke has changed.  While the
				pen is down it keeps itself going on a 50 ms idler, and stops
				when the pen is up and the buffer read.

				Everything else asked of the pen goes to it too, as a
				{'newt, 'inkr, command} RPC (TInkerEvent): the pen modes, the
				calibration read (0x16) and written (0x17), whether the
				tablet wants calibrating (0x21), the busy box, and the
				calibration screen itself (5, TInker::Calibrate): a target
				in each of two corners, held until twenty readings agree
				(GetRawPoint), a scale and offset each way worked out from
				the two, and a third target to check the result by, all of
				it again until the check is within ten pixels.  The
				NewtonScript side - GetCalibration, SetCalibration,
				CalibrateTablet, IsTabletCalibrationNeeded, and the System
				soup's "Calibration" entry that keeps one across a restart
				(the ROM's savecalibration and loadcalibration blocks) - is
				InkerNatives.

				The live ink is the inker's too: Convert takes the buffer a
				record at a time (a pen-down takes the next pen size, a
				sample becomes the current point, a pen-up has the inked
				bounds written into it) and DrawInk joins the last point to
				the current one - reading ahead as many samples as fit a
				tile - through TLiveInker (LiveInker.h), onto the display.
				(gWireRecog, 1 in the ROM's initialised data and never
				changed, keeps the records for the stroke world; without it
				the inker would throw away each record once inked.)

				The busy box is the inker's as well (TBusyBox, commands
				0x33-0x37: shown, hidden, shown in a second unless called
				off), drawn by qd's QDShowBusyBox onto the display.

				NOT YET RECONSTRUCTED: the armistice samples.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x002173ec-0x00219100, 0x0013fb0c-0x0014137c).  TInker is
				0x118 bytes there.
*/

#ifndef __INKER_H
#define __INKER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __AEVENTS_H
#include "AEvents.h"
#endif
#include "TimerQueue.h"
#ifndef __TABLETDRIVER_H
#include "TabletDriver.h"
#endif
#ifndef __LIVEINKER_H
#include "LiveInker.h"
#endif

#define kInkerID				'inkr'

// the inker's commands (TInkerEvent::fCommand)
enum
{
	kInkerIdle					= 2,		// the tablet has samples (TBCWakeUpInker)
	kInkerConvert				= 4,		// the samples read and inked now
	kInkerCalibrate				= 5,		// the calibration screen; fArg the time limit, ==> the error in fArg
	kInkerSetPenMode			= 7,		// 7-11: the current pen mode set to command-7, ==> the old one + 7 (and 7 the inked bounds)
	kInkerSetNextPenMode		= 12,		// 12-16: the next pen mode set to command-12, ==> the old one + 12
	kInkerGetPenMode			= 0x14,		// ==> the current pen mode + 7
	kInkerGetNextPenMode		= 0x15,		// ==> the next pen mode + 12
	kInkerGetCalibration		= 0x16,		// ==> the calibration
	kInkerSetCalibration		= 0x17,
	kInkerCalibrationNeeded		= 0x21,		// ==> in fArg whether the tablet wants calibrating
	kInkerBusyBoxFirst			= 0x33,		// ... to 0x37 (NewtWorld.h's BusyBoxSend)
	kInkerBusyBoxLast			= 0x37
};

// errors of the calibration screen
const long	kInkerCalibrationTimedOut	= -56101;	// no pen within the time limit
const long	kInkerCalibrationCancelled	= -56102;	// the machine is going to sleep

// The inker's RPC, the ROM's 0x24 bytes: the event, the command, an
// argument or result, and the data some commands carry.
struct TInkerEvent : public TAEvent
{
	ULong		fCommand;			// +08
	ULong		fArg;				// +0C
	union
	{
		Calibration	fCalibration;	// +10 (0x16, 0x17)
		Rect		fInkedBounds;	// +10 (7)
	};
};

// its replies' sizes (the ROM's 0x10, 0x18 and 0x24, sized on the host)
const ULong	kInkerReplySize				= offsetof(TInkerEvent, fCalibration);
const ULong	kInkerBoundsReplySize		= offsetof(TInkerEvent, fCalibration) + sizeof(Rect);
const ULong	kInkerCalibrationReplySize	= offsetof(TInkerEvent, fCalibration) + sizeof(Calibration);

// The busy box asked for (0x33 show, 0x34 hide, 0x35 allowed again - hidden
// and called off, 0x36 shown if the next second passes, 0x37 as 0x35):
// sent to the inker, nothing when it is not there (InkerNatives.cpp;
// the ROM's is in the newt world's code, newt/NewtWorld.h declaring it
// too).
void	BusyBoxSend(long command);				// ROM 0x0030dd60 BusyBoxSend__Fl

// The busy box: a timer, so that it can be asked to appear in a second's
// time and called off before then (0x218bd4-0x218d4c; 0x38 bytes)
class TBusyBox : public TTimerElement
{
public:
					TBusyBox();														// ROM 0x00218bd4 __ct__8TBusyBoxFv
	virtual void	Timeout(void);													// ROM 0x00218c70 Timeout__8TBusyBoxFv - shown
	long			DoCommand(long command);										// ROM 0x00218c74 DoCommand__8TBusyBoxFl - 0x33 show, 0x34 hide, 0x35/0x37 hide and call off, 0x36 show in a second
	void			HideBusyBox(void);												// ROM 0x00218cf4 HideBusyBox__8TBusyBoxFv
	void			ShowBusyBox(void);												// ROM 0x00218d20 ShowBusyBox__8TBusyBoxFv

	PixelMap		fMap;			// +18 the box (its bits the ROM's busy picture)
	long			fState;			// +34 1 shown, 0 hidden, -1 to be shown
};

class TInkerEventHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x002173ec AEHandlerProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x00217658 AECompletionProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);			// ROM 0x00218d4c IdleProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
	void			InkThem(void);														// ROM 0x00218dd8 InkThem__18TInkerEventHandlerFv

	TBusyBox		fBusyBox;			// +14
};

class TInker : public TAppWorld
{
public:
					TInker();														// ROM 0x00218df0 __ct__6TInkerFv
	virtual			~TInker();														// ROM 0x00218e48 __dt__6TInkerFv
	virtual ULong	GetSizeOf();													// ROM 0x0038aadc (unnamed) - the vtable's +0x04
	virtual long	MainConstructor();												// ROM 0x00218f00 MainConstructor__6TInkerFv

	void			IInker(void);													// ROM 0x00218ff8 IInker__6TInkerFv
	void			SetNewtPort(TUPort* port);										// ROM 0x00217814 SetNewtPort__6TInkerFP6TUPort
	UChar			GetCurrentPenMode(void);										// ROM 0x002177f4 GetCurrentPenMode__6TInkerFv
	UChar			GetNextPenMode(void);											// ROM 0x002177fc GetNextPenMode__6TInkerFv
	void			SetNextPenMode(UChar mode);										// ROM 0x00217804 SetNextPenMode__6TInkerFUc
	void			SetCurrentPenMode(UChar mode);									// ROM 0x0021780c SetCurrentPenMode__6TInkerFUc
	long			Calibrate(ULong limit);											// ROM 0x002180a0 Calibrate__6TInkerFUl - ==> 0, or kInkerCalibration...
	long			GetRawPoint(ULong* x, ULong* y, short h, short v, ULong limit);	// ROM 0x00217bc4 GetRawPoint__6TInkerFPUlT1sT3Ul
	void			InsertionSort(ULong* values, ULong count, ULong value);			// ROM 0x00217b90 InsertionSort__6TInkerFPUlUlT2
	void			PresCalibrate(void);											// ROM 0x00218bcc PresCalibrate__6TInkerFv
	static Boolean	TestForCalibrationNeeded(void);									// ROM 0x00218bd0 TestForCalibrationNeeded__6TInkerFv
	void			SendNewtIdle(void);												// (the application woken: {'newt, 'idle, 'inkr})
	Boolean			Convert(void);													// ROM 0x00217a64 Convert__6TInkerFv - the next record taken; ==> whether there was one
	void			DrawInk(Point from, Point to, Rect* inked, short pressure);		// ROM 0x0021765c DrawInk__6TInkerF5PointT1P4Rects
	void			LCDEntry(void);													// ROM 0x0021781c LCDEntry__6TInkerFv

	TInkerEventHandler*	fHandler;
	TUAsyncMessage	fNewtMessage;		// +70
	TUPort*			fNewtPort;			// +80
	short			fField84;			// +84 (cleared at each pen-down and pen-up; nothing reads it)
	Fixed			fLastX;				// +88 the point last inked to (-1: none yet)
	Fixed			fLastY;				// +8C
	Fixed			fCurrentX;			// +90 the sample just converted
	Fixed			fCurrentY;			// +94
	UChar			fCurrentPenMode;	// +C0 the pen's size (0: no ink)
	UChar			fNextPenMode;		// +C1 the size the next stroke takes
	UChar			fPressure;			// +C2 the sample's pressure
	UChar			fInkMode;			// +C3 what the last record was: 0 a pen-down, 1 other, 2 a sample, 3 a pen-up
	Rect			fInkedBounds;		// +C4 what this stroke's live ink covers
	TAEvent			fNewtEvent;			// +CC {'newt, 'idle}
	ULong			fNewtEventType;		// +D4 'inkr
	ULong			fNewtEventPad;
	TLiveInker		fLiveInker;			// +DC
};

extern Boolean	gWireRecog;			// ROM 0x0c104d2c gWireRecog - true: the stroke world gets the records the inker has inked

// the calibration's flags (gCalibrate)
struct InkerCalibrateFlags
{
	UChar			fConverting;		// +00 (TInker::Convert's; Calibrate clears it)
	UChar			fPad[7];
	UChar			fArmistice;			// +08 armistice samples being fed (never, here)
};
extern InkerCalibrateFlags	gCalibrate;		// ROM 0x0c104d20 gCalibrate

extern TInker*		gInker;				// (host: the ROM finds it as the inker task's globals)

void	StartInker(TUPort* newtPort);								// ROM 0x00218ea0 StartInker__FP6TUPort - the 'inkr world started
void	InsertArmisticeSamples(void);								// ROM 0x00217fb0 InsertArmisticeSamples__Fv

// The script side (InkerNatives.cpp)
TUPort*	InkerPort(void);											// ROM 0x0013fb0c InkerPort__Fv - the inker's port, looked up by name the first time
void	LoadInkerCalibration(void);									// ROM 0x0013fc2c LoadInkerCalibration__Fv - the stored calibration given the inker, the screen if there is none
long	CalibrateInker(void);										// ROM 0x00141098 CalibrateInker__Fv - the calibration screen, and the result saved
Boolean	CheckTabletHWCalibration(void);								// ROM 0x0014121c CheckTabletHWCalibration__Fv
void	CheckTabletCalibration(void);								// ROM 0x001412dc CheckTabletCalibration__Fv
void	RegisterInkerNatives(void);

// host: told each target the calibration screen shows (a test's pen taps
// it: hal/host/HostTablet.h)
extern void	(*gInkerCalibrationTargetHook)(short h, short v);
// host: a test's queued pen records fed a tick at a time while the
// inker idles (hal/host/HostTablet.h); ==> whether any are left
extern Boolean	(*gInkerHostIdleHook)(void);

#endif	/* __INKER_H */
