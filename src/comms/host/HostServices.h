/*
	File:		comms/host/HostServices.h

	Contains:	The comm manager services the host provides in place of the
				Newton Internet Enabler's (docs/comms/README.md, "Where the
				seam sits"): THostInetService, 'serv=inet - the TCP endpoint's
				service, which starts and opens a THostTCPTool the way the
				ROM's TAsyncService starts its serial tool.

				DEVIATION (owner's decision): the NIE's TInetService starts
				its own native TCP/IP stack; the host's starts a tool over the
				host's sockets.

				THostDNSService, 'serv=dnst - the NIE's name lookups, over a
				THostDNSTool (the host's resolver).

				NOT YET: 'ictl (the link controller).

	Host code for the NIE's services (no ROM counterpart; the shape is the
	ROM's TAsyncService, 0x0003b0c4-0x0003b14c).
*/

#ifndef __COMMS_HOSTSERVICES_H
#define __COMMS_HOSTSERVICES_H

#ifndef __CMSERVICE_H
#include "CMService.h"
#endif

PROTOCOL THostInetService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostInetService);

	THostInetService*	New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

PROTOCOL THostDNSService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(THostDNSService);

	THostDNSService*	New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

// the host's services put in the protocol registry (the kernel services
// task's job, the registry being a monitor)
void	RegisterHostCommServices(void);

#endif	/* __COMMS_HOSTSERVICES_H */
