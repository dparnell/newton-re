/*
	File:		hal/host/HostTablet.cpp

	Contains:	The host's tablet, over the tablet buffer.
*/

#include "Journal.h"
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


static long	HostTabletBypass(Boolean start);

void
HostTabletInit(void)
{
	TBCTabletBufferInit(nil);
	gHostTabletHead = gHostTabletTail = 0;
	gHostWaitHook = HostTabletWait;
	gTabletDriverBypass = HostTabletBypass;
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


// The host's tablet driver's state, as TResistiveTablet keeps its own:
// 0 idle, 1 the pen down, 8 bypassed.  StartBypassTablet
// (TResistiveTablet::StartBypassTablet 0x0005ad04) is refused while the
// pen is down; StopBypassTablet (0x0005ad50) ends it (the driver's PenUp,
// which the host has no record to make for).
static long	gHostTabletState = 0;

static long
HostTabletBypass(Boolean start)
{
	if (start)
	{
		if (gHostTabletState != 0 && gHostTabletState != 8)
			return -1;
		gHostTabletState = 8;
		return 0;
	}
	if (gHostTabletState != 8)
		return -1;
	gHostTabletState = 0;
	return 0;
}


Boolean
HostTabletBypassed(void)
{
	return gHostTabletState == 8;
}


void
HostTabletPenState(Boolean down)
{
	if (gHostTabletState != 8)
		gHostTabletState = down ? 1 : 0;
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


// The queue is the wait hook's, and the wait hook only runs when there
// is no inker task - a task's Wait sleeps in the kernel, so nothing would
// ever feed it.  With the inker running, a queued record therefore goes
// straight into the tablet buffer, where the inker reads it, which is
// also what the window does with a real pen.  Without that, a script's
// pen would never reach the ROM's own tracking loops: they read the
// stroke over and over without waiting, and would spin for ever.
static Boolean				gInkerRunning = false;
// A script that wants its pen to arrive a sample a tick, as a real pen's
// does - to be held down while a view tracks it - asks for the records
// to be paced: they stay queued and the inker feeds one per tick.  (A
// ROM loop that spins without waiting would then spin for ever, so it is
// for the pen a view tracks with Wait, such as a drag.)
static Boolean				gPaced = false;


static Boolean
FeedDirectly(void)
{
	return gInkerRunning && !gPaced;
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
	T h e   i n k e r ' s   s t a n d - i n
------------------------------------------------------------------------------*/

static std::atomic<bool>	gInkerStop(false);
static TUPort*				gInkerNewtPort = nil;

// the task: every tick the queued records (a test's) and the buffer read
// into the stroke queue; when a stroke changed, the newt world woken with
// the inker's event - {'newt, 'idle, 'inkr}, sent asynchronously to the
// Newt port as TInker::LCDEntry 0x002150ec does after RealStrokeTime -
// so its event loop idles the strokes (the clicks reach the views) even
// when its idle timer is stopped
static void
HostInkerMain(void)
{
	TUAsyncMessage message;
	message.Init(true);
	static ULong event[4] = { 'newt', 'idle', 'inkr', 0 };
	while (!gInkerStop.load())
	{
		Wait(1);
		HostTabletPump();
		if (StrokeTime() != 0 && gInkerNewtPort != nil)
			gInkerNewtPort->Send(&message, event, sizeof(event), 0);
	}
	gInkerRunning = false;
}


// TInker::SetNewtPort 0x002150e4 (the ROM's inker's) - the port woken
void
HostInkerSetNewtPort(TUPort* port)
{
	gInkerNewtPort = port;
}


Boolean
HostInkerStart(void)
{
	if (!gOSIsRunning || gCurrentTask == nil || gInkerRunning)
		return false;
	gInkerStop.store(false);
	TUTask task;
	if (task.Init((TaskProcPtr) HostInkerMain, 0x2000, 0, nil, kUserTaskPriority, 'inkr') != noErr)
		return false;
	gInkerRunning = true;
	task.Start();
	return true;
}


void
HostInkerStop(void)
{
	gInkerStop.store(true);
}
