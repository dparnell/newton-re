/*
	File:		recognition/StrokeCentral.h

	Contains:	StrokeCentral (gStrokeWorld), the stroke world: where the
				strokes the inker queued become units.  IdleStrokes, run
				from the application's idle loop, takes the next stroke
				(StrokeGet), makes a TClickUnit of it in the root domain
				and gives it to the controller - a click goes to the view
				at once, while the pen is still down - then, as the stroke
				goes on, keeps the click unit's box up to date
				(IdleCurrentStroke) and makes a TClickEventUnit when the
				stroke queue has noted a tap, double tap, hilite click or
				tap-and-drag in it; when the stroke ends it remembers its
				times and triggers recognition.  BlockStrokes holds the
				strokes back (for ten idles at most), FlushStrokes throws
				the queued strokes away (each made into a click whose ink
				is taken off), BeforeLastFlush tells whether a time falls
				before the last flush.  The expired strokes - the ones no
				recogniser claimed - are grouped and compressed into ink
				for the views (NOT YET RECONSTRUCTED: AddExpiredStroke,
				ExpireGroup, CompressGroup, IdleCompress, ExpireAll, the
				deferred strokes).  The ROM's StrokeCentral is 0x44 bytes.

				NOT YET RECONSTRUCTED: TController - DEVIATION: with no
				controller the host hands every click and click-event unit
				straight to HandleUnit (as the ROM does for the units the
				controller calls externally arbitrated) and disposes the
				units itself; the journal.

	Reconstructed from the MP2x00 US ROM (0x001447c4-0x00144cac,
	0x00144d40-0x00144df8, 0x00145644-0x001456a0, 0x00145e10-0x00145f78);
	each function cites its origin.
*/

#ifndef __STROKECENTRAL_H
#define __STROKECENTRAL_H

#include "Unit.h"
#include "objects.h"
#include "NewtonTime.h"

class TClickUnit;

class StrokeCentral
{
public:
						~StrokeCentral();						// ROM 0x001447c4 __dt__13StrokeCentralFv
	void				Init(void);								// ROM 0x00144ad8 Init__13StrokeCentralFv - the fields, the stroke queue, the tablet
	void				InitFields(void);						// ROM 0x00144d40 InitFields__13StrokeCentralFv
	void				DoneFields(void);						// ROM 0x00145644 DoneFields__13StrokeCentralFv

	void				IdleStrokes(void);						// ROM 0x001448b8 IdleStrokes__13StrokeCentralFv
	void				StartNewStroke(TStroke* stroke);		// ROM 0x00145e10 StartNewStroke__13StrokeCentralFP7TStroke - the stroke made current, the last stroke's times noted in it
	void				DoneCurrentStroke(void);				// ROM 0x00145f14 DoneCurrentStroke__13StrokeCentralFv - its times kept as the last, the unit let go
	void				IdleCurrentStroke(void);				// ROM 0x00145f68 IdleCurrentStroke__13StrokeCentralFv - the click unit's box brought up to the stroke's
	TStroke*			CurrentStroke(void);					// ROM 0x001447fc CurrentStroke__13StrokeCentralFv
	void				InvalidateCurrentStroke(void);			// ROM 0x001447f0 InvalidateCurrentStroke__13StrokeCentralFv
	void				BlockStrokes(void);						// ROM 0x00144ab4 BlockStrokes__13StrokeCentralFv
	void				UnblockStrokes(void);					// ROM 0x00144ac4 UnblockStrokes__13StrokeCentralFv
	Boolean				FlushStrokes(void);						// ROM 0x00144af8 FlushStrokes__13StrokeCentralFv - ==> whether any was thrown away
	Boolean				BeforeLastFlush(long time);				// ROM 0x00144bc8 BeforeLastFlush__13StrokeCentralFl - whether the time is before the last flush (which is forgotten after ten seconds)
	void				AddDeferredStroke(RefArg stroke, long a, long b);	// ROM 0x00144810 AddDeferredStroke__13StrokeCentralFRC6RefVarlT2
	void				IdleCompress(void);						// ROM 0x001454fc IdleCompress__13StrokeCentralFv - the expired strokes compressed into ink once the compress time has come (no stroke current)
	void				ExpireAll(void);						// ROM 0x00144cd8 ExpireAll__13StrokeCentralFv - the compress group grouped and compressed (NOT YET); the compress time cleared when no expired stroke is left

	Boolean				fHasCurrent;		// +0x00
	TStroke*			fCurrentStroke;		// +0x04
	TClickUnit*			fCurrentUnit;		// +0x08
	ULong				fLastDownTime;		// +0x0c
	ULong				fLastUpTime;		// +0x10
	long				fBlocked;			// +0x14  BlockStrokes count
	long				fBlockedIdles;		// +0x18  idles while blocked
	ULong				fLastFlushTime;		// +0x1c
	RefStruct*			fDeferredStrokes;	// +0x20  [stroke, a, b, ...]
	ULong				fUnused24;			// +0x24
	TUnitList*			fExpiredStrokes;	// +0x28
	TTime				fNextCompressTime;	// +0x2c
	Handle				fCompressGroup;		// +0x34  (NOT YET)
	Boolean				fFlag38;			// +0x38
	ULong				fUnused3c;			// +0x3c
	RefStruct*			fCompressBundle;	// +0x40  (NOT YET)
};

extern StrokeCentral	gStrokeWorld;						// ROM 0x0c1018cc gStrokeWorld

void	IdleStrokes(void);									// ROM 0x00144878 IdleStrokes__Fv - the stroke world idled, not re-entered
Boolean	OnlyStrokeWritten(class TStrokeUnit* unit);			// ROM 0x0020bf58 OnlyStrokeWritten__FP11TStrokeUnit (Recognizer.h)

// the unit flag the stroke world sets on the click of the stroke in progress
enum { kUnitStrokeInProgress = 0x04000000 };

#endif	/* __STROKECENTRAL_H */
