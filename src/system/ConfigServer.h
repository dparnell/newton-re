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

	The registry underneath is the name server's own: a service's default
	configuration is registered under the service's four characters
	(TUConfigServer, user/UserConfigServer.cpp).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __CONFIGSERVER_H
#define __CONFIGSERVER_H

#include "objects.h"
#include "HALOptions.h"


class TNSConfigServer : public TUConfigServer
{
public:
					TNSConfigServer();
					~TNSConfigServer();

	NewtonErr		InitConfigServer(RefArg serviceType, RefArg configName);
	Ref				GetConfig(NewtonErr* err);
	NewtonErr		SetConfig(RefArg config);

	char*			fName;			// +0x10  the configuration's name, as a C string
	ULong			fServiceType;	// +0x14  the service's four characters
};									// 0x18 bytes


Ref		CSInstantiate(RefArg rcvr, RefArg serviceType, RefArg configName);
Ref		CSGetDefaultConfig(RefArg rcvr);
Ref		CSSetDefaultConfig(RefArg rcvr, RefArg config);
Ref		CSDispose(RefArg rcvr);
TNSConfigServer*	GetClient(RefArg rcvr);		// ROM 0x000ac358 GetClient__FRC6RefVar

void	RegisterConfigServerNatives(void);

#endif	/* __CONFIGSERVER_H */
