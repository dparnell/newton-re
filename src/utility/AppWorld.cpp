/*
	File:		utility/AppWorld.cpp

	Contains:	TAppWorld and TAppWorldState (AppWorld.h): the event-driven
				task.  The main loop (AEventLoop) fires the world's timers,
				receives the next message on the world's port into the
				state's event buffer, and dispatches it (AEDispatch): a
				completion of one of the world's own asynchronous sends goes to
				the handler named in the message's refcon; an event with the
				"all handlers" id goes to every handler; any other to the
				handler chain registered for its (class, id).  A message that
				expects a reply is replied to with what the handler set (or
				the event itself) unless the handler deferred the reply.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Layout of TAppWorld (0x70 bytes): the TForkWorld (0x30), fRegisteredName
	+0x30, fMainState +0x34, fCurrentState +0x38, fTimerComparer +0x3c,
	fEventComparer +0x48, fHandlerComparer +0x54, fHandlers +0x60, fTimers
	+0x64, fAllHandlersEventID +0x68, fAllHandlersEventClass +0x6c.
*/

#include "AppWorld.h"
#include "NameServer.h"
#include "SortedList.h"
#include "ListIterator.h"
#include "TimerQueue.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "UCErrors.h"
#include "OSErrors.h"
#include "os600/ObjectMessage.h"

// the type a world's port is registered under with the name server
static char kPortNameType[] = "TUPort";

// A world's name as the four-character string the ROM registers: the word
// as it lies in the ARM's (big-endian) memory, then a terminator.
static void
NameToString(ULong name, char* out)
{
	out[0] = (char) (name >> 24);
	out[1] = (char) (name >> 16);
	out[2] = (char) (name >> 8);
	out[3] = (char) name;
	out[4] = 0;
}


/* -------------------------------------------------------------------------------
	TAppWorldState
------------------------------------------------------------------------------- */

// ROM 0x000313f4 __ct__14TAppWorldStateFv
TAppWorldState::TAppWorldState()
{
	fToken = TUMsgToken();
	fError = noErr;
	fFilter = kMsgType_MatchAll;
	fTokenOnly = false;
	fOnMsgAvail = false;
	fReplySize = 0;
	fSize = kEventBufferSize;
	fReplyToken = nil;
	fEvent = (TAEvent*) fEventBuffer;
	fDone = false;
	fPort = nil;
	fOwnsPort = false;
}


// ROM 0x00031670 __dt__14TAppWorldStateFv
// DEVIATION: the ROM deletes the port whether the state made it (Init())
// or was lent it (Init(TUPort*), as a nested loop is lent the world's) -
// a nested event loop ends by freeing the world's port object.  Only a port
// of the state's own is deleted here (fOwnsPort).
TAppWorldState::~TAppWorldState()
{
	if (fPort != nil && fOwnsPort)
		delete fPort;
}


// ROM 0x00031ae0 Init__14TAppWorldStateFv
// A port of its own.
long
TAppWorldState::Init()
{
	fPort = new TUPort;
	if (fPort == nil)
		return MemError();
	fOwnsPort = true;
	ObjectMessage msg;
	return fPort->MakeObject(kObjectPort, &msg, kObjectMessage_HeaderSize);
}


// ROM 0x00031b1c Init__14TAppWorldStateFP6TUPort
long
TAppWorldState::Init(TUPort* port)
{
	fPort = port;
	return noErr;
}


// ROM 0x00031b28 Init__14TAppWorldStateFUl
long
TAppWorldState::Init(TObjectId portId)
{
	fPort = new TUPort(portId);
	if (fPort == nil)
		return MemError();
	fOwnsPort = true;
	return noErr;
}


// ROM 0x00030e2c NestedEventLoop__14TAppWorldStateFv
// Runs the world's loop on this state until it is terminated; an exception
// out of a handler ends it too.
void
TAppWorldState::NestedEventLoop()
{
	newton_try
	{
		((TAppWorld*) GetGlobals())->AEventLoop(this);
	}
	newton_catch_all
	{
		fDone = true;
	}
	end_try;
}


// ROM 0x00030e80 TerminateNestedEventLoop__14TAppWorldStateFv
void
TAppWorldState::TerminateNestedEventLoop()
{
	fDone = true;
}


/* -------------------------------------------------------------------------------
	TAppWorld
------------------------------------------------------------------------------- */

// ROM 0x00030e8c __ct__9TAppWorldFv
TAppWorld::TAppWorld()
{
	fRegisteredName = 0;
	fMainState = nil;
	fCurrentState = nil;
	fHandlers = nil;
	fTimers = nil;
	fAllHandlersEventID = '****';
	fAllHandlersEventClass = kNewtEventClass;
}


// ROM 0x00030f14 __dt__9TAppWorldFv
TAppWorld::~TAppWorld()
{ }


// ROM 0x00031394 GetSizeOf__9TAppWorldFv
ULong
TAppWorld::GetSizeOf()
{
	return sizeof(TAppWorld);
}


// ROM 0x00030f54 Init__9TAppWorldFUlUcT1
// Starts the world's task; the name is registered with the name server
// (type "TUPort", the world's port) if asked.
long
TAppWorld::Init(ULong name, Boolean registerName, ULong stackSize)
{
	if (registerName)
		fRegisteredName = name;
	fName = name;
	fStackSize = stackSize;
	return StartTask(true, false, kNoTimeout, stackSize, fPriority, name);
}


// ROM 0x00030f64 Init__9TAppWorldFUlUcN31
long
TAppWorld::Init(ULong name, Boolean registerName, ULong stackSize, ULong priority, TObjectId environment)
{
	if (registerName)
		fRegisteredName = name;
	return MainInit(name, stackSize, priority, environment);
}


// ROM 0x00030f98 MainConstructor__9TAppWorldFv
// In the new task: the main state with a port of its own, the name
// registration, the handler list and the timer queue.
long
TAppWorld::MainConstructor()
{
	long err = TForkWorld::MainConstructor();
	if (err != noErr)
		return err;
	fMainState = new TAppWorldState;
	if (fMainState != nil)
	{
		fMainState->fEvent = (TAEvent*) fMainState->fEventBuffer;
		fMainState->fReplyToken = &fMainState->fToken;
		fCurrentState = fMainState;
		if ((err = fMainState->Init()) != noErr)
			return err;
		if (fRegisteredName != 0)
		{
			char name[8];
			NameToString(fRegisteredName, name);
			TUNameServer nameServer;
			if ((err = nameServer.RegisterName(name, kPortNameType, *GetMyPort(), 0)) != noErr)
				return err;
		}
		fHandlers = new CSortedList(&fEventComparer);
		if (fHandlers != nil)
		{
			fTimers = new TTimerQueue;
			if (fTimers != nil)
				return noErr;
		}
	}
	return MemError();
}


// ROM 0x000310a4 MainDestructor__9TAppWorldFv
// The handlers (the head of each chain), the name, the state, the list
// and the timers go.
void
TAppWorld::MainDestructor()
{
	TForkWorld::MainDestructor();
	if (fHandlers != nil)
	{
		CListIterator iter(fHandlers);
		for (TAEventHandler* handler = (TAEventHandler*) iter.FirstItem(); iter.More(); handler = (TAEventHandler*) iter.NextItem())
		{
			if (handler != nil)
				delete handler;
		}
	}
	if (fRegisteredName != 0)
	{
		char name[8];
		NameToString(fRegisteredName, name);
		TUNameServer nameServer;
		nameServer.UnRegisterName(name, kPortNameType);
	}
	if (fMainState != nil)
		delete fMainState;
	if (fHandlers != nil)
		delete fHandlers;
	if (fTimers != nil)
		delete fTimers;
}


// ROM 0x000311b0 ForkInit__9TAppWorldFP10TForkWorld
// A fork of an app world shares its parent's name, comparers, handlers and
// timers; it gets a state of its own in ForkConstructor.
long
TAppWorld::ForkInit(TForkWorld* parent)
{
	long err = TForkWorld::ForkInit(parent);
	if (err != noErr)
		return err;
	TAppWorld* world = (TAppWorld*) parent;
	fRegisteredName = world->fRegisteredName;
	fMainState = nil;
	fCurrentState = nil;
	fTimerComparer = world->fTimerComparer;
	fEventComparer = world->fEventComparer;
	fHandlerComparer = world->fHandlerComparer;
	fHandlers = world->fHandlers;
	fTimers = world->fTimers;
	fAllHandlersEventID = world->fAllHandlersEventID;
	fAllHandlersEventClass = world->fAllHandlersEventClass;
	return noErr;
}


// ROM 0x00031238 ForkConstructor__9TAppWorldFP10TForkWorld
// In the fork's task: a state on the parent's port; the parent's loop is
// told to end (the fork takes over receiving).
long
TAppWorld::ForkConstructor(TForkWorld* parent)
{
	long err = TForkWorld::ForkConstructor(parent);
	if (err != noErr)
		return err;
	fMainState = new TAppWorldState;
	if (fMainState == nil)
		return MemError();
	fMainState->fEvent = (TAEvent*) fMainState->fEventBuffer;
	fMainState->fReplyToken = &fMainState->fToken;
	fCurrentState = fMainState;
	err = fMainState->Init(((TAppWorld*) parent)->fMainState->fPort);
	if (err == noErr)
		((TAppWorld*) parent)->AETerminateLoop();
	return err;
}


// ROM 0x00031350 ForkDestructor__9TAppWorldFv
// (the port is the parent's: forgotten, not deleted)
void
TAppWorld::ForkDestructor()
{
	if (fMainState != nil)
	{
		fMainState->fPort = nil;
		delete fMainState;
	}
}


// ROM 0x0003138c TheMain__9TAppWorldFv
void
TAppWorld::TheMain()
{
	AEventLoop(fCurrentState);
}


// ROM 0x0003139c InterruptHandler__9TAppWorldFPUlP7TAEvent
void
TAppWorld::InterruptHandler(ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x000313a0 GetError__9TAppWorldFv
long
TAppWorld::GetError()
{
	return fCurrentState->fError;
}


// ROM 0x000313ac SetFilter__9TAppWorldFUl
void
TAppWorld::SetFilter(ULong bits)
{
	fCurrentState->fFilter |= bits;
}


// ROM 0x000313c0 ClearFilter__9TAppWorldFUl
void
TAppWorld::ClearFilter(ULong bits)
{
	fCurrentState->fFilter &= ~bits;
}


// ROM 0x000313d4 TokenOnly__9TAppWorldFv
Boolean
TAppWorld::TokenOnly()
{
	return fCurrentState->fTokenOnly;
}


// ROM 0x000313e0 SetTokenOnly__9TAppWorldFUc
void
TAppWorld::SetTokenOnly(Boolean tokenOnly)
{
	fCurrentState->fTokenOnly = tokenOnly;
}


// ROM 0x000313ec GetMyPort__9TAppWorldFv
// ROM 0x00031b60 GetPort__14TAppWorldStateFv (the state's port, which GetMyPort branches to)
TUPort*
TAppWorld::GetMyPort()
{
	return fCurrentState->fPort;
}


// ROM 0x000314d8 AEInstallHandler__9TAppWorldFP14TAEventHandler
// The handler joins the chain for its (class, id) - at its head - or
// starts one.
long
TAppWorld::AEInstallHandler(TAEventHandler* handler)
{
	fHandlerComparer.SetTestItem(handler);
	ArrayIndex index;
	TAEventHandler* head = (TAEventHandler*) fHandlers->Search(&fHandlerComparer, index);
	if (head == nil)
		fHandlers->InsertAt(index, handler);
	else
	{
		handler->AddHandler(head);
		fHandlers->ReplaceAt(index, handler);
	}
	return noErr;
}


// ROM 0x00031460 AERemoveHandler__9TAppWorldFP14TAEventHandler
long
TAppWorld::AERemoveHandler(TAEventHandler* handler)
{
	fHandlerComparer.SetTestItem(handler);
	ArrayIndex index;
	TAEventHandler* head = (TAEventHandler*) fHandlers->Search(&fHandlerComparer, index);
	if (head != nil)
	{
		TAEventHandler* newHead = handler->RemoveHandler(head);
		if (newHead == nil)
			fHandlers->RemoveElementsAt(index, 1);
		else if (newHead != head)
			fHandlers->ReplaceAt(index, newHead);
	}
	return noErr;
}


// ROM 0x00030e28 AEInstallIdleHandler__9TAppWorldFP14TAEventHandler
void
TAppWorld::AEInstallIdleHandler(TAEventHandler* /*handler*/)
{ }


// ROM 0x00030e24 AERemoveIdleHandler__9TAppWorldFP14TAEventHandler
void
TAppWorld::AERemoveIdleHandler(TAEventHandler* /*handler*/)
{ }


// ROM 0x00031728 AEFindHandler__9TAppWorldFUlT1
// The head of the chain for the (class, id), or nil.
TAEventHandler*
TAppWorld::AEFindHandler(AEEventID id, AEEventClass eventClass)
{
	TAEvent event;
	event.fAEventClass = eventClass;
	event.fAEventID = id;
	fEventComparer.SetTestItem(&event);
	ArrayIndex index;
	return (TAEventHandler*) fHandlers->Search(&fEventComparer, index);
}


// ROM 0x00031548 AEGetCollectedEvent__9TAppWorldFUlP10TUMsgTokenPUlPP7TAEventT3
// A collected message: for one of our asynchronous sends that completed,
// the reply memory's buffer is the event and the message's refcon (the
// handler that sent it) comes back; for a collected receive, the message is
// cashed into the event buffer.
long
TAppWorld::AEGetCollectedEvent(ULong msgType, TUMsgToken* token, ULong* size, TAEvent** event, ULong* refCon)
{
	long err = noErr;
	if (msgType & kMsgType_CollectedSender)
	{
		if (token->GetReplyId() != 0)
		{
			TUSharedMem replyMem(token->GetReplyId());
			err = replyMem.GetSize(size, (void**) event);
			if (err == noErr)
				err = token->GetUserRefCon(refCon);
		}
	}
	else if (msgType & kMsgType_CollectedReceiver)
		err = token->CashMessageToken(size, *event, TAppWorldState::kEventBufferSize, 0, true);
	return err;
}


// ROM 0x00031600 AEDeferReply__9TAppWorldFv
void
TAppWorld::AEDeferReply()
{
	fCurrentState->fReplyToken = nil;
}


// ROM 0x00031610 AESetReply__9TAppWorldFUl
void
TAppWorld::AESetReply(ULong size)
{
	fCurrentState->fReplySize = size;
}


// ROM 0x0003161c AESetReply__9TAppWorldFUlP7TAEvent
void
TAppWorld::AESetReply(ULong size, TAEvent* event)
{
	fCurrentState->fEvent = event;
	fCurrentState->fReplySize = size;
}


// ROM 0x00031630 AESetReply__9TAppWorldFP10TUMsgToken
void
TAppWorld::AESetReply(TUMsgToken* token)
{
	fCurrentState->fReplyToken = token;
}


// ROM 0x0003163c AESetReply__9TAppWorldFP10TUMsgTokenUlP7TAEvent
void
TAppWorld::AESetReply(TUMsgToken* token, ULong size, TAEvent* event)
{
	fCurrentState->fReplyToken = token;
	fCurrentState->fEvent = event;
	fCurrentState->fReplySize = size;
}


// ROM 0x00031658 AEGetMsgToken__9TAppWorldFv
TUMsgToken*
TAppWorld::AEGetMsgToken()
{
	return fCurrentState->fReplyToken;
}


// ROM 0x00031664 AEGetMsgType__9TAppWorldFv
ULong
TAppWorld::AEGetMsgType()
{
	return fCurrentState->fMsgType;
}


// ROM 0x000316b8 AEGetAEvent__9TAppWorldFv
TAEvent*
TAppWorld::AEGetAEvent()
{
	return fCurrentState->fEvent;
}


// ROM 0x000316c4 AEGetMsgSize__9TAppWorldFv
ULong
TAppWorld::AEGetMsgSize()
{
	return fCurrentState->fSize;
}


// ROM 0x000316d0 AEReplyImmed__9TAppWorldFv
long
TAppWorld::AEReplyImmed()
{
	long err = noErr;
	TAppWorldState* state = fCurrentState;
	if (state->fReplyToken != nil)
	{
		err = state->fReplyToken->ReplyRPC(state->fEvent, state->fReplySize, noErr);
		AEDeferReply();
	}
	return err;
}


// ROM 0x00031718 AETerminateLoop__9TAppWorldFv
void
TAppWorld::AETerminateLoop()
{
	fCurrentState->fDone = true;
}


// ROM 0x00031770 AEDispatch__9TAppWorldFUlP10TUMsgTokenPUlP7TAEvent
// Where a received message goes; the result is what the reply carries.
long
TAppWorld::AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event)
{
	long err = noErr;
	TAEventHandler* sender = nil;
	if ((msgType & kMsgType_CollectorMask) && token != nil)
	{
		err = AEGetCollectedEvent(msgType, token, size, &event, (ULong*) &sender);
		if (err == noErr && sender != nil)
		{
			// one of our own asynchronous sends completed: its handler hears
			sender->AECompletionProc(token, size, event);
			AEDeferReply();
			return err;
		}
	}
	if (event == nil)
		return eNoHandler;
	if (event->fAEventID == fAllHandlersEventID)
	{
		// to every handler of every chain
		CListIterator iter(fHandlers);
		for (TAEventHandler* head = (TAEventHandler*) iter.FirstItem(); iter.More(); head = (TAEventHandler*) iter.NextItem())
		{
			for (TAEventHandler* handler = head; handler != nil; handler = handler->fNext)
				handler->AEHandlerProc(token, size, event);
		}
		return err;
	}
	fEventComparer.SetTestItem(event);
	ArrayIndex index;
	TAEventHandler* head = (TAEventHandler*) fHandlers->Search(&fEventComparer, index);
	if (head == nil)
		return eNoHandler;
	if (msgType & kMsgType_CollectedSender)
	{
		head->AEDoComplete(token, size, event);
		AEDeferReply();
		return err;
	}
	return head->AEDoEvent(token, size, event);
}


// ROM 0x000318fc AEventLoop__9TAppWorldFP14TAppWorldState
// The loop, on the given state, until AETerminateLoop: fire the timers,
// receive (with the next timer as the timeout) into the event buffer,
// dispatch what arrived (an event too large for the buffer is dispatched
// truncated), and reply to a message that wants one unless the handler
// deferred it.  The mutex is let go while waiting.
void
TAppWorld::AEventLoop(TAppWorldState* state)
{
	TAppWorldState* saved = fCurrentState;
	fCurrentState = state;
	do
	{
		state->fEvent = (TAEvent*) state->fEventBuffer;
		state->fReplyToken = &state->fToken;
		state->fReplySize = 0;
		state->fMsgType = 0;
		TTimeout timeout = fTimers->Check();
		if (!fRunsMain)
			break;
		ReleaseMutex();
		long err = state->fPort->Receive(&state->fSize, state->fEvent, TAppWorldState::kEventBufferSize, state->fReplyToken, &state->fMsgType, timeout, state->fFilter, state->fOnMsgAvail, state->fTokenOnly);
		AcquireMutex();
		if (err != kError_Message_Timed_Out)
		{
			state->fError = err;
			if (err == noErr || err == kError_Size_To_Large_Copy_Truncated)
			{
				long result = AEDispatch(state->fMsgType, state->fReplyToken, &state->fSize, state->fEvent);
				if (state->fReplyToken != nil && state->fReplyToken->GetReplyId() != 0)
					state->fToken.ReplyRPC(state->fEvent, state->fReplySize, result);
			}
		}
	} while (!state->fDone);
	state->fDone = false;
	fCurrentState = saved;
}


// ROM 0x00031a48 AEventLoop__9TAppWorldFP14TAppWorldStateP10TUMsgToken
// (empty in the ROM)
void
TAppWorld::AEventLoop(TAppWorldState* /*state*/, TUMsgToken* /*token*/)
{ }


// ROM 0x00031a4c AEventLoop__9TAppWorldFv
void
TAppWorld::AEventLoop()
{
	AEventLoop(fMainState);
}


// ROM 0x00031a54 NestedEventLoop__9TAppWorldFv
// A loop within a loop (a handler that must wait for something), on a
// state of its own over the world's port; an exception out of it ends it.
void
TAppWorld::NestedEventLoop()
{
	TAppWorldState state;
	state.Init(GetMyPort());
	state.fReplyToken = &state.fToken;
	newton_try
	{
		AEventLoop(&state);
	}
	newton_catch_all
	{
		AETerminateLoop();
	}
	end_try;
}
