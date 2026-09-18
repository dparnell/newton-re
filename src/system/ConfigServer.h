/*
	File:		system/ConfigServer.h

	Contains:	TNSConfigServer - protoConfigServer's C++ side.

				A script that wants to say which piece of hardware a
				communications service should use makes a protoConfigServer
				and instantiates it with a service type (four characters,
				'mdem or 'srlx and the like) and the name of a configuration.
				The object it gets keeps those two, and its GetConfig and
				SetConfig ask the name server for the default configuration
				registered for that service, through TUConfigServer
				(ddk/HALOptions.h).

				The C++ object hangs off the script's frame in a ciPrivate
				slot, as the ROM's "magic" objects do.

	NOT YET RECONSTRUCTED: GetConfig 0x0013c7f4 and SetConfig 0x0013c888,
	which go through TUConfigServer::GetDefaultConfig/SetDefaultConfig -
	the name server's configuration registry, which nothing registers
	anything in yet.  Instantiating is what the boot needs, and that is
	what is here.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#ifndef __CONFIGSERVER_H
#define __CONFIGSERVER_H

#include "objects.h"
#include "NameServer.h"


// The ROM derives it from TUConfigServer (ddk/HALOptions.h), which adds
// no instance variables to TUNameServer and only the GetDefaultConfig /
// SetDefaultConfig calls that GetConfig and SetConfig - NOT YET - would
// make; the layout is the same either way, and HALOptions.h drags in
// SerialOptions.h, whose four-character constants this compiler will not
// take.
class TNSConfigServer : public TUNameServer
{
public:
					TNSConfigServer();
					~TNSConfigServer();

	NewtonErr		InitConfigServer(RefArg serviceType, RefArg configName);

	char*			fName;			// +0x10  the configuration's name, as a C string
	ULong			fServiceType;	// +0x14  the service's four characters
};									// 0x18 bytes


Ref		CSInstantiate(RefArg rcvr, RefArg serviceType, RefArg configName);

void	RegisterConfigServerNatives(void);

#endif	/* __CONFIGSERVER_H */
