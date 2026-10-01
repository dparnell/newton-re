/*
	File:		print/host/HostIPPTool.cpp

	Contains:	THostIPPTool, THostIPPService and THostIPPPSDriver
				(HostIPPTool.h), and the host's IPP printers installed
				(HostIPP.h).

	Host only.
*/

#include "print/host/HostIPPTool.h"
#include "print/host/dnssd/HostDNSSD.h"
#include "print/HPPCL.h"
#include "Endpoint.h"
#include "OptionArray.h"
#include "CommOptions.h"
#include "CommErrors.h"
#include "BufferList.h"
#include "CommManager.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "HostSockets.h"
#include "utility/Unicode.h"
#include "utility/AppWorld.h"
#include "Ports.h"
#include "UserTasks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char				gIPPPrinter[512] = "";
static long				gIPPJobsDone = 0;
static HostIPPResponse	gIPPLastJob = { 0, -1, -1 };
static uint32_t			gIPPRequestId = 0;

// what became of the last jobs, by ticket (HostIPPTicketResult)
enum { kIPPTicketSlots = 16 };
static ULong			gIPPTickets = 0;
static NewtonErr		gIPPTicketResults[kIPPTicketSlots];

// how long a write to the printer may take (the ROM's ThpPCL's)
static const TTimeout	kIPPSendTimeout = (TTimeout) 0x34bc000;


/*------------------------------------------------------------------------------
	T h e   p r i n t e r
------------------------------------------------------------------------------*/

void
HostSetIPPPrinter(const char* uri)
{
	snprintf(gIPPPrinter, sizeof(gIPPPrinter), "%s", uri != nil ? uri : "");
}


const char*
HostIPPPrinter(void)
{
	return gIPPPrinter;
}


long
HostIPPJobsDone(void)
{
	return gIPPJobsDone;
}


void
HostIPPLastJob(HostIPPResponse* response)
{
	*response = gIPPLastJob;
}


ULong
HostIPPNewTicket(void)
{
	ULong ticket = ++gIPPTickets;
	if (ticket == 0)
		ticket = ++gIPPTickets;
	gIPPTicketResults[ticket % kIPPTicketSlots] = noErr;
	return ticket;
}


NewtonErr
HostIPPTicketResult(ULong ticket)
{
	return (ticket != 0) ? gIPPTicketResults[ticket % kIPPTicketSlots] : noErr;
}


static void
SetTicketResult(ULong ticket, NewtonErr result)
{
	if (ticket != 0)
		gIPPTicketResults[ticket % kIPPTicketSlots] = result;
}


/*------------------------------------------------------------------------------
	T H o s t I P P T o o l
------------------------------------------------------------------------------*/

THostIPPTool::THostIPPTool(ULong serviceId)
	: TCommTool(serviceId)
{
	fConnecting = false;
	fHeaderSent = false;
	fFinishing = false;
	fPutPending = false;
	fPutCount = 0;
	fOut = nil;
	fOutSize = fOutDone = fOutCapacity = 0;
	fIn = nil;
	fInSize = 0;
	fPolls = 0;
	memset(&fURI, 0, sizeof(fURI));
	fURIText[0] = 0;		// (kHostIPPURIOption may name the printer)
	fTicket = 0;
	fPin[0] = 0;
}


THostIPPTool::~THostIPPTool()
{
	// (the task's copy; the parent's never opened anything)
	fConn.Close();
	if (fOut != nil)
		DisposePtr((Ptr) fOut);
	if (fIn != nil)
		DisposePtr((Ptr) fIn);
}


ULong
THostIPPTool::GetSizeOf()
{
	return sizeof(THostIPPTool);
}


UChar*
THostIPPTool::GetToolName()
{
	return (UChar*) "Host IPP";
}


NewtonErr
THostIPPTool::OpenStart(TOptionArray* /*options*/)
{
	if (HostSocketsInit() != kHostSocketOK)
		return kCommErrResourceNotAvailable;
	fTimerInterval = kHostIPPPollInterval;
	return noErr;
}


// The printer's URI, when the endpoint names one (kHostIPPURIOption).
ULong
THostIPPTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	if (label == kHostIPPURIOption)
	{
		if ((opcode == opSetNegotiate || opcode == opSetRequired)
		 && theOption->Length() >= (size_t) (((const THostIPPURIOption*) theOption)->fURI - (const char*) (theOption + 1)))
		{
			const THostIPPURIOption* where = (const THostIPPURIOption*) theOption;
			fTicket = where->fTicket;
			memcpy(fPin, where->fPin, sizeof(fPin));
			fPin[sizeof(fPin) - 1] = 0;
			const char* uri = where->fURI;
			size_t n = theOption->Length() - (size_t) (uri - (const char*) (theOption + 1));
			if (n >= sizeof(fURIText))
				n = sizeof(fURIText) - 1;
			memcpy(fURIText, uri, n);
			fURIText[n] = 0;
		}
		return opSuccess;
	}
	return TCommTool::ProcessOptionStart(theOption, label, opcode);
}


// After every message: the poll kept going (as THostTCPTool's: the host's
// clock stands still while tasks run).
void
THostIPPTool::HandleInternalEvent()
{
	if (fTimerInterval != 0 && fTimeout == 0)
		fTimeout = fTimerInterval;
}


// The printer the job goes to - the one the endpoint named, else the
// configured one - named and connected to.
void
THostIPPTool::ConnectStart()
{
	if (fURIText[0] == 0)
		snprintf(fURIText, sizeof(fURIText), "%s", HostIPPPrinter());
	if (!HostIPPParseURI(fURIText, &fURI))
	{
		printf("[host] IPP: no printer (newton --ipp-printer ipp://host:631/path)\n");
		fflush(stdout);
		SetTicketResult(fTicket, kPR_ERR_NotFound);
		ConnectComplete(kCommErrResourceNotAvailable);
		return;
	}
	if (fConn.Connect(&fURI) != kHostSocketOK)
	{
		printf("[host] IPP: %s\n", fConn.fError);
		fflush(stdout);
		SetTicketResult(fTicket, kPR_ERR_NotFound);
		ConnectComplete(kCommErrIncompatibleRemote);
		return;
	}
	fConnecting = true;
	PollConnect();
}


void
THostIPPTool::PollConnect()
{
	int result = fConn.Connected();
	if (result == kHostSocketWouldBlock)
		return;
	fConnecting = false;
	if (result != kHostSocketOK)
	{
		printf("[host] IPP: %s (%s port %u)\n", fConn.fError, fURI.fHost, (unsigned) fURI.fPort);
		fflush(stdout);
		fConn.Close();
		SetTicketResult(fTicket, kPR_ERR_NotFound);
		ConnectComplete(kCommErrIncompatibleRemote);
		return;
	}
	// over TLS: a certificate the system does not vouch for must be the
	// one the driver had trusted (HostIPPCheckTrust asked the user before)
	if (fConn.fTLS && !fConn.fSystemTrusts && strcmp(fConn.fFingerprint, fPin) != 0)
	{
		printf("[host] IPP: the printer's certificate %s is not the one trusted\n", fConn.fFingerprint);
		fflush(stdout);
		fConn.Close();
		SetTicketResult(fTicket, kPR_ERR_PrinterError);
		ConnectComplete(kCommErrIncompatibleRemote);
		return;
	}
	fConnectInfo.fErrorFree = true;
	ConnectComplete(noErr);
}


// bytes added to what is waiting to go
Boolean
THostIPPTool::Queue(const void* data, Size size)
{
	if (fOutDone > 0 && fOutDone == fOutSize)
		fOutDone = fOutSize = 0;
	if (fOutSize + size > fOutCapacity)
	{
		Size capacity = (fOutSize + size) * 2 + 1024;
		UByte* bigger = (UByte*) NewPtr(capacity);
		if (bigger == nil)
			return false;
		if (fOut != nil)
		{
			memcpy(bigger, fOut, fOutSize);
			DisposePtr((Ptr) fOut);
		}
		fOut = bigger;
		fOutCapacity = capacity;
	}
	memcpy(fOut + fOutSize, data, size);
	fOutSize += size;
	return true;
}


// bytes as one chunk of the body
Boolean
THostIPPTool::QueueChunk(const void* data, Size size)
{
	char head[16];
	snprintf(head, sizeof(head), "%lx\r\n", (unsigned long) size);
	return Queue(head, strlen(head)) && Queue(data, size) && Queue("\r\n", 2);
}


// The client's bytes: the request's head first time, then a chunk.  The
// put completes when they have all gone.
void
THostIPPTool::PutBytes(CBufferList* clientBuffer)
{
	Size size = clientBuffer->GetSize() - clientBuffer->Position();
	UByte* bytes = (UByte*) NewPtr(size > 0 ? size : 1);
	if (bytes == nil)
	{
		PutComplete(kError_No_Memory, 0);
		return;
	}
	size = clientBuffer->Getn(bytes, size);
	Boolean queued = true;
	if (!fHeaderSent && size > 0)
	{
		char header[1024];
		unsigned char request[2048];
		const char* format = HostIPPDocumentFormat(bytes, size);
		size_t headerSize = HostIPPHTTPHeader(&fURI, header, sizeof(header));
		size_t requestSize = HostIPPPrintJob(fURIText, "newton", "Newton print job", format, ++gIPPRequestId, request, sizeof(request));
		queued = headerSize != 0 && requestSize != 0 && Queue(header, headerSize) && QueueChunk(request, requestSize);
		fHeaderSent = true;
		printf("[host] IPP: Print-Job to %s (%s)\n", fURIText, format);
		fflush(stdout);
	}
	if (queued && size > 0)
		queued = QueueChunk(bytes, size);
	DisposePtr((Ptr) bytes);
	if (!queued)
	{
		PutComplete(kError_No_Memory, 0);
		return;
	}
	fPutPending = true;
	fPutCount = size;
	PollSend();
}


void
THostIPPTool::PutFramedBytes(CBufferList* clientBuffer, Boolean /*endOfFrame*/)
{
	PutBytes(clientBuffer);
}


// What is waiting sent as the socket takes it; a put completes when it
// has all gone.
void
THostIPPTool::PollSend()
{
	while (fConn.IsOpen() && fOutDone < fOutSize)
	{
		size_t sent;
		int result = fConn.Send(fOut + fOutDone, fOutSize - fOutDone, &sent);
		if (result == kHostSocketWouldBlock)
			return;
		if (result != kHostSocketOK)
		{
			printf("[host] IPP: the printer closed the connection\n");
			fflush(stdout);
			fOutDone = fOutSize = 0;
			if (fPutPending)
			{
				fPutPending = false;
				PutComplete(kCommErrConnectionAborted, 0);
			}
			if (!fFinishing)
				StartAbort(kCommErrConnectionAborted);
			return;
		}
		fOutDone += sent;
	}
	if (fPutPending)
	{
		fPutPending = false;
		PutComplete(noErr, fPutCount);
	}
}


void
THostIPPTool::KillPut()
{
	fPutPending = false;
	KillPutComplete(noErr);
}


// Nothing comes back from a printer on the connection but its answer, which
// the tool reads itself: a get waits until it is killed.
void
THostIPPTool::GetBytes(CBufferList* /*clientBuffer*/)
{
}


void
THostIPPTool::GetFramedBytes(CBufferList* clientBuffer)
{
	GetBytes(clientBuffer);
}


void
THostIPPTool::GetBytesImmediate(CBufferList* clientBuffer, Size /*threshold*/)
{
	GetBytes(clientBuffer);
}


void
THostIPPTool::KillGet()
{
	KillGetComplete(noErr);
}


void
THostIPPTool::HandleTimerTick()
{
	if (fConnecting)
		PollConnect();
	if (fConn.IsOpen() && fOutDone < fOutSize)
		PollSend();
	if (fConn.IsOpen())
		fConn.Flush();
	if (fFinishing)
		PollAnswer();
}


// The printer's answer read as it comes, until it is whole, the printer
// closes or the wait is over.
void
THostIPPTool::PollAnswer()
{
	if (fOutDone < fOutSize)
	{
		if (++fPolls > kHostIPPAnswerPolls)
		{
			printf("[host] IPP: the printer took no more of the job\n");
			fflush(stdout);
			SetTicketResult(fTicket, kPR_ERR_LostContact);
			Finished();
		}
		return;
	}
	Boolean closed = false;
	for ( ; ; )
	{
		UByte bytes[1024];
		size_t got;
		int result = fConn.Receive(bytes, sizeof(bytes), &got);
		if (result == kHostSocketWouldBlock)
			break;
		if (result != kHostSocketOK)
		{
			closed = true;
			break;
		}
		UByte* bigger = (UByte*) NewPtr(fInSize + got + 1);
		if (bigger == nil)
		{
			closed = true;
			break;
		}
		if (fIn != nil)
		{
			memcpy(bigger, fIn, fInSize);
			DisposePtr((Ptr) fIn);
		}
		memcpy(bigger + fInSize, bytes, got);
		fIn = bigger;
		fInSize += got;
	}
	HostIPPResponse response;
	int complete = (fIn != nil) ? HostIPPParseResponse(fIn, fInSize, closed, &response) : (closed ? -1 : 0);
	if (complete != 0 || closed || ++fPolls > kHostIPPAnswerPolls)
	{
		if (complete == 1)
		{
			gIPPLastJob = response;
			gIPPJobsDone++;
			if (response.fJobId >= 0)
				printf("[host] IPP: the printer answered %d, %s (0x%04x), job %ld\n", response.fHTTPStatus, HostIPPStatusName(response.fIPPStatus), response.fIPPStatus, (long) response.fJobId);
			else
				printf("[host] IPP: the printer answered %d, %s (0x%04x)\n", response.fHTTPStatus, HostIPPStatusName(response.fIPPStatus), response.fIPPStatus);
		}
		else
			printf("[host] IPP: no answer from the printer\n");
		fflush(stdout);
		// what became of the job, as the printing system's error
		static const NewtonErr kOutcomes[] = { noErr, kPR_ERR_Busy, kPR_ERR_PrinterError, kPR_ERR_LostContact };
		SetTicketResult(fTicket, kOutcomes[HostIPPJobResult(complete, &response)]);
		Finished();
	}
}


// The socket closed and the termination gone on with.
void
THostIPPTool::Finished()
{
	fFinishing = false;
	fConn.Close();
	TerminateConnection();
}


// The termination's first phase: a job sent is ended - the last chunk,
// and the printer's answer - before the socket is closed.  ==> true when
// that is all done at once (no job), false when the tool goes on with the
// termination itself once the answer has come.
Boolean
THostIPPTool::FinishProc(void* tool)
{
	THostIPPTool* self = (THostIPPTool*) tool;
	self->fConnecting = false;
	if (self->fPutPending)
	{
		self->fPutPending = false;
		self->PutComplete(kCommErrConnectionAborted, 0);
	}
	if (self->fConn.IsOpen() && self->fHeaderSent && self->fOutDone <= self->fOutSize)
	{
		if (self->Queue("0\r\n\r\n", 5))
		{
			self->fFinishing = true;
			self->fPolls = 0;
			self->PollSend();
			return false;
		}
	}
	self->fConn.Close();
	return true;
}


void
THostIPPTool::GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc)
{
	if (terminationPhase == 0)
	{
		terminationFlag = kToolStateConnecting | kToolStateConnected | kToolStateTerminating;
		terminationProc = FinishProc;
	}
	else
		TCommTool::GetNextTermProc(terminationPhase, terminationFlag, terminationProc);
}


/*------------------------------------------------------------------------------
	T H o s t I P P S e r v i c e
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(THostIPPService)
PROTOCOL_CLASSINFO(THostIPPService, "TCMService", "serv\0ippc\0\0", 0, 0, nil)

THostIPPService*
THostIPPService::New()
{
	return this;
}

void
THostIPPService::Delete()
{
}

// As the host's inet service (comms/host/HostServices.cpp), over the IPP
// tool.
NewtonErr
THostIPPService::Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo)
{
	THostIPPTool tool(serviceId);
	NewtonErr err = StartCommTool(&tool, serviceId, serviceInfo);
	if (err == noErr)
		err = OpenCommTool(serviceInfo->GetPortId(), options, this);
	return err;
}

NewtonErr
THostIPPService::DoneStarting(TAEvent* event, ULong /*size*/, TServiceInfo* /*serviceInfo*/)
{
	return ((TCommToolReply*) event)->fResult;
}


/*------------------------------------------------------------------------------
	T H o s t I P P P S D r i v e r
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(THostIPPPSDriver)
PROTOCOL_CLASSINFO(THostIPPPSDriver, "TPSPrinterDriver", "", 0x20000, 0, nil)


THostIPPURIOption::THostIPPURIOption(const char* uri, ULong ticket, const char* pin)
	: TOption(kOptionType)
{
	SetAsOption(kHostIPPURIOption);
	fTicket = ticket;
	snprintf(fPin, sizeof(fPin), "%s", pin != nil ? pin : "");
	snprintf(fURI, sizeof(fURI), "%s", uri != nil ? uri : "");
	SetLength((size_t) (fURI - (char*) ((TOption*) this + 1)) + strlen(fURI) + 1);
}


THostIPPConnection::THostIPPConnection()
{
	fEndpoint = nil;
	fTicket = 0;
	fURI[0] = 0;
}


THostIPPConnection::~THostIPPConnection()
{
	if (fEndpoint != nil)
		fEndpoint->Delete();
}


// An endpoint of the IPP service, opened (EasyOpen: connected to the
// printer).  A printer that cannot be reached is not found
// (kPR_ERR_NotFound: "No printer is connected.").
NewtonErr
THostIPPConnection::Open(const char* uri, RefArg name, TPrinter* printer)
{
	fTicket = HostIPPNewTicket();
	snprintf(fURI, sizeof(fURI), "%s", (uri != nil && uri[0] != 0) ? uri : HostIPPPrinter());
	// an ipps printer's certificate trusted first (the user asked if need be)
	char pin[96];
	NewtonErr err = HostIPPCheckTrust(fURI, name, printer, pin);
	if (err != noErr)
		return err;
	TOptionArray options;
	err = options.Init();
	if (err == noErr)
	{
		TOption service(kOptionType);
		service.SetAsService(kHostIPPService);
		err = options.InsertOptionAt(options.GetArrayCount(), &service);
	}
	if (err == noErr)
	{
		THostIPPURIOption where(uri, fTicket, pin);
		err = options.InsertOptionAt(options.GetArrayCount(), &where);
	}
	if (err == noErr)
		err = CMGetEndpoint(&options, &fEndpoint, false);
	if (err == noErr && fEndpoint != nil)
		err = fEndpoint->EasyOpen(0);
	if (err != noErr)
	{
		if (fEndpoint != nil)
		{
			fEndpoint->Delete();
			fEndpoint = nil;
		}
		NewtonErr result = HostIPPTicketResult(fTicket);
		return (result != noErr) ? result : kPR_ERR_NewtonError;
	}
	return noErr;
}


NewtonErr
THostIPPConnection::Send(const char* data, ULong size, ULong& sent)
{
	sent = 0;
	if (fEndpoint == nil)
		return kPR_ERR_NewtonError;
	Size count = size;
	NewtonErr err = fEndpoint->Snd((UByte*) data, count, 0, kIPPSendTimeout);
	sent = count;
	if (err == noErr)
		return noErr;
	if (err == kError_Call_Aborted)
		return kPR_ERR_UserCancel;
	if (err == kError_Message_Timed_Out)
		return kPR_ERR_LostContact;
	return kPR_ERR_NewtonError;
}


// The endpoint closed, which ends the job at the printer; ==> what became
// of it (HostIPPTicketResult): the printer's refusal is the job's error.
NewtonErr
THostIPPConnection::Close()
{
	NewtonErr err = noErr;
	if (fEndpoint != nil)
	{
		err = fEndpoint->EasyClose();
		fEndpoint->Delete();
		fEndpoint = nil;
	}
	if (err != noErr)
		return kPR_ERR_NewtonError;
	return HostIPPTicketResult(fTicket);
}


NewtonErr
THostIPPConnection::Status(TPrinter* printer)
{
	return HostIPPPrinterStatus(fURI, printer);
}


/*------------------------------------------------------------------------------
	T h e   p r i n t e r ' s   s t a t e
	Get-Printer-Attributes over a socket of the task's own, the task waiting
	in PrReleaseControl between looks at it.
------------------------------------------------------------------------------*/

static const TTimeout	kIPPStatusPoll = (TTimeout) (20 * kMilliseconds);
enum { kIPPStatusPolls = 150 };

NewtonErr
HostIPPPrinterStatus(const char* uri, TPrinter* printer)
{
	HostIPPURI where;
	if (uri == nil || !HostIPPParseURI(uri, &where))
		return kPR_ERR_LostContact;
	THostIPPSocket sock;
	if (sock.Connect(&where) != kHostSocketOK)
		return kPR_ERR_LostContact;
	unsigned char request[1024];
	size_t bodySize = HostIPPGetPrinterState(uri, ++gIPPRequestId, request + 512, sizeof(request) - 512);
	size_t headSize = HostIPPHTTPRequestHeader(&where, bodySize, (char*) request, 512);
	if (bodySize == 0 || headSize == 0)
		return kPR_ERR_NewtonError;
	memmove(request + headSize, request + 512, bodySize);
	size_t toSend = headSize + bodySize, sent = 0;
	unsigned char answer[4096];
	size_t got = 0;
	int complete = 0;
	HostIPPResponse response;
	Boolean connected = false;
	for (long polls = 0; polls < kIPPStatusPolls && complete == 0; polls++)
	{
		if (!connected)
		{
			int result = sock.Connected();
			if (result == kHostSocketOK)
				connected = true;
			else if (result != kHostSocketWouldBlock)
				break;
		}
		if (connected && sent < toSend)
		{
			size_t n = 0;
			int result = sock.Send(request + sent, toSend - sent, &n);
			if (result == kHostSocketOK)
				sent += n;
			else if (result != kHostSocketWouldBlock)
				break;
		}
		if (connected && sent == toSend)
		{
			Boolean closed = false;
			for ( ; ; )
			{
				size_t n = 0;
				if (got == sizeof(answer))
				{
					closed = true;
					break;
				}
				int result = sock.Receive(answer + got, sizeof(answer) - got, &n);
				if (result == kHostSocketWouldBlock)
					break;
				if (result != kHostSocketOK || n == 0)
				{
					closed = true;
					break;
				}
				got += n;
			}
			complete = (got > 0) ? HostIPPParseResponse(answer, got, closed, &response) : (closed ? -1 : 0);
			if (complete != 0)
				break;
		}
		PrReleaseControl(kIPPStatusPoll, printer);
	}
	sock.Close();
	if (complete != 1)
		return kPR_ERR_LostContact;
	if (response.fHTTPStatus != 200 || response.fIPPStatus < 0 || response.fIPPStatus >= 0x0100)
		return noErr;		// (a printer that will not say is taken to be well)
	static const NewtonErr kProblems[] = { noErr, kPR_PROB_NoPaper, kPR_PROB_NoInk, kPR_PROB_Jammed, kPR_PROB_DoorOpen, kPR_PROB_OffLine };
	NewtonErr problem = kProblems[HostIPPPrinterCondition(&response)];
	if (problem != noErr)
	{
		printf("[host] IPP: the printer's state %d (%s): problem %ld\n", response.fPrinterState, response.fStateReasons, (long) problem);
		fflush(stdout);
	}
	return problem;
}


Boolean
HostPrinterFrameURI(RefArg printer, char* uri, size_t size)
{
	uri[0] = 0;
	if (!IsFrame(printer))
		return false;
	// a printer found on the network is looked for again by its name (an
	// address and port need not last), the URI it had kept for when it is
	// not found
	RefVar service(GetFrameSlot(printer, RefVar(Intern((char*) "hostService"))));
	if (IsString(service))
	{
		char name[64];
		HostDNSSDPrinter found;
		ConvertFromUnicode(GetCString(service), name, kMacRomanEncoding, sizeof(name) - 1);
		if (HostDNSSDResolve(name, &found, kHostDNSSDWait))
		{
			snprintf(uri, size, "%s", found.fURI);
			return true;
		}
	}
	RefVar where(GetFrameSlot(printer, RefVar(Intern((char*) "hostURI"))));
	if (!IsString(where))
		return false;
	ConvertFromUnicode(GetCString(where), uri, kMacRomanEncoding, (long) size - 1);
	return uri[0] != 0;
}


THostIPPPSDriver*
THostIPPPSDriver::New()
{
	fConnection = nil;
	fError = noErr;
	fCancelled = false;
	fSent = 0;
	return this;
}


void
THostIPPPSDriver::Delete()
{
	delete fConnection;
	fConnection = nil;
}


// A connection to the printer the printer frame names (its hostURI), else
// to the configured one.
NewtonErr
THostIPPPSDriver::Open()
{
	fError = noErr;
	fCancelled = false;
	fSent = 0;
	char uri[256];
	RefVar connectInfo(fConnect->fConnectInfo);
	RefVar printer(IsFrame(connectInfo) ? GetFrameSlot(connectInfo, RefVar(Intern((char*) "printer"))) : NILREF);
	HostPrinterFrameURI(printer, uri, sizeof(uri));
	if (fConnection == nil)
		fConnection = new THostIPPConnection;
	RefVar name(IsFrame(printer) ? GetFrameSlot(printer, RefVar(Intern((char*) "name"))) : NILREF);
	return fConnection->Open(uri, name, fPrinter);
}


NewtonErr
THostIPPPSDriver::Close(Boolean /*abort*/)
{
	return (fConnection != nil) ? fConnection->Close() : noErr;
}


// The printer's state looked at as each page begins and ends, as the
// ROM's TPSPAPDriver looks at its printer's status: a problem found is
// kept, and is the next write's error (TPSPAPDriver's PutData failing
// while its printer has a problem), which TPSPrinter puts to the user.
NewtonErr
THostIPPPSDriver::OpenPage()
{
	fError = GetStatus();
	return noErr;
}


NewtonErr
THostIPPPSDriver::ClosePage()
{
	if (fError == noErr)
		fError = GetStatus();
	return noErr;
}


void
THostIPPPSDriver::CancelJob(Boolean asyncCancel)
{
	if (asyncCancel)
		fCancelled = true;
	else
		fError = kPR_ERR_UserCancel;
}


// (asked while the problem slip is up: TPrintView's HandleProblem)
PrProblemResolution
THostIPPPSDriver::IsProblemResolved()
{
	fError = GetStatus();
	return (PrProblemResolution) (fError != noErr);
}


// the printer's state (HostIPPPrinterStatus)
NewtonErr
THostIPPPSDriver::GetStatus()
{
	return (fConnection != nil) ? fConnection->Status(fPrinter) : noErr;
}


// Bytes written to the endpoint; an aborted write is the user's cancel, one
// that timed out the printer lost, anything else a Newton error (as
// ThpPCL::SendData has it).
NewtonErr
THostIPPPSDriver::Send(const char* data, ULong size, ULong& sent)
{
	sent = 0;
	if (fCancelled)
		return kPR_ERR_UserCancel;
	if (fConnection == nil)
		return kPR_ERR_NewtonError;
	if (fError >= kPR_ERR_MINPROBLEM && fError <= kPR_ERR_MAXPROBLEM)
	{
		// the printer has a problem: asked again, and the write refused
		// while it lasts
		fError = GetStatus();
		if (fError != noErr)
			return fError;
	}
	NewtonErr err = fConnection->Send(data, size, sent);
	if (err == noErr && (++fSent & 7) == 7)
		fError = GetStatus();
	return err;
}


NewtonErr
THostIPPPSDriver::SendPSText(char* text, ULong& sent, Boolean /*eoj*/)
{
	return Send(text, strlen(text), sent);
}


NewtonErr
THostIPPPSDriver::RepeatPSPage()
{
	return noErr;
}


NewtonErr
THostIPPPSDriver::SendPSBinary(char* data, ULong size, ULong& sent)
{
	return Send(data, size, sent);
}


// (nothing to read: the printer answers the job when it ends)
NewtonErr
THostIPPPSDriver::RecvPSText(char* /*text*/, ULong& size)
{
	size = 0;
	return kPR_ERR_NewtonError;
}


/*------------------------------------------------------------------------------
	I n s t a l l i n g   t h e m
------------------------------------------------------------------------------*/

// the HP driver's way to an IPP printer (print/HPPCL.h): for the model
// kHostIPPPrinterModel, the IPP service and the printer frame's URI, and
// the job's ticket (one HP job at a time: the print view's)
static ThpPCL*	gPCLDriver = nil;
static ULong	gPCLTicket = 0;

static Boolean
IPPServiceForPCL(ThpPCL* driver, TOptionArray* options)
{
	if (driver->fPrModel != kHostIPPPrinterModel)
		return false;
	TOption service(kOptionType);
	service.SetAsService(kHostIPPService);
	driver->fError = options->InsertOptionAt(options->GetArrayCount(), &service);
	char uri[256];
	RefVar connectInfo(driver->fConnect->fConnectInfo);
	RefVar printer(IsFrame(connectInfo) ? GetFrameSlot(connectInfo, RefVar(Intern((char*) "printer"))) : NILREF);
	if (!HostPrinterFrameURI(printer, uri, sizeof(uri)))
		uri[0] = 0;
	gPCLDriver = driver;
	gPCLTicket = HostIPPNewTicket();
	// an ipps printer's certificate trusted first (the user asked if need
	// be); not trusted, the connection is not made - which ThpPCL::Open
	// makes "Newton is unable to print.", as the ROM's makes any failed one
	char pin[96] = "";
	if (driver->fError == noErr)
	{
		RefVar name(IsFrame(printer) ? GetFrameSlot(printer, RefVar(Intern((char*) "name"))) : NILREF);
		driver->fError = HostIPPCheckTrust(uri[0] != 0 ? uri : HostIPPPrinter(), name, driver->fPrinter, pin);
	}
	if (driver->fError == noErr)
	{
		THostIPPURIOption where(uri, gPCLTicket, pin);
		driver->fError = options->InsertOptionAt(options->GetArrayCount(), &where);
	}
	return true;
}


// ... and what became of its job, once its endpoint is closed: the
// printer's refusal the job's error (print/HPPCL.h's gPrinterCloseHook)
static NewtonErr
IPPResultForPCL(ThpPCL* driver, NewtonErr err)
{
	if (driver->fPrModel != kHostIPPPrinterModel || driver != gPCLDriver)
		return err;
	gPCLDriver = nil;
	return (err == noErr) ? HostIPPTicketResult(gPCLTicket) : err;
}


static Ref
MakeBounds(long right, long bottom)
{
	RefVar bounds(AllocateFrame());
	SetFrameSlot(bounds, RSSYMbottom, MAKEINT(bottom));
	SetFrameSlot(bounds, RSSYMright, MAKEINT(right));
	return bounds;
}


// A printer frame as the ROM's AvailablePrinters has them.
static Ref
MakePrinterFrame(const char* name, const char* driver, const char* imaging, long letterRight, long letterBottom, long a4Right, long a4Bottom, long top, long left, long prModel)
{
	RefVar printer(AllocateFrame());
	SetFrameSlot(printer, RSSYMname, MakeString(name));
	SetFrameSlot(printer, RSSYMdrivername, MakeString(driver));
	SetFrameSlot(printer, RSSYMimagingname, MakeString(imaging));
	SetFrameSlot(printer, RSSYMtype, RefVar(Intern((char*) "serialSym")));
	if (prModel >= 0)
		SetFrameSlot(printer, RefVar(Intern((char*) "prModel")), MAKEINT(prModel));
	RefVar pageBounds(AllocateFrame());
	SetFrameSlot(pageBounds, RefVar(Intern((char*) "eightByEleven")), MakeBounds(letterRight, letterBottom));
	SetFrameSlot(pageBounds, RSSYMa4, MakeBounds(a4Right, a4Bottom));
	SetFrameSlot(printer, RSSYMprinterpagebounds, pageBounds);
	RefVar origin(AllocateFrame());
	SetFrameSlot(origin, RSSYMtop, MAKEINT(top));
	SetFrameSlot(origin, RSSYMleft, MAKEINT(left));
	SetFrameSlot(printer, RefVar(Intern((char*) "printableOrigin")), origin);
	return printer;
}


// The two printers put in AvailablePrinters, once: the PostScript one with
// the LaserWriter's page (the ROM's network PostScript printer frame), the
// HP one with the DeskWriter's.
static void
AddIPPPrinters(void)
{
	static Boolean added = false;
	if (added)
		return;
	RefVar printers(GetFrameSlot(RefVar(gVarFrame), RSSYMavailableprinters));
	if (!IsArray(printers))
		return;
	AddArraySlot(printers, RefVar(MakePrinterFrame(kHostIPPPSPrinterName, kHostIPPPSDriverName, "TPSPrinter", 576, 775, 559, 797, 8, 8, -1)));
	AddArraySlot(printers, RefVar(MakePrinterFrame(kHostIPPPCLPrinterName, "ThpPCL", "TDotPrinter", 576, 756, 559, 797, 14, 18, kHostIPPPrinterModel)));
	added = true;
}


// HostSetIPPPrinter(uri): the printer jobs go to, and the printers offered
static Ref
FHostSetIPPPrinter(RefArg /*rcvr*/, RefArg uri)
{
	char text[512];
	ConvertFromUnicode(GetCString(uri), text, kMacRomanEncoding, sizeof(text) - 1);
	HostSetIPPPrinter(text);
	AddIPPPrinters();
	return TRUEREF;
}


// HostIPPJobs(): how many jobs the printer has answered
static Ref
FHostIPPJobs(RefArg /*rcvr*/)
{
	return MAKEINT(gIPPJobsDone);
}


// HostIPPLastStatus(): the last job's IPP status-code (-1: none)
static Ref
FHostIPPLastStatus(RefArg /*rcvr*/)
{
	return MAKEINT(gIPPLastJob.fIPPStatus);
}


void
HostInstallIPPPrinters(void)
{
	THostIPPService::ClassInfo()->Register();
	THostIPPPSDriver::ClassInfo()->Register();
	gPrinterServiceHook = IPPServiceForPCL;
	gPrinterCloseHook = IPPResultForPCL;
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSetIPPPrinter")), RefVar(MakeCFunction((void*) FHostSetIPPPrinter, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostIPPJobs")), RefVar(MakeCFunction((void*) FHostIPPJobs, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostIPPLastStatus")), RefVar(MakeCFunction((void*) FHostIPPLastStatus, 0, nil)));
	if (gIPPPrinter[0] == 0)
	{
		const char* fromEnvironment = getenv("NEWTON_IPP_PRINTER");
		if (fromEnvironment != nil)
			HostSetIPPPrinter(fromEnvironment);
	}
	if (gIPPPrinter[0] != 0)
		AddIPPPrinters();
	// the network printers: the ROM's chooser answered from the host's
	// DNS-SD, and the Network Printers panel (HostNetworkPrinters.cpp)
	HostInstallNetworkPrinters();
}


/*------------------------------------------------------------------------------
	T r u s t i n g   a   p r i n t e r ' s   c e r t i f i c a t e
	An ipps:// printer's certificate, before a job goes to it: one the
	system's store vouches for is trusted; any other (a printer's own,
	self-signed - the usual case) must be the one pinned for that printer on
	the Newton's store, and when there is none, or it has changed, the user
	is asked - a slip opened from the print task as TPrintView's
	HandleProblem opens the print problem slip, the task letting the world's
	mutex go and sleeping until it is answered.  The owner's policy:
	unknown asks, changed warns, and either may be accepted (the new
	certificate pinned).
------------------------------------------------------------------------------*/

static const TTimeout	kTrustPoll = (TTimeout) (200 * kMilliseconds);
enum { kTrustPolls = 1500 };			// five minutes

static Ref
NetworkPrintersFrame(void)
{
	return GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "NetworkPrinters:host")));
}


// the slip opened and waited on; ==> true when the user trusts the
// certificate
static Boolean
AskTrust(RefArg printers, RefArg name, const char* uri, const char* fingerprint, RefArg known)
{
	GrafPort* saved = GetCurrentPort();
	SetPort(&gGrafPort);
	RefVar args(MakeArray(4));
	SetArraySlot(args, 0, name);
	SetArraySlot(args, 1, RefVar(MakeString(uri)));
	SetArraySlot(args, 2, RefVar(MakeString(fingerprint)));
	SetArraySlot(args, 3, known);
	RefVar slip(NSSendWithArgArray(printers, RefVar(Intern((char*) "AskTrust")), args));
	RefVar answer;
	for (long polls = 0; polls < kTrustPolls && NOTNIL(slip); polls++)
	{
		((TForkWorld*) GetGlobals())->ReleaseMutex();
		Sleep(kTrustPoll);
		((TForkWorld*) GetGlobals())->AcquireMutex();
		answer = GetFrameSlot(slip, RefVar(Intern((char*) "answer")));
		if (NOTNIL(answer))
			break;
	}
	if (ISNIL(answer) && NOTNIL(slip))
		NSSend(slip, RefVar(Intern((char*) "Close")));
	SetPort(saved);
	return EQ(answer, RefVar(Intern((char*) "trust")));
}


NewtonErr
HostIPPCheckTrust(const char* uri, RefArg name, TPrinter* printer, char pin[96])
{
	pin[0] = 0;
	HostIPPURI where;
	if (uri == nil || !HostIPPParseURI(uri, &where) || !where.fTLS)
		return noErr;
	THostIPPSocket sock;
	int result = sock.Connect(&where);
	for (long polls = 0; result == kHostSocketOK && polls < kIPPStatusPolls; polls++)
	{
		result = sock.Connected();
		if (result == kHostSocketWouldBlock)
		{
			PrReleaseControl(kIPPStatusPoll, printer);
			result = kHostSocketOK;
			continue;
		}
		break;
	}
	if (result != kHostSocketOK || sock.fFingerprint[0] == 0)
	{
		printf("[host] IPP: %s\n", sock.fError[0] != 0 ? sock.fError : "no TLS connection to the printer");
		fflush(stdout);
		return kPR_ERR_NotFound;
	}
	sock.Close();
	snprintf(pin, 96, "%s", sock.fFingerprint);
	if (sock.fSystemTrusts)
		return noErr;
	// the pin is kept by the printer's name (what the user chose it by),
	// else by its address
	char place[300];
	if (IsString(name))
		ConvertFromUnicode(GetCString(name), place, kMacRomanEncoding, sizeof(place) - 1);
	else
		snprintf(place, sizeof(place), "%s:%u", where.fHost, (unsigned) where.fPort);
	RefVar printers(NetworkPrintersFrame());
	if (ISNIL(printers))
		return kPR_ERR_NewtonError;
	RefVar known(NSSend(printers, RefVar(Intern((char*) "PinFor")), RefVar(MakeString(place))));
	char knownText[96] = "";
	if (IsString(known))
		ConvertFromUnicode(GetCString(known), knownText, kMacRomanEncoding, sizeof(knownText) - 1);
	if (strcmp(knownText, pin) == 0)
		return noErr;
	printf("[host] IPP: %s's certificate %s is %s\n", place, pin, knownText[0] != 0 ? "not the one trusted" : "not known");
	fflush(stdout);
	if (!AskTrust(printers, name, uri, pin, known))
	{
		printf("[host] IPP: the certificate was not trusted: the job is cancelled\n");
		fflush(stdout);
		return kPR_ERR_UserCancel;
	}
	NSSend(printers, RefVar(Intern((char*) "Pin")), RefVar(MakeString(place)), RefVar(MakeString(pin)));
	printf("[host] IPP: %s's certificate %s trusted\n", place, pin);
	fflush(stdout);
	return noErr;
}
