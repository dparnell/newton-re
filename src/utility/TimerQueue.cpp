/*
	File:		utility/TimerQueue.cpp

	Contains:	TTimerQueue, TTimerElement and TTimerPort (TimerQueue.h): the
				delta queue of timers a task polls between receives.  Each
				element carries the time to go after its predecessor fires
				(fDelta); Calibrate charges the time elapsed since the last
				call against the head elements, Check fires (dequeues and
				calls Timeout on) every element that is due - within 4 ticks -
				and answers how long until the next, which TTimerPort uses as
				its receive timeout.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Layouts: TTimerElement 0x18 (vptr, fQueue +4, fNext +8, fDelta +0xc,
	fRefCon +0x10, fPrimed +0x14); TTimerQueue 0x10 (fHead, fLastCalibrate
	+4, fTimeoutInProgress +0xc); TTimerPort 0xc (the TUPort, fQueue +8).
*/

#include "TimerQueue.h"
#include "NewtonMemory.h"
#include "OSErrors.h"

// a delta this small is "now": Check fires it, and a delta is never left
// below it (so a queue of many elements at the same time keeps its order)
const TTimeout kTimerSlop = 4;


/* -------------------------------------------------------------------------------
	TTimerElement
------------------------------------------------------------------------------- */

// ROM 0x0025564c __ct__13TTimerElementFP11TTimerQueueUl
TTimerElement::TTimerElement(TTimerQueue* q, ULong refCon)
{
	fQueue = q;
	fDelta = 0;
	fPrimed = false;
	fNext = nil;
	fRefCon = refCon;
}


// ROM 0x0025569c __dt__13TTimerElementFv
TTimerElement::~TTimerElement()
{
	Cancel();
}


// ROM 0x00255894 Prime__13TTimerElementFUl
// (Re)starts the timer to fire delta from now; a zero delta does nothing.
Boolean
TTimerElement::Prime(TTimeout delta)
{
	if (fQueue != nil && delta != 0)
	{
		if (fPrimed)
			fQueue->Dequeue(this, true);
		fDelta = delta;
		fQueue->Calibrate();
		fQueue->Enqueue(this);
	}
	return fPrimed;
}


// ROM 0x002558ec Cancel__13TTimerElementFv
Boolean
TTimerElement::Cancel()
{
	if (fPrimed && fQueue != nil)
		fQueue->Dequeue(this, true);
	return true;
}


/* -------------------------------------------------------------------------------
	TTimerQueue
------------------------------------------------------------------------------- */

// ROM 0x0025591c __ct__11TTimerQueueFv
TTimerQueue::TTimerQueue()
{
	fHead = nil;
	fLastCalibrate = GetGlobalTime();
	fTimeoutInProgress = false;
}


// ROM 0x0025595c __dt__11TTimerQueueFv
TTimerQueue::~TTimerQueue()
{ }


// ROM 0x00255968 Check__11TTimerQueueFv
// Fires every element that is due, then answers the time until the next
// (0 with none).  Timeout handlers may prime timers; the queue is not
// recalibrated while they run.
TTimeout
TTimerQueue::Check()
{
	TTimeout next = 0;
	if (fHead != nil)
	{
		Calibrate();
		fTimeoutInProgress = true;
		while (fHead != nil && fHead->fDelta <= kTimerSlop)
		{
			TTimerElement* due = Dequeue(fHead, false);
			due->Timeout();
		}
		fTimeoutInProgress = false;
		if (fHead != nil)
			next = fHead->fDelta;
	}
	return next;
}


// ROM 0x002559f8 Calibrate__11TTimerQueueFv
// Takes the time elapsed since the last calibration off the head of the
// queue: elements it passes are left due (kTimerSlop), the one it lands in
// keeps what remains (never less than kTimerSlop).
void
TTimerQueue::Calibrate()
{
	if (fTimeoutInProgress)
		return;
	TTime now = GetGlobalTime();
	Int64 elapsed = now.time;
	CompSub(&fLastCalibrate.time, &elapsed);
	TTimerElement* i = fHead;
	if (i != nil)
	{
		TTimeout remaining = (TTimeout) elapsed.lo;
		while (i->fDelta < remaining)
		{
			remaining -= i->fDelta;
			i->fDelta = kTimerSlop;
			if (remaining <= kTimerSlop || (i = i->fNext) == nil)
				goto done;
		}
		TTimeout left = i->fDelta - remaining;
		if (left <= kTimerSlop)
			left = kTimerSlop;
		i->fDelta = left;
	}
done:
	fLastCalibrate = now;
}


// ROM 0x002556d8 Cancel__11TTimerQueueFUl
// Dequeues the first element with that refcon (its successor takes over its
// delta); nil if none.
TTimerElement*
TTimerQueue::Cancel(ULong refCon)
{
	TTimerElement* prev = nil;
	TTimerElement* i = fHead;
	for (;;)
	{
		if (i == nil)
			return nil;
		if (i->fRefCon == refCon)
			break;
		prev = i;
		i = i->fNext;
	}
	if (i->fNext != nil)
		i->fNext->fDelta += i->fDelta;
	if (prev == nil)
		fHead = i->fNext;
	else
		prev->fNext = i->fNext;
	i->fNext = nil;
	i->fPrimed = false;
	return i;
}


// ROM 0x00255abc Enqueue__11TTimerQueueFP13TTimerElement
// Inserts the element (fDelta from now) in delta order: its delta becomes
// relative to its predecessor, and its successor's to it.  Only an element
// of this queue with a non-zero delta goes in; nil otherwise.
TTimerElement*
TTimerQueue::Enqueue(TTimerElement* item)
{
	if (item == nil || item->fQueue != this || item->fDelta == 0)
		return nil;
	TTimerElement* prev = nil;
	TTimerElement* i;
	for (i = fHead; i != nil; i = i->fNext)
	{
		if (item->fDelta < i->fDelta)
			break;
		TTimeout delta = item->fDelta - i->fDelta;
		if (delta <= kTimerSlop)
			delta = kTimerSlop;
		item->fDelta = delta;
		prev = i;
	}
	item->fNext = i;
	if (prev == nil)
		fHead = item;
	else
		prev->fNext = item;
	if (i != nil)
	{
		TTimeout delta = i->fDelta - item->fDelta;
		if (delta <= kTimerSlop)
			delta = kTimerSlop;
		i->fDelta = delta;
	}
	item->fPrimed = true;
	return item;
}


// ROM 0x00255b68 Dequeue__11TTimerQueueFP13TTimerElementUc
// Unlinks the element; with adjust its successor takes over its delta (a
// fired element's successor is already due relative to now).
TTimerElement*
TTimerQueue::Dequeue(TTimerElement* item, Boolean adjust)
{
	if (item == nil || item->fQueue != this)
		return nil;
	TTimerElement* prev = nil;
	for (TTimerElement* i = fHead; i != nil; i = i->fNext)
	{
		if (i == item)
		{
			if (adjust && i->fNext != nil)
				i->fNext->fDelta += i->fDelta;
			if (prev == nil)
				fHead = i->fNext;
			else
				prev->fNext = i->fNext;
			i->fNext = nil;
			i->fPrimed = false;
			return i;
		}
		prev = i;
	}
	return nil;
}


/* -------------------------------------------------------------------------------
	TTimerPort
------------------------------------------------------------------------------- */

// ROM 0x00255758 __ct__10TTimerPortFv
TTimerPort::TTimerPort()
{
	fQueue = nil;
}


// ROM 0x00255790 __dt__10TTimerPortFv
TTimerPort::~TTimerPort()
{
	if (fQueue != nil)
		delete fQueue;
}


// ROM 0x002557d4 Init__10TTimerPortFv
NewtonErr
TTimerPort::Init()
{
	NewtonErr err = TUPort::Init();
	if (err == noErr)
	{
		fQueue = new TTimerQueue;
		if (fQueue == nil)
			err = MemError();
	}
	return err;
}


// ROM 0x00255814 TimedReceive__10TTimerPortFPUlPvUlP10TUMsgTokenT1T3UcT7
// A receive that fires the queue's timers: it waits until the next timer is
// due, fires what is due, and goes round until a message arrives.
NewtonErr
TTimerPort::TimedReceive(ULong* returnSize, void* content, ULong size, TUMsgToken* token, ULong* returnMsgType, ULong msgFilter, Boolean onMsgAvail, Boolean tokenOnly)
{
	NewtonErr err;
	do
	{
		TTimeout timeout = fQueue->Check();
		err = Receive(returnSize, content, size, token, returnMsgType, timeout, msgFilter, onMsgAvail, tokenOnly);
	} while (err == kError_Message_Timed_Out);
	return err;
}
