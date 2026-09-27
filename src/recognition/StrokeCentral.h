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
				recogniser claimed - are grouped into words
				(AddExpiredStroke, over InkGroups.h's
				IGGroupAndCompressStrokes) and each word handed over as
				one piece of ink (IGCompressGroup, CompressGroup,
				ExpireGroup): an aeInkWord or aeRawInk command to the view
				under it (ExpireUsingCommand), or a stroke bundle to the
				fExpireProc a caller set (Recognize's HandleBulkStrokes);
				ExpireAll settles whatever is waiting, half a second after
				the last stroke (IdleCompress).  NOT YET RECONSTRUCTED: the
				deferred strokes.  The ROM's StrokeCentral is 0x44 bytes.

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
class TStrokeUnit;
class TArray;
class TUnitPublic;

class StrokeCentral
{
public:
						~StrokeCentral();						// ROM 0x001447c4 __dt__13StrokeCentralFv
	// The ROM's constructor (new, then InitFields).  A factory here,
	// because gStrokeWorld is a static object whose fields must not be
	// made before the heaps are up (Init makes them).
	static StrokeCentral*	New(void);								// ROM 0x00144790 __ct__13StrokeCentralFv
	void				Init(void);								// ROM 0x00144ad8 Init__13StrokeCentralFv - the fields, the stroke queue, the tablet
	void				InitFields(void);						// ROM 0x00144d40 InitFields__13StrokeCentralFv
	void				DoneFields(void);						// ROM 0x00145644 DoneFields__13StrokeCentralFv
	struct StrokeCentralState*	SaveRecognitionState(UChar* failed);	// ROM 0x001457fc SaveRecognitionState__13StrokeCentralFPUc - the fields put aside and started again
	void				RestoreRecognitionState(struct StrokeCentralState* state);	// ROM 0x00145b6c RestoreRecognitionState__13StrokeCentralFUl

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
	void				ExpireAll(void);						// ROM 0x00144cd8 ExpireAll__13StrokeCentralFv - the compress group settled and compressed; the compress time cleared when no expired stroke is left
	void				AddExpiredStroke(TStrokeUnit* unit);	// ROM 0x00144df8 AddExpiredStroke__13StrokeCentralFP11TStrokeUnit - a stroke nobody claimed, grouped with the others (the compress time half a second on)
	void				IGCompressGroup(TStrokeUnit** units);	// ROM 0x00144c78 IGCompressGroup__13StrokeCentralFPP11TStrokeUnit - a word's strokes (nil-ended) made the expired strokes and compressed
	void				CompressGroup(void);					// ROM 0x0014532c CompressGroup__13StrokeCentralFv - the expired strokes' ink taken off and the group expired, then let go
	void				ExpireGroup(TUnitPublic** units);		// ROM 0x00145210 ExpireGroup__13StrokeCentralFPP11TUnitPublic - to the expire proc as a stroke bundle, or as a command to the view under them

	Boolean				fHasCurrent;		// +0x00
	TStroke*			fCurrentStroke;		// +0x04
	TClickUnit*			fCurrentUnit;		// +0x08
	ULong				fLastDownTime;		// +0x0c
	ULong				fLastUpTime;		// +0x10
	long				fBlocked;			// +0x14  BlockStrokes count
	long				fBlockedIdles;		// +0x18  idles while blocked
	ULong				fLastFlushTime;		// +0x1c
	RefStruct*			fDeferredStrokes;	// +0x20  [stroke, a, b, ...]
	ULong				fGroupCount;		// +0x24  how many of the expired strokes are the group being compressed
	TUnitList*			fExpiredStrokes;	// +0x28
	TTime				fNextCompressTime;	// +0x2c
	Handle				fCompressGroup;		// +0x34  (NOT YET)
	Boolean				fFlag38;			// +0x38
	void				(*fExpireProc)(RefArg, RefArg);	// +0x3c  what a group of expired strokes is handed to as a stroke bundle (ExpireGroup; nil: the view under it, as ink): Recognize's HandleBulkStrokes
	RefStruct*			fCompressBundle;	// +0x40  ... with this as its first argument
};

// what SaveRecognitionState puts aside: every field but the blocking
// count, the idles while blocked and the last flush time (0x38 bytes in
// the ROM)
struct StrokeCentralState
{
	Boolean			fHasCurrent;
	TStroke*		fCurrentStroke;
	TClickUnit*		fCurrentUnit;
	ULong			fLastDownTime;
	ULong			fLastUpTime;
	RefStruct*		fDeferredStrokes;
	ULong			fGroupCount;
	TUnitList*		fExpiredStrokes;
	TTime			fNextCompressTime;
	Handle			fCompressGroup;
	Boolean			fFlag38;
	void			(*fExpireProc)(RefArg, RefArg);
	RefStruct*		fCompressBundle;
};

extern StrokeCentral	gStrokeWorld;						// ROM 0x0c1018cc gStrokeWorld

// A stroke made ready to be recognised as if it had just been written -
// its box, its times (ending now), done - and a stroke unit made of it in
// the stroke domain, numbered as the controller's next stroke (defined
// in ShapeDomain.cpp, whose shapes were their first users).
void			PrepStrokeForRecognition(TStroke* stroke);
TStrokeUnit*	MakeStrokeUnit(TStroke* stroke, TArray* areas, long contextID);

void	IdleStrokes(void);									// ROM 0x00144878 IdleStrokes__Fv - the stroke world idled, not re-entered
Boolean	OnlyStrokeWritten(class TStrokeUnit* unit);			// ROM 0x0020bf58 OnlyStrokeWritten__FP11TStrokeUnit (Recognizer.h)

// the unit flag the stroke world sets on the click of the stroke in progress
enum { kUnitStrokeInProgress = 0x04000000 };

#endif	/* __STROKECENTRAL_H */
