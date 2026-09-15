/*
	File:		utility/AEventHandler.cpp

	Contains:	The event classes (AEvents.h: TAEvent, TAESystemEvent,
				TPowerEvent, TAEventComparer) and the handlers (AEventHandler.h:
				TAEventHandler, TAEIdleTimer, TAEHandlerComparer,
				TAEHandlerIterator, TSystemEventHandler) of a TAppWorld.

				A handler is installed in its world for one (class, id) of
				event; the world keeps the handlers of one kind chained
				(fNext) and dispatches an event down the chain to the first
				handler whose AETestEvent accepts it.  A handler may also have
				an idle timer that calls its IdleProc.  The world is found
				through the task's globals (GetGlobals() is the TAppWorld).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	Layouts: TAEventHandler 0x14 (vptr, fNext +4, fEventClass +8, fEventID
	+0xc, fIdler +0x10); TAEIdleTimer 0x20 (the TTimerElement, fHandler
	+0x18, fIdleTime +0x1c); TSystemEventHandler 0x18 (fInited +0x14);
	TAEHandlerIterator 0xc; TAEvent 8; TAESystemEvent 0xc; TPowerEvent 0x10.
*/

#include "AppWorld.h"
#include "SystemEvents.h"
#include "TimerQueue.h"
#include "NewtonMemory.h"
#include "UCErrors.h"


/* -------------------------------------------------------------------------------
	Events
------------------------------------------------------------------------------- */

// ROM 0x00025dcc __ct__7TAEventFv
// (only the class is set; the id is the subclass's or the sender's to set)
TAEvent::TAEvent()
{
	fAEventClass = kNewtEventClass;
}


// ROM 0x00025e00 __ct__14TAESystemEventFv
TAESystemEvent::TAESystemEvent()
{
	fAEventID = kAESystemEventID;
	fSysEventType = 0;
}


// ROM 0x00025e48 __ct__14TAESystemEventFUl
TAESystemEvent::TAESystemEvent(ULong type)
{
	fAEventID = kAESystemEventID;
	fSysEventType = type;
}


// ROM 0x00025e8c __ct__11TPowerEventFv
TPowerEvent::TPowerEvent()
{
	fReason = 0;
}


// ROM 0x00025ec8 __ct__11TPowerEventFUlT1
TPowerEvent::TPowerEvent(ULong type, ULong reason)
	: TAESystemEvent(type)
{
	fReason = reason;
}


// The comparers order handlers by (class, id) as one 64-bit key, class
// the more significant.
static inline int
CompareKeys(AEEventClass classA, AEEventID idA, AEEventClass classB, AEEventID idB)
{
	if (classA != classB)
		return classA < classB ? -1 : 1;
	if (idA != idB)
		return idA < idB ? -1 : 1;
	return 0;
}


// ROM 0x00025f0c __ct__15TAEventComparerFv
TAEventComparer::TAEventComparer()
{ }


// ROM 0x00025f4c TestItem__15TAEventComparerCFPCv
// The item is a handler, the criterion (fItem) the event looked for.
// NOTE: "less" and "greater" are the other way about from the base
// comparer's; CSortedList's bisection only needs them consistent.
CompareResult
TAEventComparer::TestItem(const void* testItem) const
{
	const TAEventHandler* handler = (const TAEventHandler*) testItem;
	const TAEvent* event = (const TAEvent*) fItem;
	int order = CompareKeys(handler->fEventClass, handler->fEventID, event->fAEventClass, event->fAEventID);
	if (order < 0)
		return kItemGreaterThanCriteria;
	if (order > 0)
		return kItemLessThanCriteria;
	return kItemEqualCriteria;
}


// ROM 0x00025d8c __ct__18TAEHandlerComparerFv
TAEHandlerComparer::TAEHandlerComparer()
{ }


// ROM 0x00025518 TestItem__18TAEHandlerComparerCFPCv
// Both the item and the criterion are handlers.
CompareResult
TAEHandlerComparer::TestItem(const void* testItem) const
{
	const TAEventHandler* handler = (const TAEventHandler*) testItem;
	const TAEventHandler* wanted = (const TAEventHandler*) fItem;
	int order = CompareKeys(handler->fEventClass, handler->fEventID, wanted->fEventClass, wanted->fEventID);
	if (order < 0)
		return kItemGreaterThanCriteria;
	if (order > 0)
		return kItemLessThanCriteria;
	return kItemEqualCriteria;
}


/* -------------------------------------------------------------------------------
	TAEHandlerIterator
------------------------------------------------------------------------------- */

// ROM 0x00025578 __ct__18TAEHandlerIteratorFP14TAEventHandler
TAEHandlerIterator::TAEHandlerIterator(TAEventHandler* chainHead)
{
	fFirstHandler = chainHead;
	fCurrentHandler = chainHead;
	fNextHandler = chainHead != nil ? chainHead->GetNextHandler() : nil;
}


// ROM 0x000255c8 Advance__18TAEHandlerIteratorFv
void
TAEHandlerIterator::Advance()
{
	fCurrentHandler = fNextHandler;
	if (fCurrentHandler != nil)
		fNextHandler = fCurrentHandler->GetNextHandler();
}


// ROM 0x000255f4 Reset__18TAEHandlerIteratorFv
void
TAEHandlerIterator::Reset()
{
	fCurrentHandler = fFirstHandler;
	fNextHandler = fFirstHandler != nil ? fFirstHandler->GetNextHandler() : nil;
}


/* -------------------------------------------------------------------------------
	TAEventHandler
------------------------------------------------------------------------------- */

// ROM 0x00025624 __ct__14TAEventHandlerFv
TAEventHandler::TAEventHandler()
{
	fNext = nil;
	fEventClass = 0;
	fEventID = 0;
	fIdler = nil;
}


// ROM 0x0002566c __dt__14TAEventHandlerFv
// The idler goes, and an initialised handler leaves its world.
TAEventHandler::~TAEventHandler()
{
	if (fIdler != nil)
		delete fIdler;
	if (fEventClass != 0 && fEventID != 0)
		((TAppWorld*) GetGlobals())->AERemoveHandler(this);
}


// ROM 0x000256d8 Init__14TAEventHandlerFUlT1
NewtonErr
TAEventHandler::Init(AEEventID eventID, AEEventClass eventClass)
{
	fEventID = eventID;
	fEventClass = eventClass;
	((TAppWorld*) GetGlobals())->AEInstallHandler(this);
	return noErr;
}


// ROM 0x00025704 DeferReply__14TAEventHandlerFv
void
TAEventHandler::DeferReply()
{
	((TAppWorld*) GetGlobals())->fCurrentState->fReplyToken = nil;
}


// ROM 0x0002571c SetReply__14TAEventHandlerFUlP7TAEvent
void
TAEventHandler::SetReply(ULong size, TAEvent* event)
{
	TAppWorldState* state = ((TAppWorld*) GetGlobals())->fCurrentState;
	state->fEvent = event;
	state->fReplySize = size;
}


// ROM 0x00025744 SetReply__14TAEventHandlerFP10TUMsgToken
void
TAEventHandler::SetReply(TUMsgToken* token)
{
	((TAppWorld*) GetGlobals())->fCurrentState->fReplyToken = token;
}


// ROM 0x00025768 SetReply__14TAEventHandlerFP10TUMsgTokenUlP7TAEvent
void
TAEventHandler::SetReply(TUMsgToken* token, ULong size, TAEvent* event)
{
	TAppWorldState* state = ((TAppWorld*) GetGlobals())->fCurrentState;
	state->fReplyToken = token;
	state->fEvent = event;
	state->fReplySize = size;
}


// ROM 0x00025798 ReplyImmed__14TAEventHandlerFv
// Replies now with what is set, and defers (so that the loop does not
// reply again).
NewtonErr
TAEventHandler::ReplyImmed()
{
	TAppWorld* world = (TAppWorld*) GetGlobals();
	TAppWorldState* state = world->fCurrentState;
	long err = noErr;
	if (state->fReplyToken != nil)
	{
		err = state->fReplyToken->ReplyRPC(state->fEvent, state->fReplySize, noErr);
		world->AEDeferReply();
	}
	return err;
}


// ROM 0x000257b0 AddHandler__14TAEventHandlerFP14TAEventHandler
// Puts this handler in front of the chain; returns the new head.
TAEventHandler*
TAEventHandler::AddHandler(TAEventHandler* headOfChain)
{
	fNext = headOfChain;
	return this;
}


// ROM 0x000257b8 RemoveHandler__14TAEventHandlerFP14TAEventHandler
// Unlinks this handler from the chain; returns the (possibly new) head.
TAEventHandler*
TAEventHandler::RemoveHandler(TAEventHandler* headOfChain)
{
	TAEHandlerIterator iter(headOfChain);
	TAEventHandler* prev = iter.FirstHandler();
	if (prev == this)
		return fNext;
	while (iter.More())
	{
		if (prev->fNext == this)
		{
			prev->fNext = prev->fNext->fNext;
			return headOfChain;
		}
		prev = iter.NextHandler();
	}
	return headOfChain;
}


// ROM 0x00025830 AEDoEvent__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The first handler down the chain that accepts the event handles it;
// eNoHandler if none does.
NewtonErr
TAEventHandler::AEDoEvent(TUMsgToken* token, ULong* size, TAEvent* event)
{
	if (AETestEvent(event))
	{
		AEHandlerProc(token, size, event);
		return noErr;
	}
	if (fNext != nil)
		return fNext->AEDoEvent(token, size, event);
	return eNoHandler;
}


// ROM 0x000258b8 AEDoComplete__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The same for a completion (an asynchronous send of ours came back).
void
TAEventHandler::AEDoComplete(TUMsgToken* token, ULong* size, TAEvent* event)
{
	for (TAEventHandler* handler = this; handler != nil; handler = handler->fNext)
	{
		if (handler->AETestEvent(event))
		{
			handler->AECompletionProc(token, size, event);
			return;
		}
	}
}


// ROM 0x00025924 AEHandlerProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TAEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x00025928 AECompletionProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TAEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x0002592c IdleProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TAEventHandler::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x00025930 AETestEvent__14TAEventHandlerFP7TAEvent
Boolean
TAEventHandler::AETestEvent(TAEvent* /*event*/)
{
	return true;
}


// ROM 0x00025954 InitIdler__14TAEventHandlerFUlT1Uc
// An idle timer on the world's queue; started unless told otherwise.
NewtonErr
TAEventHandler::InitIdler(TTimeout idle, ULong refCon, Boolean start)
{
	fIdler = new TAEIdleTimer(((TAppWorld*) GetGlobals())->GetTimerQueue(), refCon, this, idle);
	if (fIdler == nil)
		return MemError();
	if (start && !fIdler->Start())
		return -1;
	return noErr;
}


// ROM 0x0002593c InitIdler__14TAEventHandlerFUl9TimeUnitsT1Uc
NewtonErr
TAEventHandler::InitIdler(ULong idleAmount, TimeUnits idleUnits, ULong refCon, Boolean start)
{
	return InitIdler(idleAmount * idleUnits, refCon, start);
}


// ROM 0x000259cc StartIdle__14TAEventHandlerFv
NewtonErr
TAEventHandler::StartIdle()
{
	if (fIdler != nil && fIdler->Start())
		return noErr;
	return -1;
}


// ROM 0x00025a00 StopIdle__14TAEventHandlerFv
NewtonErr
TAEventHandler::StopIdle()
{
	if (fIdler != nil && fIdler->Stop())
		return noErr;
	return -1;
}


// ROM 0x00025a30 ResetIdle__14TAEventHandlerFv
// (a Prime dequeues a primed timer itself, so this is StartIdle again)
NewtonErr
TAEventHandler::ResetIdle()
{
	if (fIdler != nil && fIdler->Start())
		return noErr;
	return -1;
}


// ROM 0x00025a64 ResetIdle__14TAEventHandlerFUl
// (the new delay is used once; fIdleTime keeps the original)
NewtonErr
TAEventHandler::ResetIdle(TTimeout idle)
{
	if (fIdler != nil && fIdler->Prime(idle))
		return noErr;
	return -1;
}


// ROM 0x00025a94 ResetIdle__14TAEventHandlerFUl9TimeUnits
// Also times out the world's receive (a port reset of its receivers) so
// that the loop picks up the new timeout.
NewtonErr
TAEventHandler::ResetIdle(ULong amount, TimeUnits units)
{
	NewtonErr err = ResetIdle(amount * units);
	((TAppWorld*) GetGlobals())->GetMyPort()->Reset(0, kPortFlags_Timeout);
	return err;
}


// ROM 0x00025ac8 GetNextHandler__14TAEventHandlerFv
TAEventHandler*
TAEventHandler::GetNextHandler()
{
	return fNext;
}


/* -------------------------------------------------------------------------------
	TAEIdleTimer
------------------------------------------------------------------------------- */

// ROM 0x00025cc4 __ct__12TAEIdleTimerFP11TTimerQueueUlP14TAEventHandlerT2
TAEIdleTimer::TAEIdleTimer(TTimerQueue* q, ULong refCon, TAEventHandler* handler, TTimeout idle)
	: TTimerElement(q, refCon)
{
	fHandler = handler;
	fIdleTime = idle;
}


// ROM 0x00025d28 Timeout__12TAEIdleTimerFv
// The handler's IdleProc gets a timer event naming the timer and its refcon.
// (The ROM leaves the event's id as TAEvent's constructor does: unset.)
void
TAEIdleTimer::Timeout()
{
	ULong size = sizeof(TAETimerEvent);
	TAETimerEvent event;
	event.fTimerID = (ULong) (uintptr_t) this;
	event.fTimerRefCon = GetRefCon();
	fHandler->IdleProc(nil, &size, &event);
}


/* -------------------------------------------------------------------------------
	TSystemEventHandler
------------------------------------------------------------------------------- */

// ROM 0x00025ad0 __ct__19TSystemEventHandlerFv
TSystemEventHandler::TSystemEventHandler()
{
	fInited = false;
}


// ROM 0x00025b18 Init__19TSystemEventHandlerFUlT1
// Registers the world's port for the system event with the name server;
// the first time, installs itself as the world's 'sysm' handler.
NewtonErr
TSystemEventHandler::Init(ULong systemEvent, ULong sendFilter)
{
	TSystemEvent event(systemEvent);
	NewtonErr err = event.RegisterForSystemEvent(*((TAppWorld*) GetGlobals())->GetMyPort(), sendFilter, kNoTimeout);
	if (err == noErr && !fInited)
	{
		err = TAEventHandler::Init(kAESystemEventID, kNewtEventClass);
		fInited = true;
	}
	return err;
}


// ROM 0x00025ba0 AnySystemEvents__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::AnySystemEvents(TAEvent* /*event*/)
{ }


// ROM 0x00025ba4 PowerOn__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::PowerOn(TAEvent* /*event*/)
{ }


// ROM 0x00025ba8 PowerOff__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::PowerOff(TAEvent* /*event*/)
{ }


// ROM 0x00025bac NewCard__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::NewCard(TAEvent* /*event*/)
{ }


// ROM 0x00025bb0 AppAlive__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::AppAlive(TAEvent* /*event*/)
{ }


// ROM 0x00025bb4 DeviceNotification__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::DeviceNotification(TAEvent* /*event*/)
{ }


// ROM 0x00025bb8 PowerOffPending__19TSystemEventHandlerFP7TAEvent
void
TSystemEventHandler::PowerOffPending(TAEvent* /*event*/)
{ }


// ROM 0x00025bbc AEHandlerProc__19TSystemEventHandlerFP10TUMsgTokenPUlP7TAEvent
// Every system event goes to AnySystemEvents, then to its own method.
void
TSystemEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	AnySystemEvents(event);
	switch (((TAESystemEvent*) event)->fSysEventType)
	{
	case kSysEvent_PowerOn:				PowerOn(event); break;
	case kSysEvent_PowerOff:			PowerOff(event); break;
	case kSysEvent_AppAlive:			AppAlive(event); break;
	case kSysEvent_NewICCard:			NewCard(event); break;
	case kSysEvent_DeviceNotification:	DeviceNotification(event); break;
	case kSysEvent_PowerOffPending:		PowerOffPending(event); break;
	}
}
