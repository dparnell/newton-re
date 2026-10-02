/*
	File:		hal/host/HostSockets.cpp

	Contains:	The host's network (HostSockets.h) over Winsock on Windows
				and BSD sockets elsewhere.  Built without the Newton include
				paths (src/hal/CMakeLists.txt, hal_host_sockets).

	Host code: no ROM counterpart (the NIE's TCP/IP stack is what it stands
	in for - docs/comms/README.md).
*/

#include "HostSockets.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET HostSocket;
#define CLOSE_SOCKET closesocket
#define LAST_ERROR() WSAGetLastError()
#define ERR_WOULDBLOCK WSAEWOULDBLOCK
#define ERR_INPROGRESS WSAEWOULDBLOCK
#define ERR_REFUSED WSAECONNREFUSED
#define ERR_UNREACH WSAENETUNREACH
#define ERR_HOSTUNREACH WSAEHOSTUNREACH
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ifaddrs.h>
#include <net/if.h>
typedef int HostSocket;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET close
#define LAST_ERROR() errno
#define ERR_WOULDBLOCK EWOULDBLOCK
#define ERR_INPROGRESS EINPROGRESS
#define ERR_REFUSED ECONNREFUSED
#define ERR_UNREACH ENETUNREACH
#define ERR_HOSTUNREACH EHOSTUNREACH
#endif


// The handles are indices into a table of the host's sockets, so that a
// Windows SOCKET (pointer-sized) fits the interface's int.
enum { kMaxSockets = 64 };
static HostSocket	sSockets[kMaxSockets];
static int			sInitialised = 0;
static int			sLastError = 0;


static int
NewHandle(HostSocket s)
{
	for (int i = 0; i < kMaxSockets; i++)
		if (sSockets[i] == INVALID_SOCKET)
		{
			sSockets[i] = s;
			return i;
		}
	return -1;
}


static HostSocket
SocketOf(int handle)
{
	if (handle < 0 || handle >= kMaxSockets)
		return INVALID_SOCKET;
	return sSockets[handle];
}


static int
ErrorResult(int error)
{
	sLastError = error;
	if (error == ERR_REFUSED)
		return kHostSocketRefused;
	if (error == ERR_UNREACH || error == ERR_HOSTUNREACH)
		return kHostSocketUnreachable;
	return kHostSocketError;
}


static int
SetNonBlocking(HostSocket s)
{
#ifdef _WIN32
	u_long on = 1;
	return ioctlsocket(s, FIONBIO, &on) == 0 ? 0 : -1;
#else
	int flags = fcntl(s, F_GETFL, 0);
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}


static void
MakeAddress(struct sockaddr_in* sa, uint32_t address, uint16_t port)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_addr.s_addr = htonl(address);
	sa->sin_port = htons(port);
}


int
HostSocketsInit(void)
{
	if (sInitialised)
		return kHostSocketOK;
#ifdef _WIN32
	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		return kHostSocketError;
#else
	// a send to a connection the other end has reset answers EPIPE rather
	// than ending the program with SIGPIPE (a POSIX host's default) - here
	// and in the TLS library writing to the same sockets
	signal(SIGPIPE, SIG_IGN);
#endif
	for (int i = 0; i < kMaxSockets; i++)
		sSockets[i] = INVALID_SOCKET;
	sInitialised = 1;
	return kHostSocketOK;
}


int
HostSocketLastError(void)
{
	return sLastError;
}


int
HostTCPConnect(uint32_t address, uint16_t port, int* handle)
{
	HostSocketsInit();
	HostSocket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET)
		return ErrorResult(LAST_ERROR());
	int one = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*) &one, sizeof(one));
	SetNonBlocking(s);
	struct sockaddr_in sa;
	MakeAddress(&sa, address, port);
	if (connect(s, (struct sockaddr*) &sa, sizeof(sa)) == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		if (error != ERR_INPROGRESS && error != ERR_WOULDBLOCK)
		{
			CLOSE_SOCKET(s);
			return ErrorResult(error);
		}
	}
	int h = NewHandle(s);
	if (h < 0)
	{
		CLOSE_SOCKET(s);
		return kHostSocketError;
	}
	*handle = h;
	return kHostSocketOK;
}


int
HostSocketConnected(int handle)
{
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	fd_set writable, failed;
	FD_ZERO(&writable);
	FD_ZERO(&failed);
	FD_SET(s, &writable);
	FD_SET(s, &failed);
	struct timeval now = { 0, 0 };
	int n = select((int) s + 1, NULL, &writable, &failed, &now);
	if (n == SOCKET_ERROR)
		return ErrorResult(LAST_ERROR());
	if (n == 0)
		return kHostSocketWouldBlock;
	int error = 0;
	socklen_t length = sizeof(error);
	getsockopt(s, SOL_SOCKET, SO_ERROR, (char*) &error, &length);
	if (error != 0 || FD_ISSET(s, &failed))
		return ErrorResult(error != 0 ? error : ERR_REFUSED);
	return kHostSocketOK;
}


int
HostTCPListen(uint32_t address, uint16_t* port, int* handle)
{
	HostSocketsInit();
	HostSocket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET)
		return ErrorResult(LAST_ERROR());
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*) &one, sizeof(one));
	struct sockaddr_in sa;
	MakeAddress(&sa, address, *port);
	if (bind(s, (struct sockaddr*) &sa, sizeof(sa)) == SOCKET_ERROR
	||  listen(s, 4) == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		CLOSE_SOCKET(s);
		return ErrorResult(error);
	}
	socklen_t length = sizeof(sa);
	getsockname(s, (struct sockaddr*) &sa, &length);
	*port = ntohs(sa.sin_port);
	SetNonBlocking(s);
	int h = NewHandle(s);
	if (h < 0)
	{
		CLOSE_SOCKET(s);
		return kHostSocketError;
	}
	*handle = h;
	return kHostSocketOK;
}


int
HostTCPAccept(int listener, int* handle, uint32_t* address, uint16_t* port)
{
	HostSocket l = SocketOf(listener);
	if (l == INVALID_SOCKET)
		return kHostSocketError;
	struct sockaddr_in sa;
	socklen_t length = sizeof(sa);
	HostSocket s = accept(l, (struct sockaddr*) &sa, &length);
	if (s == INVALID_SOCKET)
	{
		int error = LAST_ERROR();
		if (error == ERR_WOULDBLOCK)
			return kHostSocketWouldBlock;
		return ErrorResult(error);
	}
	SetNonBlocking(s);
	int one = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*) &one, sizeof(one));
	int h = NewHandle(s);
	if (h < 0)
	{
		CLOSE_SOCKET(s);
		return kHostSocketError;
	}
	*handle = h;
	if (address)
		*address = ntohl(sa.sin_addr.s_addr);
	if (port)
		*port = ntohs(sa.sin_port);
	return kHostSocketOK;
}


int
HostSocketSend(int handle, const void* data, size_t size, size_t* count)
{
	*count = 0;
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	if (size == 0)
		return kHostSocketOK;
#ifdef MSG_NOSIGNAL
	int n = send(s, (const char*) data, (int) size, MSG_NOSIGNAL);
#else
	int n = send(s, (const char*) data, (int) size, 0);
#endif
	if (n == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		if (error == ERR_WOULDBLOCK)
			return kHostSocketWouldBlock;
		return ErrorResult(error);
	}
	*count = n;
	return kHostSocketOK;
}


int
HostSocketReceive(int handle, void* data, size_t size, size_t* count)
{
	*count = 0;
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	if (size == 0)
		return kHostSocketOK;
	int n = recv(s, (char*) data, (int) size, 0);
	if (n == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		if (error == ERR_WOULDBLOCK)
			return kHostSocketWouldBlock;
		return ErrorResult(error);
	}
	if (n == 0)
		return kHostSocketClosed;
	*count = n;
	return kHostSocketOK;
}


int
HostSocketAddresses(int handle, uint32_t* localAddress, uint16_t* localPort, uint32_t* remoteAddress, uint16_t* remotePort)
{
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	struct sockaddr_in sa;
	socklen_t length = sizeof(sa);
	if (getsockname(s, (struct sockaddr*) &sa, &length) == 0)
	{
		if (localAddress)
			*localAddress = ntohl(sa.sin_addr.s_addr);
		if (localPort)
			*localPort = ntohs(sa.sin_port);
	}
	length = sizeof(sa);
	if (getpeername(s, (struct sockaddr*) &sa, &length) == 0)
	{
		if (remoteAddress)
			*remoteAddress = ntohl(sa.sin_addr.s_addr);
		if (remotePort)
			*remotePort = ntohs(sa.sin_port);
	}
	return kHostSocketOK;
}


int
HostSocketShutdown(int handle)
{
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
#ifdef _WIN32
	shutdown(s, SD_SEND);
#else
	shutdown(s, SHUT_WR);
#endif
	return kHostSocketOK;
}


// Every socket closed, the listeners with them: the run is over (a
// restarted newton - host/HostRestart.h - listens on the same ports)
void
HostSocketsCloseAll(void)
{
	for (int i = 0; i < kMaxSockets; i++)
		if (sInitialised && sSockets[i] != INVALID_SOCKET)
		{
			CLOSE_SOCKET(sSockets[i]);
			sSockets[i] = INVALID_SOCKET;
		}
}


int
HostSocketClose(int handle)
{
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	CLOSE_SOCKET(s);
	sSockets[handle] = INVALID_SOCKET;
	return kHostSocketOK;
}


/*------------------------------------------------------------------------------
	UDP over a multicast group (the host IR port's LAN medium)
------------------------------------------------------------------------------*/

int
HostInterfaceAddresses(uint32_t* addresses, int maxCount, int* count)
{
	HostSocketsInit();
	int n = 0;
#ifdef _WIN32
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
		return ErrorResult(LAST_ERROR());
	INTERFACE_INFO list[32];
	DWORD bytes = 0;
	if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, NULL, 0, list, sizeof(list), &bytes, NULL, NULL) == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		closesocket(s);
		return ErrorResult(error);
	}
	closesocket(s);
	for (DWORD i = 0; i < bytes / sizeof(INTERFACE_INFO); i++)
	{
		u_long flags = list[i].iiFlags;
		if ((flags & IFF_UP) == 0 || (flags & (IFF_MULTICAST | IFF_LOOPBACK)) == 0)
			continue;
		if (list[i].iiAddress.Address.sa_family != AF_INET)
			continue;
		if (n < maxCount)
			addresses[n] = ntohl(list[i].iiAddress.AddressIn.sin_addr.s_addr);
		n++;
	}
#else
	struct ifaddrs* all = NULL;
	if (getifaddrs(&all) != 0)
		return ErrorResult(LAST_ERROR());
	for (struct ifaddrs* ifa = all; ifa != NULL; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & (IFF_MULTICAST | IFF_LOOPBACK)) == 0)
			continue;
		if (n < maxCount)
			addresses[n] = ntohl(((struct sockaddr_in*) ifa->ifa_addr)->sin_addr.s_addr);
		n++;
	}
	freeifaddrs(all);
#endif
	*count = n < maxCount ? n : maxCount;
	return kHostSocketOK;
}


int
HostUDPMulticastOpen(uint32_t group, uint16_t port, const uint32_t* interfaces, int count, int* handle, int* joined)
{
	HostSocketsInit();
	*joined = 0;
	HostSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
		return ErrorResult(LAST_ERROR());
	// every newton on the host binds the same port and each hears every
	// datagram (a multicast is delivered to all of them)
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*) &one, sizeof(one));
#ifdef SO_REUSEPORT
	setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*) &one, sizeof(one));
#endif
	struct sockaddr_in sa;
#ifdef _WIN32
	// Windows hands a multicast to a socket bound to the interface it
	// arrived on, so one interface asked for is bound to: a newton kept to
	// the loopback interface listens on nothing the firewall asks about
	MakeAddress(&sa, count == 1 ? interfaces[0] : 0, port);
#else
	// elsewhere a socket bound to an interface's address hears only what is
	// sent to that address, never the group's
	MakeAddress(&sa, 0, port);
#endif
	if (bind(s, (struct sockaddr*) &sa, sizeof(sa)) == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		CLOSE_SOCKET(s);
		return ErrorResult(error);
	}
	struct ip_mreq request;
	memset(&request, 0, sizeof(request));
	request.imr_multiaddr.s_addr = htonl(group);
	if (count == 0)
	{
		request.imr_interface.s_addr = htonl(INADDR_ANY);
		if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*) &request, sizeof(request)) == 0)
			(*joined)++;
	}
	for (int i = 0; i < count; i++)
	{
		request.imr_interface.s_addr = htonl(interfaces[i]);
		if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*) &request, sizeof(request)) == 0)
			(*joined)++;
	}
	if (*joined == 0)
	{
		int error = LAST_ERROR();
		CLOSE_SOCKET(s);
		return ErrorResult(error);
	}
#ifdef _WIN32
	DWORD ttl = 1, loop = 1;
#else
	unsigned char ttl = 1, loop = 1;
#endif
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*) &ttl, sizeof(ttl));
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*) &loop, sizeof(loop));
	SetNonBlocking(s);
	int h = NewHandle(s);
	if (h < 0)
	{
		CLOSE_SOCKET(s);
		return kHostSocketError;
	}
	*handle = h;
	return kHostSocketOK;
}


int
HostUDPMulticastSend(int handle, uint32_t group, uint16_t port, uint32_t interfaceAddress, const void* data, size_t size)
{
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	struct in_addr from;
	from.s_addr = htonl(interfaceAddress);
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*) &from, sizeof(from));
	struct sockaddr_in sa;
	MakeAddress(&sa, group, port);
#ifdef MSG_NOSIGNAL
	int n = sendto(s, (const char*) data, (int) size, MSG_NOSIGNAL, (struct sockaddr*) &sa, sizeof(sa));
#else
	int n = sendto(s, (const char*) data, (int) size, 0, (struct sockaddr*) &sa, sizeof(sa));
#endif
	if (n == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
		if (error == ERR_WOULDBLOCK)
			return kHostSocketWouldBlock;
		return ErrorResult(error);
	}
	return kHostSocketOK;
}


int
HostUDPReceive(int handle, void* data, size_t size, size_t* count, uint32_t* from)
{
	*count = 0;
	HostSocket s = SocketOf(handle);
	if (s == INVALID_SOCKET)
		return kHostSocketError;
	struct sockaddr_in sa;
	socklen_t length = sizeof(sa);
	int n = recvfrom(s, (char*) data, (int) size, 0, (struct sockaddr*) &sa, &length);
	if (n == SOCKET_ERROR)
	{
		int error = LAST_ERROR();
#ifdef _WIN32
		// a datagram longer than the buffer (cut short), or the answer to
		// an earlier send that found no one (ICMP port unreachable): not
		// this one's business - nothing taken
		if (error == WSAEMSGSIZE || error == WSAECONNRESET)
			return kHostSocketOK;
#endif
		if (error == ERR_WOULDBLOCK)
			return kHostSocketWouldBlock;
		return ErrorResult(error);
	}
	*count = n;
	if (from)
		*from = ntohl(sa.sin_addr.s_addr);
	return kHostSocketOK;
}


int
HostResolveName(const char* name, uint32_t* addresses, int maxCount, int* count)
{
	HostSocketsInit();
	*count = 0;
	struct addrinfo hints, *result = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(name, NULL, &hints, &result) != 0 || result == NULL)
		return kHostSocketNotFound;
	int n = 0;
	for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next)
	{
		if (ai->ai_family != AF_INET)
			continue;
		if (n < maxCount)
			addresses[n] = ntohl(((struct sockaddr_in*) ai->ai_addr)->sin_addr.s_addr);
		n++;
	}
	freeaddrinfo(result);
	*count = n;
	return n > 0 ? kHostSocketOK : kHostSocketNotFound;
}


int
HostResolveAddress(uint32_t address, char* name, size_t size)
{
	HostSocketsInit();
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(address);
	if (size == 0 || getnameinfo((struct sockaddr*) &sa, sizeof(sa), name, (socklen_t) size, NULL, 0, NI_NAMEREQD) != 0)
		return kHostSocketNotFound;
	return kHostSocketOK;
}
