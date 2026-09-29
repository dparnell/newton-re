/*
	File:		comms/host/HostTCPTool.cpp

	Contains:	THostTCPTool (HostTCPTool.h).

	Host code for the NIE's TCP tool (no ROM counterpart).
*/

#include "HostTCPTool.h"
#include "BufferList.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "CommErrors.h"
#include "HostSockets.h"


// a big-endian halfword and word out of an option's data
static uint16_t
DataShort(const UByte* p)
{
	return (p[0] << 8) | p[1];
}

static uint32_t
DataLong(const UByte* p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static NewtonErr
SocketError(int result)
{
	switch (result)
	{
	case kHostSocketRefused:
	case kHostSocketUnreachable:
		return kCommErrIncompatibleRemote;
	case kHostSocketClosed:
		return kCommErrConnectionAborted;
	default:
		return kCommErrNotConnected;
	}
}


THostTCPTool::THostTCPTool(ULong serviceId)
	: TCommTool(serviceId)
{
	fSocket = -1;
	fListener = -1;
	fRemoteAddress = 0;
	fRemotePort = 0;
	fLocalPort = 0;
	fTransport = kInetTransportTCP;
	fConnecting = false;
	fListening = false;
	fGetBuffer = nil;
	fGetCount = 0;
	fGetThreshold = -1;
	fPutBytes = nil;
	fPutSize = 0;
	fPutDone = 0;
}


THostTCPTool::~THostTCPTool()
{
	// (the task's copy; the parent's never opened anything)
	if (fSocket >= 0)
		HostSocketClose(fSocket);
	if (fListener >= 0)
		HostSocketClose(fListener);
}


ULong
THostTCPTool::GetSizeOf()
{
	return sizeof(THostTCPTool);
}


UChar*
THostTCPTool::GetToolName()
{
	return (UChar*) "Host TCP";
}


// Opened: the socket is looked at every kHostTCPPollInterval from now on.
NewtonErr
THostTCPTool::OpenStart(TOptionArray* options)
{
	if (HostSocketsInit() != kHostSocketOK)
		return kCommErrResourceNotAvailable;
	fTimerInterval = kHostTCPPollInterval;
	return noErr;
}


ULong
THostTCPTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	UByte* data = (UByte*) (theOption + 1);
	Boolean set = (opcode == opSetNegotiate || opcode == opSetRequired);
	switch (label)
	{
	case kInetRemoteSocketOption:
		if (set)
		{
			if (theOption->Length() < 6)
				return opFailure;
			fRemoteAddress = DataLong(data);
			fRemotePort = DataShort(data + 4);
		}
		else if (theOption->Length() >= 6)
		{
			data[0] = fRemoteAddress >> 24;
			data[1] = fRemoteAddress >> 16;
			data[2] = fRemoteAddress >> 8;
			data[3] = fRemoteAddress;
			data[4] = fRemotePort >> 8;
			data[5] = fRemotePort;
		}
		return opSuccess;

	case kInetLocalPortOption:
		if (set)
		{
			if (theOption->Length() < 2)
				return opFailure;
			fLocalPort = DataShort(data);
		}
		else if (theOption->Length() >= 2)
		{
			data[0] = fLocalPort >> 8;
			data[1] = fLocalPort;
		}
		return opSuccess;

	case kInetTransportServiceOption:
		if (set)
		{
			if (theOption->Length() < 4)
				return opFailure;
			fTransport = DataLong(data);
			return (fTransport == kInetTransportTCP) ? opSuccess : opNotSupported;
		}
		return opSuccess;

	case kInetLinkIdOption:
		return opSuccess;
	}
	return TCommTool::ProcessOptionStart(theOption, label, opcode);
}


// ---------------------------------------------------------------------------
//	Connecting
// ---------------------------------------------------------------------------

void
THostTCPTool::ConnectStart()
{
	int result = HostTCPConnect(fRemoteAddress, fRemotePort, &fSocket);
	if (result != kHostSocketOK)
	{
		fSocket = -1;
		ConnectComplete(SocketError(result));
		return;
	}
	fConnecting = true;
	PollConnect();
}


void
THostTCPTool::PollConnect()
{
	int result = HostSocketConnected(fSocket);
	if (result == kHostSocketWouldBlock)
		return;
	fConnecting = false;
	if (result != kHostSocketOK)
	{
		HostSocketClose(fSocket);
		fSocket = -1;
		ConnectComplete(SocketError(result));
		return;
	}
	fConnectInfo.fErrorFree = true;
	ConnectComplete(noErr);
}


void
THostTCPTool::ListenStart()
{
	uint16_t port = fLocalPort;
	int result = HostTCPListen(0, &port, &fListener);
	if (result != kHostSocketOK)
	{
		fListener = -1;
		ListenComplete(SocketError(result));
		return;
	}
	fLocalPort = port;
	fListening = true;
	PollAccept();
}


// A caller has come: the listen completes, and the accept takes it.
void
THostTCPTool::PollAccept()
{
	uint32_t address;
	uint16_t port;
	int result = HostTCPAccept(fListener, &fSocket, &address, &port);
	if (result == kHostSocketWouldBlock)
		return;
	fListening = false;
	HostSocketClose(fListener);
	fListener = -1;
	if (result != kHostSocketOK)
	{
		fSocket = -1;
		ListenComplete(SocketError(result));
		return;
	}
	fRemoteAddress = address;
	fRemotePort = port;
	ListenComplete(noErr);
}


void
THostTCPTool::AcceptStart()
{
	fConnectInfo.fErrorFree = true;
	AcceptComplete(fSocket >= 0 ? noErr : kCommErrNotConnected);
}


// ---------------------------------------------------------------------------
//	Data
// ---------------------------------------------------------------------------

// The client's data copied out of its buffer (it may be another task's
// shared memory) and sent as the socket takes it.
void
THostTCPTool::PutBytes(CBufferList* clientBuffer)
{
	Size size = clientBuffer->GetSize() - clientBuffer->Position();
	fPutBytes = (UByte*) NewPtr(size > 0 ? size : 1);
	if (fPutBytes == nil)
	{
		PutComplete(kError_No_Memory, 0);
		return;
	}
	fPutSize = clientBuffer->Getn(fPutBytes, size);
	fPutDone = 0;
	PollPut();
}


void
THostTCPTool::PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame)
{
	// a stream has no frames
	PutBytes(clientBuffer);
}


void
THostTCPTool::PollPut()
{
	while (fPutDone < fPutSize)
	{
		size_t sent;
		int result = HostSocketSend(fSocket, fPutBytes + fPutDone, fPutSize - fPutDone, &sent);
		if (result == kHostSocketWouldBlock)
			return;
		if (result != kHostSocketOK)
		{
			Size done = fPutDone;
			DisposePtr((Ptr) fPutBytes);
			fPutBytes = nil;
			PutComplete(SocketError(result), done);
			return;
		}
		fPutDone += sent;
	}
	DisposePtr((Ptr) fPutBytes);
	fPutBytes = nil;
	PutComplete(noErr, fPutDone);
}


void
THostTCPTool::KillPut()
{
	if (fPutBytes != nil)
	{
		DisposePtr((Ptr) fPutBytes);
		fPutBytes = nil;
	}
	KillPutComplete(noErr);
}


// A get completes when the client's buffer is full.
void
THostTCPTool::GetBytes(CBufferList* clientBuffer)
{
	fGetBuffer = clientBuffer;
	fGetCount = 0;
	fGetThreshold = -1;
	PollGet();
}


void
THostTCPTool::GetFramedBytes(CBufferList* clientBuffer)
{
	GetBytes(clientBuffer);
}


// A non-blocking get completes as soon as it has threshold bytes (at least
// one).
void
THostTCPTool::GetBytesImmediate(CBufferList* clientBuffer, Size threshold)
{
	fGetBuffer = clientBuffer;
	fGetCount = 0;
	fGetThreshold = (threshold > 0) ? threshold : 1;
	PollGet();
}


void
THostTCPTool::PollGet()
{
	UByte bytes[1024];
	for ( ; ; )
	{
		Size room = fGetBuffer->GetSize() - fGetBuffer->Position();
		if (room <= 0 || (fGetThreshold > 0 && fGetCount >= fGetThreshold))
			break;
		size_t got;
		int result = HostSocketReceive(fSocket, bytes, room < (Size) sizeof(bytes) ? room : sizeof(bytes), &got);
		if (result == kHostSocketWouldBlock)
			return;
		if (result != kHostSocketOK)
		{
			// the other end has gone: what came is handed over, then the
			// connection is aborted (a disconnect event, TerminateComplete)
			Size count = fGetCount;
			fGetBuffer = nil;
			GetComplete(count > 0 ? noErr : SocketError(result), false, count);
			PeerClosed();
			return;
		}
		fGetCount += fGetBuffer->Putn(bytes, got);
	}
	Size count = fGetCount;
	fGetBuffer = nil;
	GetComplete(noErr, false, count);
}


void
THostTCPTool::KillGet()
{
	fGetBuffer = nil;
	KillGetComplete(noErr);
}


// The other end closed: abort, which posts the disconnect event.
void
THostTCPTool::PeerClosed()
{
	StartAbort(kCommErrConnectionAborted);
}


// ---------------------------------------------------------------------------
//	Polling and terminating
// ---------------------------------------------------------------------------

void
THostTCPTool::HandleTimerTick()
{
	if (fConnecting)
		PollConnect();
	if (fListening)
		PollAccept();
	if (fSocket >= 0 && (fToolState & kToolStateConnected) && !(fToolState & kToolStateWantAbort))
	{
		if (fPutBytes != nil)
			PollPut();
		if (fGetBuffer != nil)
			PollGet();
	}
}


// After every message: the poll kept going.  TaskMain counts its timeout
// down by the time a message took, and a timeout of nought is none at all;
// the host's clock stands still while tasks run (it moves only when the
// machine idles), so the countdown can come to nought and leave the next
// Receive waiting for ever.
void
THostTCPTool::HandleInternalEvent()
{
	if (fTimerInterval != 0 && fTimeout == 0)
		fTimeout = fTimerInterval;
}


// One termination phase: the socket closed (and anything in hand
// forgotten; TerminateComplete answers it).
void
THostTCPTool::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	if (terminationPhase == 0)
	{
		terminationFlag = kToolStateConnecting | kToolStateConnected | kToolStateTerminating;
		terminationProc = CloseSocketProc;
	}
	else
		TCommTool::GetNextTermProc(terminationPhase, terminationFlag, terminationProc);
}


Boolean
THostTCPTool::CloseSocketProc(void* tool)
{
	THostTCPTool* self = (THostTCPTool*) tool;
	if (self->fSocket >= 0)
	{
		HostSocketClose(self->fSocket);
		self->fSocket = -1;
	}
	if (self->fListener >= 0)
	{
		HostSocketClose(self->fListener);
		self->fListener = -1;
	}
	self->fConnecting = false;
	self->fListening = false;
	self->fGetBuffer = nil;
	if (self->fPutBytes != nil)
	{
		DisposePtr((Ptr) self->fPutBytes);
		self->fPutBytes = nil;
	}
	return true;
}
