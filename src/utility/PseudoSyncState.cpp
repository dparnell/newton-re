/*
	File:		utility/PseudoSyncState.cpp

	Contains:	TPseudoSyncState.  See PseudoSyncState.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PseudoSyncState.h"
#include "AppWorld.h"


// ROM 0x00195350 __ct__13TUnblockEventFPv
TUnblockEvent::TUnblockEvent(void* state)
{
	fAEventID = 'sync';
	fState = state;
}


// ROM 0x00195394 __dt__13TUnblockEventFv
TUnblockEvent::~TUnblockEvent()
{ }


// ROM 0x001953a0 __ct__16TPseudoSyncStateFv
TPseudoSyncState::TPseudoSyncState()
{ }


// ROM 0x001953d4 __dt__16TPseudoSyncStateFv
TPseudoSyncState::~TPseudoSyncState()
{ }


// ROM 0x00195404 Init__16TPseudoSyncStateFv
long
TPseudoSyncState::Init(void)
{
	return TUPort::Init();
}


// ROM 0x00195408 Block__16TPseudoSyncStateFUl
// The world forked - a new task takes over its event loop - and then this
// task waits, the mutex let go, for the unblock event.  A world that could
// not fork does not wait at all.
long
TPseudoSyncState::Block(ULong timeout)
{
	long err = ((TForkWorld*) GetGlobals())->Fork(nil);
	if (err != noErr)
		return err;
	TUnblockEvent event(nil);
	ULong size;
	((TForkWorld*) GetGlobals())->ReleaseMutex();
	// DEVIATION: the event is 0x0c bytes in the ROM, larger with a host pointer
	err = Receive(&size, &event, sizeof(TUnblockEvent), nil, nil, (TTimeout) timeout, kMsgType_MatchAll, false, false);
	((TForkWorld*) GetGlobals())->AcquireMutex();
	return err;
}


// ROM 0x0019549c Unblock__16TPseudoSyncStateFv
// The waiting task sent its event, the mutex let go meanwhile.
void
TPseudoSyncState::Unblock(void)
{
	TUnblockEvent event(this);
	((TForkWorld*) GetGlobals())->ReleaseMutex();
	Send(&event, sizeof(TUnblockEvent));
	((TForkWorld*) GetGlobals())->AcquireMutex();
}
