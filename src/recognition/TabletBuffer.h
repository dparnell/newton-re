/*
	File:		recognition/TabletBuffer.h

	Contains:	The tablet buffer - the ring of 250 words the tablet driver's
				sampling fills and the inker reads: a sample is one word
				(the x coordinate in bits 18-31 and y in bits 4-17, both in
				eighths of a pixel, the pressure in the low nibble, 0-7), a
				pen-down two words (0xd, the time in ticks), a pen-up four
				(0xe, the time, a dummy sample 0x14000000, the time again).
				The writer is InsertTabletSample (the ROM's from the tablet
				driver's interrupt, then the inker task woken; on the host
				from the window or a test); the reader is the "collect"
				state machine of xGetTabPt, which the recogniser sees as
				GetTabPt/LastTabPt/GetDownTime/GetUpTime (Tablet.h).  The
				polling mode (SetTabletPolling: samples kept one at a time
				for TBCPollTablet, the calibration screen's) keeps the last
				sample instead of buffering.

				NOT YET RECONSTRUCTED: the tablet driver itself (gTabletDriver,
				a TTabletDriver protocol - TResistiveTablet; GetSampleRate,
				the calibration, the orientation), TBCTabletBufferInit's
				inker port and the inker wake-ups (TBCWakeUpInker).

	Reconstructed from the MP2x00 US ROM (0x002501e4-0x002507fc,
	0x000380f4-0x00038348); each function cites its origin.
*/

#ifndef __TABLETBUFFER_H
#define __TABLETBUFFER_H

#include "Stroke.h"

class TUPort;

enum
{
	kTabletBufferSize	= 250,
	kTabletPenDown		= 0xd,
	kTabletPenUp		= 0xe,
	kTabletNoSample		= 0xf,			// InsertTabletSample: nothing to keep in polling mode
	kTabletPenUpSample	= 0x14000000	// the dummy sample in a pen-up record
};

// the buffer's words and indexes (the ROM's are at 0x0c104458): the
// write index, the inker's read index (the inker draws the samples and
// passes them on), the stroker's read index (the recogniser's, behind the
// inker's), the words
extern ULong	gTabData;						// ROM 0x0c107390 gTabData - the write index
extern ULong	gTabletInkerIndex;				// (the ROM's word at 0x0c10445c) the inker's read index
extern ULong	gTabletStrokerIndex;			// (0x0c104460) the stroker's read index
extern ULong	gTabletBuffer[kTabletBufferSize];	// (0x0c104464)

void	TBCTabletBufferInit(TUPort* inkerPort);		// ROM 0x002500b0 TBCTabletBufferInit__FP6TUPort - emptied, polling off (NOT YET: the inker's port and wake-up message)
Boolean	TBCTabletBufferEmpty(void);						// ROM 0x00250148 TBCTabletBufferEmpty__Fv - both readers caught up
Boolean	TabletBufferEmpty(void);						// ROM 0x002507b0 TabletBufferEmpty__Fv - the driver's own form of it

// the writer's side
long	TBCInsertTabletSample(ULong sample, ULong time);	// ROM 0x00250430 TBCInsertTabletSample__FUlT1 - ==> 0, or -56006 (kTabletBufferFull) when the ring is full; time 0: now
long	InsertTabletSample(ULong sample, ULong time);	// ROM 0x0025077c InsertTabletSample__FUlT1 - ... and the inker woken (NOT YET)

// The tablet bypassed: its own samples ignored while something else - the
// journal - puts samples in (the ROM's TResistiveTablet goes into its state
// 8, refused while the pen is down).  ==> 0, or -1.
long	StartBypassTablet(void);						// ROM 0x0025075c StartBypassTablet__Fv
long	StopBypassTablet(void);							// ROM 0x0025076c StopBypassTablet__Fv - -1 when it was not bypassed
// DEVIATION: the ROM asks the tablet driver (gTabletDriver, a TTabletDriver
// protocol - NOT YET); the host's tablet (hal/host/HostTablet.h) answers
// through this, and with none the tablet cannot be bypassed
extern long	(*gTabletDriverBypass)(Boolean start);
void	TBCFlushTabletBuffer(void);						// ROM 0x00250630 TBCFlushTabletBuffer__Fv - emptied
void	TBCFlushInkerBuffer(void);						// ROM 0x00250648 TBCFlushInkerBuffer__Fv - the reader catches up
void	TBCSetTabletPolling(Boolean polling);			// ROM 0x00250270 TBCSetTabletPolling__FUc
void	SetTabletPolling(Boolean polling);				// ROM 0x00250740 SetTabletPolling__FUc
Boolean	TBCGetTabletPolling(void);						// ROM 0x00250260 TBCGetTabletPolling__Fv
long	TBCPollTablet(long* x, long* y, ULong* pressure, Boolean* penUp);	// ROM 0x00250288 TBCPollTablet__FPlT1PUlPUc - the last sample in polling mode; ==> 0, or -56007 when there is none new

// the inker's side
Boolean	TBCInkerBufferEmpty(void);						// ROM 0x00250220 TBCInkerBufferEmpty__Fv - the inker has read up to the writer
ULong	TBCGetInkerData(void);							// ROM 0x00250170 TBCGetInkerData__Fv
void	TBCSetInkerData(ULong word);					// ROM 0x00250188 TBCSetInkerData__FUl - the word at the inker's index replaced
void	TBCSetInkerData(ULong word, ULong offset);		// ROM 0x002501a0 TBCSetInkerData__FUlT1
void	TBCIncInkerIndex(ULong count);					// ROM 0x002501c4 TBCIncInkerIndex__FUl

// the stroker's side (the recogniser's, through xGetTabPt)
Boolean	TBCStrokerBufferEmpty(void);					// ROM 0x0025060c TBCStrokerBufferEmpty__Fv - the stroker has read up to the inker
ULong	TBCGetStrokerData(void);						// ROM 0x002501e4 TBCGetStrokerData__Fv - the word at the read index
ULong	TBCGetStrokerData(ULong offset);				// ROM 0x002501fc TBCGetStrokerData__FUl - ... offset words on
void	TBCIncStrokerIndex(ULong count);				// ROM 0x00250240 TBCIncStrokerIndex__FUl
Boolean	StrokerBufferEmpty(void);						// ROM 0x002507b8 StrokerBufferEmpty__Fv
ULong	GetStrokerData(void);							// ROM 0x002507d8 GetStrokerData__Fv
ULong	GetStrokerData(ULong offset);					// ROM 0x002507dc GetStrokerData__FUl
void	IncStrokerIndex(ULong count);					// ROM 0x002507ec IncStrokerIndex__FUl

// the tablet driver (NOT YET: a constant)
void	GetTabletResolution(long* x, long* y);				// ROM 0x00250700 GetTabletResolution__FPlT1 - Fixed samples an inch (800)
ULong	GetSampleRate(void);							// ROM 0x002507fc GetSampleRate__Fv - the ticks of the 3.6864 MHz tablet timer between two samples (0xb400: 80 a second)

// the "collect" state (the ROM's at 0x0c1008a8): whether the tablet is
// collecting, the pen state the reader is in, the last down and up times
struct TabletCollectState
{
	Boolean		collect;			// +0x00
	ULong		nextTab;			// +0x04  (NextTab: 0)
	ULong		nextDown;			// +0x08
	ULong		nextUp;				// +0x0c
	ULong		penState;			// +0x10  3 before a stroke, 6 after its pen-down, 4 in it, 1 just after its pen-up
	ULong		unused14;
	ULong		lastDownTime;		// +0x18  gLastDownTime (0x0c1008c0), cleared when read
	ULong		lastUpTime;			// +0x1c  gLastUpTime (0x0c1008c4)
};
extern TabletCollectState	gTabletCollect;			// ROM 0x0c1008a8 collect

enum
{
	kPenStateUp			= 1,
	kPenStateIdle		= 3,
	kPenStateDown		= 4,
	kPenStateJustDown	= 6
};

void	xTabInit(void);									// ROM 0x000380f4 xTabInit__Fv
void	xTabOn(void);									// ROM 0x00038124 xTabOn__Fv
Boolean	xGetTabPt(TabPt* pt);							// ROM 0x00038148 xGetTabPt__FP5TabPt - the next point (x -1 for the pen-up); ==> whether there was one
Boolean	xLastPoint(TabPt* pt);							// ROM 0x000382b8 xLastPoint__FP5TabPt - whether it is the pen-up
ULong	xGetDownTime(void);								// ROM 0x00038308 xGetDownTime__Fv - the last pen-down's time, once
ULong	xGetUpTime(void);								// ROM 0x00038320 xGetUpTime__Fv
void	xGetTabScale(FPoint* scale);					// ROM 0x00038338 xGetTabScale__FP6FPoint - 8.0 each way (eighths)

#endif	/* __TABLETBUFFER_H */
