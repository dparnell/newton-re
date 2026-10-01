/*
	File:		print/host/HostIPPTool.cpp

	Contains:	THostIPPTool, THostIPPService and THostIPPPSDriver
				(HostIPPTool.h), and the host's IPP printers installed
				(HostIPP.h).

	Host only.
*/

#include "print/host/HostIPPTool.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char				gIPPPrinter[512] = "";
static long				gIPPJobsDone = 0;
static HostIPPResponse	gIPPLastJob = { 0, -1, -1 };
static uint32_t			gIPPRequestId = 0;

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


/*------------------------------------------------------------------------------
	T H o s t I P P T o o l
------------------------------------------------------------------------------*/

THostIPPTool::THostIPPTool(ULong serviceId)
	: TCommTool(serviceId)
{
	fSocket = -1;
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
	fURIText[0] = 0;
}


THostIPPTool::~THostIPPTool()
{
	// (the task's copy; the parent's never opened anything)
	if (fSocket >= 0)
		HostSocketClose(fSocket);
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


// After every message: the poll kept going (as THostTCPTool's: the host's
// clock stands still while tasks run).
void
THostIPPTool::HandleInternalEvent()
{
	if (fTimerInterval != 0 && fTimeout == 0)
		fTimeout = fTimerInterval;
}


// The printer the jobs go to, named and connected to.
void
THostIPPTool::ConnectStart()
{
	snprintf(fURIText, sizeof(fURIText), "%s", HostIPPPrinter());
	if (!HostIPPParseURI(fURIText, &fURI))
	{
		printf("[host] IPP: no printer (newton --ipp-printer ipp://host:631/path)\n");
		fflush(stdout);
		ConnectComplete(kCommErrResourceNotAvailable);
		return;
	}
	uint32_t address;
	int count = 0;
	if (HostResolveName(fURI.fHost, &address, 1, &count) != kHostSocketOK || count == 0)
	{
		printf("[host] IPP: cannot find %s\n", fURI.fHost);
		fflush(stdout);
		ConnectComplete(kCommErrIncompatibleRemote);
		return;
	}
	if (HostTCPConnect(address, fURI.fPort, &fSocket) != kHostSocketOK)
	{
		fSocket = -1;
		ConnectComplete(kCommErrIncompatibleRemote);
		return;
	}
	fConnecting = true;
	PollConnect();
}


void
THostIPPTool::PollConnect()
{
	int result = HostSocketConnected(fSocket);
	if (result == kHostSocketWouldBlock)
		return;
	fConnecting = false;
	if (result != kHostSocketOK)
	{
		printf("[host] IPP: cannot connect to %s port %u\n", fURI.fHost, (unsigned) fURI.fPort);
		fflush(stdout);
		HostSocketClose(fSocket);
		fSocket = -1;
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
	while (fSocket >= 0 && fOutDone < fOutSize)
	{
		size_t sent;
		int result = HostSocketSend(fSocket, fOut + fOutDone, fOutSize - fOutDone, &sent);
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
	if (fSocket >= 0 && fOutDone < fOutSize)
		PollSend();
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
			Finished();
		return;
	}
	Boolean closed = false;
	for ( ; ; )
	{
		UByte bytes[1024];
		size_t got;
		int result = HostSocketReceive(fSocket, bytes, sizeof(bytes), &got);
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
		Finished();
	}
}


// The socket closed and the termination gone on with.
void
THostIPPTool::Finished()
{
	fFinishing = false;
	if (fSocket >= 0)
	{
		HostSocketClose(fSocket);
		fSocket = -1;
	}
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
	if (self->fSocket >= 0 && self->fHeaderSent && self->fOutDone <= self->fOutSize)
	{
		if (self->Queue("0\r\n\r\n", 5))
		{
			self->fFinishing = true;
			self->fPolls = 0;
			self->PollSend();
			return false;
		}
	}
	if (self->fSocket >= 0)
	{
		HostSocketClose(self->fSocket);
		self->fSocket = -1;
	}
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


THostIPPPSDriver*
THostIPPPSDriver::New()
{
	fEndpoint = nil;
	fError = noErr;
	fCancelled = false;
	return this;
}


void
THostIPPPSDriver::Delete()
{
	if (fEndpoint != nil)
		fEndpoint->Delete();
	fEndpoint = nil;
}


// An endpoint of the IPP service, opened (EasyOpen: connected to the
// printer).
NewtonErr
THostIPPPSDriver::Open()
{
	fError = noErr;
	fCancelled = false;
	TOptionArray options;
	NewtonErr err = options.Init();
	if (err == noErr)
	{
		TOption service(kOptionType);
		service.SetAsService(kHostIPPService);
		err = options.InsertOptionAt(options.GetArrayCount(), &service);
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
		return kPR_ERR_NewtonError;
	}
	return noErr;
}


// The endpoint closed, which ends the job at the printer.
NewtonErr
THostIPPPSDriver::Close(Boolean /*abort*/)
{
	NewtonErr err = noErr;
	if (fEndpoint != nil)
	{
		err = fEndpoint->EasyClose();
		fEndpoint->Delete();
		fEndpoint = nil;
	}
	return (err == noErr) ? noErr : kPR_ERR_NewtonError;
}


NewtonErr
THostIPPPSDriver::OpenPage()
{
	return noErr;
}


NewtonErr
THostIPPPSDriver::ClosePage()
{
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


PrProblemResolution
THostIPPPSDriver::IsProblemResolved()
{
	return kPrProblemFixed;
}


// (an IPP printer says nothing while the job is sent)
NewtonErr
THostIPPPSDriver::GetStatus()
{
	return noErr;
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

// the HP driver's way to the IPP printer (print/HPPCL.h)
static ULong
IPPServiceForModel(long prModel)
{
	return (prModel == kHostIPPPrinterModel) ? (ULong) kHostIPPService : 0;
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
	gPrinterServiceHook = IPPServiceForModel;
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
}
