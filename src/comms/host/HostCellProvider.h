/*
	File:		comms/host/HostCellProvider.h

	Contains:	The host's cellular module made a telephone provider for
				the ROM's Phone transport: HostCell.ns (compiled into the
				program) evaluated, kept in the global HostCell:host and
				started - registered with RegCommConfig, its comm tool the
				'cell service (HostServices.h, HostCellTool.h).

				DEVIATION (host addition): the ROM registers no provider
				(docs/host-tdeck-pro-phone.md).

	Host code (no ROM counterpart).
*/

#ifndef __COMMS_HOSTCELLPROVIDER_H
#define __COMMS_HOSTCELLPROVIDER_H

// HostCell.ns (the telephone provider), HostText.ns (the text message
// transport), HostMessages.ns (the Messages application) and HostPhone.ns
// (the Phone application, in the Calls icon's place) evaluated and started
// when the host has a cellular module
// (hal/host/HostCellular.h's engine started); a program calls it once the
// NewtonScript world is up (its PreMain hook).
void	HostCellProviderStart(void);

#endif
