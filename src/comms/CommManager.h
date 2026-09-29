/*
	File:		comms/CommManager.h

	Contains:	The comm manager: the 'cmgr' task every endpoint goes to for
				a service, and the TCMService protocol services are.

				CMStartService sends the comm manager ('comg' events to the
				'cmgr' port, CMSendMessage) the options an endpoint was
				given; its TCMEventHandler finds the first service option not
				yet processed and asks the protocol registry for the
				TCMService implementation whose 'serv' capability is that
				service's four characters (a 'sid ' option naming a port
				short-circuits it: the service is already running there).
				The service's Start starts its comm tool (StartCommTool) and
				opens it (OpenCommTool) - an asynchronous RPC whose reply
				comes back to the comm manager's port, where
				AECompletionProc finds the TAsyncServiceMessage it belongs to
				and the TStartInfo waiting on that service, lets the service
				look at the reply (DoneStarting), and answers the original
				request with the tool's port (TStartInfo::Complete).

				The world also keeps the last device the machine was
				connected to (CMGetLastDevice/CMSetLastDevice) and the last
				package the docking loader loaded.

				NOT YET RECONSTRUCTED: the docking package loader
				(TSCPLoader, SCPLoad - the 'scpl task the power-on and
				app-alive events start, which loads the package a connected
				device asks for), the TICHandler the event handler notifies
				the Newt world through (event 9), InitializeCommHardware
				(the serial ports), and all of RegisterROMProtcols' services
				but the ones reconstructed (the host's own - comms/host/ -
				stand in for the NIE's 'inet, 'ictl and 'dnst services).

	Reconstructed from the MP2x00 US ROM (0x00049448-0x000495e4,
	0x0006b5c0-0x0006ccc4, 0x00382998-0x003829f8); each function cites its
	origin.  docs/comms/README.md.
*/

#ifndef __COMMS_COMMMANAGER_H
#define __COMMS_COMMMANAGER_H

#ifndef __CMSERVICE_H
#include "CMService.h"
#endif
#ifndef __COMMMANAGERINTERFACE_H
#include "CommManagerInterface.h"
#endif
#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __AEVENTHANDLER_H
#include "AEventHandler.h"
#endif
#ifndef __LIST_H
#include "List.h"
#endif
#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif

class TEndpoint;

// the comm manager's events ('comg in the 'newt class) and what they are for
#define kCommManagerId				'cmgr'
#define kCMEventId					'comg'

enum
{
	kCMStartServiceOutside = 1,		// the options are a shared-memory object
	kCMStartService,				// the options are a pointer
	kCMGetLastDevice = 4,
	kCMSetLastDevice,
	kCMSCPLoad,
	kCMSetLastPackage,
	kCMGetLastPackage,
	kCMNotify						// the Newt world told something (TICHandler, NOT YET)
};


// A comm manager event: the TAEvent, what is asked, and the answer.
class TCMEvent : public TAEvent			// 0x10 bytes
{
public:
						TCMEvent()		{ fAEventID = kCMEventId; fEvent = 0; fEventError = noErr; }

	ULong				fEvent;			// +0x08
	NewtonErr			fEventError;	// +0x0c
};

// Start a service: the options (a pointer, or a shared-memory object of
// fCount options).
class TCMServiceEvent : public TCMEvent	// 0x18 bytes
{
public:
	TOptionArray*		fOptions;		// +0x10  (an object id for kCMStartServiceOutside)
	ULong				fCount;			// +0x14
};

// ...and the answer: where the service is.
class TCMServiceReply : public TCMEvent	// 0x1c bytes
{
public:
	TServiceInfo		fServiceInfo;	// +0x10
};

class TCMDeviceEvent : public TCMEvent	// 0x28 bytes
{
public:
	TConnectedDevice	fDevice;		// +0x10
};

class TCMPackageEvent : public TCMEvent	// 0x18 bytes
{
public:
	ULong				fPackageB;		// +0x10  (TCMWorld's +0xdc)
	ULong				fPackageA;		// +0x14  (TCMWorld's +0xe0)
};


// A service being started: the request waiting for its answer.
class TStartInfo : public SingleObject	// 0x30 bytes
{
public:
						TStartInfo();
						~TStartInfo();

	NewtonErr			Init(TUMsgToken* token, TCMEvent* event);
	void				Complete(NewtonErr result);

	Boolean				fOptionsAllocated;	// +0x00  fOptions is a copy of the client's
	TOptionArray*		fOptions;			// +0x04
	TUMsgToken			fToken;				// +0x08  the request
	TCMService*			fService;			// +0x18
	TServiceInfo		fServiceInfo;		// +0x1c
	NewtonErr			fResult;			// +0x28
	TCMOServiceIdentifier*	fServiceIdOption;	// +0x2c  the request's 'sid ' option, if it had one
};


// The comm manager's event handler: 'comg events.
class TCMEventHandler : public TAEventHandler	// 0x20 bytes
{
public:
						TCMEventHandler();

	NewtonErr			Init(ULong eventId, ULong eventClass);

	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);

	NewtonErr			StartService(TUMsgToken* token, TCMEvent* event);
	NewtonErr			GetLastDevice(TCMEvent* event);
	NewtonErr			SetLastDevice(TCMEvent* event);
	NewtonErr			GetLastPackage(TCMEvent* event);
	NewtonErr			SetLastPackage(TCMEvent* event);

	void*				fICHandler;			// +0x14  the Newt world's notifier (TICHandler, NOT YET)
	ULong				fICState;			// +0x18
	Boolean				fNotifyPending;		// +0x1c
};


// Power on, power off and app-alive: the docking loader is started.
class TCMSystemEventHandler : public TSystemEventHandler
{
public:
	virtual void		PowerOn(TAEvent* event);
	virtual void		PowerOff(TAEvent* event);
	virtual void		AppAlive(TAEvent* event);
};


// The comm manager's world ('cmgr), 0xe4 bytes.
class TCMWorld : public TAppWorld
{
public:
						TCMWorld();

	virtual ULong		GetSizeOf();
	virtual long		MainConstructor();
	virtual void		MainDestructor();

	NewtonErr			SCPCheck(ULong reason);
	NewtonErr			SCPLoad(ULong waitPeriod, ULong tries, ULong filter, TUMsgToken* token, ULong reason);
	TAsyncServiceMessage*	MatchPendingServiceMessage(TUMsgToken* token);
	TStartInfo*			MatchPendingStartInfo(TCMService* service);
	void				SetDevice(TConnectedDevice* device);
	void				SetLastPackage(ULong a, ULong b);

	TCMEventHandler		fEventHandler;		// +0x70
	CList				fServiceMessages;	// +0x90  the TAsyncServiceMessages in flight
	CList				fStartInfos;		// +0xa8  the TStartInfos waiting
	TConnectedDevice	fLastDevice;		// +0xc0
	void*				fSCPMessage;		// +0xd8  the docking loader's request in flight (TCMSCPAsyncMessage, NOT YET)
	ULong				fLastPackageB;		// +0xdc
	ULong				fLastPackageA;		// +0xe0
};


NewtonErr	InitializeCommManager(void);
NewtonErr	GetCommManagerPort(TUPort* port);
NewtonErr	GetOSPortFromName(ULong name, TUPort* port);
NewtonErr	CMSendMessage(TCMEvent* message, ULong messageSize, TCMEvent* reply, ULong replySize);
NewtonErr	CMSetLastDevice(TConnectedDevice* lastDevice);
NewtonErr	CMSCPLoad(ULong waitPeriod, ULong tries, ULong filter);
void		CMSCPSetLastLoadedPackage(ULong a, ULong b);
void		CMSCPGetLastLoadedPackage(ULong* a, ULong* b);

// A comm tool opened asynchronously for a service (CommTools.h's
// StartCommTool starts it).
NewtonErr	OpenCommTool(TObjectId portId, TOptionArray* options, TCMService* service);

#endif	/* __COMMS_COMMMANAGER_H */
