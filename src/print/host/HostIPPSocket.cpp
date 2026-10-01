/*
	File:		print/host/HostIPPSocket.cpp

	Contains:	THostIPPSocket (HostIPPSocket.h).

	Host only.
*/

#include "print/host/HostIPPSocket.h"
#include "HostSockets.h"
#include "HostTLS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


THostIPPSocket::THostIPPSocket()
{
	fSocket = -1;
	fSession = NULL;
	fTLS = false;
	fTCPUp = false;
	fReady = false;
	fPeerClosed = false;
	fSystemTrusts = false;
	fFingerprint[0] = 0;
	fError[0] = 0;
	fOut = NULL;
	fOutSize = 0;
	fOutDone = 0;
	fOutCapacity = 0;
}


THostIPPSocket::~THostIPPSocket()
{
	Close();
	free(fOut);
}


void
THostIPPSocket::Close()
{
	if (fSocket >= 0)
		HostSocketClose(fSocket);
	fSocket = -1;
	if (fSession != NULL)
		HostTLSFree(fSession);
	fSession = NULL;
	fTCPUp = false;
	fReady = false;
	fOutSize = 0;
	fOutDone = 0;
}


int
THostIPPSocket::Connect(const HostIPPURI* uri)
{
	Close();
	fTLS = uri->fTLS;
	fPeerClosed = false;
	fSystemTrusts = false;
	fFingerprint[0] = 0;
	fError[0] = 0;
	if (fTLS && !HostTLSAvailable())
	{
		snprintf(fError, sizeof(fError), "this host has no TLS for %s", uri->fHost);
		return kHostSocketError;
	}
	uint32_t address;
	int count = 0;
	if (HostResolveName(uri->fHost, &address, 1, &count) != kHostSocketOK || count == 0)
	{
		snprintf(fError, sizeof(fError), "cannot find %s", uri->fHost);
		return kHostSocketNotFound;
	}
	int result = HostTCPConnect(address, uri->fPort, &fSocket);
	if (result != kHostSocketOK)
	{
		fSocket = -1;
		snprintf(fError, sizeof(fError), "cannot connect to %s port %u", uri->fHost, (unsigned) uri->fPort);
		return result;
	}
	if (fTLS)
		fSession = HostTLSNew(uri->fHost);
	return kHostSocketOK;
}


// what the TLS session has to send, moved to the bytes waiting for the
// socket
static bool
Append(unsigned char*& out, size_t& size, size_t& capacity, size_t& done, const unsigned char* data, size_t n)
{
	if (done > 0 && done == size)
		size = done = 0;
	if (size + n > capacity)
	{
		size_t bigger = (size + n) * 2;
		unsigned char* p = (unsigned char*) realloc(out, bigger);
		if (p == NULL)
			return false;
		out = p;
		capacity = bigger;
	}
	memcpy(out + size, data, n);
	size += n;
	return true;
}


int
THostIPPSocket::Flush()
{
	if (fSession != NULL)
	{
		unsigned char bytes[8192];
		size_t n;
		while ((n = HostTLSTakeToSend(fSession, bytes, sizeof(bytes))) > 0)
			if (!Append(fOut, fOutSize, fOutCapacity, fOutDone, bytes, n))
				return kHostSocketError;
	}
	while (fOutDone < fOutSize)
	{
		size_t sent = 0;
		int result = HostSocketSend(fSocket, fOut + fOutDone, fOutSize - fOutDone, &sent);
		if (result == kHostSocketWouldBlock)
			return kHostSocketWouldBlock;
		if (result != kHostSocketOK)
			return result;
		fOutDone += sent;
		if (sent == 0)
			return kHostSocketWouldBlock;
	}
	return kHostSocketOK;
}


int
THostIPPSocket::Pump()
{
	int result = Flush();
	if (result != kHostSocketOK && result != kHostSocketWouldBlock)
		return result;
	while (!fPeerClosed)
	{
		unsigned char bytes[4096];
		size_t got = 0;
		int r = HostSocketReceive(fSocket, bytes, sizeof(bytes), &got);
		if (r == kHostSocketWouldBlock)
			break;
		if (r == kHostSocketClosed || (r == kHostSocketOK && got == 0))
		{
			fPeerClosed = true;
			break;
		}
		if (r != kHostSocketOK)
			return r;
		HostTLSPutReceived(fSession, bytes, got);
	}
	return kHostSocketOK;
}


int
THostIPPSocket::Connected()
{
	if (fSocket < 0)
		return kHostSocketError;
	if (fReady)
		return kHostSocketOK;
	if (!fTCPUp)
	{
		int result = HostSocketConnected(fSocket);
		if (result != kHostSocketOK)
		{
			if (result != kHostSocketWouldBlock)
				snprintf(fError, sizeof(fError), "the connection was refused");
			return result;
		}
		fTCPUp = true;
		if (!fTLS)
		{
			fReady = true;
			return kHostSocketOK;
		}
	}
	if (fSession == NULL)
	{
		snprintf(fError, sizeof(fError), "no TLS session");
		return kHostSocketError;
	}
	// the handshake, a step at a time
	for (int steps = 0; steps < 8; steps++)
	{
		int state = HostTLSHandshake(fSession);
		int pumped = Pump();
		if (pumped != kHostSocketOK)
		{
			snprintf(fError, sizeof(fError), "the TLS connection failed");
			return pumped;
		}
		if (state < 0)
		{
			snprintf(fError, sizeof(fError), "the TLS handshake failed: %s", HostTLSError(fSession));
			return kHostSocketError;
		}
		if (state == 1)
		{
			fReady = true;
			fSystemTrusts = HostTLSSystemTrusts(fSession) != 0;
			HostTLSFingerprint(fSession, fFingerprint);
			return kHostSocketOK;
		}
		if (fPeerClosed)
		{
			snprintf(fError, sizeof(fError), "the printer closed the connection during the TLS handshake");
			return kHostSocketClosed;
		}
	}
	return kHostSocketWouldBlock;
}


int
THostIPPSocket::Send(const void* data, size_t size, size_t* count)
{
	*count = 0;
	if (!fTLS)
		return HostSocketSend(fSocket, data, size, count);
	if (!fReady)
		return kHostSocketWouldBlock;
	// (the whole of it taken, and sent as the socket allows)
	if (fOutSize - fOutDone > 0x10000)
	{
		int result = Flush();
		if (result != kHostSocketOK)
			return result;
	}
	if (HostTLSWrite(fSession, (const unsigned char*) data, size) != 0)
	{
		snprintf(fError, sizeof(fError), "TLS: %s", HostTLSError(fSession));
		return kHostSocketError;
	}
	*count = size;
	int result = Flush();
	return (result == kHostSocketWouldBlock) ? kHostSocketOK : result;
}


int
THostIPPSocket::Receive(void* data, size_t size, size_t* count)
{
	*count = 0;
	if (!fTLS)
		return HostSocketReceive(fSocket, data, size, count);
	if (!fReady)
		return kHostSocketWouldBlock;
	int result = Pump();
	if (result != kHostSocketOK)
		return result;
	long n = HostTLSRead(fSession, (unsigned char*) data, size);
	if (n > 0)
	{
		*count = (size_t) n;
		return kHostSocketOK;
	}
	if (n < 0 || fPeerClosed)
		return kHostSocketClosed;
	return kHostSocketWouldBlock;
}
