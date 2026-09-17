/*
	File:		recognition/StrokeQueue.h

	Contains:	The stroke queue - the ring of 64 strokes (gStrokeQ) between
				the inker, which fills them from the tablet buffer, and the
				stroke world (StrokeCentral), which takes them out:
				StrokeNext starts a new TSStroke at the head when the pen
				goes down, RealStrokeTime (the inker's, from its LCD entry)
				reads the tablet points into it - the first point takes the
				pen-down time and the pen tip, every point re-estimates the
				up time from the sample count, the pen-up ends the stroke -
				and StrokeGet answers the oldest stroke not yet taken (or
				the one being written when it has points).  As the points
				come in CheckHiliteState watches for the click events - a
				tap (up within 10 ticks, moved under gMaxTapSize), a double
				tap (a tap within gDoubleTapInterval and gDoubleTapDistance
				of the last), a hilite click (held still 45 ticks, or 90
				ticks moving little), a tap-and-drag (a press within reach
				of the last tap) - and notes them in the stroke's
				fClickEvent; StrokeHiliteState keeps what it saw of the
				last stroke and the current one.  TSStroke is a TStroke
				with its first point and the next point the inker is to
				draw.  SetupDistances scales the distances to the screen's
				resolution (4, 6 and 6 points; the ROM asks the gestalt).

				NOT YET RECONSTRUCTED: the inker task (StrokeUpdate draws
				the buffered strokes; the LCD entry calling RealStrokeTime -
				on the host StrokeTime, the recogniser's hook, does the
				inker's reading, DEVIATION), the stroke queue semaphore,
				the journalling, NukeEgregiousStrokes' callers.

	Reconstructed from the MP2100 D ROM (0x001fc7f8-0x001fd6c0,
	0x00220724-0x00220880, 0x0011d348-0x0011d364, 0x001f6f50); each
	function cites its origin.
*/

#ifndef __STROKEQUEUE_H
#define __STROKEQUEUE_H

#include "Stroke.h"

// the recogniser's tablet glue (0x0011d348-0x0011d364: the x* readers of
// TabletBuffer.h)
void	TabInit(void);										// ROM 0x0011d348 TabInit__Fv
void	TabOn(void);										// ROM 0x0011d34c TabOn__Fv
Boolean	GetTabPt(TabPt* pt);								// ROM 0x0011d350 GetTabPt__FP5TabPt
Boolean	LastTabPt(TabPt* pt);								// ROM 0x0011d354 LastTabPt__FP5TabPt
ULong	GetDownTime(void);									// ROM 0x0011d35c GetDownTime__Fv
ULong	GetUpTime(void);									// ROM 0x0011d360 GetUpTime__Fv
void	GetTabScale(FPoint* scale);							// ROM 0x0011d364 GetTabScale__FP6FPoint

long	CheapDistPoint(const FPoint* a, const FPoint* b);	// ROM 0x001f6f50 CheapDistPoint__FP6FPointT1 - an approximation of the distance
void	GetMidPoint(const FPoint* a, const FPoint* b, FPoint* mid);	// ROM 0x001a6908 GetMidPoint

// a stroke as the inker fills it: its first point, and the point the
// inker is to draw from next (0x58 bytes)
class TSStroke : public TStroke
{
public:
	static TSStroke*	Make(ULong count);						// ROM 0x00220804 Make__8TSStrokeSFUl
	long				AddPoint(TabPt* pt);					// ROM 0x00220724 AddPoint__8TSStrokeFP5TabPt - ==> 0, or 1 for no memory

	Point				fFirstPoint;	// +0x4c  (-1, -1) until the first point
	Point				fInkPoint;		// +0x50  where the inker draws from next
	Boolean				fInkPending;	// +0x54  fInkPoint is set
};

// the click events (TStroke::fClickEvent) are Unit.h's kTapClick...; the
// stroke queue's own flags on a stroke:
enum
{
	kStrokeTaken		= 0x80000000,		// StrokeGet gave it to the stroke world
	kStrokeInQueue		= 0x10000000		// the inker still has it (Unit.h: kBufferedStroke)
};

// the queue: the head (the stroke being written), the tail (the next to
// take), 64 slots
struct StrokeQueue
{
	UShort		fHead;				// +0x00
	UShort		fTail;				// +0x02
	TSStroke*	fStrokes[64];		// +0x04
};
extern StrokeQueue*	gStrokeQ;							// ROM 0x0c101988 gStrokeQ
extern Boolean		gStrokeValid;						// ROM 0x0c101d24 gStrokeValid - a stroke is being written at the head
extern ULong		gTickOff;							// ROM 0x0c101d18 gTickOff - added to the tablet's times
extern UShort		gHiliteDistance;					// ROM 0x0c101d28 gHiliteDistance - pixels the pen may wander in a hilite click (4 points)
extern UShort		gMaxTapSize;						// ROM 0x0c101d2c gMaxTapSize - pixels a tap may move (6 points)
extern UShort		gDoubleTapDistance;					// ROM 0x0c101d30 gDoubleTapDistance - pixels between the taps of a double tap (6 points)
extern Fixed		gSamplesToTicks;					// ROM 0x0c101d34 gSamplesToTicks - ticks per sample
extern ULong		gDoubleTapInterval;					// ROM 0x0c101940 gDoubleTapInterval - ticks between the taps (27)
extern ULong		useStylus;							// ROM 0x0c10124c useStylus (0x10000: the tablet is turned on)
extern FPoint		gTabScale;							// ROM 0x0c101980 gTabScale - (8.0, 8.0)

// what the click-event watcher knows of a stroke (0x1c bytes)
struct StrokeHiliteState
{
	FPoint		fStart;				// +0x00  the first point (the midpoint of a tap once it is one)
	ULong		fDownTime;			// +0x08
	ULong		fUpTime;			// +0x0c
	Boolean		fNotTap;			// +0x10  moved past gMaxTapSize, or held over 10 ticks
	Boolean		fNotDoubleTap;		// +0x11  not within gDoubleTapInterval of the last tap
	Boolean		fMoved;				// +0x12  moved past twice gHiliteDistance
	UChar		fDecided;			// +0x13  how many of the three are known (3: nothing left to watch)
	ULong		fSamples;			// +0x14  points so far
	ULong		fEvent;				// +0x18  what it became: 0 nothing yet, 1 a tap, 2 a hilite click, 3 a double tap, 4 a tap-and-drag, 5 nothing
};
extern StrokeHiliteState	oldHilite;					// ROM 0x0c103fa0 oldHilite - the last stroke's
extern StrokeHiliteState	newHilite;					// ROM 0x0c103fbc newHilite - the current stroke's

void	InitHiliteGlobals(StrokeHiliteState* state);		// ROM 0x001fd068 InitHiliteGlobals__FP17StrokeHiliteState
void	InitHiliteState(TStroke* stroke, StrokeHiliteState* last, StrokeHiliteState* state);	// ROM 0x001fd094 InitHiliteState__FP7TStrokeP17StrokeHiliteStateT2 - the state started for a new stroke, judged against the last
Boolean	CheckHiliteState(TStroke* stroke, StrokeHiliteState* last, StrokeHiliteState* state, Boolean done);	// ROM 0x001fd184 CheckHiliteState__FP7TStrokeP17StrokeHiliteStateT2Uc - the stroke's newest point judged; ==> whether the stroke world should look

void	SetupDistances(void);								// ROM 0x001fc7f8 SetupDistances__Fv
void	RealStrokeInit(void);								// ROM 0x001fc8dc RealStrokeInit__Fv - the queue made (the inker's)
void	StrokeInit(void);									// ROM 0x001fd39c StrokeInit__Fv (nothing: the inker task does RealStrokeInit)
void	StrokeReInit(void);									// ROM 0x001fd530 StrokeReInit__Fv - the queue emptied and the tablet turned on
Boolean	StrokeNext(void);									// ROM 0x001fcd2c StrokeNext__Fv - a new stroke started at the head; ==> whether there was room
TStroke*	StrokeGet(void);								// ROM 0x001fcc04 StrokeGet__Fv - the next stroke for the stroke world (marked taken), nil for none
void	StrokeTime(void);									// ROM 0x001fcdec StrokeTime__Fv (the ROM: nothing; DEVIATION: the host reads the tablet here)
long	RealStrokeTime(void);								// ROM 0x001fce38 RealStrokeTime__Fv - the tablet's points read into the head stroke; ==> 0 nothing new, 1 a stroke changed, 2 a pen-down with no stroke to put it in
void	StrokeUpdate(FRect* rect);							// ROM 0x001fd3a0 StrokeUpdate__FP5FRect - the queued strokes in the rect drawn (NOT YET: the inker)
void	ClearStrokeBuf(void);								// ROM 0x001fd4dc ClearStrokeBuf__Fv - every queued stroke disposed
Boolean	CheckStrokeQueueEvents(ULong start, ULong duration);	// ROM 0x001fd59c CheckStrokeQueueEvents__FUlT1 - whether an untaken stroke with points began in the interval
Boolean	ScanStrokeQueueEvents(ULong start, ULong duration, Boolean takenToo);	// ROM 0x001fd5a4 ScanStrokeQueueEvents__FUlT1Uc
Boolean	AbandonedStroke(TStroke* stroke, ULong before);		// ROM 0x001fd660 AbandonedStroke__FP7TStrokeUl - an untaken, done stroke with points whose pen-up was before the time
Boolean	NukeEgregiousStrokes(FRect* rect);					// ROM 0x001fc9e0 NukeEgregiousStrokes__FP5FRect - the strokes abandoned for 10 seconds disposed, their boxes united; ==> whether any

#endif	/* __STROKEQUEUE_H */
