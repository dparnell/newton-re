/*
	File:		os600/user/host/HostKick.cpp

	Contains:	THostKick: a host task woken by a host thread
				(HostKick.h).  Host code (no ROM counterpart).
*/

#include "HostKick.h"
#include "UserPorts.h"
#include "hal/Timer.h"
#include "hal/host/HostInterruptSources.h"
#include "hal/host/HostIdleWait.h"

// the kicks made, for the one interrupt source that serves them all
const int			kMaxKicks = 4;
static THostKick*	gKicks[kMaxKicks];
static int			gKickCount = 0;


// Due at once while a kick is marked: time nought, long past (the clock
// read here would be later than the now it is compared with, and never due)
static Boolean
KickDeadline(Int64* when)
{
	for (int i = 0; i < gKickCount; i++)
		if (gKicks[i]->fDue.load())
		{
			when->hi = 0;
			when->lo = 0;
			return true;
		}
	return false;
}


// Each kick marked: its task's port sent a word (an interrupt's send; one
// already on its way is enough - the send then fails, and nothing is lost)
static void
KickDeliver(void)
{
	for (int i = 0; i < gKickCount; i++)
	{
		THostKick* kick = gKicks[i];
		if (kick->fDue.exchange(false))
			SendForInterrupt(kick->fPort->fId, kick->fMessage->GetMsgId(), 0, &kick->fWord, sizeof(kick->fWord),
							 kMsgType_FromInterrupt, 0, nil, false);
	}
}


long
THostKick::Init(void)
{
	if (gKickCount == kMaxKicks)
		return -1;
	fPort = new TUPort;
	fMessage = new TUAsyncMessage;
	long err;
	if (fPort == nil || fMessage == nil || (err = fPort->Init()) != noErr || (err = fMessage->Init(false)) != noErr)
		return -1;
	gKicks[gKickCount++] = this;
	HostRegisterInterruptSource(KickDeadline, KickDeliver);
	return noErr;
}


void
THostKick::Kick(void)
{
	fDue.store(true);
	HostIdleWake();
}


Boolean
THostKick::Wait(TTimeout timeout)
{
	if (fPort == nil)
		return false;
	ULong size = 0, word = 0;
	return fPort->Receive(&size, &word, sizeof(word), nil, nil, timeout) == noErr;
}
