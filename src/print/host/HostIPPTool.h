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

class TEndpoint;

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

	int					fSocket;			// the connection (-1: none)
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

	TEndpoint*		fEndpoint;
	NewtonErr		fError;
	Boolean			fCancelled;
};

#endif	/* __HOSTIPPTOOL_H */
