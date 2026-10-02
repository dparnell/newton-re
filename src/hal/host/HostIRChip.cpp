/*
	File:		hal/host/HostIRChip.cpp

	Contains:	THostIRChip (HostIRChip.h): the built-in IR port over a TCP
				connection to another host Newton, or over a multicast
				group every newton on the network hears (the LAN medium).

	Host code (no ROM counterpart); the protocol's methods are the ROM's
	(TSerialChipVoyager's IR channel, 0x001d6780, is the model for what the
	IR tools ask of it: 'irlk', ConfigureForOutput).
*/

#include "HALSerialChip.h"
#include "HostIRChip.h"
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
#include <stdlib.h>

#define kRxSize		4096
#define kTxSize		4096
#define kWireSize	(2 * kTxSize)
#define kMaxIRChips	4

// the modulation a byte crosses the socket with
enum { kWireASK = 0, kWireIrDA = 1 };

// The LAN medium (HostIRChip.h): a datagram is a burst - what the port sent
// since the last poll - as
//	 'N' 'w' 'I' 'R'	the magic
//	 version			1
//	 0					(reserved)
//	 instance (4)		which newton sent it (its own are dropped)
//	 sequence (4)		numbered per instance, so that a datagram heard
//						twice (sent out of several interfaces) or late is
//						dropped - light is not reordered
//	 to (4)				the newton it is pointed at, or 0 for whoever
//						hears it (below)
//	 (modulation, byte)...
// the numbers big-endian.
//
// The network has no geometry, so who faces whom is decided as a user
// pointing one MessagePad at another decides it: a newton that has heard
// another (its receiver on and taking what was sent) faces that one -
// its partner - and from then on hears only its partner and sends only
// to it, until kLanPartnerTime passes without a word from it.  So a beam
// is answered by whichever receiver answers first, and the others, no
// longer faced, hear no more of it and give up as a MessagePad off to
// the side would; IrDA's own addressing would see to that anyway, but
// Sharp IR has none - without this every listening receiver would take
// the beam.
#define kLanGroup		0xEFFF4E77		// 239.255.78.119: 'N' 'w', administratively scoped
#define kLanPort		3681
#define kLanHeader		18
#define kLanPartnerTime	(5 * kSeconds)
#define kLanMaxPairs	700				// a datagram of 1414 bytes: unfragmented on any Ethernet
#define kLanInterfaces	32
#define kLanSenders		16


PROTOCOL THostIRChip : public TSerialChip
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostIRChip);

	THostIRChip*		New();
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
	NewtonErr			Open(const char* peer);
	Boolean				Due(Int64* when);
	void				Poll(void);
	void				Pace(void);
	void				Deliver(void);
	Boolean				Hears(UByte modulation);
	Boolean				Heard(UByte modulation, UByte b);
	NewtonErr			OpenLan(const char* spec);
	void				PollLan(void);
	Boolean				LanFresh(uint32_t instance, uint32_t sequence);

	unsigned short		fPort;
	int					fListener;			// listening for the peer, or -1
	int					fPeer;				// the peer's connection, or -1
	uint32_t			fPeerAddress;		// connecting: where the peer is (0: listening)
	unsigned short		fPeerPort;
	Boolean				fConnecting;		// fPeer is a connection not yet made
	void*				fTool;				// the tool that has the chip, and its handlers
	SCCChannelInts		fHandlers;
	Boolean				fIntEnabled;
	Boolean				fPowered;
	Boolean				fReceiving;			// the receiver on (ConfigureForOutput(false))
	Boolean				fTxIntPending;		// the transmitter has emptied: tell the tool
	UByte				fLinkMode;			// 'irlk': kSerIRLink_...
	UByte				fLinkConfig;		// 'irlk': kSerIRLinkCfg_...
	UByte				fLinkStatus;		// 'irlk': kSerIRLinkSts_IRDADetect
	SerialOutputControl	fOutputs;
	BitRate				fSpeed;
	UByte				fRx[kRxSize];		// what was heard, not yet read
	long				fRxHead, fRxCount;
	long				fRxReady;			// how many of them have arrived at the port's speed
	uint64_t			fLastPace;
	UByte				fWire[kWireSize];	// what the tool put, as (modulation, byte) pairs not yet sent
	long				fWireCount;
	Boolean				fHalfRecord;		// the peer's last read ended between a modulation and its byte
	UByte				fHalfModulation;
	Int64				fNextPoll;
	// the LAN medium
	int					fLan;				// the group's socket, or -1
	uint32_t			fLanInterfaces[kLanInterfaces];	// sent out of each (none: the default)
	int					fLanInterfaceCount;
	uint32_t			fInstance;			// this newton's, in each datagram
	uint32_t			fSequence;
	struct { uint32_t instance, sequence; long used; }	fSenders[kLanSenders];	// the last heard from each
	long				fSendersUsed;
	unsigned			fLanLoss;			// NEWTON_IR_LAN_LOSS: the per cent of datagrams lost
	uint32_t			fLossSeed;
	uint32_t			fPartner;			// the newton faced, or 0
	Int64				fPartnerHeard;		// when it was last heard
};

PROTOCOL_IMPL_SOURCE_MACRO(THostIRChip)
PROTOCOL_CLASSINFO(THostIRChip, "TSerialChip", "v2.0", 0x30000, 0, nil)

static THostIRChip*	gIRChips[kMaxIRChips];
static THostIRChip*	gInstalledIRChip = nil;
static int			gTraceIR = -1;			// NEWTON_TRACE_IR: what each chip sends, hears and loses


static bool
TraceIR(void)
{
	if (gTraceIR < 0)
		gTraceIR = getenv("NEWTON_TRACE_IR") != nil;
	return gTraceIR != 0;
}


static int
ChipIndex(THostIRChip* chip)
{
	for (int i = 0; i < kMaxIRChips; i++)
		if (gIRChips[i] == chip)
			return i;
	return -1;
}


THostIRChip*
THostIRChip::New()
{
	fPort = 0;
	fListener = -1;
	fPeer = -1;
	fPeerAddress = 0;
	fPeerPort = 0;
	fConnecting = false;
	fTool = nil;
	memset(&fHandlers, 0, sizeof(fHandlers));
	fIntEnabled = false;
	fPowered = true;
	fReceiving = true;
	fTxIntPending = false;
	fLinkMode = kSerIRLink_SharpIR;
	fLinkConfig = kSerIRLinkCfg_Default;
	fLinkStatus = 0;
	fOutputs = 0;
	fSpeed = 9600;
	fRxHead = fRxCount = 0;
	fRxReady = 0;
	fLastPace = 0;
	fWireCount = 0;
	fHalfRecord = false;
	fHalfModulation = 0;
	fNextPoll.hi = fNextPoll.lo = 0;
	fLan = -1;
	fLanInterfaceCount = 0;
	fInstance = 0;
	fSequence = 0;
	memset(fSenders, 0, sizeof(fSenders));
	fSendersUsed = 0;
	fLanLoss = 0;
	fLossSeed = 0;
	fPartner = 0;
	fPartnerHeard.hi = fPartnerHeard.lo = 0;
	return this;
}


void
THostIRChip::Delete()
{
	if (fPeer >= 0)
		HostSocketClose(fPeer);
	if (fListener >= 0)
		HostSocketClose(fListener);
	if (fLan >= 0)
		HostSocketClose(fLan);
	fPeer = fListener = fLan = -1;
	for (int i = 0; i < kMaxIRChips; i++)
		if (gIRChips[i] == this)
			gIRChips[i] = nil;
}


NewtonErr
THostIRChip::InstallChipHandler(void* serialTool, SCCChannelInts* intHandlers)
{
	if (fTool != nil)
		return -1;
	fHandlers = *intHandlers;
	fTool = serialTool;
	return noErr;
}


NewtonErr
THostIRChip::RemoveChipHandler(void* serialTool)
{
	if (serialTool != fTool)
		return -1;
	fTool = nil;
	fIntEnabled = false;
	memset(&fHandlers, 0, sizeof(fHandlers));
	return noErr;
}


// A byte sent, with the modulation the port is in.
void
THostIRChip::PutByte(UByte nextChar)
{
	if (fWireCount + 2 <= kWireSize)
	{
		fWire[fWireCount++] = (fLinkMode == kSerIRLink_SharpIR) ? kWireASK : kWireIrDA;
		fWire[fWireCount++] = nextChar;
	}
	fTxIntPending = true;
}


void
THostIRChip::ResetTxBEmpty()
{
	fTxIntPending = false;
}


UByte
THostIRChip::GetByte()
{
	if (fRxReady == 0)
		return 0;
	UByte b = fRx[fRxHead];
	fRxHead = (fRxHead + 1) % kRxSize;
	fRxCount--;
	fRxReady--;
	return b;
}


Boolean			THostIRChip::TxBufEmpty()		{ return fWireCount + 2 <= kWireSize; }
Boolean			THostIRChip::RxBufFull()		{ return fRxReady > 0; }
RxErrorStatus	THostIRChip::GetRxErrorStatus()	{ return 0; }


// (an IR port has no modem lines)
SerialStatus
THostIRChip::GetSerialStatus()
{
	SerialStatus status = 0;
	if (fRxReady > 0)
		status |= kSerialRxCharAvailable;
	if (fWireCount + 2 <= kWireSize)
		status |= kSerialTxBufferEmpty;
	return status;
}


void				THostIRChip::ResetSerialStatus()					{ }
void				THostIRChip::SetSerialOutputs(SerialOutputControl c)	{ fOutputs |= c; }
void				THostIRChip::ClearSerialOutputs(SerialOutputControl c)	{ fOutputs &= ~c; }
SerialOutputControl	THostIRChip::GetSerialOutputs()					{ return fOutputs; }
void				THostIRChip::PowerOff()								{ fPowered = false; fIntEnabled = false; }
void				THostIRChip::PowerOn()								{ fPowered = true; fIntEnabled = true; }
Boolean				THostIRChip::PowerIsOn()							{ return fPowered; }
void				THostIRChip::SetInterruptEnable(Boolean enable)		{ fIntEnabled = enable; }


void
THostIRChip::Reset()
{
	fRxHead = fRxCount = 0;
	fRxReady = 0;
	fWireCount = 0;
	fTxIntPending = false;
}


void				THostIRChip::SetBreak(Boolean)						{ }
InterfaceSpeed		THostIRChip::SetSpeed(BitRate bitsPerSec)			{ fSpeed = bitsPerSec; return bitsPerSec; }
void				THostIRChip::SetIOParms(TCMOSerialIOParms*)			{ }
void				THostIRChip::Reconfigure()							{ }
NewtonErr			THostIRChip::Init(TCardSocket*, TCardHandler*, UByte*)	{ return -1; }
void				THostIRChip::CardRemoved()							{ }


// The Voyager's IR channel's (0x1001e3) bar the TV remote: output wants
// configuring (half duplex - ConfigureForOutput is only called when this
// says so), all-sent, the error byte with each.
SerialFeatures
THostIRChip::GetFeatures()
{
	return kSerFeatureDefaults | kSerFeatureVersion2 | kSerFeatureAllSent | kSerFeatureTxConfigNeeded
		 | kSerFeatureGetErrByte | kSerFeatureWaitForAllSent;
}


NewtonErr			THostIRChip::InitByOption(TOption*)					{ return noErr; }


// 'sers: what the chip is (the built-in IR, no modem lines - as the
// Voyager's IR channel answers).  'irlk': the IR mode set (opSetRequired,
// as the Voyager takes it), and the status answered either way.  Anything
// else is not this chip's.
NewtonErr
THostIRChip::ProcessOption(TOption* opt)
{
	ULong opCode = opt->GetOpCode();
	if (opt->Label() == kCMOSerialChipSpec)
	{
		TCMOSerialChipSpec* spec = (TCMOSerialChipSpec*) opt;
		spec->fHWLoc = kHWLocBuiltInIR;
		spec->fSerFeatures = GetFeatures();
		spec->fSerOutSupported = 0;
		spec->fSerInSupported = 0;
		spec->fParitySupport = 0x0c;
		spec->fDataStopBitSupport = 0x7f;
		spec->fUARTType = 0x23;
		spec->fChipNotInUse = (fTool == nil);
		return noErr;
	}
	if (opt->Label() == kHMOSerIRLinkConfig)
	{
		THMOSerIRLinkConfig* link = (THMOSerIRLinkConfig*) opt;
		if (link->fIRLinkMode > kSerIRLink_LastMode)
			return -1;
		link->fStatus = fLinkStatus;
		if (opCode == opSetRequired)
		{
			fLinkMode = link->fIRLinkMode;
			fLinkConfig = link->fConfigFlags;
		}
		return noErr;
	}
	return -4;					// (kOptionUnsupported: the Voyager's 0xfffffffc)
}


NewtonErr			THostIRChip::SetSerialMode(SerialMode mode)			{ return (mode & kSerModeMask) == kSerModeAsync ? noErr : -1; }
void				THostIRChip::SysEventNotify(ULong)					{ }
void				THostIRChip::SetTxDTransceiverEnable(Boolean)		{ }


RxErrorStatus
THostIRChip::GetByteAndStatus(UByte* nextCharPtr)
{
	*nextCharPtr = GetByte();
	return 0;
}


NewtonErr			THostIRChip::SetIntSourceEnable(SerialIntSource, Boolean)	{ return noErr; }
Boolean				THostIRChip::AllSent()								{ return fWireCount == 0; }


// Half duplex, as the Voyager's (0x001d7abc): what was sent sent first,
// then the transmitter on and the receiver off, or the other way round.
void
THostIRChip::ConfigureForOutput(Boolean start)
{
	WaitForAllSent();
	fReceiving = !start;
}


NewtonErr			THostIRChip::InitTxDMA(TCircleBuf*, SCCIntHandler)	{ return -1; }
NewtonErr			THostIRChip::InitRxDMA(TCircleBuf*, ULong, RxDMAIntHandler)	{ return -1; }
NewtonErr			THostIRChip::TxDMAControl(DMAControl)				{ return -1; }
NewtonErr			THostIRChip::RxDMAControl(DMAControl)				{ return -1; }
void				THostIRChip::SetSDLCAddress(UByte)					{ }
void				THostIRChip::ReEnableReceiver(Boolean)				{ fReceiving = true; }
Boolean				THostIRChip::LinkIsFree(Boolean)					{ return true; }
Boolean				THostIRChip::SendControlPacket(UByte, UByte, Boolean)	{ return false; }
void				THostIRChip::WaitForPacket(ULong)					{ }
NewtonErr			THostIRChip::WaitForAllSent()						{ Poll(); return noErr; }


/*------------------------------------------------------------------------------
	The medium
------------------------------------------------------------------------------*/

NewtonErr
THostIRChip::Open(const char* peer)
{
	if (peer == nil)
		return noErr;					// (no medium: nobody is ever in front of the port)
	if (HostSocketsInit() != kHostSocketOK)
		return -1;
	if (strncmp(peer, "lan", 3) == 0 && (peer[3] == 0 || peer[3] == ':' || peer[3] == '@'))
		return OpenLan(peer + 3);
	const char* colon = strrchr(peer, ':');
	if (colon == nil)
		return -1;
	long port = strtol(colon + 1, nil, 10);
	if (port < 0 || port > 65535)
		return -1;
	if ((size_t) (colon - peer) == 6 && strncmp(peer, "listen", 6) == 0)
	{
		uint16_t p = (uint16_t) port;
		if (HostTCPListen(0x7F000001, &p, &fListener) != kHostSocketOK)
			return -1;
		fPort = p;
		return noErr;
	}
	char name[256];
	size_t n = colon - peer;
	if (n == 0 || n >= sizeof(name))
		return -1;
	memcpy(name, peer, n);
	name[n] = 0;
	uint32_t address;
	int count = 0;
	if (HostResolveName(name, &address, 1, &count) != kHostSocketOK || count == 0)
		return -1;
	fPeerAddress = address;
	fPeerPort = (unsigned short) port;
	fPort = fPeerPort;
	return noErr;
}


// A byte in the air heard?  Only by a claimed, powered receiver that is
// on, and only in the modulation it listens for (either, in auto-receive).
Boolean
THostIRChip::Hears(UByte modulation)
{
	if (fTool == nil || !fPowered || !fReceiving)
		return false;
	if (fLinkConfig & kSerIRLinkCfg_AutoRx)
		return true;
	return modulation == ((fLinkMode == kSerIRLink_SharpIR) ? kWireASK : kWireIrDA);
}


// A byte in the air, sent the way modulation says: heard or lost.
Boolean
THostIRChip::Heard(UByte modulation, UByte b)
{
	if (!Hears(modulation))
	{
		if (TraceIR())
			printf("[ir %d] lost %02x (%s%s%s%s)\n", ChipIndex(this), b, fTool == nil ? "unclaimed " : "",
				fPowered ? "" : "off ", fReceiving ? "" : "transmitting ", modulation == kWireIrDA ? "IrDA" : "ASK");
		return false;
	}
	if (TraceIR())
		printf("[ir %d] heard %02x\n", ChipIndex(this), b);
	if (fLinkConfig & kSerIRLinkCfg_AutoRx)
		fLinkStatus = (modulation == kWireIrDA) ? kSerIRLinkSts_IRDADetect : 0;
	if (fRxCount < kRxSize)
	{
		fRx[(fRxHead + fRxCount) % kRxSize] = b;
		fRxCount++;
	}
	return true;
}


// The interrupt is looked for every few milliseconds while there is a
// peer (or one to find) - bytes that arrive while nobody listens are lost
// then, not kept - and at once when the tool has work (with no medium at
// all only then: what it sends goes nowhere, and nothing comes).
Boolean
THostIRChip::Due(Int64* when)
{
	Boolean work = fTool != nil && fIntEnabled && (fTxIntPending || fRxReady > 0);
	if (fPeer < 0 && fListener < 0 && fPeerAddress == 0 && fLan < 0 && !work)
		return false;
	if (work)
		GetClock(when);
	else
		*when = fNextPoll;
	return true;
}


// The medium looked at: the peer found (accepted, or connected to - again
// until it answers), what it sent heard or lost, what the tool put sent.
void
THostIRChip::Poll(void)
{
	if (fLan >= 0)
	{
		PollLan();
		return;
	}
	if (fPeer < 0 && fListener >= 0)
	{
		int peer;
		uint32_t address;
		uint16_t port;
		if (HostTCPAccept(fListener, &peer, &address, &port) == kHostSocketOK)
			fPeer = peer;
	}
	else if (fPeer < 0 && fPeerAddress != 0)
	{
		if (HostTCPConnect(fPeerAddress, fPeerPort, &fPeer) == kHostSocketOK)
			fConnecting = true;
		else
			fPeer = -1;
	}
	if (fPeer >= 0 && fConnecting)
	{
		int result = HostSocketConnected(fPeer);
		if (result == kHostSocketWouldBlock)
			return;
		if (result != kHostSocketOK)
		{
			HostSocketClose(fPeer);		// (not there yet: again next time)
			fPeer = -1;
			fConnecting = false;
			return;
		}
		fConnecting = false;
	}
	if (fPeer < 0)
	{
		fWireCount = 0;					// (nobody in front of the port: the light goes nowhere)
		return;
	}
	UByte records[512];
	for (;;)
	{
		size_t got = 0;
		int result = HostSocketReceive(fPeer, records, sizeof(records), &got);
		if (result == kHostSocketClosed || result < 0)
		{
			HostSocketClose(fPeer);
			fPeer = -1;
			fHalfRecord = false;
			return;
		}
		if (got == 0)
			break;
		for (size_t i = 0; i < got; i++)
		{
			if (!fHalfRecord)
			{
				fHalfModulation = records[i];
				fHalfRecord = true;
				continue;
			}
			fHalfRecord = false;
			Heard(fHalfModulation, records[i]);
		}
	}
	if (fWireCount > 0)
	{
		size_t sent = 0;
		HostSocketSend(fPeer, fWire, fWireCount, &sent);
		if (TraceIR())
		{
			printf("[ir %d] sent", ChipIndex(this));
			for (size_t i = 1; i < sent; i += 2)
				printf(" %02x", fWire[i]);
			printf("\n");
		}
		memmove(fWire, fWire + sent, fWireCount - sent);
		fWireCount -= sent;
	}
}


/*------------------------------------------------------------------------------
	The LAN medium
------------------------------------------------------------------------------*/

static void
PutWord(UByte* p, uint32_t w)
{
	p[0] = (UByte) (w >> 24);
	p[1] = (UByte) (w >> 16);
	p[2] = (UByte) (w >> 8);
	p[3] = (UByte) w;
}


static uint32_t
GetWord(const UByte* p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}


static bool
ParseAddress(const char* text, uint32_t* address)
{
	unsigned a, b, c, d;
	char extra;
	if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
		return false;
	*address = (a << 24) | (b << 16) | (c << 8) | d;
	return true;
}


// ":PORT", ":PORT@ADDRESS", "@ADDRESS" or nothing: the group's port
// (kLanPort when none is given) and the one interface to keep to (every
// interface that can multicast when none is).
NewtonErr
THostIRChip::OpenLan(const char* spec)
{
	long port = kLanPort;
	if (*spec == ':')
		spec++;
	if (*spec != 0 && *spec != '@')
	{
		char* end;
		port = strtol(spec, &end, 10);
		if (port <= 0 || port > 65535 || (*end != 0 && *end != '@'))
			return -1;
		spec = end;
	}
	fLanInterfaceCount = 0;
	if (*spec == '@')
	{
		if (!ParseAddress(spec + 1, &fLanInterfaces[0]))
			return -1;
		fLanInterfaceCount = 1;
	}
	else
	{
		int count = 0;
		if (HostInterfaceAddresses(fLanInterfaces, kLanInterfaces, &count) == kHostSocketOK)
			fLanInterfaceCount = count;
	}
	int joined = 0;
	if (HostUDPMulticastOpen(kLanGroup, (uint16_t) port, fLanInterfaces, fLanInterfaceCount, &fLan, &joined) != kHostSocketOK)
	{
		fLan = -1;
		return -1;
	}
	fPort = (unsigned short) port;
	// which newton this is: the clock's fine bits and an address, which
	// differ between two started together
	Int64 clock;
	GetClock(&clock);
	uintptr_t here = (uintptr_t) this;
	fInstance = ((uint32_t) clock.lo * 2654435761u) ^ (uint32_t) (here >> 4) ^ (uint32_t) ((uint64_t) here >> 32) ^ (uint32_t) clock.hi;
	if (fInstance == 0)
		fInstance = 1;
	fSequence = 0;
	const char* loss = getenv("NEWTON_IR_LAN_LOSS");
	fLanLoss = loss != nil ? (unsigned) strtoul(loss, nil, 10) : 0;
	fLossSeed = fInstance;
	if (TraceIR())
	{
		printf("[ir %d] LAN medium: group 239.255.78.119:%ld, instance %08x, %d interface(s), joined on %d:", ChipIndex(this), port,
			fInstance, fLanInterfaceCount, joined);
		for (int i = 0; i < fLanInterfaceCount; i++)
			printf(" %u.%u.%u.%u", fLanInterfaces[i] >> 24, (fLanInterfaces[i] >> 16) & 0xff, (fLanInterfaces[i] >> 8) & 0xff, fLanInterfaces[i] & 0xff);
		printf("\n");
	}
	return noErr;
}


// A datagram not heard before from its sender?  (One sent out of several
// interfaces arrives once for each, and one overtaken by a later one is
// lost rather than heard out of order, as light would be.)
Boolean
THostIRChip::LanFresh(uint32_t instance, uint32_t sequence)
{
	fSendersUsed++;
	int oldest = 0;
	for (int i = 0; i < kLanSenders; i++)
	{
		if (fSenders[i].instance == instance)
		{
			if ((int32_t) (sequence - fSenders[i].sequence) <= 0)
				return false;
			fSenders[i].sequence = sequence;
			fSenders[i].used = fSendersUsed;
			return true;
		}
		if (fSenders[i].used < fSenders[oldest].used)
			oldest = i;
	}
	fSenders[oldest].instance = instance;
	fSenders[oldest].sequence = sequence;
	fSenders[oldest].used = fSendersUsed;
	return true;
}


// The group looked at: every other newton's datagrams heard (or lost, as
// Heard says), then what the tool put sent - out of each interface - as
// datagrams of at most kLanMaxPairs bytes.
void
THostIRChip::PollLan(void)
{
	UByte datagram[kLanHeader + 2 * kLanMaxPairs + 64];
	Int64 now;
	GetClock(&now);
	if (fPartner != 0)
	{
		Int64 until = fPartnerHeard;
		Int64 wait = { 0, (ULong) kLanPartnerTime };
		CompAdd(&wait, &until);
		if (CompCompare(&now, &until) > 0)
		{
			if (TraceIR())
				printf("[ir %d] no longer faces %08x\n", ChipIndex(this), fPartner);
			fPartner = 0;
		}
	}
	for (int n = 0; n < 256; n++)
	{
		size_t got = 0;
		uint32_t from = 0;
		if (HostUDPReceive(fLan, datagram, sizeof(datagram), &got, &from) != kHostSocketOK || got == 0)
			break;
		if (got < kLanHeader || memcmp(datagram, "NwIR", 4) != 0 || datagram[4] != 1)
			continue;
		uint32_t instance = GetWord(datagram + 6);
		uint32_t sequence = GetWord(datagram + 10);
		uint32_t to = GetWord(datagram + 14);
		if (instance == fInstance || !LanFresh(instance, sequence))
			continue;					// (our own, come back; or heard already)
		if ((to != 0 && to != fInstance) || (fPartner != 0 && instance != fPartner))
			continue;					// (pointed at another, or from one not faced)
		if (fLanLoss != 0)
		{
			fLossSeed = fLossSeed * 1103515245u + 12345u;
			if ((fLossSeed >> 16) % 100 < fLanLoss)
			{
				if (TraceIR())
					printf("[ir %d] lost %u bytes from %08x (NEWTON_IR_LAN_LOSS)\n", ChipIndex(this), (unsigned) (got - kLanHeader) / 2, instance);
				continue;
			}
		}
		Boolean taken = false;
		for (size_t i = kLanHeader; i + 1 < got; i += 2)
			if (Heard(datagram[i], datagram[i + 1]))
				taken = true;
		if (taken)
		{
			if (fPartner != instance && TraceIR())
				printf("[ir %d] faces %08x\n", ChipIndex(this), instance);
			fPartner = instance;
			fPartnerHeard = now;
		}
	}
	for (long sent = 0; sent < fWireCount; )
	{
		long pairs = (fWireCount - sent) / 2;
		if (pairs > kLanMaxPairs)
			pairs = kLanMaxPairs;
		memcpy(datagram, "NwIR", 4);
		datagram[4] = 1;
		datagram[5] = 0;
		PutWord(datagram + 6, fInstance);
		PutWord(datagram + 10, ++fSequence);
		PutWord(datagram + 14, fPartner);
		memcpy(datagram + kLanHeader, fWire + sent, 2 * pairs);
		size_t size = kLanHeader + 2 * pairs;
		if (fLanInterfaceCount == 0)
			HostUDPMulticastSend(fLan, kLanGroup, fPort, 0, datagram, size);
		for (int i = 0; i < fLanInterfaceCount; i++)
			HostUDPMulticastSend(fLan, kLanGroup, fPort, fLanInterfaces[i], datagram, size);
		if (TraceIR())
		{
			printf("[ir %d] sent", ChipIndex(this));
			for (long i = 1; i < 2 * pairs; i += 2)
				printf(" %02x", fWire[sent + i]);
			printf("\n");
		}
		sent += 2 * pairs;
	}
	fWireCount = 0;
}


// What was heard released to the tool no faster than the port's speed.
void
THostIRChip::Pace(void)
{
	Int64 clock;
	GetClock(&clock);
	uint64_t now = ((uint64_t) (ULong) clock.hi << 32) | clock.lo;
	uint64_t perSecond = fSpeed / 10;
	if (perSecond == 0)
		perSecond = 1;
	if (fRxReady >= fRxCount || fLastPace == 0 || now < fLastPace)
	{
		fLastPace = now;
		return;
	}
	uint64_t bytes = (now - fLastPace) * perSecond / kSeconds;
	if (bytes == 0)
		return;
	fLastPace += bytes * kSeconds / perSecond;
	if (bytes > (uint64_t) (fRxCount - fRxReady))
		bytes = fRxCount - fRxReady;
	fRxReady += (long) bytes;
}


void
THostIRChip::Deliver(void)
{
	Poll();
	if (fTool == nil)
		fRxHead = fRxCount = fRxReady = 0;		// (heard by nobody)
	else if (fIntEnabled)
	{
		Pace();
		for (long n = 0; fTool != nil && fRxReady > 0 && fHandlers.RxCAvailIntHandler != nil && n < kRxSize; n++)
			fHandlers.RxCAvailIntHandler(fTool);
		for (long n = 0; fTool != nil && fTxIntPending && fWireCount + 2 <= kWireSize && n < kTxSize; n++)
		{
			long before = fWireCount;
			if (fHandlers.TxBEmptyIntHandler != nil)
				fHandlers.TxBEmptyIntHandler(fTool);
			if (fWireCount == before)
				break;
		}
		Poll();
	}
	GetClock(&fNextPoll);
	Int64 step = { 0, (ULong) (5 * kMilliseconds) };
	CompAdd(&step, &fNextPoll);
}


static Boolean
HostIRDeadline(Int64* when)
{
	Boolean due = false;
	for (int i = 0; i < kMaxIRChips; i++)
	{
		Int64 t;
		if (gIRChips[i] != nil && gIRChips[i]->Due(&t))
		{
			if (!due || CompCompare(&t, when) < 0)
				*when = t;
			due = true;
		}
	}
	return due;
}


static void
HostIRDeliver(void)
{
	Int64 now;
	GetClock(&now);
	for (int i = 0; i < kMaxIRChips; i++)
	{
		Int64 t;
		if (gIRChips[i] != nil && gIRChips[i]->Due(&t) && CompCompare(&t, &now) <= 0)
			gIRChips[i]->Deliver();
	}
}


NewtonErr
HostIRChipMake(const char* peer, TSerialChip** chipPtr)
{
	*chipPtr = nil;
	int slot = -1;
	for (int i = 0; i < kMaxIRChips; i++)
		if (gIRChips[i] == nil)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return -1;
	THostIRChip::ClassInfo()->Register();
	THostIRChip* chip = (THostIRChip*) TSerialChip::New((char*) "THostIRChip");
	if (chip == nil)
		return -10007;
	NewtonErr err = chip->Open(peer);
	if (err != noErr)
	{
		chip->Delete();
		return err;
	}
	gIRChips[slot] = chip;
	HostRegisterInterruptSource(HostIRDeadline, HostIRDeliver);
	*chipPtr = chip;
	return noErr;
}


unsigned short
HostIRChipPort(TSerialChip* chip)
{
	return chip != nil ? ((THostIRChip*) chip)->fPort : 0;
}


Boolean
HostIRChipConnected(TSerialChip* chip)
{
	if (chip != nil && ((THostIRChip*) chip)->fLan >= 0)
		return true;			// (the group is always there)
	return chip != nil && ((THostIRChip*) chip)->fPeer >= 0 && !((THostIRChip*) chip)->fConnecting;
}


NewtonErr
HostIRChipInstall(const char* peer)
{
	NewtonErr err = InitSerialChipRegistry();
	if (err != noErr)
		return err;
	if (gInstalledIRChip != nil)
		return noErr;
	TSerialChip* chip;
	err = HostIRChipMake(peer, &chip);
	if (err == noErr)
		err = GetSerialChipRegistry()->Register(chip, kHWLocBuiltInIR);
	if (err != noErr)
	{
		if (chip != nil)
			chip->Delete();
		return err;
	}
	gInstalledIRChip = (THostIRChip*) chip;
	return noErr;
}


TSerialChip*
HostIRChipInstalled(void)
{
	return gInstalledIRChip;
}
