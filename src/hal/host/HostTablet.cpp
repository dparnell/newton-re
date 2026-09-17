/*
	File:		hal/host/HostTablet.cpp

	Contains:	The host's tablet, over the tablet buffer.
*/

#include "HostTablet.h"
#include "TabletBuffer.h"
#include "StrokeQueue.h"
#include "UserBoot.h"

struct HostTabletRecord
{
	ULong	sample;			// kTabletPenDown, kTabletPenUp, a sample word, or kTabletNoSample for a tick with nothing
	ULong	time;
};
// the queued records: a ring of 4096
const long kHostTabletQueueSize = 4096;
static HostTabletRecord	gHostTabletRing[kHostTabletQueueSize];
static long		gHostTabletHead = 0;		// the next to feed
static long		gHostTabletTail = 0;		// the next slot to fill

static void
Enqueue(ULong sample, ULong time)
{
	long next = (gHostTabletTail + 1) % kHostTabletQueueSize;
	if (next == gHostTabletHead)
		return;					// full: dropped
	gHostTabletRing[gHostTabletTail].sample = sample;
	gHostTabletRing[gHostTabletTail].time = time;
	gHostTabletTail = next;
}

static Boolean
Dequeue(HostTabletRecord* record)
{
	if (gHostTabletHead == gHostTabletTail)
		return false;
	*record = gHostTabletRing[gHostTabletHead];
	gHostTabletHead = (gHostTabletHead + 1) % kHostTabletQueueSize;
	return true;
}


void
HostTabletInit(void)
{
	TBCTabletBufferInit(nil);
	gHostTabletHead = gHostTabletTail = 0;
	gHostWaitHook = HostTabletWait;
}


// The sample word: x in bits 18-31 and y in bits 4-17, both in eighths of
// a pixel, the pressure in the low nibble.
ULong
HostTabletSample(long x, long y, ULong pressure)
{
	ULong x8 = (ULong) (x * 8) & 0x3fff;
	ULong y8 = (ULong) (y * 8) & 0x3fff;
	return (x8 << 18) | (y8 << 4) | (pressure & 7);
}


void
HostTabletPenDown(long x, long y, ULong time)
{
	InsertTabletSample(kTabletPenDown, time);
	InsertTabletSample(HostTabletSample(x, y, 3), 0);
}


void
HostTabletPenMove(long x, long y, ULong pressure)
{
	InsertTabletSample(HostTabletSample(x, y, pressure), 0);
}


void
HostTabletPenUp(ULong time)
{
	InsertTabletSample(kTabletPenUp, time);
}


void
HostTabletQueuePenDown(long x, long y, ULong time)
{
	Enqueue(kTabletPenDown, time);
	Enqueue(HostTabletSample(x, y, 3), 0);
}


void
HostTabletQueuePenMove(long x, long y, ULong pressure)
{
	Enqueue(HostTabletSample(x, y, pressure), 0);
}


void
HostTabletQueuePenUp(ULong time)
{
	Enqueue(kTabletPenUp, time);
}


void
HostTabletQueueNothing(void)
{
	Enqueue(kTabletNoSample, 0);
}


long
HostTabletQueued(void)
{
	return (gHostTabletTail - gHostTabletHead + kHostTabletQueueSize) % kHostTabletQueueSize;
}


// The next queued record fed into the buffer (a pen-down's sample goes
// with it, so the stroke starts at once) and the strokes read.
Boolean
HostTabletPump(void)
{
	HostTabletRecord record;
	if (!Dequeue(&record))
		return false;
	if (record.sample != kTabletNoSample)
	{
		InsertTabletSample(record.sample, record.time);
		HostTabletRecord pt;
		if (record.sample == kTabletPenDown && Dequeue(&pt))
			InsertTabletSample(pt.sample, pt.time);
	}
	StrokeTime();
	return true;
}


// The wait hook: a record per tick, then the strokes read.
void
HostTabletWait(ULong ticks)
{
	for (ULong i = 0; i < ticks; i++)
		HostTabletPump();
	StrokeTime();
}
