/*
	File:		host/HostSettings.h

	Contains:	The Host preferences panel: the settings of the machine
				newton runs on, changed while it runs from the Newton's own
				Preferences (HostSettings.ns, docs/host-settings.md).

				A setting is something the host can switch without a
				restart:
				  lanBeam      beaming over the network - the built-in IR
				               port's medium the LAN group (--ir-lan) or
				               nobody/the --ir-peer it was started with
				               (hal/host/HostIRChip.h's HostIRChipSetPeer)
				  penInk       the reMarkable panel's pen waveform for ink
				  touch        a finger as the pen (reMarkable)
				  clearGhosts  a flashing redraw of the panel (reMarkable; a
				               button, not kept)
				the last three only where the window has them
				(host/HostWindow.h's HostWindowOption).  The NewtonScript
				side sees HostSettingsList() - [{setting, label, kind,
				value}] - and HostSetSetting(setting, value) ==> whether it
				was taken.  The panel keeps what is changed in the System
				soup and gives it back at the next boot.
*/

#ifndef __HOSTSETTINGS_H
#define __HOSTSETTINGS_H

void	HostSettingsSetBeamPeers(const char* lan, const char* other);	// before boot: the LAN medium's spec ("lan[:PORT][@ADDRESS]") and the medium beaming over the network off is (the --ir-peer, or nil)
void	HostInstallSettings(void);		// in the newt world, once its globals are built: the natives, HostSettings:host, the panel

#endif	/* __HOSTSETTINGS_H */
