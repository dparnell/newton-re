/*
	File:		comms/host/HostAppIcon.h

	Contains:	A host application's icon taken out of the Extras drawer
				again: the built-in host applications that want hardware
				(Messages the cellular module, Mesh the LoRa radio) put
				their icon in the Extras soup when they start, and a store
				keeps it - so a newton started on that store without the
				hardware (a desktop without --cell-port or --mesh-air, a
				reMarkable) takes it out, and the application is nowhere
				to be seen (the owner's ask, 2026-10-13).

	Host code (no ROM counterpart).
*/

#ifndef __COMMS_HOSTAPPICON_H
#define __COMMS_HOSTAPPICON_H

// every Extras entry whose app is appSymbol ("Mesh:host") removed; called
// once the NewtonScript world is up (a PreMain hook's)
void	HostRemoveAppIcon(const char* appSymbol);

// the Extras drawer's Calls icon, which the Phone application
// (HostPhone.ns, TakeIcon) takes where there is a cellular module, given
// back to the ROM's Calls application where there is none
void	HostRestoreCallsIcon(void);

#endif
