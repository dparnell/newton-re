/*
	File:		utility/AppWorld.h

	Contains:	TForkWorld and TAppWorld, the task frameworks the OS services
				are written in, and TAppWorldState, an event loop's state.
				The DDK ships AEventHandler.h/AEvents.h but not AppWorld.h;
				these declarations follow the ROM (layouts and vtables
				Ghidra-verified, tools/newton-rom/analysis/vtable.py).

	A TForkWorld is a TUTaskWorld whose task may spawn "forks": further
	tasks running copies of the object that share its mutex, so that only
	one of the family runs its main code at a time (AcquireMutex/
	ReleaseMutex/Yield).  A TAppWorld is a TForkWorld whose main code is an
	event loop: it receives messages (events, TAEvent) on its port, timed by
	a TTimerQueue, and dispatches them to the TAEventHandlers installed for
	their (class, id).  The world object is the task's globals (GetGlobals()
	is the TAppWorld*), which is how a handler finds its world.
*/

#ifndef __APPWORLD_H
#define __APPWORLD_H

#ifndef __USERTASKS_H
#include "UserTasks.h"
#endif
#ifndef __USERSEMAPHORE_H
#include "UserSemaphore.h"
#endif
#ifndef __AEVENTHANDLER_H
#include "AEventHandler.h"
#endif
#ifndef __AEVENTS_H
#include "AEvents.h"
#endif

class CSortedList;
class TTimerQueue;


// The mutex a fork family shares, with a count of the worlds using it.
// (0x14 bytes: the TULockingSemaphore and two words; unnamed in the ROM.)
class TForkMutex : public TULockingSemaphore
{
public:
						TForkMutex() : TULockingSemaphore() { fWorlds = 0; fForks = 0; }

	ULong				fWorlds;		// +0x0c  worlds constructed on it, less those destructed
	ULong				fForks;			// +0x10  worlds ever constructed on it
};


class TForkWorld : public TUTaskWorld		// 0x30 bytes
{
public:
						TForkWorld();
	virtual				~TForkWorld();

	virtual ULong		GetSizeOf();
	virtual long		TaskConstructor();
	virtual void		TaskDestructor();
	virtual void		TaskMain();

	// a fork's construction: it copies the parent's settings and shares its mutex
	virtual long		ForkInit(TForkWorld* parent);
	virtual long		ForkConstructor(TForkWorld* parent);
	virtual void		ForkDestructor();

	// the main world's construction
	virtual long		MainInit(ULong name, ULong stackSize);
	virtual long		MainInit(ULong name, ULong stackSize, ULong priority, TObjectId environment);
	virtual long		MainConstructor();
	virtual void		MainDestructor();

	// the main code, under the mutex
	virtual long		PreMain();
	virtual void		TheMain() = 0;
	virtual void		PostMain();

	virtual void		ForkSwitch(Boolean acquired);		// told each time the mutex changes hands
	virtual TForkWorld*	MakeFork();							// a subclass makes a fork of itself (DEVIATION: the ROM declares it long and answers the object in it, which a host pointer does not fit)

	long				Fork(TForkWorld* fork = nil);
	void				Yield();
	Boolean				EnableForking(Boolean enable);

	long				AcquireMutex();
	long				ReleaseMutex();

protected:
	TForkMutex*			fMutex;				// +0x18
	Boolean				fIsMain;			// +0x1c  the main world (false: a fork)
	Boolean				fRunsMain;			// +0x1d  runs PreMain/TheMain/PostMain and is destroyed as the main world; gates MakeFork
	Boolean				fRunning;			// +0x1e  TaskMain has begun
	Boolean				fForkingEnabled;	// +0x1f
	TForkWorld*			fParent;			// +0x20  a fork's parent world
	ULong				fStackSize;			// +0x24
	ULong				fPriority;			// +0x28
	ULong				fName;				// +0x2c
};


// One event loop's state: the port it receives on, the message being
// handled and how to reply to it, the receive parameters.  A world's main
// loop has one; a nested loop makes its own on the stack.
class TAppWorldState		// 0x134 bytes
{
public:
	enum { kEventBufferSize = kMAXEVENTSIZE };

						TAppWorldState();
						~TAppWorldState();

	long				Init();						// a port of its own
	long				Init(TUPort* port);			// somebody else's port
	long				Init(TObjectId portId);

	TUPort*				GetPort()			{ return fPort; }
	long				GetError()			{ return fError; }

	void				NestedEventLoop();
	void				TerminateNestedEventLoop();

	long				fError;				// +0x00  the last receive's result
	TUPort*				fPort;				// +0x04
	TUMsgToken			fToken;				// +0x08  the message being handled
	ULong				fSize;				// +0x18  its size
	ULong				fReplySize;			// +0x1c  the reply's size
	ULong				fMsgType;			// +0x20  its message type
	ULong				fFilter;			// +0x24  receive filter
	Boolean				fDone;				// +0x28  the loop is to end
	Boolean				fTokenOnly;			// +0x29  receive parameters
	Boolean				fOnMsgAvail;		// +0x2a
	Boolean				fOwnsPort;			// +0x2b  DEVIATION: see ~TAppWorldState (a padding byte in the ROM)
	TAEvent*			fEvent;				// +0x2c  the event (in the buffer), or what to reply with
	TUMsgToken*			fReplyToken;		// +0x30  the token to reply to (nil once deferred or replied)
	char				fEventBuffer[kEventBufferSize];	// +0x34
};


class TAppWorld : public TForkWorld			// 0x70 bytes
{
public:
						TAppWorld();
	virtual				~TAppWorld();

	virtual ULong		GetSizeOf();

	virtual long		ForkInit(TForkWorld* parent);
	virtual long		ForkConstructor(TForkWorld* parent);
	virtual void		ForkDestructor();
	virtual long		MainConstructor();
	virtual void		MainDestructor();
	virtual void		TheMain();

	virtual long		Init(ULong name, Boolean registerName, ULong stackSize);
	virtual long		Init(ULong name, Boolean registerName, ULong stackSize, ULong priority, TObjectId environment);
	virtual void		InterruptHandler(ULong* size, TAEvent* event);
	virtual long		AEDispatch(ULong msgType, TUMsgToken* token, ULong* size, TAEvent* event);

	// the event loops
	void				AEventLoop();							// the main state's
	void				AEventLoop(TAppWorldState* state);
	void				AEventLoop(TAppWorldState* state, TUMsgToken* token);	// (empty in the ROM)
	void				NestedEventLoop();
	void				AETerminateLoop();

	// handlers
	long				AEInstallHandler(TAEventHandler* handler);
	long				AERemoveHandler(TAEventHandler* handler);
	void				AEInstallIdleHandler(TAEventHandler* handler);		// (empty in the ROM)
	void				AERemoveIdleHandler(TAEventHandler* handler);		// (empty in the ROM)
	TAEventHandler*		AEFindHandler(AEEventID id, AEEventClass eventClass);

	// the message being handled, and the reply to it
	long				AEGetCollectedEvent(ULong msgType, TUMsgToken* token, ULong* size, TAEvent** event, ULong* refCon);
	void				AEDeferReply();
	void				AESetReply(ULong size);
	void				AESetReply(ULong size, TAEvent* event);
	void				AESetReply(TUMsgToken* token);
	void				AESetReply(TUMsgToken* token, ULong size, TAEvent* event);
	long				AEReplyImmed();
	TUMsgToken*			AEGetMsgToken();
	ULong				AEGetMsgType();
	TAEvent*			AEGetAEvent();
	ULong				AEGetMsgSize();

	long				GetError();
	void				SetFilter(ULong bits);
	void				ClearFilter(ULong bits);
	Boolean				TokenOnly();
	void				SetTokenOnly(Boolean tokenOnly);
	TUPort*				GetMyPort();

	TTimerQueue*		GetTimerQueue()		{ return fTimers; }

protected:
	friend class TAEventHandler;

	ULong				fRegisteredName;	// +0x30  the name registered with the name server (0: none)
	TAppWorldState*		fMainState;			// +0x34
	TAppWorldState*		fCurrentState;		// +0x38  the loop running (the main one, or a nested one)
	CItemComparer		fTimerComparer;		// +0x3c  (unused by the ROM's methods)
	TAEventComparer		fEventComparer;		// +0x48  finds the handler chain for an event
	TAEHandlerComparer	fHandlerComparer;	// +0x54  finds the handler chain for a handler
	CSortedList*		fHandlers;			// +0x60  the heads of the handler chains, one per (class, id)
	TTimerQueue*		fTimers;			// +0x64
	AEEventID			fAllHandlersEventID;	// +0x68  '****': an event with this id goes to every handler
	AEEventClass		fAllHandlersEventClass;	// +0x6c  'newt'
};

#endif	/* __APPWORLD_H */
