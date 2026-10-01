/*
	File:		print/host/HostIPPTool.h

	Contains:	THostIPPTool, the comm tool whose connection is an IPP
				print job (HostIPP.h), its service THostIPPService
				('serv=ippc), and THostIPPPSDriver, the PostScript printer's
				driver over an endpoint of that service.

				The tool is the ROM's TCommTool with ConnectStart, PutBytes
				and the termination turned into socket calls, as
				comms/host/HostTCPTool.h does: the socket is non-blocking and
				polled from HandleTimerTick, so the tool's task never waits
				in the host where the Newton scheduler cannot see it.

				Connect: the configured printer's name resolved and a TCP
				connection made to it.  The first put: the HTTP POST's head,
				then the IPP Print-Job request - its document-format named
				from the put's first bytes - as the first chunk of the body,
				then the put's bytes as the next.  Every later put: one more
				chunk.  The disconnect (the termination's first phase): the
				last chunk, then the printer's answer read (up to thirty
				seconds), logged as "[host] IPP: ..." and kept for
				HostIPPJobsDone/HostIPPLastJob, and the socket closed.  The
				answer cannot fail the job - an endpoint's disconnect
				answers nothing - so a printer that refuses a job is only
				seen in the log.

				THostIPPPSDriver: Open makes an endpoint of the service and
				opens it (EasyOpen: open, bind, connect), SendPSText and
				SendPSBinary write to it, Close closes it (EasyClose); the
				errors are made the printing system's as ThpPCL::SendData
				makes them.

	Host only (no ROM counterpart; the shapes are the ROM's TAsyncService and
	TPSPAPDriver).
*/

#ifndef __HOSTIPPTOOL_H
#define __HOSTIPPTOOL_H

#include "print/PSPrinter.h"
#include "CMService.h"
#include "CommTools.h"
#include "print/host/HostIPP.h"
#include "print/host/HostIPPSocket.h"

class TEndpoint;

// The option that names the printer a connection goes to (its URI, a C
// string; empty: the configured printer, HostIPPPrinter) and the job's
// ticket - where the tool leaves what became of the job (HostIPPTicketResult).
#define kHostIPPURIOption		'iuri'

struct THostIPPURIOption : public TOption
{
						THostIPPURIOption(const char* uri, ULong ticket, const char* pin);
	ULong				fTicket;
	char				fPin[96];		// an ipps printer's certificate the driver trusted (HostIPPCheckTrust)
	char				fURI[256];
};

// A job's ticket, and what became of the job it was given to, as the
// printing system's error: noErr printed, kPR_ERR_NotFound the printer could
// not be reached, kPR_ERR_PrinterError refused (any answer but
// successful-*), kPR_ERR_Busy busy (server-error-busy, HTTP 503),
// kPR_ERR_LostContact the connection ended with no answer.  The tool runs
// in a task of its own and the endpoint's disconnect carries no error, so
// this is how the driver hears of it once the endpoint is closed.
ULong		HostIPPNewTicket(void);
NewtonErr	HostIPPTicketResult(ULong ticket);

// What an IPP printer says of its state (Get-Printer-Attributes), as the
// ROM's PostScript driver has its printer's status messages: noErr, or the
// printer problem it stands for (kPR_PROB_NoPaper, _NoInk, _Jammed,
// _DoorOpen, _OffLine), or kPR_ERR_LostContact when it does not answer.
// The task waits for it in PrReleaseControl (three seconds at most).
NewtonErr	HostIPPPrinterStatus(const char* uri, TPrinter* printer);

// An ipps:// printer's certificate trusted before a job goes to it: one
// the system's store vouches for, or the one pinned for the printer
// (by its name, else "host:port") in the Network Printers' System soup entry; else the user
// is asked by a slip naming the printer (`name`) and showing the
// fingerprint - unknown, or changed from the pinned one - as the print
// problem slip is put up from the print task, and the certificate pinned
// if trusted.  ==> noErr (pin: the trusted certificate's fingerprint),
// kPR_ERR_UserCancel not trusted, kPR_ERR_NotFound no TLS connection.  A
// URI that is not TLS is noErr at once.
NewtonErr	HostIPPCheckTrust(const char* uri, RefArg name, TPrinter* printer, char pin[96]);

// A connection to an IPP printer as the host's drivers make one: an
// endpoint of the 'ippc service opened (EasyOpen), the job's bytes written
// to it and the endpoint closed, which ends the job.  An error is made the
// printing system's as ThpPCL::SendData makes it.
class THostIPPConnection
{
public:
						THostIPPConnection();
						~THostIPPConnection();
	NewtonErr			Open(const char* uri, RefArg name, TPrinter* printer);	// uri nil or "": the configured printer
	NewtonErr			Send(const char* data, ULong size, ULong& sent);
	NewtonErr			Close();
	NewtonErr			Status(TPrinter* printer);		// HostIPPPrinterStatus of the printer it went to

	TEndpoint*			fEndpoint;
	ULong				fTicket;
	char				fURI[320];
};

// The URI of the printer a printer frame names, into uri: the printer the
// host's DNS-SD finds by the frame's hostService name, else the frame's own
// hostURI; ==> false when it has neither.  (Waits up to kHostDNSSDWait ms
// for a browse.)
const int	kHostDNSSDWait = 4000;
Boolean		HostPrinterFrameURI(RefArg printer, char* uri, size_t size);

// how often the tool looks at its socket, and how long it waits for the
// printer's answer at the end (in polls)
#define kHostIPPPollInterval	(10 * kMilliseconds)
enum { kHostIPPAnswerPolls = 3000 };

class THostIPPTool : public TCommTool
{
public:
						THostIPPTool(ULong serviceId);
	virtual				~THostIPPTool();

protected:
	virtual ULong		GetSizeOf();
	virtual UChar*		GetToolName();

	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual void		HandleTimerTick();
	virtual void		HandleInternalEvent();

	virtual void		ConnectStart();

	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		KillPut();
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		GetBytesImmediate(CBufferList* clientBuffer, Size threshold);
	virtual void		KillGet();

	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);

	Boolean				Queue(const void* data, Size size);
	Boolean				QueueChunk(const void* data, Size size);
	void				PollConnect();
	void				PollSend();
	void				PollAnswer();
	void				Finished();
	static Boolean		FinishProc(void* tool);

	Boolean				fConnecting;		// a connect waiting for the host
	Boolean				fHeaderSent;		// the POST's head and the request have been queued
	Boolean				fFinishing;			// the last chunk queued: the answer awaited
	Boolean				fPutPending;		// a put waiting for its bytes to go
	Size				fPutCount;
	UByte*				fOut;				// what is waiting to be sent
	Size				fOutSize;
	Size				fOutDone;
	Size				fOutCapacity;
	UByte*				fIn;				// the printer's answer so far
	Size				fInSize;
	long				fPolls;				// how long the answer has been waited for
	HostIPPURI			fURI;
	ULong				fTicket;			// the job's (kHostIPPURIOption; 0: none)
	char				fPin[96];			// the certificate trusted (kHostIPPURIOption)
	THostIPPSocket		fConn;				// the connection (plain or TLS)
	char				fURIText[512];
};


PROTOCOL THostIPPService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostIPPService);

	THostIPPService*	New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};


PROTOCOL THostIPPPSDriver : public TPSPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostIPPPSDriver);

	THostIPPPSDriver*	New();
	void			Delete();
	NewtonErr		Open();
	NewtonErr		Close(Boolean abort);
	NewtonErr		OpenPage();
	NewtonErr		ClosePage();
	void			CancelJob(Boolean asyncCancel);
	PrProblemResolution	IsProblemResolved();
	NewtonErr		GetStatus();
	NewtonErr		SendPSText(char* text, ULong& sent, Boolean eoj);
	NewtonErr		RepeatPSPage();
	NewtonErr		SendPSBinary(char* data, ULong size, ULong& sent);
	NewtonErr		RecvPSText(char* text, ULong& size);

	NewtonErr		Send(const char* data, ULong size, ULong& sent);

	THostIPPConnection*	fConnection;
	NewtonErr		fError;
	Boolean			fCancelled;
	ULong			fSent;			// the writes so far (the status asked every eighth)
};

#endif	/* __HOSTIPPTOOL_H */
