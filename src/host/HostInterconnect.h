/*
	File:		host/HostInterconnect.h

	Contains:	The host's hand at the serial port's interconnect pin, for
				scripts: HostInterconnect(state) tells the comm manager
				something was plugged into the external port (1) or taken
				out (0), as the port's interconnect handler would
				(TICHandler, NOT YET) - a plug-in has the docking loader
				ask the device on the port who it is and load its package
				(comms/SCPLoader.h; tools/dock/scpdevice.py is such a
				device, src/host/demo/scpload.ns the ctest's script).
*/

#ifndef __HOST_HOSTINTERCONNECT_H
#define __HOST_HOSTINTERCONNECT_H

void	HostRegisterInterconnectFunctions(void);

#endif	/* __HOST_HOSTINTERCONNECT_H */
