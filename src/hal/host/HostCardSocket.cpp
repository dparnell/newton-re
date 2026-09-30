/*
	File:		hal/host/HostCardSocket.cpp

	Contains:	TCardSocket (hal/CardSocket.h) on a host: a socket over the
				card hal/host/HostCard.h keeps in it.

				The windows are the card's own memory: the base addresses
				are host addresses (the ROM's are its virtual windows,
				gCardSocketVAddr, which the machine's MMU maps onto the
				Voyager's), so code that reads a card's CIS or common
				memory through them reads the card.  The pins are the
				card's: card detect while one is in, ready, write protect
				as its switch says, both battery detects good, 5 V voltage
				sense, and the lock switch locked.  Power, speeds, the bus
				and the controller's other registers are things that
				simply succeed.

				A card put in or taken out raises the card-detect interrupt
				and the lock interrupt (the ROM's machine has the switch
				locked and unlocked around a card): they are pending until
				the handler the card server registered is called, from the
				socket's interrupt source, once the server has said it is
				ready for them (EnableSocketInterrupt(kSocketOkToEnableInt))
				and the interrupt is enabled.

				Where a method is plain logic over the socket's fields, the
				ROM's is followed and cited; the rest stand in for the
				Voyager's registers.

	Written by:	the reconstruction (DEVIATION: the socket is hardware)
*/

#include "CardSocket.h"
#include "HostCard.h"
#include "HostInterruptSources.h"
#include "UserPhys.h"
#include "OSErrors.h"
#include "NewtErrors.h"

#include <string.h>

// The sockets there are (the ROM keeps its own list in the card server,
// gCardSockets; this is the host's, for the interrupts).
static TCardSocket*	gHostSockets[kHostCardSockets];
static Boolean		gOkToEnable = false;

const ULong	kHostVccSpec = kPCMCIA5VAvailable | kPCMCIA3p3VAvailable;		// (the platform driver's, GetPCMCIAPowerSpec)
const ULong	kHostVppSpec = kPCMCIA5VAvailable | kPCMCIA12VAvailable;


static Boolean
SocketDeadline(Int64* when)
{
	if (!gOkToEnable)
		return false;
	for (ULong i = 0; i < kHostCardSockets; i++)
	{
		TCardSocket* socket = gHostSockets[i];
		if (socket != nil && socket->InterruptState((TSocketInt) 0xFF) != 0)
		{
			when->hi = 0;
			when->lo = 0;
			return true;
		}
	}
	return false;
}


static void
SocketDeliver(void)
{
	if (!gOkToEnable)
		return;
	for (ULong i = 0; i < kHostCardSockets; i++)
		if (gHostSockets[i] != nil)
			gHostSockets[i]->InterruptDispatcher(0);
}


// HostCard's change proc: the card-detect and lock interrupts made pending
void
HostCardSocketChanged(ULong socketNumber, Boolean /*inserted*/)
{
	if (socketNumber >= kHostCardSockets || gHostSockets[socketNumber] == nil)
		return;
	TCardSocket* socket = gHostSockets[socketNumber];
	socket->fIntPending |= (1 << kSocketCardDetectedInt) | (1 << kSocketCardLockInt);
}


TCardSocket::TCardSocket(ULong socketNumber)
{
	fSocketDomain = 0;
	fSocketNumber = socketNumber;
	fControl = 0;
	fCardServerPort = 0;
	for (int i = 0; i < kSocketIntCount; i++)
	{
		fIntProcs[i] = nil;
		fIntObjects[i] = nil;
	}
	fIntEnabled = 0;
	fIntPending = 0;
	fVccOn = false;
	fVppOn = false;
	fAccessEnabled = false;
	fCommonMemSpeed = 250;
}


TCardSocket::~TCardSocket(void)
{
	if (fSocketNumber < kHostCardSockets && gHostSockets[fSocketNumber] == this)
		gHostSockets[fSocketNumber] = nil;
}


// A socket the host has: registered for its interrupts, and a card already
// in it pending as if it had just been put in.
NewtonErr
TCardSocket::Init(void)
{
	if (fSocketNumber >= kHostCardSockets)
		return kError_Bad_Parameters;
	gHostSockets[fSocketNumber] = this;
	HostCardSetChangeProc(HostCardSocketChanged);
	HostRegisterInterruptSource(SocketDeadline, SocketDeliver);
	if (HostCardIsInserted(fSocketNumber))
		fIntPending |= (1 << kSocketCardDetectedInt) | (1 << kSocketCardLockInt);
	return noErr;
}


ULong		TCardSocket::GetChipInfo(void)				{ return 1; }
TObjectId	TCardSocket::SocketDomain(void)				{ return fSocketDomain; }
TObjectId	TCardSocket::SocketPhysResource(void)		{ return 0; }

// ROM 0x00055468 SocketNumber__11TCardSocketFv
ULong		TCardSocket::SocketNumber(void)				{ return fSocketNumber; }

ULong		TCardSocket::SocketBaseAddr(void)			{ return (ULong) HostCardAttributeMemory(fSocketNumber); }
ULong		TCardSocket::AttributeMemBaseAddr(void)		{ return (ULong) HostCardAttributeMemory(fSocketNumber); }
ULong		TCardSocket::CommonMemBaseAddr(void)		{ return (ULong) HostCardCommonMemory(fSocketNumber); }
ULong		TCardSocket::IOBaseAddr(void)				{ return 0; }


// DEVIATION: no physical memory object is made.  The ROM's is the card's
// window (gCardSocketPAddr + the offset), which the PSS manager and the card
// package loader map into their domains; the host has no MMU to map it
// through (its windows are host addresses already), so the object is left
// without an id, which those callers take as nothing to map.
NewtonErr
TCardSocket::CreateSocketPhys(TUPhys*, ULong, ULong, Boolean)
{
	return noErr;
}


TCardDMAEngine*	TCardSocket::DMAEngine(void)			{ return nil; }


// ROM 0x0005505c RegisterSocketInterrupt__11TCardSocketF10TSocketIntPFPvP11TCardSocket_lPv
NewtonErr
TCardSocket::RegisterSocketInterrupt(TSocketInt intType, IntProcPtr intProc, void* intProcObj)
{
	NewtonErr err = noErr;
	if (intType < kSocketIntCount)
	{
		DisableSocketInterrupt(intType);
		ClearSocketInterrupt(intType);
		fIntProcs[intType] = intProc;
		fIntObjects[intType] = intProcObj;
	}
	else
		err = kError_Bad_Parameters;
	return err;
}


void
TCardSocket::DeregisterSocketInterrupt(TSocketInt intType)
{
	if (intType < kSocketIntCount)
	{
		DisableSocketInterrupt(intType);
		fIntProcs[intType] = nil;
		fIntObjects[intType] = nil;
	}
}


void
TCardSocket::EnableSocketInterrupt(TSocketInt intType)
{
	if (intType == kSocketOkToEnableInt)
		gOkToEnable = true;
	else if (intType < kSocketIntCount)
		fIntEnabled |= 1 << intType;
}


void
TCardSocket::DisableSocketInterrupt(TSocketInt intType)
{
	if (intType < kSocketIntCount)
		fIntEnabled &= ~(1 << intType);
}


// (the controller latches a change of the card-detect and lock pins until
// it is cleared: the card server clears them before it enables them again,
// once it has looked at the pins itself)
void
TCardSocket::ClearSocketInterrupt(TSocketInt intType)
{
	if (intType < kSocketIntCount)
		fIntPending &= ~(1 << intType);
}


NewtonErr	TCardSocket::SetSocketInterruptFlags(TSocketInt, TSocketIntFlags)	{ return noErr; }
// ROM 0x00055524 ResetInterrupts__11TCardSocketFv
// Every interrupt but the card-detect and lock ones disabled and cleared
// (the Voyager's enable register and'ed with 0xffff800c, its clear register
// written 0x7ff3): those two stay as the card server set them.
void
TCardSocket::ResetInterrupts(void)
{
	fIntEnabled &= 0xFFFF800C;
	fIntPending &= ~0x7FF3;
}


// The interrupts pending and enabled (0xFF: all of them), as a mask.
ULong
TCardSocket::InterruptState(TSocketInt intType)
{
	ULong live = fIntPending & fIntEnabled;
	if (intType == (TSocketInt) 0xFF)
		return live;
	return intType < kSocketIntCount ? (live >> intType) & 1 : 0;
}


// Every pending, enabled interrupt's proc called, in the order of the
// interrupt types (the ROM's walks the controller's priority vectors).
ULong
TCardSocket::InterruptDispatcher(ULong)
{
	ULong result = 0;
	for (int i = 0; i < kSocketIntCount; i++)
	{
		ULong bit = 1 << i;
		if ((fIntPending & fIntEnabled & bit) != 0)
		{
			fIntPending &= ~bit;
			if (fIntProcs[i] != nil)
				result = fIntProcs[i](fIntObjects[i], this);
		}
	}
	return result;
}


// ROM 0x00055408 CardLockIntHandler__11TCardSocketFv
void
TCardSocket::CardLockIntHandler(void)
{
	if (fIntProcs[kSocketCardLockInt] == nil)
		return;
	fIntProcs[kSocketCardLockInt](fIntObjects[kSocketCardLockInt], this);
}


void		TCardSocket::SetControl(ULong control)		{ fControl = control; }
ULong		TCardSocket::GetControl(void)				{ return fControl; }
Boolean		TCardSocket::IsIOInteface(void)				{ return false; }
Boolean		TCardSocket::IsPCMCIABusEnable(void)		{ return true; }


// What the Voyager's pin register would say, in GetPCPins' bits.
ULong
TCardSocket::GetPCPins(void)
{
	ULong pins = 0;
	if (HostCardIsInserted(fSocketNumber))
	{
		pins |= kCardCD1 | kCardCD2 | kCardReadyIREQ | kCardMemBVD1 | kCareMemBVD2 | kCardVS1 | kCardVS2;
		if (HostCardIsWriteProtected(fSocketNumber))
			pins |= kCardWPIOIs16;
	}
	return pins;
}


// The Voyager's pin register itself, which GetPCPins translates (ROM
// 0x00055c24): 0x10 and 0x20 the voltage sense pins (both high - left open
// - for a 5 V card), 0x400 ready, 1 and 2 the battery pins, 0x200 write
// protect, and 4 and 8 the card-detect pins, which are low when a card is
// in.
ULong
TCardSocket::GetVPCPins(void)
{
	if (!HostCardIsInserted(fSocketNumber))
		return 0x4 | 0x8;
	ULong pins = 0x10 | 0x20 | 0x400 | 0x1 | 0x2;
	if (HostCardIsWriteProtected(fSocketNumber))
		pins |= 0x200;
	return pins;
}


// ROM 0x00055d50 IsCardDetected__11TCardSocketFv (the card-detect pins)
Boolean
TCardSocket::IsCardDetected(void)
{
	return (GetPCPins() & (kCardCD1 | kCardCD2)) == (kCardCD1 | kCardCD2);
}


// ROM 0x00055d70 IsReady__11TCardSocketFv
Boolean
TCardSocket::IsReady(void)
{
	return (GetPCPins() & kCardReadyIREQ) != 0;
}


Boolean		TCardSocket::IsIRQ(void)					{ return false; }
Boolean		TCardSocket::IsStatusChanged(void)			{ return false; }


// ROM 0x00055dd4 IsWriteProtected__11TCardSocketFv
Boolean
TCardSocket::IsWriteProtected(void)
{
	return (GetPCPins() & kCardWPIOIs16) != 0;
}


TNanoAmp	TCardSocket::VccMaxCurrent(void)			{ return 500000000; }
TNanoAmp	TCardSocket::VppMaxCurrent(void)			{ return 120000000; }

// ROM 0x00055ec4 Vpp1MaxCurrent__11TCardSocketFv
TNanoAmp	TCardSocket::Vpp1MaxCurrent(void)			{ return VppMaxCurrent() >> 1; }
TNanoAmp	TCardSocket::Vpp2MaxCurrent(void)			{ return VppMaxCurrent() >> 1; }


// ROM 0x00055ef4 VccMaxVoltage__11TCardSocketFv
TMicroVolt
TCardSocket::VccMaxVoltage(void)
{
	ULong spec = VccVoltageSpec();
	long tenths = (spec & 2) ? 120 : (spec & 1) ? 50 : (spec & 4) ? 33 : 0;
	return tenths * 100000;
}


// ROM 0x00055f34 VccMinVoltage__11TCardSocketFv
TMicroVolt
TCardSocket::VccMinVoltage(void)
{
	ULong spec = VccVoltageSpec();
	long tenths = (spec & 4) ? 33 : (spec & 1) ? 50 : (spec & 2) ? 120 : 0;
	return tenths * 100000;
}


// ROM 0x00055f74 VppMaxVoltage__11TCardSocketFv
TMicroVolt
TCardSocket::VppMaxVoltage(void)
{
	ULong spec = VppVoltageSpec();
	long tenths = (spec & 2) ? 120 : (spec & 1) ? 50 : (spec & 4) ? 33 : 0;
	return tenths * 100000;
}


// ROM 0x00055fb4 VppMinVoltage__11TCardSocketFv
TMicroVolt
TCardSocket::VppMinVoltage(void)
{
	ULong spec = VppVoltageSpec();
	long tenths = (spec & 4) ? 33 : (spec & 1) ? 50 : (spec & 2) ? 120 : 0;
	return tenths * 100000;
}


TNanoSecond	TCardSocket::VccRisingTime(void)			{ return 0; }


// ROM 0x00055e8c VppRisingTime__11TCardSocketFv
TNanoSecond
TCardSocket::VppRisingTime(void)
{
	return (VppVoltageSpec() & 2) != 0 ? 50000000 : 0;
}


void		TCardSocket::PCMCIAReset(void)				{ }
void		TCardSocket::SelectMemoryInterface(void)	{ }
void		TCardSocket::SelectIOInterface(void)		{ }
void		TCardSocket::SetDefaultSpeeds(void)			{ fCommonMemSpeed = 250; }
void		TCardSocket::SetAttributeMemSpeed(TNanoSecond)	{ }
void		TCardSocket::SetCommonMemSpeed(TNanoSecond speed)	{ fCommonMemSpeed = speed; }
void		TCardSocket::SetIOSpeed(TNanoSecond)		{ }
void		TCardSocket::SetWatchTimer(TNanoSecond)		{ }
void		TCardSocket::SetDMAWatchTimer(TNanoSecond)	{ }
void		TCardSocket::SetBusTimer(TNanoSecond)		{ }
TNanoSecond	TCardSocket::GetCommonMemSpeed(void)		{ return fCommonMemSpeed; }
ULong		TCardSocket::Version(void)					{ return 2; }
ULong		TCardSocket::VccVoltageSpec(void)			{ return kHostVccSpec; }
ULong		TCardSocket::VppVoltageSpec(void)			{ return kHostVppSpec; }
NewtonErr	TCardSocket::SelectVoltageLevel(TSocketPowerLevels)	{ return noErr; }
ULong		TCardSocket::GetSocketASICId(void)			{ return 0; }
void		TCardSocket::SetRdWrQueueControl(ULong)		{ }
ULong		TCardSocket::GetRdWrQueueControl(void)		{ return 0; }
ULong		TCardSocket::GetRdWrQueueStatus(void)		{ return 0; }
void		TCardSocket::SetPullupControl(ULong)		{ }
ULong		TCardSocket::GetPullupControl(void)			{ return 0; }


// A 16-bit bus cycle, big-endian, into the common memory (the card's
// flash takes its writes through the host's card TFlash, so this only
// reads; a write is dropped).
void		TCardSocket::Do16BitWrite(ULong, ULong)		{ }

ULong
TCardSocket::Do16BitRead(ULong addr)
{
	const unsigned char* p = (const unsigned char*) addr;
	return ((ULong) p[0] << 8) | p[1];
}


ULong		TCardSocket::ConvertWaitCount(ULong count)	{ return count; }
void		TCardSocket::EnableSocketAccess(void)		{ fAccessEnabled = true; }
ULong		TCardSocket::DisableSocketAccess(void)		{ fAccessEnabled = false; return 0; }
void		TCardSocket::EnableSocketAbort(void)		{ }
void		TCardSocket::DisableSocketAbort(void)		{ }
NewtonErr	TCardSocket::MakeSocketAccessible(ULong, ULong)		{ return noErr; }
NewtonErr	TCardSocket::MakeSocketInaccessible(ULong, ULong)	{ return noErr; }
TCardSocket*	TCardSocket::SelectPCMCIABus(void)		{ return this; }
TCardSocket*	TCardSocket::DeselectPCMCIABus(void)	{ return this; }
ULong		TCardSocket::IsPCMCIABus(void)				{ return 1; }
void		TCardSocket::EnableBus(void)				{ }
void		TCardSocket::DisableBus(void)				{ }
NewtonErr	TCardSocket::RequestPower(TSocketPowerLevels, ULong)	{ return noErr; }
TCardSocket*	TCardSocket::SetCardServerPort(ULong port)	{ fCardServerPort = port; return this; }
void		TCardSocket::SetDefaultConfig(void)			{ }
void		TCardSocket::VccOn(void)					{ fVccOn = true; }
void		TCardSocket::VccOff(void)					{ fVccOn = false; }
void		TCardSocket::VppOn(void)					{ fVppOn = true; }
void		TCardSocket::VppOff(void)					{ fVppOn = false; }
TCardSocket*	TCardSocket::Vpp1On(void)				{ fVppOn = true; return this; }
TCardSocket*	TCardSocket::Vpp1Off(void)				{ fVppOn = false; return this; }
TCardSocket*	TCardSocket::Vpp2On(void)				{ fVppOn = true; return this; }
TCardSocket*	TCardSocket::Vpp2Off(void)				{ fVppOn = false; return this; }
Boolean		TCardSocket::IsVccOn(void)					{ return fVccOn; }
Boolean		TCardSocket::IsVppOn(void)					{ return fVppOn; }
