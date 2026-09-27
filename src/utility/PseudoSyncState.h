/*
	File:		utility/PseudoSyncState.h

	Contains:	TPseudoSyncState, which is how a fork world waits for
				something without stopping the world: Block forks the world
				(TForkWorld::Fork) so that a new task carries on running its
				event loop, then waits on a port of its own - with the
				family's mutex let go - until Unblock sends it a
				TUnblockEvent.  A modal dialog is the use the ROM makes of
				it (FModalDialog): the script that opened the dialog stops
				where it is, the fork goes on handling the pen and the keys,
				and the dialog's exit unblocks the script, which runs to its
				end and lets its task finish.

	Reconstructed from the MP2x00 US ROM (0x00195350-0x00195518); each
	function cites its origin.
*/

#ifndef __PSEUDOSYNCSTATE_H
#define __PSEUDOSYNCSTATE_H

#ifndef __USERPORTS_H
#include "UserPorts.h"
#endif
#ifndef __AEVENTS_H
#include "AEvents.h"
#endif

// what Unblock sends: an event of the newt class, id 'sync', naming the
// state (0x0c bytes in the ROM)
class TUnblockEvent : public TAEvent
{
public:
				TUnblockEvent(void* state);				// ROM 0x00195350 __ct__13TUnblockEventFPv
				~TUnblockEvent();						// ROM 0x00195394 __dt__13TUnblockEventFv

	void*		fState;					// +0x08
};

// a port to wait on (8 bytes: the TUPort)
class TPseudoSyncState : public TUPort
{
public:
				TPseudoSyncState();						// ROM 0x001953a0 __ct__16TPseudoSyncStateFv
				~TPseudoSyncState();					// ROM 0x001953d4 __dt__16TPseudoSyncStateFv

	long		Init(void);								// ROM 0x00195404 Init__16TPseudoSyncStateFv - the port made
	long		Block(ULong timeout);					// ROM 0x00195408 Block__16TPseudoSyncStateFUl - the world forked, then the wait for Unblock; ==> the fork's error, else the receive's
	void		Unblock(void);							// ROM 0x0019549c Unblock__16TPseudoSyncStateFv
};

#endif	/* __PSEUDOSYNCSTATE_H */
