/*
	File:		comms/host/HostEchoServer.h

	Contains:	A TCP echo server on 127.0.0.1, in a host thread of its own
				(it touches nothing of the Newton's) - what the NewtonScript
				endpoint's demo (src/host/demo/echo.ns, newton --tcp-echo
				port) connects to.  One connection at a time; every byte
				that comes is sent back.

	Host only: not part of the ROM.
*/

#ifndef __HOSTECHOSERVER_H
#define __HOSTECHOSERVER_H

#include <stdint.h>

// ==> the port listened on (port nought: one the host picks), or 0 when
// no server could be started
uint16_t	HostStartEchoServer(uint16_t port);
void		HostStopEchoServer(void);

#endif
