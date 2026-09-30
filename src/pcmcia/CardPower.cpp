/*
	File:		pcmcia/CardPower.cpp

	Contains:	The card sockets' power (the DDK's CardPower.h): Vcc, the
				card's supply, and Vpp, its programming voltage, each
				counted per socket - the first On switches it on (and waits
				for it to rise), the last Off starts a countdown the idle
				task runs down (VccIdleOff, VppIdleOff) before switching it
				off - and the notifications a socket's owner asks for when
				its Vcc goes.  The internal flash's Vpp is hal/Flash.h's.

				Switching a supply on tells the card server (a 0x50 message:
				the socket has power), which the server passes on.

	Reconstructed from the MP2x00 US ROM (0x000502fc-0x0005105c); each
	function cites its origin.
*/

#include "CardPower.h"
#include "CardSocket.h"
#include "CardMessage.h"
#include "CardServerGlobals.h"
#include "hal/Flash.h"
#include "UserPorts.h"
#include "UserSemaphore.h"
#include "UserTasks.h"
#include "NameServer.h"
#include "hal/Atomic.h"
#include "OSErrors.h"

static ULong				gVccCount[kMaxCardSockets];				// ROM 0x0c105f64 gVccCount
static UChar				gVccState[kMaxCardSockets];				// ROM 0x0c100984 gVccState
static ULong				gVccOffCountdown[kMaxCardSockets];		// ROM 0x0c105f74 gVccOffCountdown
static ULong				gVppCount[kMaxCardSockets];				// ROM 0x0c105f84 gVppCount
static UChar				gVppState[kMaxCardSockets];				// ROM 0x0c100988 gVppState
static ULong				gVppOffCountdown[kMaxCardSockets];		// ROM 0x0c105f94 gVppOffCountdown
static TTimeout				gVccSocketTimeouts[kMaxCardSockets];	// ROM 0x0c105fc4 gVccSocketTimeouts
static TULockingSemaphore*	gPowerSemaphore[kMaxCardSockets];		// ROM 0x0c105f54 gPowerSemaphore

struct VccOffNotifyInfo
{
	VccOffNotifyFuncPtr	fFunc;
	void*				fRefCon;
};
static VccOffNotifyInfo		gVccOffNotifyInfo[kMaxCardSockets];		// ROM 0x0c105fa4 gVccOffNotifyInfo
static ULong				gCountVccOffNotify = 0;					// ROM 0x0c100a6c gCountVccOffNotify
static TTimeout				gVccTimeout = 0;						// ROM 0x0c100a70 gVccTimeout

void	VccOffNotify();						// (not in CardPower.h)

// the power message to the card server, and the port it goes to
static TUPort				gPowerServerPort;						// ROM 0x0c10099c (unnamed)
static TUAsyncMessage		gPowerAsyncMessage;						// ROM 0x0c1009a4 (unnamed)
static TCardMessage			gPowerMessage;							// ROM 0x0c1009b4 (unnamed)

const TTimeout	kVccDefaultTimeout = 0x2328000;
const TTimeout	kVppOffDelay = 0x1c20000;
const TTimeout	kPowerIdleTick = 0x708000;


// Waiting for a supply to rise: its rising time, in milliseconds, as a
// timeout (0xe66 units a millisecond).
static void
WaitForRise(TNanoSecond risingTime)
{
	ULong milliseconds = (uint32_t) risingTime / 1000000;
	Sleep((TTimeout) (milliseconds * 0xE66));
}


// The power message to the card server: this socket has power.
static void
TellServer(int socket)
{
	gPowerMessage.MessageStuff(kCardMessagePowerOn, socket, 0);
	gPowerServerPort.Send(&gPowerAsyncMessage, &gPowerMessage, sizeof(TCardMessage), kNoTimeout, nil, 0, false);
}


// ROM 0x000502fc InitVppManager__Fv
// Every socket's supplies off, their counts and countdowns cleared, a
// semaphore for each; the card server's port found for the power messages.
NewtonErr
InitVppManager()
{
	gVccTimeout = kVccDefaultTimeout;
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
	{
		gVccCount[i] = 0;
		gVccState[i] = 0;
		gVccOffCountdown[i] = 0;
		gVppCount[i] = 0;
		gVppState[i] = 0;
		gVppOffCountdown[i] = 0;
		gCardSockets[i]->DisableBus();
		gCardSockets[i]->VppOff();
		gCardSockets[i]->VccOff();
		gVccOffNotifyInfo[i].fFunc = nil;
		gCountVccOffNotify = 0;
		gVccSocketTimeouts[i] = kVccDefaultTimeout;
		gPowerSemaphore[i] = new TULockingSemaphore;
		gPowerSemaphore[i]->Init();
	}
	// (the internal flash's semaphore, gInternalPowerSemaphore, is hal/Flash.h's)
	TUNameServer nameServer;
	TObjectId port = 0;
	ULong spec;
	nameServer.Lookup("cdsv", "TUPort", &port, &spec);
	gPowerServerPort = port;
	NewtonErr err = gPowerAsyncMessage.Init(true);
	return err;
}


// ROM 0x00050a78 VccOn__FiUc
// The socket's supply counted on (switched on the first time, the server
// told, the supply let rise, the bus enabled).  fail_if_not_on: only when
// it is on already.  ==> whether it is on.
Boolean
VccOn(int socket, Boolean fail_if_not_on)
{
	Boolean on = true;
	gPowerSemaphore[socket]->Acquire(kWaitOnBlock);
	if (fail_if_not_on == 0 || (on = gVccCount[socket] != 0))
	{
		gVccCount[socket]++;
		if (gVccState[socket] == 0)
		{
			gCardSockets[socket]->VccOn();
			TellServer(socket);
			gVccState[socket] = 1;
			WaitForRise(gCardSockets[socket]->VccRisingTime());
			gCardSockets[socket]->EnableBus();
		}
	}
	gPowerSemaphore[socket]->Release();
	return on;
}


// The supply off now, and anyone who asked told.
static void
CallVccOffNotifies(void)
{
	for (long i = (long) gNumberOfHWSockets - 1, j = 0; i >= 0; i--, j++)
		if (gVccOffNotifyInfo[j].fFunc != nil)
			gVccOffNotifyInfo[j].fFunc(gVccOffNotifyInfo[j].fRefCon);
}


// ROM 0x00050d28 VccOff__FiUl
// The socket's supply counted off: at the last, it stays on for `delay`
// (the idle task counts it down) unless delay is 0, when it goes at once;
// kResetTimeOut forgets the count and switches it off.
void
VccOff(int socket, TTimeout delay)
{
	gPowerSemaphore[socket]->Acquire(kWaitOnBlock);
	Boolean switchedOff = false;
	if (gVccState[socket] == 1)
	{
		Boolean off = true;
		if (delay == (TTimeout) kResetTimeOut)
		{
			gVccCount[socket] = 0;
			gVccOffCountdown[socket] = 0;
		}
		else
		{
			if (delay == 0)
				gVccOffCountdown[socket] = 0;
			else if (gVccOffCountdown[socket] < (ULong) delay)
				gVccOffCountdown[socket] = delay;
			if (gVccCount[socket] != 0)
				gVccCount[socket]--;
			off = gVccCount[socket] == 0 && gVccOffCountdown[socket] == 0;
		}
		if (off)
		{
			gCardSockets[socket]->DisableBus();
			gVccState[socket] = 0;
			gCardSockets[socket]->VccOff();
			switchedOff = true;
		}
	}
	gPowerSemaphore[socket]->Release();
	if (switchedOff && gCountVccOffNotify != 0)
		CallVccOffNotifies();
}


// ROM 0x00050d18 VccOff__Fi
// (with the longest socket timeout)
void
VccOff(int socket)
{
	VccOff(socket, gVccTimeout);
}


// ROM 0x00050510 VppOn__FiUc
// The socket's programming voltage (and so its supply) counted on; socket
// -1 is the internal flash's.
Boolean
VppOn(int socket, Boolean fail_if_not_on)
{
	Boolean on = true;
	if (socket == -1)
	{
		InternalVppOn();
		return true;
	}
	VccOn(socket, fail_if_not_on);
	gPowerSemaphore[socket]->Acquire(kWaitOnBlock);
	if (fail_if_not_on == 0 || (on = gVppCount[socket] != 0))
	{
		gVppCount[socket]++;
		if (gVppState[socket] == 0)
		{
			gCardSockets[socket]->VppOn();
			gVppState[socket] = 1;
			WaitForRise(gCardSockets[socket]->VppRisingTime());
		}
	}
	gPowerSemaphore[socket]->Release();
	return on;
}


// ROM 0x00050c50 VppOff__FiUl
// The programming voltage (and the supply VppOn counted on) counted off,
// as VccOff counts the supply.
void
VppOff(int socket, TTimeout delay)
{
	if (socket == -1)
	{
		InternalVppOff();
		return;
	}
	VccOff(socket);
	gPowerSemaphore[socket]->Acquire(kWaitOnBlock);
	if (gVppState[socket] == 1)
	{
		Boolean off = true;
		if (delay == (TTimeout) kResetTimeOut)
		{
			gVppCount[socket] = 0;
			gVppOffCountdown[socket] = 0;
		}
		else
		{
			if (delay == 0)
				gVppOffCountdown[socket] = 0;
			else if (gVppOffCountdown[socket] < (ULong) delay)
				gVppOffCountdown[socket] = delay;
			if (gVppCount[socket] != 0)
				gVppCount[socket]--;
			off = gVppCount[socket] == 0 && gVppOffCountdown[socket] == 0;
		}
		if (off)
		{
			gVppState[socket] = 0;
			gCardSockets[socket]->VppOff();
		}
	}
	gPowerSemaphore[socket]->Release();
}


// ROM 0x00050c44 VppOff__Fi
// (the voltage kept on for kVppOffDelay after its last holder)
void
VppOff(int socket)
{
	VppOff(socket, kVppOffDelay);
}


// ROM 0x000505ec VccIdleOff__FUl
// The socket's supply off at once, whoever holds it.
Boolean
VccIdleOff(ULong socket)
{
	gCardSockets[socket]->DisableBus();
	gCardSockets[socket]->VccOff();
	if (gCountVccOffNotify != 0)
		VccOffNotify();
	return false;
}


// ROM 0x00050630 RestoreVppState__Fv
NewtonErr
RestoreVppState()
{
	return noErr;
}


// ROM 0x00050634 RestoreCardPower__FUl
// After a sleep: the supplies that were on switched on again.
// DEVIATION: the internal flash's Vpp (IOPowerOn) is hal/Flash.h's, which
// the host keeps as a count only.
NewtonErr
RestoreCardPower(ULong socket)
{
	gPowerSemaphore[socket]->Acquire(kWaitOnBlock);
	if (gVccState[socket] == 1)
	{
		gCardSockets[socket]->VccOn();
		WaitForRise(gCardSockets[socket]->VccRisingTime());
		gCardSockets[socket]->EnableBus();
	}
	UChar vpp = gVppState[socket];
	if (vpp == 1)
		gCardSockets[socket]->VppOn();
	gPowerSemaphore[socket]->Release();
	if (vpp == 1)
		WaitForRise(gCardSockets[socket]->VppRisingTime());
	return noErr;
}


// ROM 0x00050958 AnyVppOn__Fv
// DEVIATION: the internal flash's state is hal/Flash.h's, which the host
// does not keep; only the sockets' are asked.
Boolean
AnyVppOn()
{
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
		if (gVppState[i] == 1)
			return true;
	return false;
}


// ROM 0x000509b0 VccOffNotify__Fv
void
VccOffNotify()
{
	CallVccOffNotifies();
}


// ROM 0x000509fc RegisterVccOffNotify__FiPFPv_vPv
// (a socket has one; a second is ignored)
void
RegisterVccOffNotify(int socket, VccOffNotifyFuncPtr func, void* refCon)
{
	if (gVccOffNotifyInfo[socket].fFunc != nil)
		return;
	if (func == nil)
		return;
	gVccOffNotifyInfo[socket].fFunc = func;
	gVccOffNotifyInfo[socket].fRefCon = refCon;
	gCountVccOffNotify++;
}


// The longest socket timeout, which VccOff(socket) waits.
static void
UpdateVccTimeout(void)
{
	gVccTimeout = kVccDefaultTimeout;
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
		if ((ULong) gVccTimeout < (ULong) gVccSocketTimeouts[i])
			gVccTimeout = gVccSocketTimeouts[i];
}


// ROM 0x00050a38 UnregisterVccOffNotify__Fi
// ...and the socket's timeout back to the default.
void
UnregisterVccOffNotify(int socket)
{
	if (gVccOffNotifyInfo[socket].fFunc == nil)
		return;
	gVccOffNotifyInfo[socket].fFunc = nil;
	gCountVccOffNotify--;
	EnterAtomic();
	gVccSocketTimeouts[socket] = kVccDefaultTimeout;
	UpdateVccTimeout();
	ExitAtomic();
}


// ROM 0x00050b9c IsVccOffNotifyRegistered__Fi
Boolean
IsVccOffNotifyRegistered(int socket)
{
	return gVccOffNotifyInfo[socket].fFunc != nil;
}


// ROM 0x00050bb8 GetVccTimeout__Fi
TTimeout
GetVccTimeout(int socket)
{
	return gVccSocketTimeouts[socket];
}


// ROM 0x00050bc8 SetVccTimeout__FiUl
// (never less than the default)
void
SetVccTimeout(int socket, TTimeout timeout)
{
	EnterAtomic();
	if ((ULong) timeout <= (ULong) kVccDefaultTimeout)
		timeout = kVccDefaultTimeout;
	gVccSocketTimeouts[socket] = timeout;
	UpdateVccTimeout();
	ExitAtomic();
}


// ROM 0x00050e10 VppIdleOff__FUc
// The idle task's pass: each socket whose Vpp has no holder counts down a
// tick and switches off at the end (force_off: all off now).  ==> whether
// any socket's Vcc is still on.
Boolean
VppIdleOff(Boolean force_off)
{
	Boolean anyVcc = false;
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
	{
		if (force_off == 0)
		{
			if (gPowerSemaphore[i]->Acquire(kNoWaitOnBlock) == noErr)
			{
				if (gVppState[i] == 1 && gVppCount[i] == 0)
				{
					ULong countdown = gVppOffCountdown[i];
					if (countdown > 0x6FFFFF && countdown - 0x700000 > 0x7FFF)
						gVppOffCountdown[i] = countdown - kPowerIdleTick;
					else
					{
						gVppOffCountdown[i] = 0;
						gVppState[i] = 0;
						gCardSockets[i]->VppOff();
					}
				}
				gPowerSemaphore[i]->Release();
			}
			if (gVccState[i] == 1)
				anyVcc = true;
		}
		else
			gCardSockets[i]->VppOff();
	}
	return anyVcc;
}


// ROM 0x00050f24 VccIdleOff__FUc
// As VppIdleOff, for the supplies (the notifications called when one goes).
Boolean
VccIdleOff(Boolean force_off)
{
	Boolean anyVcc = false;
	Boolean switchedOff = false;
	if (gNumberOfHWSockets == 0)
		return false;
	for (ULong i = 0; i < gNumberOfHWSockets; i++)
	{
		if (force_off == 0)
		{
			if (gPowerSemaphore[i]->Acquire(kNoWaitOnBlock) == noErr)
			{
				if (gVccState[i] == 1 && gVccCount[i] == 0)
				{
					ULong countdown = gVccOffCountdown[i];
					if (countdown > 0x6FFFFF && countdown - 0x700000 > 0x7FFF)
						gVccOffCountdown[i] = countdown - kPowerIdleTick;
					else
					{
						gCardSockets[i]->DisableBus();
						gVccOffCountdown[i] = 0;
						gVccState[i] = 0;
						gCardSockets[i]->VccOff();
						switchedOff = true;
					}
				}
				gPowerSemaphore[i]->Release();
			}
		}
		else
		{
			gCardSockets[i]->DisableBus();
			gCardSockets[i]->VccOff();
		}
		if (gVccState[i] == 1)
			anyVcc = true;
	}
	if (switchedOff && gCountVccOffNotify != 0)
		VccOffNotify();
	return anyVcc;
}


// (for tests: a socket's counts)
ULong	CardVccCount(int socket)	{ return gVccCount[socket]; }
ULong	CardVppCount(int socket)	{ return gVppCount[socket]; }
Boolean	CardVccIsOn(int socket)		{ return gVccState[socket] == 1; }
