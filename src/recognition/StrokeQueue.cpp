/*
	File:		recognition/StrokeQueue.cpp

	Contains:	The stroke queue between the inker and the stroke world, the
				click-event watcher, TSStroke.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "StrokeQueue.h"
#include "TabletBuffer.h"
#include "Unit.h"
#include "FixedMath.h"
#include <stdio.h>

static StrokeQueue	sQ;								// ROM 0x0c103e4c sQ - the queue gStrokeQ points to
StrokeQueue*	gStrokeQ = &sQ;						// ROM 0x0c101988 gStrokeQ
Boolean		gStrokeValid = true;					// ROM 0x0c101d24 gStrokeValid
ULong		gTickOff = 0;							// ROM 0x0c101d18 gTickOff
UShort		gHiliteDistance = 4;					// ROM 0x0c101d28 gHiliteDistance
UShort		gMaxTapSize = 6;						// ROM 0x0c101d2c gMaxTapSize
UShort		gDoubleTapDistance = 6;					// ROM 0x0c101d30 gDoubleTapDistance
Fixed		gSamplesToTicks = 0x10000;				// ROM 0x0c101d34 gSamplesToTicks
ULong		gDoubleTapInterval = 27;				// ROM 0x0c101940 gDoubleTapInterval
ULong		useStylus = 0x10000;					// ROM 0x0c10124c useStylus
FPoint		gTabScale = { 0x80000, 0x80000 };		// ROM 0x0c101980 gTabScale
StrokeHiliteState	oldHilite;						// ROM 0x0c103fa0 oldHilite
StrokeHiliteState	newHilite;						// ROM 0x0c103fbc newHilite
static Boolean	gPenDown = false;					// (the ROM's byte at 0x0c101d38) the pen is down (RealStrokeTime has seen its first point)
extern ULong	gLastPenTip;						// ROM 0x0c1008bc gLastPenTip (Stroke.cpp)


/*------------------------------------------------------------------------------
	T h e   t a b l e t   g l u e
------------------------------------------------------------------------------*/

// ROM 0x0011d348 TabInit__Fv
void
TabInit(void)
{
	xTabInit();
}


// ROM 0x0011d34c TabOn__Fv
void
TabOn(void)
{
	xTabOn();
}


// ROM 0x0011d350 GetTabPt__FP5TabPt
Boolean
GetTabPt(TabPt* pt)
{
	return xGetTabPt(pt);
}


// ROM 0x0011d354 LastTabPt__FP5TabPt
Boolean
LastTabPt(TabPt* pt)
{
	return xLastPoint(pt);
}


// ROM 0x0011d35c GetDownTime__Fv
ULong
GetDownTime(void)
{
	return xGetDownTime();
}


// ROM 0x0011d360 GetUpTime__Fv
ULong
GetUpTime(void)
{
	return xGetUpTime();
}


// ROM 0x0011d364 GetTabScale__FP6FPoint
void
GetTabScale(FPoint* scale)
{
	xGetTabScale(scale);
}


// ROM 0x001f6f50 CheapDistPoint__FP6FPointT1
// The distance between two points, roughly: the longer leg plus a
// fraction of the shorter (the ROM's shifts).
long
CheapDistPoint(const FPoint* a, const FPoint* b)
{
	long dx = a->x - b->x;
	long dy = a->y - b->y;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	long big = dx, small = dy;
	if (dy > dx)
	{
		big = dy;
		small = dx;
	}
	if (small < big >> 1)
		return big + (small >> 2) - (small >> 7) - (small >> 8) - (small >> 9);
	return big - (big >> 3) - (big >> 5) - (big >> 6) + (small >> 1) + (small >> 4) + (small >> 5);
}


// ROM 0x001a6908 GetMidPoint
void
GetMidPoint(const FPoint* a, const FPoint* b, FPoint* mid)
{
	mid->x = (a->x + b->x) >> 1;
	mid->y = (a->y + b->y) >> 1;
}


/*------------------------------------------------------------------------------
	T S S t r o k e
------------------------------------------------------------------------------*/

// ROM 0x00220804 Make__8TSStrokeSFUl
TSStroke*
TSStroke::Make(ULong count)
{
	TSStroke* stroke = new TSStroke;
	if (stroke != nil)
	{
		stroke->fData = nil;
		stroke->fClickEvent = 0;
		long err = stroke->IStroke(count);
		stroke->fInkPending = false;
		stroke->fFirstPoint.v = stroke->fFirstPoint.h = -1;
		stroke->fInkPoint.v = stroke->fInkPoint.h = -1;
		if (err != 0)
		{
			stroke->Dispose();
			stroke = nil;
		}
	}
	return stroke;
}


// ROM 0x00220724 AddPoint__8TSStrokeFP5TabPt
// The point added; the first is kept, and the inker's next point set when
// it has none pending.  ==> 0, or 1 for no memory.
long
TSStroke::AddPoint(TabPt* pt)
{
	if (TStroke::AddPoint(pt) != 0)
		return 1;
	if (fCount == 1)
	{
		fFirstPoint.h = (short) ((pt->x + 0x8000) >> 16);
		fFirstPoint.v = (short) ((pt->y + 0x8000) >> 16);
	}
	if (!fInkPending)
	{
		fInkPoint.h = (short) ((pt->x + 0x8000) >> 16);
		fInkPoint.v = (short) ((pt->y + 0x8000) >> 16);
		fInkPending = true;
	}
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   c l i c k - e v e n t   w a t c h e r
------------------------------------------------------------------------------*/

// ROM 0x001fd068 InitHiliteGlobals__FP17StrokeHiliteState
// Nothing known: no event, not a tap, all decided, no times.
void
InitHiliteGlobals(StrokeHiliteState* state)
{
	state->fEvent = 0;
	state->fNotTap = true;
	state->fDecided = 3;
	oldHilite.fUpTime = 0;
	state->fDownTime = 0;
}


// ROM 0x001fd094 InitHiliteState__FP7TStrokeP17StrokeHiliteStateT2
// The state for a new stroke: its first point and down time; a double
// tap is still possible when the last stroke was a tap and this one came
// within gDoubleTapInterval of its pen-up; a tap-and-drag when the last
// was a tap-and-drag's first half (event 5) within 41 ticks - then the
// tap and the move are already decided.
void
InitHiliteState(TStroke* stroke, StrokeHiliteState* last, StrokeHiliteState* state)
{
	if (last->fDecided != 3 && last->fEvent == 0)
		InitHiliteGlobals(last);
	state->fDownTime = stroke->fDownTime;
	state->fUpTime = 0;
	stroke->GetFPoint(0, &state->fStart);
	state->fDecided = 0;
	if (last->fEvent == 1 && state->fDownTime - last->fUpTime < gDoubleTapInterval)
		state->fNotDoubleTap = false;
	else
	{
		state->fNotDoubleTap = true;
		state->fDecided = 1;
	}
	if (state->fDownTime - last->fUpTime < 41 && last->fEvent == 5)
	{
		state->fNotTap = true;
		state->fDecided++;
		state->fMoved = true;
		state->fDecided++;
	}
	else
	{
		state->fNotTap = false;
		state->fMoved = false;
	}
	state->fSamples = 1;
	state->fEvent = 0;
}


// ROM 0x001fd184 CheckHiliteState__FP7TStrokeP17StrokeHiliteStateT2Uc
// The stroke's newest point judged (done: it is the pen-up).  While the
// pen has not moved twice gHiliteDistance from the start, holding it 45
// ticks within gHiliteDistance (or 90 within twice) makes a hilite click
// (the stroke's event 4).  While the stroke could still be a tap, moving
// past gMaxTapSize or staying down over 10 ticks rules it out - and when
// a double tap was still possible, and the stroke has no event yet, a
// press within gDoubleTapDistance of the last tap is a tap-and-drag
// (event 5); the pen-up of a stroke still a tap makes it a double tap
// (3) when within gDoubleTapDistance of the last tap and the interval
// allowed, else a tap (2).  Once all three are decided with no event the
// stroke is marked processed (1).  ==> whether the stroke world should
// look at the stroke now.
Boolean
CheckHiliteState(TStroke* stroke, StrokeHiliteState* last, StrokeHiliteState* state, Boolean done)
{
	state->fUpTime = stroke->fUpTime;
	state->fSamples++;
	Boolean look = false;
	if (state->fEvent != 0)
		return false;
	FPoint pt;
	stroke->GetFPoint(stroke->Count() - 1, &pt);
	long dist = CheapDistPoint(&state->fStart, &pt);
	ULong downTime = state->fDownTime;
	if (!state->fMoved)
	{
		Fixed hilite = (Fixed) gHiliteDistance << 16;
		if (dist >= hilite * 2)
		{
			state->fMoved = true;
			state->fDecided++;
		}
		else if ((state->fSamples > 45 && dist < hilite) || (state->fSamples > 90 && dist < hilite * 2))
		{
			stroke->fClickEvent = kHiliteClick;
			state->fEvent = 2;
			return true;
		}
	}
	if (!state->fNotTap || !state->fNotDoubleTap)
	{
		if (dist > (Fixed) gMaxTapSize << 16 || state->fUpTime - downTime > 10)
		{
			if (!state->fNotTap)
			{
				state->fNotTap = true;
				state->fDecided++;
			}
			if (!state->fNotDoubleTap)
			{
				look = true;
				state->fNotDoubleTap = true;
				state->fDecided++;
				if (!done && stroke->fClickEvent == 0 && CheapDistPoint(&last->fStart, &state->fStart) < (Fixed) gDoubleTapDistance << 16)
				{
					stroke->fClickEvent = kTapDragClick;
					state->fEvent = 4;
					return look;
				}
			}
		}
		else if (done)
		{
			GetMidPoint(&state->fStart, &pt, &state->fStart);
			if (!state->fNotDoubleTap && CheapDistPoint(&last->fStart, &state->fStart) < (Fixed) gDoubleTapDistance << 16)
			{
				state->fEvent = 3;
				stroke->fClickEvent = kDoubleTapClick;
			}
			else
			{
				stroke->fClickEvent = kTapClick;
				state->fEvent = 1;
			}
			return look;
		}
	}
	if (state->fDecided == 3)
	{
		stroke->fClickEvent = kProcessedClick;
		state->fEvent = 5;
	}
	return false;
}


/*------------------------------------------------------------------------------
	T h e   q u e u e
------------------------------------------------------------------------------*/

// ROM 0x001fc7f8 SetupDistances__Fv
// The distances scaled to the screen: 4, 6 and 6 points at the screen's
// resolution (NOT YET RECONSTRUCTED: the gestalt's screen resolution -
// 100 dpi, the MP2100's), and the ticks a sample takes.
void
SetupDistances(void)
{
	long dpi = 100;
	Fixed pixelsPerPoint = FixedDivide((Fixed) dpi << 16, 72 << 16);
	gHiliteDistance = (UShort) ((FixedMultiply(pixelsPerPoint, 4 << 16) + 0x8000) >> 16);
	gMaxTapSize = (UShort) ((FixedMultiply(pixelsPerPoint, 6 << 16) + 0x8000) >> 16);
	gDoubleTapDistance = (UShort) ((FixedMultiply(pixelsPerPoint, 6 << 16) + 0x8000) >> 16);
	ULong samplesPerSecond = GetSampleRate() / 0x384000;
	gSamplesToTicks = FixedDivide(60 << 16, samplesPerSecond << 16);
}


// ROM 0x001fc8dc RealStrokeInit__Fv
// The queue made empty with a stroke ready at the head, the watcher
// reset, the distances set up (NOT YET: the queue's semaphore, the inker's).
void
RealStrokeInit(void)
{
	for (long i = 0; i < 64; i++)
		gStrokeQ->fStrokes[i] = nil;
	gStrokeQ->fStrokes[0] = TSStroke::Make(0);
	gStrokeQ->fHead = 0;
	gStrokeQ->fTail = 0;
	StrokeReInit();
	InitHiliteGlobals(&oldHilite);
	SetupDistances();
}


// ROM 0x001fd39c StrokeInit__Fv
// Nothing: the inker task does RealStrokeInit when it starts.
// DEVIATION: the host has no inker task, so the queue is made here.
void
StrokeInit(void)
{
	RealStrokeInit();
}


// ROM 0x001fd530 StrokeReInit__Fv
// The queue emptied, a stroke ready at the head, the tablet turned on.
void
StrokeReInit(void)
{
	ClearStrokeBuf();
	gStrokeQ->fStrokes[0] = TSStroke::Make(0);
	gStrokeQ->fHead = 0;
	gStrokeQ->fTail = 0;
	TabInit();
	if (useStylus >> 16)
		TabOn();
	gTabScale.x = 0x80000;
	gTabScale.y = 0x80000;
}


// ROM 0x001fcd2c StrokeNext__Fv
// A new stroke started in the slot after the head (locked in the heap
// and flagged as the inker's); none when the slot is still in use.
// ==> whether there is a stroke to write to (gStrokeValid).
Boolean
StrokeNext(void)
{
	long next = gStrokeQ->fHead + 1;
	if (next > 63)
		next -= 64;
	if (gStrokeQ->fStrokes[next] == nil)
	{
		TSStroke* stroke = TSStroke::Make(0);
		gStrokeValid = stroke != nil;
		if (stroke != nil)
		{
			stroke->Lock();
			gStrokeQ->fStrokes[next] = stroke;
			stroke->SetFlags(kStrokeInQueue);
			gStrokeQ->fHead = (UShort) next;
		}
	}
	else
		gStrokeValid = false;
	return gStrokeValid;
}


// ROM 0x001fcc04 StrokeGet__Fv
// The next stroke for the stroke world: the one at the tail when it has
// not been taken (the tail moves on past a taken or empty slot); a stroke
// with points is marked taken (and inkless when the default ink is off)
// and answered.  Nil when there is none - or the tail's stroke has no
// points yet.
TStroke*
StrokeGet(void)
{
	TStroke* got = nil;
	StrokeTime();
	TSStroke* stroke = gStrokeQ->fStrokes[gStrokeQ->fTail];
	if (stroke == nil || stroke->TestFlags(kStrokeTaken))
	{
		if (gStrokeQ->fTail == gStrokeQ->fHead)
			return nil;
		gStrokeQ->fTail++;
		if (gStrokeQ->fTail > 63)
			gStrokeQ->fTail -= 64;
		stroke = gStrokeQ->fStrokes[gStrokeQ->fTail];
	}
	if (stroke != nil)
	{
		Boolean acquired = AcquireStroke(stroke);
		if (stroke->Count() != 0)
		{
			stroke->SetFlags(kStrokeTaken);
			got = stroke;
			if (!gDefaultInk)
				stroke->SetFlags(kStrokeNoInk);
		}
		if (acquired)
			ReleaseStroke();
	}
	return got;
}


// ROM 0x001fcdec StrokeTime__Fv
// The ROM's does nothing: the inker task reads the tablet.  DEVIATION:
// the host has no inker task - the tablet buffer is read here, as the
// inker would (its own read index just follows the writer's: no ink is
// drawn, NOT YET), until nothing is left.
void
StrokeTime(void)
{
	while (!TBCInkerBufferEmpty())
		TBCIncInkerIndex(1);
	while (RealStrokeTime() != 0)
		;
}


// ROM 0x001fce38 RealStrokeTime__Fv
// The tablet's points read into the stroke at the head until something
// happens: a point while a stroke is being written is added to it - the
// first point takes the pen-down time and the pen tip and starts the
// click-event watcher, the rest re-estimate the up time from the samples
// and consult the watcher; the pen-up ends the stroke with the up time,
// judges it, remembers it as the last for the watcher, and starts the
// next stroke (when the ended one has points).  A point with no stroke
// to write to (the queue full) is dropped, and the pen-down time with
// it.  ==> 0 for nothing new, 1 for a stroke changed, 2 for a pen-down
// that had nowhere to go.
long
RealStrokeTime(void)
{
	TSStroke* stroke = nil;
	long result = 0;
	do
	{
		TabPt pt;
		if (!GetTabPt(&pt))
			break;
		if (gStrokeValid)
		{
			stroke = gStrokeQ->fStrokes[gStrokeQ->fHead];
			if (stroke == nil)
				gStrokeValid = false;
		}
		if (!LastTabPt(&pt))
		{
			if (!gStrokeValid)
			{
				if (!gPenDown)
				{
					GetDownTime();
					gPenDown = true;
					result = 2;
					break;
				}
			}
			else
			{
				if (!gPenDown)
				{
					gPenDown = true;
					result = 1;
				}
				stroke->AddPoint(&pt);
				if (stroke->fDownTime == 0)
				{
					stroke->fDownTime = GetDownTime() + gTickOff;
					stroke->UnsetFlags(0xff00);
					stroke->SetFlags(gLastPenTip << 8);
					InitHiliteState(stroke, &oldHilite, &newHilite);
				}
				else
				{
					Fixed elapsed = FixedMultiply((Fixed) stroke->Count() << 16, gSamplesToTicks);
					stroke->fUpTime = stroke->fDownTime + (((elapsed + 0x8000) >> 16) & 0xffff);
					if (CheckHiliteState(stroke, &oldHilite, &newHilite, false))
						result = 1;
				}
			}
		}
		else
		{
			gPenDown = false;
			if (gStrokeValid)
			{
				result = 1;
				stroke->fUpTime = GetUpTime() + gTickOff;
				CheckHiliteState(stroke, &oldHilite, &newHilite, true);
				oldHilite = newHilite;
				oldHilite.fUpTime = stroke->fUpTime;
				if (stroke->Count() == 0)
					printf("<<<<nopointstroke\r");
				else
					StrokeNext();
				stroke->EndStroke();
				break;
			}
			GetUpTime();
			StrokeNext();
		}
	} while (result == 0);
	return result;
}


// ROM 0x001fd3a0 StrokeUpdate__FP5FRect
// The queued strokes still the inker's, with points, that are not done
// or lie in the rect, drawn.  NOT YET RECONSTRUCTED: the inker (Draw only
// flags them).
void
StrokeUpdate(FRect* rect)
{
	TStroke* toDraw[64];
	long count = 0;
	for (long i = 0; i < 64; i++)
	{
		TSStroke* stroke = gStrokeQ->fStrokes[i];
		if (stroke != nil && stroke->TestFlags(kStrokeInQueue) && stroke->Count() != 0)
		{
			Boolean inRect = true;
			if (stroke->TestFlags(kStrokeDrawnWhenDone))
			{
				FRect sect;
				inRect = SectRectangle(&sect, rect, &stroke->fBBox);
			}
			if (inRect)
				toDraw[count++] = stroke;
		}
	}
	for (long i = 0; i < count; i++)
		toDraw[i]->Draw();
}


// ROM 0x001fd474 UnbufferStroke__FP7TStroke
// The stroke taken out of the queue (a unit that owns it is about to
// dispose of it).  The ROM holds the queue's semaphore meanwhile.
void
UnbufferStroke(TStroke* stroke)
{
	for (long i = 0; i < 64; i++)
		if (gStrokeQ->fStrokes[i] == stroke)
		{
			gStrokeQ->fStrokes[i] = nil;
			break;
		}
}


// ROM 0x001fd4dc ClearStrokeBuf__Fv
// Every queued stroke disposed.
void
ClearStrokeBuf(void)
{
	for (long i = 0; i < 64; i++)
	{
		TSStroke* stroke = gStrokeQ->fStrokes[i];
		if (stroke != nil)
		{
			stroke->Dispose();
			gStrokeQ->fStrokes[i] = nil;
		}
	}
}


// ROM 0x001fd59c CheckStrokeQueueEvents__FUlT1
// Whether an untaken stroke with points went down within the interval.
Boolean
CheckStrokeQueueEvents(ULong start, ULong duration)
{
	return ScanStrokeQueueEvents(start, duration, false);
}


// ROM 0x001fd5a4 ScanStrokeQueueEvents__FUlT1Uc
// ... counting the taken ones too when asked.
Boolean
ScanStrokeQueueEvents(ULong start, ULong duration, Boolean takenToo)
{
	for (long i = 0; i < 64; i++)
	{
		TSStroke* stroke = gStrokeQ->fStrokes[i];
		if (stroke == nil)
			continue;
		if (!takenToo && stroke->TestFlags(kStrokeTaken))
			continue;
		if (stroke->Count() == 0)
			continue;
		if (stroke->fDownTime > start && stroke->fDownTime < start + duration)
			return true;
	}
	return false;
}


// ROM 0x001fd660 AbandonedStroke__FP7TStrokeUl
// An untaken, done stroke with points whose pen-up was before the time.
Boolean
AbandonedStroke(TStroke* stroke, ULong before)
{
	return stroke != nil && !stroke->TestFlags(kStrokeTaken) && stroke->Count() != 0 && stroke->Done() && stroke->fUpTime < before;
}


// ROM 0x001fc9e0 NukeEgregiousStrokes__FP5FRect
// The strokes nobody took for ten seconds disposed - from the tail while
// they are abandoned (the tail moving on; the queue re-started when it
// empties), then the rest of the ring - their boxes united into the rect.
// ==> whether any was.
Boolean
NukeEgregiousStrokes(FRect* rect)
{
	Boolean any = false;
	ULong before = GetTicks() - 600;
	UShort origTail = gStrokeQ->fTail;
	while (gStrokeQ->fTail != gStrokeQ->fHead)
	{
		long index = gStrokeQ->fTail;
		TSStroke* stroke = gStrokeQ->fStrokes[index];
		Boolean skipped = false;
		if (stroke == nil || stroke->TestFlags(kStrokeTaken))
		{
			index++;
			if (index > 63)
				index -= 64;
			skipped = true;
			stroke = gStrokeQ->fStrokes[index];
		}
		if (!AbandonedStroke(stroke, before))
			break;
		AddRect(&stroke->fBBox, rect, !any);
		stroke->Dispose();
		gStrokeQ->fStrokes[index] = nil;
		gStrokeQ->fTail = (UShort) index;
		any = true;
		if (gStrokeQ->fTail == gStrokeQ->fHead)
		{
			StrokeNext();
			break;
		}
		if (!skipped)
			break;
	}
	long index = gStrokeQ->fHead + 1;
	if (index > 63)
		index -= 64;
	while (index != origTail)
	{
		TSStroke* stroke = gStrokeQ->fStrokes[index];
		if (AbandonedStroke(stroke, before))
		{
			AddRect(&stroke->fBBox, rect, !any);
			stroke->Dispose();
			any = true;
			gStrokeQ->fStrokes[index] = nil;
		}
		index++;
		if (index > 63)
			index -= 64;
	}
	return any;
}
