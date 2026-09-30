/*
	File:		comms/SCPLoader.h

	Contains:	The docking loader: the 'scpl task the comm manager starts
				(TCMWorld::SCPLoad) to see what is plugged into a serial
				port and load the package that drives it.

				It opens the port through the framed serial service ('fser,
				9600 bps, 8N1) and waits for the device to say who it is -
				a connection-protocol message (CPMessages.h) with a 'd_id'
				tuple.  The device is recorded as the comm manager's last
				device (CMSetLastDevice) and announced as a 'dnot' system
				event; if the load asked for it (a device type, or '****'
				for any) and no service of that type is registered, it asks
				the device for its package: the Newton's id, a 'sire for a
				'pack, a 'csre offering five speeds, then (the speed changed
				as the device's 'csrp chose) a 'rese for the package the
				device's 'sirp describes, which the device sends as a 'pack
				tuple followed by the package itself.  That is stored on the
				internal store (StorePackage) and the newt world told of it
				(TSCPEvent, newt/SCPEvents.h: RegisterNewPackage) - after
				telling it to remove the package the previous device left,
				unless the device is the same one, when the new copy is
				deleted instead.  The load ends with an 'abrt 1.  Finally a
				service of the device's type with an 'auto capability is
				opened on the port and left running.

				A load tries the external port ('extr) and then, when no
				device answered there, the modem's ('mdem), each up to the
				number of tries asked for (at most five); the request's
				reason 0x20 has the platform check for a keyboard first
				(PowerOnDeviceCheck).  The task ends when it has answered.

				tools/dock/scpdevice.py is a device for the host's serial
				port.

				DEVIATION: the comm manager (library comms) starts the
				task through a hook RegisterSCPLoader sets (this library,
				comms_dock, is above it).

	Reconstructed from the MP2x00 US ROM (0x001b9d08-0x001baaf8); each
	function cites its origin.
*/

#ifndef __COMMS_SCPLOADER_H
#define __COMMS_SCPLOADER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __COMMS_CPMESSAGES_H
#include "CPMessages.h"
#endif
#ifndef __COMMMANAGERINTERFACE_H
#include "CommManagerInterface.h"
#endif

class TEndpoint;
class TEndpointPipe;
class TUAsyncMessage;
class TAESystemEvent;
class TSendSystemEvent;

#define kSCPLoaderName			'scpl'

class TSCPLoaderEventHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);
};

class TSCPLoader : public TAppWorld		// 0x9c bytes in the ROM
{
public:
					TSCPLoader();

	virtual ULong	GetSizeOf();
	virtual long	MainConstructor();
	virtual void	MainDestructor();

	NewtonErr		SCPLoad(ULong waitPeriod, ULong filter, ULong hwLocation);
	NewtonErr		SCPInit(ULong hwLocation);
	NewtonErr		Look(ULong waitPeriod, ULong filter);
	NewtonErr		GetPackage(void);
	NewtonErr		DeviceNotify(TConnectedDevice* device);
	void			DeviceNotifyCompletion(void);
	ULong			GetLastDevice(void);

	TSCPLoaderEventHandler*	fEventHandler;	// +0x70
	TEndpoint*		fEndpoint;			// +0x74  the framed serial endpoint
	TEndpointPipe*	fPipe;				// +0x78
	TCPReadMessage	fMessage;			// +0x7c
	ULong			fSpeed;				// +0x88  the port's speed
	TUAsyncMessage*	fNotifyMessage;		// +0x8c  the 'dnot' system event's
	TAESystemEvent*	fNotifyEvent;		// +0x90
	TSendSystemEvent*	fNotifySender;	// +0x94
	ULong			fDeviceType;		// +0x98  the device that answered (0: none)
};

// The device a 'dnot' system event announces (0x24 bytes in the ROM).
class TDeviceNotifyEvent : public TAESystemEvent
{
public:
	TConnectedDevice	fDevice;		// +0x0c
};

// ROM 0x00192924 PowerOnDeviceCheck__FUc
void		PowerOnDeviceCheck(Boolean poweredOn);

// The comm manager's hook set: its SCPLoad starts the task through it.
void		RegisterSCPLoader(void);

#endif	/* __COMMS_SCPLOADER_H */
