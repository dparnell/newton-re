/*
	File:		print/host/HostIPPSocket.h

	Contains:	A connection to an IPP printer as the host's IPP code makes
				one: a TCP connection over the host's sockets
				(hal/host/HostSockets.h), with TLS over it for ipps:// and
				https:// (tls/HostTLS.h: the host's own library).  Never
				waits: every call does what it can and says so, in
				HostSockets.h's terms, so the polled comm tool
				(HostIPPTool.h) and a task waiting in PrReleaseControl use it
				alike.

				Whether a TLS printer is to be trusted is not decided here:
				after the handshake it says whether the system's store vouches
				for the certificate (fSystemTrusts) and gives its SHA-256
				fingerprint (fFingerprint), for the caller's pin.

	Host only.
*/

#ifndef __HOSTIPPSOCKET_H
#define __HOSTIPPSOCKET_H

#include "print/host/HostIPP.h"
#include <stddef.h>

struct HostTLS;

class THostIPPSocket
{
public:
				THostIPPSocket();
				~THostIPPSocket();

	// The connection begun (the name resolved, which blocks as a resolver
	// does, and the TCP connection started).  ==> kHostSocketOK, or the
	// error (kHostSocketNotFound: no such name).
	int			Connect(const HostIPPURI* uri);

	// The connection carried on: kHostSocketWouldBlock until it is made -
	// and for TLS the handshake done - kHostSocketOK then, or the error.
	int			Connected();

	// As HostSocketSend and HostSocketReceive (kHostSocketClosed when the
	// printer has closed and nothing is left to read).  A TLS send takes
	// the whole of the data and sends it as the socket allows (Flush).
	int			Send(const void* data, size_t size, size_t* count);
	int			Receive(void* data, size_t size, size_t* count);

	// TLS bytes still waiting for the socket sent as it allows: ==>
	// kHostSocketOK when they are all gone, kHostSocketWouldBlock, or the
	// error.
	int			Flush();

	void		Close();
	bool		IsOpen() const { return fSocket >= 0; }

	bool		fTLS;				// over TLS
	bool		fSystemTrusts;		// the system's store vouches for its certificate
	char		fFingerprint[96];	// its certificate's SHA-256 (TLS)
	char		fError[160];		// what went wrong, for the log

private:
	int			Pump();				// TLS: sent what is waiting, received what has come

	int			fSocket;
	HostTLS*	fSession;
	unsigned char*	fOut;			// TLS bytes waiting for the socket
	size_t		fOutSize;
	size_t		fOutDone;
	size_t		fOutCapacity;
	bool		fTCPUp;
	bool		fReady;
	bool		fPeerClosed;
};

#endif	/* __HOSTIPPSOCKET_H */
