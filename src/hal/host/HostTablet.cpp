/*
	File:		hal/host/HostTablet.cpp

	Contains:	The host's tablet, over the tablet buffer.
*/

#include "Journal.h"
#include "TabletDriver.h"
#include "Inker.h"
#include "Rects.h"
#include "Ports.h"
#include "HostTablet.h"
#include "TabletBuffer.h"
#include "StrokeQueue.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "UserPorts.h"
#include "KernelGlobals.h"
#include "hal/Timer.h"
#include "Host.h"			// HostAdvanceClock
#include "NewtonTime.h"
#include <atomic>

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
	Boolean wasEmpty = gHostTabletHead == gHostTabletTail;
	gHostTabletRing[gHostTabletTail].sample = sample;
	gHostTabletRing[gHostTabletTail].time = time;
	gHostTabletTail = next;
	// the inker woken for the first record only: it then feeds them an
	// idle at a time (every 50 ms while the pen is down) - a wake-up per
	// record would have it feed one per wake-up, all of them at once,
	// and a paced pen would be up before anything tracking it looked
	if (gInker != nil && wasEmpty)
		TBCWakeUpInker(0);
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


// The buffer made ready and the wait hook installed; the host's tablet
// driver registered for TabInitialize (the inker's) to find, and made and
// put in use straight away for a test with no inker (DEVIATION: the ROM
// always has the inker make it; when the inker starts, TabInitialize makes
// the one it uses in place of this).
void
HostTabletInit(void)
{
	TBCTabletBufferInit(nil);
	gHostTabletHead = gHostTabletTail = 0;
	gHostWaitHook = HostTabletWait;
	HostTabletRegisterDriver();
	if (gTabletDriver == nil)
	{
		gTabletDriver = HostTabletMakeDriver();
		if (gTabletDriver != nil)
		{
			gTabletDriver->Init(qdGlobals.fScreenBits.bounds);
		}
	}
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


// The tablet bypassed (the journal playing): the window's pen is ignored.
Boolean
HostTabletBypassed(void)
{
	return HostTabletDriverBypassed();
}


// The time a record is stamped with.
//
// The ROM's tablet driver calls InsertTabletSample from its sampling
// interrupt, and an interrupt runs in supervisor mode, where
// GetGlobalTime reads the kernel's clock straight off the timer.  A task
// asking the same question makes a system call instead, and that is what
// Ticks() would do here - which is fatal from the host's window thread:
// it is not a Newton task at all, so the SWI would run the kernel's glue
// and its exit path on a thread the runtime knows nothing about, with
// gCurrentTask still pointing at the machine's own.  The host therefore
// stamps the record itself, off the same clock the supervisor would read,
// and never leaves the time as 0 for the buffer to fill in.
static ULong
HostTabletNow(void)
{
	TTime now;
	GetClock(&now.time);
	return now.ConvertTo(kMacTicks) & 0x7fffffff;
}


void
HostTabletPenDown(long x, long y, ULong time)
{
	if (time == 0)
		time = HostTabletNow();
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
	if (time == 0)
		time = HostTabletNow();
	InsertTabletSample(kTabletPenUp, time);
}


// The test pen's records read before a script goes on: with the inker
// running the strokes are read by its task, woken by each record, while
// the script that wrote them carries on in its own - so a script's
// IdleStrokes would otherwise look before there was anything to see.  The
// caller sleeps a millisecond at a time (up to a second) until the inker
// has taken everything and the stroke world has read it.
void
HostTabletSettle(void)
{
	if (gInker == nil || !gOSIsRunning || gCurrentTask == nil)
		return;
	for (int i = 0; i < 1000 && !TBCTabletBufferEmpty(); i++)
		Sleep(kMilliseconds);
}


// The queue is the wait hook's, and the wait hook only runs when there
// is no inker task - a task's Wait sleeps in the kernel, so nothing would
// ever feed it.  With the inker running, a queued record therefore goes
// straight into the tablet buffer, where the inker reads it, which is
// also what the window does with a real pen.  Without that, a script's
// pen would never reach the ROM's own tracking loops: they read the
// stroke over and over without waiting, and would spin for ever.
// A script that wants its pen to arrive a sample a tick, as a real pen's
// does - to be held down while a view tracks it - asks for the records
// to be paced: they stay queued and the inker feeds one per tick.  (A
// ROM loop that spins without waiting would then spin for ever, so it is
// for the pen a view tracks with Wait, such as a drag.)
static Boolean				gPaced = false;


static Boolean
FeedDirectly(void)
{
	return gInker != nil && !gPaced;
}


void
HostTabletSetPaced(Boolean paced)
{
	gPaced = paced;
}


void
HostTabletQueuePenDown(long x, long y, ULong time)
{
	if (FeedDirectly())
	{
		HostTabletPenDown(x, y, time);
		return;
	}
	Enqueue(kTabletPenDown, time);
	Enqueue(HostTabletSample(x, y, 3), 0);
}


void
HostTabletQueuePenMove(long x, long y, ULong pressure)
{
	if (FeedDirectly())
	{
		HostTabletPenMove(x, y, pressure);
		return;
	}
	Enqueue(HostTabletSample(x, y, pressure), 0);
}


void
HostTabletQueuePenUp(ULong time)
{
	if (FeedDirectly())
	{
		HostTabletPenUp(time);
		return;
	}
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


// The wait hook: a record per tick, then the strokes read.  Each tick
// waited moves the clock on by a tick, as a Wait would take that long
// (a sixtieth of a second: 0xf000 of the 3.6864 MHz clock).
void
HostTabletWait(ULong ticks)
{
	for (ULong i = 0; i < ticks; i++)
	{
		JournalAgentIdle();			// (the tests run without the OS, so without the test agent that plays the journal)
		HostTabletPump();
		HostAdvanceClock(0xf000);
	}
	StrokeTime();
}


/*------------------------------------------------------------------------------
	W i t h   t h e   i n k e r
------------------------------------------------------------------------------*/

// The inker's idle (recognition/Inker.h's gInkerHostIdleHook): a paced
// pen's next queued record fed into the buffer - the inker reads it - so
// the records arrive one per idle while the pen is down.  ==> whether any
// are left, which keeps the inker idling.
static Boolean
HostTabletInkerIdle(void)
{
	HostTabletRecord record;
	if (!Dequeue(&record))
		return false;
	// (straight into the buffer, not InsertTabletSample: that wakes the
	// inker, whose next idle would then come at once and feed the next
	// record, and so on - the whole stroke in one go, the pen up before
	// anything tracking it had looked)
	if (record.sample != kTabletNoSample)
	{
		TBCInsertTabletSample(record.sample, record.time);
		HostTabletRecord pt;
		if (record.sample == kTabletPenDown && Dequeue(&pt))
			TBCInsertTabletSample(pt.sample, pt.time);
	}
	return HostTabletQueued() != 0;
}

static struct HostTabletInkerHook
{
	HostTabletInkerHook()	{ gInkerHostIdleHook = HostTabletInkerIdle; }
} sHostTabletInkerHook;
