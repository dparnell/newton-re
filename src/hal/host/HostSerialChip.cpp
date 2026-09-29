/*
	File:		hal/host/HostSerialChip.cpp

	Contains:	THostSerialChip (HostSerialChip.h): the external serial port
				over a TCP socket.

	Host code (no ROM counterpart); the protocol's methods are the ROM's
	(TSerialChip16450, 0x001d52a0-0x001d5f9c, is the model for a simple
	chip).
*/

#include "HALSerialChip.h"
#include "HostSerialChip.h"
#include "HostSockets.h"
#include "HostInterruptSources.h"
#include "HALOptions.h"
#include "Options.h"
#include "SerialOptions.h"
#include "NewtonTime.h"
#include "CompMath.h"
#include "hal/Timer.h"

#include <string.h>
#include <stdio.h>

#define kRxSize		4096
#define kTxSize		4096


PROTOCOL THostSerialChip : public TSerialChip
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostSerialChip);

	THostSerialChip*	New();
	void				Delete();

	NewtonErr 			InstallChipHandler(void* serialTool, SCCChannelInts* intHandlers);
	NewtonErr 			RemoveChipHandler(void* serialTool);
	void				PutByte(UByte nextChar);
	void				ResetTxBEmpty();
	UByte				GetByte();
	Boolean				TxBufEmpty();
	Boolean				RxBufFull();
	RxErrorStatus		GetRxErrorStatus();
	SerialStatus		GetSerialStatus();
	void				ResetSerialStatus();
	void				SetSerialOutputs(SerialOutputControl);
	void				ClearSerialOutputs(SerialOutputControl);
	SerialOutputControl	GetSerialOutputs();
	void				PowerOff();
	void				PowerOn();
	Boolean				PowerIsOn();
	void				SetInterruptEnable(Boolean enable);
	void				Reset();
	void				SetBreak(Boolean assert);
	InterfaceSpeed		SetSpeed(BitRate bitsPerSec);
	void				SetIOParms(TCMOSerialIOParms* opt);
	void				Reconfigure();
	NewtonErr			Init(TCardSocket* theCardSocket, TCardHandler* theCardHandler, UByte* baseRegAddr);
	void				CardRemoved();
	SerialFeatures		GetFeatures();
	NewtonErr			InitByOption(TOption* initOpt);
	NewtonErr 			ProcessOption(TOption* opt);
	NewtonErr			SetSerialMode(SerialMode mode);
	void				SysEventNotify(ULong event);
	void				SetTxDTransceiverEnable(Boolean enable);
	RxErrorStatus		GetByteAndStatus(UByte* nextCharPtr);
	NewtonErr			SetIntSourceEnable(SerialIntSource src, Boolean enable);
	Boolean				AllSent();
	void 				ConfigureForOutput(Boolean start);
	NewtonErr			InitTxDMA(TCircleBuf* buf, SCCIntHandler txDMAIntHandler);
	NewtonErr			InitRxDMA(TCircleBuf* buf, ULong notifyLevel, RxDMAIntHandler intHandler);
	NewtonErr			TxDMAControl(DMAControl ctl);
	NewtonErr			RxDMAControl(DMAControl ctl);
	void 				SetSDLCAddress(UByte nodeID);
	void				ReEnableReceiver(Boolean reset);
	Boolean 			LinkIsFree(Boolean resetClks);
	Boolean 			SendControlPacket(UByte pType, UByte dest, Boolean syncPulse);
	void				WaitForPacket(ULong delay);
	NewtonErr			WaitForAllSent();

	// the host's side
	NewtonErr			Listen(unsigned short port);
	Boolean				Due(Int64* when);
	void				Poll(void);

	unsigned short		fPort;
	int					fListener;			// the listening socket, or -1
	int					fClient;			// the desktop's connection, or -1
	void*				fTool;				// the tool that has the chip, and its handlers
	SCCChannelInts		fHandlers;
	Boolean				fIntEnabled;
	Boolean				fPowered;
	Boolean				fTxIntPending;		// the transmitter has emptied: tell the tool
	Boolean				fStatusChanged;		// DCD/DSR/CTS changed: tell the tool
	SerialOutputControl	fOutputs;
	BitRate				fSpeed;
	UByte				fRx[kRxSize];		// what the desktop sent, not yet read
	long				fRxHead, fRxCount;
	UByte				fTx[kTxSize];		// what the tool put, not yet sent
	long				fTxCount;
	Int64				fNextPoll;			// when the wire is next looked at
};

PROTOCOL_IMPL_SOURCE_MACRO(THostSerialChip)
PROTOCOL_CLASSINFO(THostSerialChip, "TSerialChip", "v2.0", 0x30000, 0, nil)

static THostSerialChip*	gHostSerialChip = nil;


THostSerialChip*
THostSerialChip::New()
{
	fPort = 0;
	fListener = -1;
	fClient = -1;
	fTool = nil;
	memset(&fHandlers, 0, sizeof(fHandlers));
	fIntEnabled = false;
	fPowered = true;
	fTxIntPending = false;
	fStatusChanged = false;
	fOutputs = 0;
	fSpeed = 38400;
	fRxHead = fRxCount = 0;
	fTxCount = 0;
	fNextPoll.hi = fNextPoll.lo = 0;
	return this;
}


void
THostSerialChip::Delete()
{
	if (fClient >= 0)
		HostSocketClose(fClient);
	if (fListener >= 0)
		HostSocketClose(fListener);
	fClient = fListener = -1;
}


NewtonErr
THostSerialChip::InstallChipHandler(void* serialTool, SCCChannelInts* intHandlers)
{
	if (fTool != nil)
		return -1;				// (claimed: as the 16450 answers)
	fHandlers = *intHandlers;
	fTool = serialTool;
	return noErr;
}


NewtonErr
THostSerialChip::RemoveChipHandler(void* serialTool)
{
	if (serialTool != fTool)
		return -1;
	fTool = nil;
	fIntEnabled = false;
	memset(&fHandlers, 0, sizeof(fHandlers));
	return noErr;
}


void
THostSerialChip::PutByte(UByte nextChar)
{
	if (fTxCount < kTxSize)
		fTx[fTxCount++] = nextChar;
	fTxIntPending = true;
}


void
THostSerialChip::ResetTxBEmpty()
{
	fTxIntPending = false;
}


UByte
THostSerialChip::GetByte()
{
	if (fRxCount == 0)
		return 0;
	UByte b = fRx[fRxHead];
	fRxHead = (fRxHead + 1) % kRxSize;
	fRxCount--;
	return b;
}


Boolean			THostSerialChip::TxBufEmpty()		{ return fTxCount < kTxSize; }
Boolean			THostSerialChip::RxBufFull()		{ return fRxCount > 0; }
RxErrorStatus	THostSerialChip::GetRxErrorStatus()	{ return 0; }


SerialStatus
THostSerialChip::GetSerialStatus()
{
	SerialStatus status = 0;
	if (fRxCount > 0)
		status |= kSerialRxCharAvailable;
	if (fTxCount < kTxSize)
		status |= kSerialTxBufferEmpty;
	if (fClient >= 0)
		status |= kSerialDCDAsserted | kSerialDSRAsserted | kSerialCTSAsserted;
	return status;
}


void				THostSerialChip::ResetSerialStatus()					{ fStatusChanged = false; }
void				THostSerialChip::SetSerialOutputs(SerialOutputControl c)	{ fOutputs |= c; }
void				THostSerialChip::ClearSerialOutputs(SerialOutputControl c)	{ fOutputs &= ~c; }
SerialOutputControl	THostSerialChip::GetSerialOutputs()					{ return fOutputs; }
void				THostSerialChip::PowerOff()								{ fPowered = false; }
void				THostSerialChip::PowerOn()								{ fPowered = true; }
Boolean				THostSerialChip::PowerIsOn()							{ return fPowered; }
void				THostSerialChip::SetInterruptEnable(Boolean enable)	{ fIntEnabled = enable; }


void
THostSerialChip::Reset()
{
	fRxHead = fRxCount = 0;
	fTxCount = 0;
	fTxIntPending = false;
}


void				THostSerialChip::SetBreak(Boolean)						{ }
InterfaceSpeed		THostSerialChip::SetSpeed(BitRate bitsPerSec)			{ fSpeed = bitsPerSec; return bitsPerSec; }
void				THostSerialChip::SetIOParms(TCMOSerialIOParms*)		{ }
void				THostSerialChip::Reconfigure()							{ }
NewtonErr			THostSerialChip::Init(TCardSocket*, TCardHandler*, UByte*)	{ return -1; }
void				THostSerialChip::CardRemoved()							{ }


SerialFeatures
THostSerialChip::GetFeatures()
{
	return kSerFeatureDefaults | kSerFeatureVersion2 | kSerFeatureAllSent;
}


NewtonErr			THostSerialChip::InitByOption(TOption*)				{ return noErr; }


// 'sers asked: what the chip is (the external port, not in use unless a
// tool has it).  Anything else is taken.
NewtonErr
THostSerialChip::ProcessOption(TOption* opt)
{
	if (opt->Label() == kCMOSerialChipSpec)
	{
		TCMOSerialChipSpec* spec = (TCMOSerialChipSpec*) opt;
		spec->fHWLoc = kHWLocExternalSerial;
		spec->fSerFeatures = GetFeatures();
		spec->fSerOutSupported = 0xff;
		spec->fSerInSupported = 0xff;
		spec->fParitySupport = 0xff;
		spec->fDataStopBitSupport = 0xff;
		spec->fUARTType = 0;
		spec->fChipNotInUse = (fTool == nil);
	}
	return noErr;
}


NewtonErr			THostSerialChip::SetSerialMode(SerialMode mode)		{ return (mode & kSerModeMask) == kSerModeAsync ? noErr : -1; }
void				THostSerialChip::SysEventNotify(ULong)					{ }
void				THostSerialChip::SetTxDTransceiverEnable(Boolean)		{ }


RxErrorStatus
THostSerialChip::GetByteAndStatus(UByte* nextCharPtr)
{
	*nextCharPtr = GetByte();
	return 0;
}


NewtonErr			THostSerialChip::SetIntSourceEnable(SerialIntSource, Boolean)	{ return noErr; }
Boolean				THostSerialChip::AllSent()								{ return fTxCount == 0; }
void				THostSerialChip::ConfigureForOutput(Boolean)			{ }
NewtonErr			THostSerialChip::InitTxDMA(TCircleBuf*, SCCIntHandler)	{ return -1; }
NewtonErr			THostSerialChip::InitRxDMA(TCircleBuf*, ULong, RxDMAIntHandler)	{ return -1; }
NewtonErr			THostSerialChip::TxDMAControl(DMAControl)				{ return -1; }
NewtonErr			THostSerialChip::RxDMAControl(DMAControl)				{ return -1; }
void				THostSerialChip::SetSDLCAddress(UByte)					{ }
void				THostSerialChip::ReEnableReceiver(Boolean)				{ }
Boolean				THostSerialChip::LinkIsFree(Boolean)					{ return true; }
Boolean				THostSerialChip::SendControlPacket(UByte, UByte, Boolean)	{ return false; }
void				THostSerialChip::WaitForPacket(ULong)					{ }
NewtonErr			THostSerialChip::WaitForAllSent()						{ Poll(); return noErr; }


/*------------------------------------------------------------------------------
	The wire
------------------------------------------------------------------------------*/

NewtonErr
THostSerialChip::Listen(unsigned short port)
{
	if (HostSocketsInit() != kHostSocketOK)
		return -1;
	uint16_t p = port;
	if (HostTCPListen(0x7F000001, &p, &fListener) != kHostSocketOK)
		return -1;
	fPort = p;
	return noErr;
}


// An interrupt is looked for every few milliseconds while a tool has the
// chip with its interrupts on, and at once when the transmitter has emptied.
Boolean
THostSerialChip::Due(Int64* when)
{
	if (fTool == nil || !fIntEnabled)
		return false;
	if (fTxIntPending || fRxCount > 0 || fStatusChanged)
		GetClock(when);
	else
		*when = fNextPoll;
	return true;
}


// The wire looked at: a desktop connecting or gone, what it sent read,
// what the tool put sent; then the tool's interrupts - status, receive,
// transmit empty (for as long as it keeps putting bytes and there is room).
void
THostSerialChip::Poll(void)
{
	if (fClient < 0 && fListener >= 0)
	{
		int client;
		uint32_t address;
		uint16_t port;
		if (HostTCPAccept(fListener, &client, &address, &port) == kHostSocketOK)
		{
			fClient = client;
			fStatusChanged = true;
		}
	}
	if (fClient >= 0)
	{
		while (fRxCount < kRxSize)
		{
			long tail = (fRxHead + fRxCount) % kRxSize;
			size_t room = (tail >= fRxHead) ? kRxSize - tail : fRxHead - tail;
			size_t got = 0;
			int result = HostSocketReceive(fClient, fRx + tail, room, &got);
			if (result == kHostSocketClosed || result < 0)
			{
				HostSocketClose(fClient);
				fClient = -1;
				fStatusChanged = true;
				break;
			}
			if (got == 0)
				break;
			fRxCount += got;
		}
	}
	if (fTxCount > 0)
	{
		if (fClient >= 0)
		{
			size_t sent = 0;
			HostSocketSend(fClient, fTx, fTxCount, &sent);
			memmove(fTx, fTx + sent, fTxCount - sent);
			fTxCount -= sent;
		}
		else
			fTxCount = 0;				// (no cable: the bytes go nowhere)
	}
}


static Boolean
HostSerialDeadline(Int64* when)
{
	return gHostSerialChip != nil && gHostSerialChip->Due(when);
}


static void
HostSerialDeliver(void)
{
	THostSerialChip* chip = gHostSerialChip;
	if (chip == nil || chip->fTool == nil || !chip->fIntEnabled)
		return;
	chip->Poll();
	if (chip->fStatusChanged && chip->fHandlers.ExtStsIntHandler != nil)
	{
		chip->fStatusChanged = false;
		chip->fHandlers.ExtStsIntHandler(chip->fTool);
	}
	if (chip->fRxCount > 0 && chip->fHandlers.RxCAvailIntHandler != nil)
		chip->fHandlers.RxCAvailIntHandler(chip->fTool);
	for (long n = 0; chip->fTool != nil && chip->fTxIntPending && chip->fTxCount < kTxSize && n < kTxSize; n++)
	{
		long before = chip->fTxCount;
		if (chip->fHandlers.TxBEmptyIntHandler != nil)
			chip->fHandlers.TxBEmptyIntHandler(chip->fTool);
		if (chip->fTxCount == before)
			break;
	}
	chip->Poll();
	GetClock(&chip->fNextPoll);
	Int64 step = { 0, (ULong) (5 * kMilliseconds) };
	CompAdd(&step, &chip->fNextPoll);
}


NewtonErr
HostSerialChipInstall(unsigned short port)
{
	NewtonErr err = InitSerialChipRegistry();
	if (err != noErr)
		return err;
	if (gHostSerialChip != nil)
		return noErr;
	THostSerialChip::ClassInfo()->Register();
	THostSerialChip* chip = (THostSerialChip*) TSerialChip::New((char*) "THostSerialChip");
	if (chip == nil)
		return -10007;
	err = chip->Listen(port);
	if (err == noErr)
		err = GetSerialChipRegistry()->Register(chip, kHWLocExternalSerial);
	if (err != noErr)
	{
		chip->Delete();
		return err;
	}
	gHostSerialChip = chip;
	HostRegisterInterruptSource(HostSerialDeadline, HostSerialDeliver);
	return noErr;
}


unsigned short
HostSerialChipPort(void)
{
	return gHostSerialChip != nil ? gHostSerialChip->fPort : 0;
}
