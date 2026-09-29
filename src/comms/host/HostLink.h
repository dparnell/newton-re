/*
	File:		comms/host/HostLink.h

	Contains:	The host's link for the Newton Internet Enabler: HostLink.ns
				(compiled into the program) evaluated and kept in the global
				HostLink:host, which puts a "Host network" physical service,
				a "Host" link service and a "Host network" setup in the NIE's
				registries once the NIE is installed; with the host's 'ictl
				service (HostServices.h, HostLinkTool.h) that is what lets
				InetGrabLink connect over the host's own network.

				DEVIATION (owner's decision - docs/comms/README.md, "The
				link"): in place of the NIE's own link services and its
				native stack's link controller.

	Host code (no ROM counterpart).
*/

#ifndef __COMMS_HOSTLINK_H
#define __COMMS_HOSTLINK_H

// HostLink.ns evaluated, kept in HostLink:host and started (it watches for
// the NIE); a program calls it once the NewtonScript world is up (newton's
// PreMain hook).
void	HostLinkStart(void);

#endif
