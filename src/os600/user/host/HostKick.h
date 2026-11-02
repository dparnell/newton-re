/*
	File:		os600/user/host/HostKick.h

	Contains:	THostKick: a host task waiting until a host thread has left
				it something - rather than looking every few milliseconds,
				which wakes the machine a hundred times a second with
				nothing to do (docs/host-runtime.md, "The idle task's
				wait").  The host's keyboard task waits on one for the
				window's keys, dropped packages and the device's turning
				(host/HostKeyboard.cpp); the T-Deck Pro's console task for
				its lines (host/esp32/main/newtonmain.cpp).

				A host thread (not a task: the window's, a board's) calls
				Kick, which only marks it and wakes the idle task
				(hal/host/HostIdleWait.h).  The kick is an interrupt source
				(hal/host/HostInterruptSources.h), due at once while marked:
				its delivery sends a word to the task's port with
				SendForInterrupt, as a driver's interrupt tells its task,
				and Wait - a Receive on that port - returns.  Kicks that come
				before the task looks are one wake: it is to take everything
				that is waiting each time it wakes.

				Host code (no ROM counterpart).
*/

#ifndef __HOSTKICK_H
#define __HOSTKICK_H

#include <atomic>
#include "NewtonTime.h"

class TUPort;
class TUAsyncMessage;

class THostKick
{
public:
				THostKick() : fPort(nullptr), fMessage(nullptr), fWord(0), fDue(false) {}
	long		Init(void);					// on the task that is to wait: its port, the source
	void		Kick(void);					// from any thread
	Boolean		Wait(TTimeout timeout);		// the task: ==> true when kicked, false when the time ran out (kNoTimeout: none)

	TUPort*				fPort;				// (the rest the source's)
	TUAsyncMessage*		fMessage;
	ULong				fWord;
	std::atomic<bool>	fDue;
};

#endif	/* __HOSTKICK_H */
