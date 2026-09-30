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

				The docking loader (SCPLoader.h) is started by SCPLoad for
				a load a client asks for (CMSCPLoad) or the interconnect
				port's notification (SCPCheck), through a hook
				(gStartSCPLoader, DEVIATION: it is in comms_dock).

				NOT YET RECONSTRUCTED: the TICHandler the event handler
				notifies the Newt world through (event 9) - the
				interconnect port's pin -, InitializeCommHardware (the
				serial ports), and RegisterROMProtcols' services that are
				not reconstructed (the host's own - comms/host/ - stand in
				for the NIE's 'inet, 'ictl and 'dnst services).  The ones
				that are come from the libraries above this one, each
				library's registration put here by the program before the
				comm manager starts (CMAddROMServices, DEVIATION).

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


// the docking loader's request (0x20 bytes in the ROM)
class TCMSCPLoadEvent : public TCMEvent
{
public:
	ULong				fTries;			// +0x10
	ULong				fWaitPeriod;	// +0x14
	ULong				fFilter;		// +0x18  the device type wanted ('****': any; 0: none)
	ULong				fReason;		// +0x1c  0x10 load, 0x20 look for a keyboard (bit 0: powered on)
};

// The comm manager's request to the docking loader: an asynchronous
// message whose answer comes back to the comm manager's port, and the
// token of the request it answers in turn (100 bytes in the ROM).
class TCMSCPAsyncMessage : public TUAsyncMessage
{
public:
						TCMSCPAsyncMessage();

	NewtonErr			Init(TObjectId port, TAEventHandler* handler);
	NewtonErr			SendRPC(TUPort* port);
	void				SetToken(TUMsgToken* token);
	NewtonErr			ReplyRPC(void);

	TCMSCPLoadEvent		fRequest;		// +0x10
	TCMSCPLoadEvent		fReply;			// +0x30
	Boolean				fHasToken;		// +0x50
	TUMsgToken			fToken;			// +0x54
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
	TCMSCPAsyncMessage*	fSCPMessage;	// +0xd8  the docking loader's request in flight
	ULong				fLastPackageB;		// +0xdc
	ULong				fLastPackageA;		// +0xe0
};


// a library's registration of its ROM services, made when the comm manager
// starts (RegisterROMProtcols); DEVIATION: the ROM makes them itself
typedef void (*CMROMServiceRegistrar)(void);
void		CMAddROMServices(CMROMServiceRegistrar registrar);

NewtonErr	InitializeCommManager(void);
NewtonErr	GetCommManagerPort(TUPort* port);
NewtonErr	GetOSPortFromName(ULong name, TUPort* port);
NewtonErr	CMSendMessage(TCMEvent* message, ULong messageSize, TCMEvent* reply, ULong replySize);
NewtonErr	CMSetLastDevice(TConnectedDevice* lastDevice);
NewtonErr	CMSCPLoad(ULong waitPeriod, ULong tries, ULong filter);
NewtonErr	CMSCPSetLastLoadedPackage(ULong a, ULong b);
NewtonErr	CMSCPGetLastLoadedPackage(ULong* a, ULong* b);
NewtonErr	GetSCPLoaderPort(TUPort* port);

// A comm tool opened asynchronously for a service (CommTools.h's
// StartCommTool starts it).
NewtonErr	OpenCommTool(TObjectId portId, TOptionArray* options, TCMService* service);

#endif	/* __COMMS_COMMMANAGER_H */
