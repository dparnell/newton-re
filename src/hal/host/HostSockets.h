/*
	File:		hal/host/HostSockets.h

	Contains:	The host's network, as the comm tools that stand in for the
				Newton Internet Enabler's reach it: TCP connections, a
				listening socket, and name resolution, over the host's own
				sockets and resolver.

				DEVIATION (owner's decision): the NIE carries its own TCP/IP
				stack as native ARM protocol parts; the host provides those
				parts' services itself over its own network stack instead
				(docs/comms/README.md).  This is the whole of the seam: a
				plain C interface over integer handles, so that the
				implementation (HostSockets.cpp: Winsock on Windows, BSD
				sockets elsewhere) is built without the Newton include paths,
				whose names collide with the platform headers'.

				Every call is non-blocking: a comm tool task polls, so that
				its task never sleeps in the host where the Newton scheduler
				cannot see it.  An address is an IPv4 address as the NIE
				writes it, a 32-bit number, most significant byte first
				(127.0.0.1 is 0x7F000001).
*/

#ifndef __HOSTSOCKETS_H
#define __HOSTSOCKETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// what the calls answer
enum
{
	kHostSocketOK = 0,
	kHostSocketWouldBlock = 1,		// nothing yet: ask again
	kHostSocketClosed = 2,			// the other end has closed the connection
	kHostSocketError = -1,			// anything else (HostSocketLastError says what)
	kHostSocketRefused = -2,		// connection refused
	kHostSocketUnreachable = -3,	// network or host unreachable
	kHostSocketNotFound = -4		// no such name
};

int		HostSocketsInit(void);
int		HostSocketLastError(void);

// Start a TCP connection to address:port; *handle is the socket.
// HostSocketConnected then answers kHostSocketWouldBlock until it is made,
// kHostSocketOK when it is, or the error.
int		HostTCPConnect(uint32_t address, uint16_t port, int* handle);
int		HostSocketConnected(int handle);

// Listen for TCP connections on port (0: any; *port says which) at address
// (0: any); take one with HostTCPAccept (kHostSocketWouldBlock: none yet).
int		HostTCPListen(uint32_t address, uint16_t* port, int* handle);
int		HostTCPAccept(int listener, int* handle, uint32_t* address, uint16_t* port);

// Send and receive as much as can be done without waiting; *count is how
// much was.  A receive answers kHostSocketClosed when the other end has
// closed and nothing is left to read.
int		HostSocketSend(int handle, const void* data, size_t size, size_t* count);
int		HostSocketReceive(int handle, void* data, size_t size, size_t* count);

// The addresses at each end of a connection.
int		HostSocketAddresses(int handle, uint32_t* localAddress, uint16_t* localPort, uint32_t* remoteAddress, uint16_t* remotePort);

// Shut the sending side (an orderly release), and close.
int		HostSocketShutdown(int handle);
int		HostSocketClose(int handle);
void	HostSocketsCloseAll(void);		// every socket, listeners too: the run is over

// A name's IPv4 addresses, as many as fit (*count says how many there were);
// a dotted address is answered as itself.  (This one blocks, as a resolver
// does.)
int		HostResolveName(const char* name, uint32_t* addresses, int maxCount, int* count);

// An IPv4 address's name (the reverse lookup), NUL-terminated in name.
// (This one blocks too.)
int		HostResolveAddress(uint32_t address, char* name, size_t size);

#ifdef __cplusplus
}
#endif

#endif	/* __HOSTSOCKETS_H */
