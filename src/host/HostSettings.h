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
				  docking      the serial port's TCP listener (--serial-port,
				               3679) open or closed (HostSerialChipSetListening)
				  waveform     the reMarkable's ink waveform: fast, pen, gray
				  directPen    the Marker read from its own device (reMarkable)
				  touch        a finger as the pen (reMarkable)
				  screenScale  the reMarkable's screen, the panel over 1-4,
				               when newton next starts (kept in "<store>.host";
				               the pen's calibration reset to the factory one
				               when the size changes)
				  clearGhosts  a flashing redraw of the panel (reMarkable; a
				               button, not kept)
				the reMarkable's only where the window has them
				(host/HostWindow.h's HostWindowOption).  The NewtonScript
				side sees HostSettingsList() - [{setting, label, kind,
				value}] - and HostSetSetting(setting, value) ==> whether it
				was taken.  The panel keeps what is changed in the System
				soup and gives it back at the next boot.
*/

#ifndef __HOSTSETTINGS_H
#define __HOSTSETTINGS_H

void	HostSettingsSetBeamPeers(const char* lan, const char* other);	// before boot: the LAN medium's spec ("lan[:PORT][@ADDRESS]") and the medium beaming over the network off is (the --ir-peer, or nil)
void	HostSettingsSetSerialPort(long port);	// before boot: docking over the network's port (--serial-port; -1 none: offered on 3679, off)
void	HostSettingsSetFile(const char* storePath);	// before boot: the settings for the next start kept in "<store>.host" (nil: none - no store file)
void	HostSettingsReadStartup(void);		// before the display is made: the screen size chosen (the window told, HostWindowSetOption "startScale")
bool	HostSettingsColourAtStart(void);	// before the display is made: whether the Host panel asked for the colour screen (qd/Colour.h)
void	HostSettingsNoteDisplay(long width, long height);	// the display made that size: the pen's calibration reset if its longer side changed since the last start
void	HostInstallSettings(void);		// in the newt world, once its globals are built: the natives, HostSettings:host, the panel

#endif	/* __HOSTSETTINGS_H */
