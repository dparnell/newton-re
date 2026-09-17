/*
	File:		recognition/TabletBuffer.cpp

	Contains:	The tablet buffer and its reader.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "TabletBuffer.h"
#include "NewtonTime.h"

ULong	gTabData = 0;						// ROM 0x0c104458 gTabData
ULong	gTabletInkerIndex = 0;				// (0x0c10445c)
ULong	gTabletStrokerIndex = 0;			// (0x0c104460)
ULong	gTabletBuffer[kTabletBufferSize];	// (0x0c104464)
static Boolean	gTBCOnlyPollTablet = false;	// ROM 0x0c101f90 gTBCOnlyPollTablet
static Boolean	gTBCPollReady = false;		// ROM 0x0c101f94 gTBCPollReady
static Boolean	gTBCPenUp = true;			// ROM 0x0c101f9c gTBCPenUp
static Boolean	gTBCBypassTablet = false;	// ROM 0x0c101fa0 gTBCBypassTablet
static ULong	gTBCPollSample = 0;			// ROM 0x0c101f98 gTBCPollSample
TabletCollectState	gTabletCollect = { false, 0, 0, 0, kPenStateIdle, 0, 0, 0 };	// ROM 0x0c1008a8 collect

const long kTabletBufferFull = -56006;
const long kTabletNoNewSample = -56007;


/*------------------------------------------------------------------------------
	T h e   b u f f e r
------------------------------------------------------------------------------*/

static ULong
NextIndex(ULong index)
{
	return index < kTabletBufferSize - 1 ? index + 1 : 0;
}


// ROM 0x0024e4e8 TBCInsertTabletSample__FUlT1
// A record put in the ring - a sample word, a pen-down (0xd, the time), a
// pen-up (0xe, the time, a dummy sample, the time); the time is now when
// 0.  ==> 0, or kTabletBufferFull when the record would catch the reader.
// In polling mode nothing is buffered: the last sample is kept for
// TBCPollTablet, with whether the pen is up.
long
TBCInsertTabletSample(ULong sample, ULong time)
{
	if (!gTBCOnlyPollTablet)
	{
		ULong write = gTabData;
		gTabletBuffer[write] = sample;
		if (sample == kTabletPenDown)
		{
			ULong w1 = NextIndex(write);
			ULong w2 = NextIndex(w1);
			if (w1 == gTabletStrokerIndex || w2 == gTabletStrokerIndex)
				return kTabletBufferFull;
			gTabletBuffer[w1] = time != 0 ? time : Ticks();
			gTabData = w2;
		}
		else if (sample == kTabletPenUp)
		{
			ULong w1 = NextIndex(write);
			ULong w2 = NextIndex(w1);
			ULong w3 = NextIndex(w2);
			ULong w4 = NextIndex(w3);
			if (w1 == gTabletStrokerIndex || w2 == gTabletStrokerIndex || w3 == gTabletStrokerIndex || w4 == gTabletStrokerIndex)
				return kTabletBufferFull;
			ULong when = time != 0 ? time : Ticks();
			gTabletBuffer[w1] = when;
			gTabletBuffer[w2] = kTabletPenUpSample;
			gTabletBuffer[w3] = when;
			gTabData = w4;
		}
		else
		{
			ULong w1 = NextIndex(write);
			if (w1 == gTabletStrokerIndex)
				return kTabletBufferFull;
			gTabData = w1;
		}
		return 0;
	}
	if (sample != kTabletNoSample)
	{
		gTBCPollSample = sample;
		if (sample == kTabletPenUp)
		{
			if (!gTBCPenUp)
			{
				gTBCPenUp = true;
				gTBCPollReady = true;
			}
		}
		else
		{
			if ((sample & 0xf) != kTabletPenDown)
			{
				if ((sample & 0xf) > 7)
					return 0;
				gTBCPollReady = true;
			}
			gTBCPenUp = false;
		}
	}
	return 0;
}


// ROM 0x0024e834 InsertTabletSample__FUlT1
// The tablet driver's entry: the record buffered and the inker woken
// (NOT YET RECONSTRUCTED: TBCWakeUpInker - the host's stroke world polls).
long
InsertTabletSample(ULong sample, ULong time)
{
	return TBCInsertTabletSample(sample, time);
}


// ROM 0x0024e168 TBCTabletBufferInit__FP6TUPort
// The buffer emptied and the modes reset.  NOT YET RECONSTRUCTED: the
// inker's port and the 'newt/'inkr event that wakes it.
void
TBCTabletBufferInit(TUPort* /*inkerPort*/)
{
	TBCFlushTabletBuffer();
	gTBCOnlyPollTablet = false;
	gTBCPollReady = false;
	gTBCBypassTablet = false;
	gTBCPollSample = 0;
	gTBCPenUp = true;
}


// ROM 0x0024e200 TBCTabletBufferEmpty__Fv
Boolean
TBCTabletBufferEmpty(void)
{
	return gTabletInkerIndex == gTabData && gTabletStrokerIndex == gTabData;
}


// ROM 0x0024e6e8 TBCFlushTabletBuffer__Fv
// Both readers caught up with the writer.
void
TBCFlushTabletBuffer(void)
{
	gTabletInkerIndex = gTabData;
	gTabletStrokerIndex = gTabData;
}


// ROM 0x0024e700 TBCFlushInkerBuffer__Fv
// The stroker caught up with the inker.
void
TBCFlushInkerBuffer(void)
{
	gTabletStrokerIndex = gTabletInkerIndex;
}


// ROM 0x0024e328 TBCSetTabletPolling__FUc
void
TBCSetTabletPolling(Boolean polling)
{
	gTBCOnlyPollTablet = polling;
	gTBCPollReady = false;
}


// ROM 0x0024e7f8 SetTabletPolling__FUc
void
SetTabletPolling(Boolean polling)
{
	gTBCOnlyPollTablet = polling;
	gTBCPollReady = false;
}


// ROM 0x0024e318 TBCGetTabletPolling__Fv
Boolean
TBCGetTabletPolling(void)
{
	return gTBCOnlyPollTablet;
}


// ROM 0x0024e340 TBCPollTablet__FPlT1PUlPUc
// The last sample in polling mode: x and y in Fixed, the pressure, whether
// the pen is up.  ==> 0, or kTabletNoNewSample when nothing came since.
long
TBCPollTablet(long* x, long* y, ULong* pressure, Boolean* penUp)
{
	long err = 0;
	ULong sample = gTBCPollSample;
	if (x != nil)
		*x = (sample & 0xfffc0000) >> 5;
	if (y != nil)
		*y = (sample & 0x3fff0) << 9;
	if (pressure != nil)
		*pressure = sample & 0xf;
	if (penUp != nil)
		*penUp = gTBCPenUp;
	if (!gTBCPollReady)
		err = kTabletNoNewSample;
	else
		gTBCPollReady = false;
	return err;
}


// ROM 0x0024e2d8 TBCInkerBufferEmpty__Fv
Boolean
TBCInkerBufferEmpty(void)
{
	return gTabletInkerIndex == gTabData;
}


// ROM 0x0024e228 TBCGetInkerData__Fv
ULong
TBCGetInkerData(void)
{
	return gTabletBuffer[gTabletInkerIndex];
}


// ROM 0x0024e240 TBCSetInkerData__FUl
void
TBCSetInkerData(ULong word)
{
	gTabletBuffer[gTabletInkerIndex] = word;
}


// ROM 0x0024e258 TBCSetInkerData__FUlT1
void
TBCSetInkerData(ULong word, ULong offset)
{
	ULong index = gTabletInkerIndex + offset;
	if (index >= kTabletBufferSize)
		index -= kTabletBufferSize;
	gTabletBuffer[index] = word;
}


// ROM 0x0024e27c TBCIncInkerIndex__FUl
void
TBCIncInkerIndex(ULong count)
{
	gTabletInkerIndex += count;
	if (gTabletInkerIndex >= kTabletBufferSize)
		gTabletInkerIndex -= kTabletBufferSize;
}


// ROM 0x0024e6c4 TBCStrokerBufferEmpty__Fv
Boolean
TBCStrokerBufferEmpty(void)
{
	return gTabletInkerIndex == gTabletStrokerIndex;
}


// ROM 0x0024e29c TBCGetStrokerData__Fv
ULong
TBCGetStrokerData(void)
{
	return gTabletBuffer[gTabletStrokerIndex];
}


// ROM 0x0024e2b4 TBCGetStrokerData__FUl
ULong
TBCGetStrokerData(ULong offset)
{
	ULong index = gTabletStrokerIndex + offset;
	if (index >= kTabletBufferSize)
		index -= kTabletBufferSize;
	return gTabletBuffer[index];
}


// ROM 0x0024e2f8 TBCIncStrokerIndex__FUl
void
TBCIncStrokerIndex(ULong count)
{
	gTabletStrokerIndex += count;
	if (gTabletStrokerIndex >= kTabletBufferSize)
		gTabletStrokerIndex -= kTabletBufferSize;
}


// ROM 0x0024e870 StrokerBufferEmpty__Fv
Boolean
StrokerBufferEmpty(void)
{
	return TBCStrokerBufferEmpty();
}


// ROM 0x0024e890 GetStrokerData__Fv
ULong
GetStrokerData(void)
{
	return TBCGetStrokerData();
}


// ROM 0x0024e894 GetStrokerData__FUl
ULong
GetStrokerData(ULong offset)
{
	return TBCGetStrokerData(offset);
}


// ROM 0x0024e8a4 IncStrokerIndex__FUl
void
IncStrokerIndex(ULong count)
{
	TBCIncStrokerIndex(count);
}


// ROM 0x0024e8b4 GetSampleRate__Fv
// The tablet driver's sampling rate (NOT YET RECONSTRUCTED: gTabletDriver;
// the MP2100 samples 60 times a second: 60.0).
ULong
GetSampleRate(void)
{
	return 60 << 16;
}


/*------------------------------------------------------------------------------
	T h e   r e a d e r
------------------------------------------------------------------------------*/

// ROM 0x000381a4 xTabInit__Fv
void
xTabInit(void)
{
	gTabletCollect.nextTab = 0;		// (NextTab, NextDown, NextUp: 0)
	gTabletCollect.nextDown = 0;
	gTabletCollect.nextUp = 0;
}


// ROM 0x000381d4 xTabOn__Fv
void
xTabOn(void)
{
	xTabInit();
	gTabletCollect.collect = true;
}


// ROM 0x000381f8 xGetTabPt__FP5TabPt
// The next point from the buffer, through the pen state: a sample while
// the pen is down (states 6 and 4) becomes a TabPt (x and y from the
// word, the pressure, no flags); a pen-down record in the idle state (3)
// takes its time and goes to 6; a pen-up record while the pen is down
// answers the pen-up point (x -1), takes its time and goes back to 3;
// anything else is skipped.  ==> whether a point was answered.
Boolean
xGetTabPt(TabPt* pt)
{
	for (;;)
	{
		if (StrokerBufferEmpty())
			return false;
		ULong word = GetStrokerData();
		ULong type = word & 0xf;
		if (type <= 7)
		{
			if (gTabletCollect.penState == kPenStateJustDown || gTabletCollect.penState == kPenStateDown)
			{
				ULong xWord = GetStrokerData();
				ULong yWord = GetStrokerData();
				pt->y = (yWord & 0x3fff0) << 9;
				pt->x = (xWord & 0xfffc0000) >> 5;
				pt->z = (UShort) (GetStrokerData() & 0xf);
				pt->p = 0;
				IncStrokerIndex(1);
				gTabletCollect.penState = kPenStateDown;
				return true;
			}
		}
		else if (type != kTabletPenDown && type != kTabletPenUp)
		{
			IncStrokerIndex(1);
			continue;
		}
		if (gTabletCollect.penState == kPenStateIdle)
		{
			gTabletCollect.lastDownTime = GetStrokerData(1);
			IncStrokerIndex(2);
			gTabletCollect.penState = kPenStateJustDown;
			continue;
		}
		if (gTabletCollect.penState != kPenStateDown)
		{
			IncStrokerIndex(4);
			gTabletCollect.penState = kPenStateIdle;
			continue;
		}
		pt->x = -1;
		gTabletCollect.penState = kPenStateUp;
		gTabletCollect.lastUpTime = GetStrokerData(1);
		IncStrokerIndex(4);
		gTabletCollect.penState = kPenStateIdle;
		return true;
	}
}


// ROM 0x00038368 xLastPoint__FP5TabPt
Boolean
xLastPoint(TabPt* pt)
{
	return pt->x == -1;
}


// ROM 0x000383b8 xGetDownTime__Fv
ULong
xGetDownTime(void)
{
	ULong time = gTabletCollect.lastDownTime;
	gTabletCollect.lastDownTime = 0;
	return time;
}


// ROM 0x000383d0 xGetUpTime__Fv
ULong
xGetUpTime(void)
{
	ULong time = gTabletCollect.lastUpTime;
	gTabletCollect.lastUpTime = 0;
	return time;
}


// ROM 0x000383e8 xGetTabScale__FP6FPoint
void
xGetTabScale(FPoint* scale)
{
	scale->x = 0x80000;
	scale->y = 0x80000;
}
