/*
	File:		comms/host/HostTCPTool.h

	Contains:	THostTCPTool, the comm tool that stands in for the Newton
				Internet Enabler's TCP tool: a TCommTool whose connection is
				a TCP connection of the host's (hal/host/HostSockets.h).

				DEVIATION (owner's decision - docs/comms/README.md): the NIE's
				own tool is native ARM code over its own TCP/IP stack; the
				host's is this, over the host's.  Everything above it - the
				requests, the option processing, the connect/disconnect state
				machine and the abort - is the ROM's TCommTool; this class
				only turns ConnectStart, ListenStart, GetBytes, PutBytes and
				the termination into socket calls.  The socket is non-
				blocking and polled from HandleTimerTick every
				kHostTCPPollInterval while the tool is open, so the tool's
				task never waits in the host where the scheduler cannot see
				it.

				The options it answers are the NIE's (their layout is the
				NIE API's, the data big-endian as a script packs it):

				'itrs  the remote TCP socket: four address bytes, a 2-byte port
				'ilpt  the local port: a 2-byte port
				'itsv  the transport service: a long, kInetTransportTCP (only
				       TCP is served)
				'ilid  the link id: a long (accepted; the host's link is
				       always up)

				Anything else goes to TCommTool (the passive claim, the
				service id, the transport info) and then to nobody.

	Host code for the NIE's TCP tool (no ROM counterpart).
*/

#ifndef __COMMS_HOSTTCPTOOL_H
#define __COMMS_HOSTTCPTOOL_H

#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif

// the NIE's option labels
#define kInetRemoteSocketOption		'itrs'
#define kInetLocalPortOption		'ilpt'
#define kInetTransportServiceOption	'itsv'
#define kInetLinkIdOption			'ilid'

// 'itsv's transport services
#define kInetTransportTCP			1
#define kInetTransportUDP			2

// how often the tool looks at its socket
#define kHostTCPPollInterval		(10 * kMilliseconds)


class THostTCPTool : public TCommTool
{
public:
						THostTCPTool(ULong serviceId);
	virtual				~THostTCPTool();

protected:
	virtual ULong		GetSizeOf();
	virtual UChar*		GetToolName();

	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual void		HandleTimerTick();
	virtual void		HandleInternalEvent();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);

	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual void		AcceptStart();

	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		KillPut();
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		GetBytesImmediate(CBufferList* clientBuffer, Size threshold);
	virtual void		KillGet();

	virtual void		GetNextTermProc(ULong terminationPhase, ULong& terminationFlag, TerminateProcPtr& terminationProc);

	void				PollConnect();
	void				PollAccept();
	void				PollPut();
	void				PollGet();
	void				PeerClosed();
	static Boolean		CloseSocketProc(void* tool);

	int					fSocket;			// the connection (-1: none)
	int					fListener;			// the listening socket (-1: none)
	uint32_t			fRemoteAddress;
	uint16_t			fRemotePort;
	uint16_t			fLocalPort;
	ULong				fTransport;
	Boolean				fConnecting;		// a connect waiting for the host
	Boolean				fListening;			// a listen waiting for a caller

	CBufferList*		fGetBuffer;			// the get in hand (nil: none)
	Size				fGetCount;			// what it has had so far
	Size				fGetThreshold;		// how much completes it (-1: when full)

	UByte*				fPutBytes;			// the put in hand, copied out (nil: none)
	Size				fPutSize;
	Size				fPutDone;
};

#endif	/* __COMMS_HOSTTCPTOOL_H */
