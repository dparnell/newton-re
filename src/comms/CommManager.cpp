/*
	File:		comms/CommManager.cpp

	Contains:	The comm manager (CommManager.h): TCMWorld, TCMEventHandler,
				TStartInfo, TAsyncServiceMessage, the TCMService glue, and the
				calls a client makes of it (CMStartService, CMSendMessage,
				the last device and the docking loader's messages).

	Reconstructed from the MP2x00 US ROM (0x00049448-0x000495e4,
	0x0006b5c0-0x0006ccc4, 0x00382998-0x003829f8); each function cites its
	origin.
*/

#include "CommManager.h"
#include "CommTools.h"
#include "SerialEndpoint.h"
#include "ListIterator.h"
#include "NameServer.h"
#include "NewtErrors.h"
#include "OSErrors.h"
#include "SharedTypes.h"
#include "UserSharedMem.h"

#include <string.h>
#include <stdlib.h>

// the comm manager's errors (kCMErrorBase = -26000)
#define kCMErr_AlreadyInitialized		(-26000)
#define kCMErr_UnknownEvent				(-26001)
#define kCMErr_NoServiceSpecified		(-26004)
#define kCMErr_ServiceNotFound			(-26005)
#define kCMErr_NoLastDevice				(-26008)
#define kCMErr_SCPLoadBusy				(-26012)
#define kCMErr_NoLastPackage			(-26015)
#define kCMErr_ServiceVersionNotFound	(-26002)

// the docking loader's starter (DEVIATION: SCPLoader.h's RegisterSCPLoader
// sets it)
NewtonErr		(*gStartSCPLoader)(void) = nil;

// the comm manager's task: its name, and its stack
#define kCMWorldStackSize				0x1400

extern Boolean	gSCPDevicePackageBusy;
Boolean			gSCPDevicePackageBusy = false;		// ROM 0x0c100b64 gSCPDevicePackageBusy


// ---------------------------------------------------------------------------
//	Starting the comm manager
// ---------------------------------------------------------------------------

// DEVIATION: the ROM's services are in the libraries above this one (the
// serial tools, MNP, the modem and fax tools, the IR tools), which this one
// cannot link; the program puts each library's registration here before the
// comm manager starts (CMAddROMServices), and RegisterROMProtcols makes them.
static CMROMServiceRegistrar	gROMServiceRegistrars[8];
static long						gROMServiceRegistrarCount = 0;

void
CMAddROMServices(CMROMServiceRegistrar registrar)
{
	for (long i = 0; i < gROMServiceRegistrarCount; i++)
		if (gROMServiceRegistrars[i] == registrar)
			return;
	if (gROMServiceRegistrarCount < (long) (sizeof(gROMServiceRegistrars) / sizeof(gROMServiceRegistrars[0])))
		gROMServiceRegistrars[gROMServiceRegistrarCount++] = registrar;
}


// ROM 0x0006ccac RegisterROMProtcols__Fv
// The ROM's services and endpoint: the ones reconstructed - TFaxService,
// TModemService, TMNPService, TAsyncService, TFramedAsyncService,
// TIrDAService, TIRService, IRProbeService - through the registrations the
// program put here, in the order it put them, then TSerialEndpoint.
// NOT YET RECONSTRUCTED: RegisterNetworkROMProtocols (0x00031b70; the NIE
// does the network, and the host's own services stand in for it -
// comms/host/HostServices.h), TP3Service, TLocalTalkService,
// TKeyboardService, TVRemoteService, IRSniffService and PMuxServiceStarter.
static NewtonErr
RegisterROMProtcols()
{
	for (long i = 0; i < gROMServiceRegistrarCount; i++)
		gROMServiceRegistrars[i]();
	TSerialEndpoint::ClassInfo()->Register();
	return noErr;
}


// ROM 0x0006c1ec InitializeCommManager__Fv
// Start the 'cmgr task, unless it is running already.  (NOT YET:
// InitializeCommHardware, 0x000ea0b4, the serial ports.)
NewtonErr
InitializeCommManager()
{
	TUPort port;
	NewtonErr err = GetCommManagerPort(&port);
	if (err == noErr)
		err = kCMErr_AlreadyInitialized;
	else
	{
		TCMWorld world;
		err = world.Init(kCommManagerId, true, kCMWorldStackSize);
		if (err == noErr)
			err = RegisterROMProtcols();
	}
	return err;
}


// ROM 0x0006b640 GetCommManagerPort__FP6TUPort
NewtonErr
GetCommManagerPort(TUPort* port)
{
	TUNameServer ns;
	TObjectId id = 0;
	ULong spec;
	NewtonErr err = ns.Lookup((char*) "cmgr", (char*) "TUPort", &id, &spec);
	port->CopyObject(id);
	return err;
}


// ROM 0x0006b5c8 GetOSPortFromName__FUlP6TUPort
// (The name's four characters in memory order, as the ROM's big-endian word
// leaves them.)
NewtonErr
GetOSPortFromName(ULong name, TUPort* port)
{
	char key[5];
	key[0] = name >> 24;
	key[1] = name >> 16;
	key[2] = name >> 8;
	key[3] = name;
	key[4] = 0;
	TUNameServer ns;
	TObjectId id = 0;
	ULong spec;
	NewtonErr err = ns.Lookup(key, (char*) "TUPort", &id, &spec);
	port->CopyObject(id);
	return err;
}


// ROM 0x0006b720 CMSendMessage__FP8TCMEventUlT1T2
// A request of the comm manager, synchronously; what it answers is the
// reply's error.
NewtonErr
CMSendMessage(TCMEvent* message, ULong messageSize, TCMEvent* reply, ULong replySize)
{
	TUPort port;
	NewtonErr err = GetCommManagerPort(&port);
	if (err == noErr)
	{
		ULong returnSize;
		err = port.SendRPC(&returnSize, message, messageSize, reply, replySize, 0, 0);
		if (err == noErr)
			err = reply->fEventError;
	}
	return err;
}


// ROM 0x0006b7cc CMStartService__FP12TOptionArrayP12TServiceInfo
NewtonErr
CMStartService(TOptionArray* options, TServiceInfo* serviceInfo)
{
	return CMStartServiceInternal(options, serviceInfo);
}


// ROM 0x0006b7d0 CMStartServiceInternal__FP12TOptionArrayP12TServiceInfo
// Ask the comm manager to start the service the options name; where it is
// comes back in serviceInfo.
NewtonErr
CMStartServiceInternal(TOptionArray* options, TServiceInfo* serviceInfo)
{
	TCMServiceEvent message;
	TCMServiceReply reply;
	message.fEvent = kCMStartService;
	message.fOptions = options;
	NewtonErr err = CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
	if (err == noErr)
		*serviceInfo = reply.fServiceInfo;
	return err;
}


// ROM 0x0006b94c CMGetLastDevice__FP16TConnectedDevice
NewtonErr
CMGetLastDevice(TConnectedDevice* lastDevice)
{
	TCMEvent message;
	TCMDeviceEvent reply;
	message.fEvent = kCMGetLastDevice;
	NewtonErr err = CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
	if (err == noErr)
		*lastDevice = reply.fDevice;
	return err;
}


// ROM 0x0006b9b4 CMSetLastDevice__FP16TConnectedDevice
NewtonErr
CMSetLastDevice(TConnectedDevice* lastDevice)
{
	TCMDeviceEvent message;
	TCMEvent reply;
	message.fEvent = kCMSetLastDevice;
	message.fDevice = *lastDevice;
	return CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
}


// ROM 0x0006ba10 CMSCPLoad__FUlN21
// (The ROM's reply buffer is a TAEvent array the request's size.)
NewtonErr
CMSCPLoad(ULong waitPeriod, ULong tries, ULong filter)
{
	TCMSCPLoadEvent message, reply;
	message.fEvent = kCMSCPLoad;
	message.fTries = tries;
	message.fWaitPeriod = waitPeriod;
	message.fFilter = filter;
	return CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
}


// ROM 0x0006b650 CMSCPSetLastLoadedPackage__FUlT1
// The package the docking loader loaded last (its store object) and the
// device it was for.
NewtonErr
CMSCPSetLastLoadedPackage(ULong a, ULong b)
{
	TCMPackageEvent message;
	TCMEvent reply;
	message.fEvent = kCMSetLastPackage;
	message.fPackageB = a;
	message.fPackageA = b;
	return CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
}


// ROM 0x0006b6ac CMSCPGetLastLoadedPackage__FPUlT1
// (The answer is read whether the request worked or not.)
NewtonErr
CMSCPGetLastLoadedPackage(ULong* a, ULong* b)
{
	TCMPackageEvent message, reply;
	message.fEvent = kCMGetLastPackage;
	NewtonErr err = CMSendMessage(&message, sizeof(message), &reply, sizeof(reply));
	*a = reply.fPackageB;
	*b = reply.fPackageA;
	return err;
}


// ROM 0x0006b710 GetSCPLoaderPort__FP6TUPort
NewtonErr
GetSCPLoaderPort(TUPort* port)
{
	TUNameServer ns;
	TObjectId id = 0;
	ULong spec;
	NewtonErr err = ns.Lookup((char*) "scpl", (char*) "TUPort", &id, &spec);
	port->CopyObject(id);
	return err;
}


// ROM 0x0006c6f8 CMGetServiceVersion__FUlPUl
// The version of a service ('vern capability, in decimal) - the one asked
// for in *version if there is one of it (nought: any), else the highest.
NewtonErr
CMGetServiceVersion(ULong serviceId, ULong* version)
{
	NewtonErr err = kCMErr_ServiceVersionNotFound;
	Boolean found = false;
	ULong wanted = *version;
	ULong highest = 0;
	ULong thisVersion = 0;
	int skipCount = 0;
	for ( ; ; )
	{
		const TClassInfo* info = gProtocolRegistry->Find("TCMService", nil, skipCount++, nil);
		if (info == nil || found)
			break;
		const char* serv = info->GetCapability('serv');
		if (serv == nil)
			continue;
		const UByte* s = (const UByte*) serv;
		if (serviceId != ((ULong) s[0] << 24) + ((ULong) s[1] << 16) + ((ULong) s[2] << 8) + s[3])
			continue;
		const char* vern = info->GetCapability('vern');
		if (vern == nil)
			continue;
		thisVersion = atoi(vern);
		if (wanted != 0xFFFFFFFF && (wanted == 0 || thisVersion == wanted))
		{
			found = true;
			err = noErr;
		}
		else if (highest < thisVersion)
			highest = thisVersion;
	}
	if (found)
		*version = thisVersion;
	else if (wanted == 0xFFFFFFFF && highest != 0)
	{
		err = noErr;
		*version = highest;
	}
	return err;
}


// ---------------------------------------------------------------------------
//	The TCMService glue
// ---------------------------------------------------------------------------

// ROM 0x00382998 New__10TCMServiceSFPc
TCMService*
TCMService::New(char* implementation)
{
	TCMService* p = (TCMService*) AllocInstanceByName(kServiceInterfaceName, implementation);
	return p != nil ? (TCMService*) p->GlueNew() : nil;
}


// ROM 0x003829c4 Delete__10TCMServiceFv
void
TCMService::Delete()
{
	GlueDelete();
}


// ---------------------------------------------------------------------------
//	TAsyncServiceMessage: a service's asynchronous RPC, whose reply comes
//	back to the comm manager
// ---------------------------------------------------------------------------

// ROM 0x00049448 __ct__20TAsyncServiceMessageFv
TAsyncServiceMessage::TAsyncServiceMessage()
{
	fService = nil;
	fMessage = nil;
	fReply = nil;
}


// ROM 0x0004948c __dt__20TAsyncServiceMessageFv
TAsyncServiceMessage::~TAsyncServiceMessage()
{
	if (fMessage != nil)
		operator delete(fMessage);
	if (fReply != nil)
		operator delete(fReply);
	((TCMWorld*) GetGlobals())->fServiceMessages.Remove(this);
}


// ROM 0x000494e8 Init__20TAsyncServiceMessageFP10TCMService
// The reply comes to the comm manager's port, with the event handler as its
// refCon, and the message is kept on the world's list until it does.
NewtonErr
TAsyncServiceMessage::Init(TCMService* service)
{
	fAsyncMessage.Init(true);
	TCMWorld* world = (TCMWorld*) GetGlobals();
	fAsyncMessage.SetCollectorPort(*world->GetMyPort());
	NewtonErr err = fAsyncMessage.SetUserRefCon((ULong) &world->fEventHandler);
	fService = service;
	world->fServiceMessages.InsertAt(world->fServiceMessages.GetArraySize(), this);
	return err;
}


// ROM 0x00049550 Send__20TAsyncServiceMessageFP6TUPortPvUlT2N23
// (The message and the reply are the object's from now on: its destructor
// frees them.)
NewtonErr
TAsyncServiceMessage::Send(TUPort* destination, void* message, ULong messageSize, void* reply, ULong replySize, ULong messageType)
{
	fReply = reply;
	fMessage = message;
	return destination->SendRPC(&fAsyncMessage, message, messageSize, reply, replySize, 0, nil, messageType);
}


// ROM 0x000495c8 Match__20TAsyncServiceMessageFP10TUMsgToken
Boolean
TAsyncServiceMessage::Match(TUMsgToken* token)
{
	return token->GetMsgId() == fAsyncMessage.GetMsgId();
}


// ROM 0x00070a1c OpenCommTool__FUlP12TOptionArrayP10TCMService
// Send the open request asynchronously on the service's behalf (its reply
// reaches the comm manager, which calls the service's DoneStarting); the
// answer is kCall_In_Progress (1) once it has gone.  (The ROM leaks the
// request and the reply when the second allocation fails; so does this.)
NewtonErr
OpenCommTool(TObjectId portId, TOptionArray* options, TCMService* service)
{
	NewtonErr err = kError_No_Memory;
	TCommToolOpenRequest* request = new TCommToolOpenRequest;
	TCommToolOpenReply* reply;
	if (request != nil && (reply = new TCommToolOpenReply) != nil)
	{
		request->fOptions = options;
		request->fOptionCount = options->GetArrayCount();
		request->fOutside = false;
		TAsyncServiceMessage* message = new TAsyncServiceMessage;
		if (message != nil)
		{
			err = message->Init(service);
			if (err == noErr)
			{
				TUPort port(portId);
				err = message->Send(&port, request, sizeof(TCommToolOpenRequest), reply, sizeof(TCommToolOpenReply), kCommToolRequestTypeControl);
			}
			if (err == noErr)
				err = 1;
		}
	}
	return err;
}


// ---------------------------------------------------------------------------
//	TStartInfo: a service being started
// ---------------------------------------------------------------------------

// ROM 0x0006c8f4 __ct__10TStartInfoFv
TStartInfo::TStartInfo()
{
	fService = nil;
	fServiceIdOption = nil;
	fOptionsAllocated = false;
}


// ROM 0x0006c93c __dt__10TStartInfoFv
TStartInfo::~TStartInfo()
{
	if (fService != nil)
	{
		fService->Delete();
		fService = nil;
	}
	if (fOptionsAllocated && fOptions != nil)
		delete fOptions;
	((TCMWorld*) GetGlobals())->fStartInfos.Remove(this);
}


// ROM 0x0006c9a4 Init__10TStartInfoFP10TUMsgTokenP8TCMEvent
// The request's options (copied in, for a request from outside), and its
// token to answer; the info is kept on the world's list meanwhile.
NewtonErr
TStartInfo::Init(TUMsgToken* token, TCMEvent* event)
{
	TCMServiceEvent* request = (TCMServiceEvent*) event;
	if (request->fEvent == kCMStartServiceOutside)
	{
		fOptions = new TOptionArray;
		if (fOptions == nil)
			return kError_No_Memory;
		NewtonErr err = fOptions->CopyFromShared((TObjectId) (ULong) request->fOptions, request->fCount);
		if (err != noErr)
			return err;
		fOptionsAllocated = true;
	}
	else
		fOptions = request->fOptions;
	fToken = *token;
	TCMWorld* world = (TCMWorld*) GetGlobals();
	world->fStartInfos.InsertAt(world->fStartInfos.GetArraySize(), this);
	return noErr;
}


// ROM 0x0006ca34 Complete__10TStartInfoFl
// Answer the request: the result and where the service is.
void
TStartInfo::Complete(NewtonErr result)
{
	fResult = result;
	TCMServiceReply reply;
	reply.fEventError = fResult;
	reply.fServiceInfo = fServiceInfo;
	fToken.ReplyRPC(&reply, sizeof(reply), 0);
}


// ---------------------------------------------------------------------------
//	TCMEventHandler
// ---------------------------------------------------------------------------

// ROM 0x0006cb2c __ct__15TCMEventHandlerFv
TCMEventHandler::TCMEventHandler()
{
}


// ROM 0x0006bafc Init__15TCMEventHandlerFUlT1
// (NOT YET: the TICHandler the ROM makes here first, on the world's port,
// and whose error it answers.)
NewtonErr
TCMEventHandler::Init(ULong eventId, ULong eventClass)
{
	fICHandler = nil;
	fICState = 0;
	fNotifyPending = false;
	return TAEventHandler::Init(eventId, eventClass);
}


// ROM 0x0006bb78 AEHandlerProc__15TCMEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TCMEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TCMEvent* cmEvent = (TCMEvent*) event;
	NewtonErr err;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMEvent));
	switch (cmEvent->fEvent)
	{
	case kCMStartServiceOutside:
	case kCMStartService:
		err = StartService(token, cmEvent);
		break;
	case kCMGetLastDevice:
		err = GetLastDevice(cmEvent);
		break;
	case kCMSetLastDevice:
		err = SetLastDevice(cmEvent);
		fNotifyPending = false;
		break;
	case kCMSCPLoad:
		{
			TCMSCPLoadEvent* load = (TCMSCPLoadEvent*) cmEvent;
			TCMWorld* world = (TCMWorld*) GetGlobals();
			err = world->SCPLoad(load->fWaitPeriod, load->fTries, load->fFilter, token, 0x10);
			world->AEDeferReply();
		}
		break;
	case kCMSetLastPackage:
		err = SetLastPackage(cmEvent);
		break;
	case kCMGetLastPackage:
		err = GetLastPackage(cmEvent);
		break;
	case kCMNotify:
		fICState = ((TCMPackageEvent*) cmEvent)->fPackageB;
		if (fICState == 1 && ((TCMWorld*) GetGlobals())->SCPCheck(0x12) == noErr)
		{
			fNotifyPending = true;
			err = noErr;
			break;
		}
		// NOT YET: TICHandler::Send(fICState) tells the Newt world
		err = kCMErr_UnknownEvent;
		break;
	default:
		err = kCMErr_UnknownEvent;
		break;
	}
	cmEvent->fEventError = err;
}


// ROM 0x0006bcb4 AECompletionProc__15TCMEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A reply to one of the comm manager's asynchronous RPCs: a service's open
// of its tool (the service looks at the reply, and the start request is
// answered with the tool's port), or the docking loader's (NOT YET).
void
TCMEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TCMWorld* world = (TCMWorld*) GetGlobals();
	if (((TCMEvent*) event)->fEvent == kCMSCPLoad)
	{
		// the loader's answer passed on to whoever asked, and the message
		// given back; then a notification that waited on the load is sent
		// to the Newt world (NOT YET: TICHandler::Send(fICHandler,
		// fICState) - the interconnect handler is not reconstructed)
		world->fSCPMessage->ReplyRPC();
		if (world->fSCPMessage != nil)
			delete world->fSCPMessage;
		world->fSCPMessage = nil;
		if (fNotifyPending)
			fNotifyPending = false;
		return;
	}
	TAsyncServiceMessage* message = world->MatchPendingServiceMessage(token);
	if (message == nil)
		return;
	TCMService* service = message->fService;
	TStartInfo* info = world->MatchPendingStartInfo(service);
	TCommToolOpenReply* reply = (TCommToolOpenReply*) message->fReply;
	if (info->fServiceIdOption != nil)
		info->fServiceIdOption->fPortId = reply->fPortId;
	info->fServiceInfo.SetPortId(reply->fPortId);
	NewtonErr err = service->DoneStarting(event, *size, &info->fServiceInfo);
	info->Complete(err);
	delete info;
	// (the ROM has the message's destructor in line)
	delete message;
}


// ROM 0x0006bdc4 StartService__15TCMEventHandlerFP10TUMsgTokenP8TCMEvent
// Start the first service the options name that is not started yet: a
// 'sid ' option with a port says the service is running there already;
// otherwise the TCMService whose 'serv capability is the service's id is
// made and started.  A service that starts asynchronously (Start answers
// kCall_In_Progress) is answered from AECompletionProc; the reply is
// deferred either way.  (The ROM leaves the start info on its list when
// Init fails or the service cannot be made.)
NewtonErr
TCMEventHandler::StartService(TUMsgToken* token, TCMEvent* event)
{
	TStartInfo* info = new TStartInfo;
	if (info == nil)
		return kError_No_Memory;
	NewtonErr err = info->Init(token, event);
	if (err != noErr)
		return err;

	TOptionIterator iter(info->fOptions);
	Boolean foundService = false;
	Boolean started = false;
	do
	{
		TOption* option = iter.CurrentOption();
		if (option == nil)
			break;
		iter.NextOption();
		if (option->IsService() && !option->IsProcessed())
		{
			foundService = true;
			ULong serviceId = option->Label();
			if (serviceId == 'sid ')
			{
				info->fServiceIdOption = (TCMOServiceIdentifier*) option;
				serviceId = info->fServiceIdOption->fServiceId;
			}
			TObjectId portId = 0;
			if (info->fServiceIdOption != nil)
				portId = info->fServiceIdOption->fPortId;
			if (info->fServiceIdOption == nil || portId == 0)
			{
				const TClassInfo* classInfo = gProtocolRegistry->Satisfy(kServiceInterfaceName, nil, 'serv', serviceId);
				if (classInfo != nil)
				{
					TCMService* service = (TCMService*) classInfo->New();
					if (service == nil)
						return kError_No_Memory;
					info->fService = service;
					started = true;
					option->SetProcessed();
					err = service->Start(info->fOptions, serviceId, &info->fServiceInfo);
					((TAppWorld*) GetGlobals())->AEDeferReply();
					if (err != 1)
					{
						if (err == noErr && info->fServiceIdOption != nil)
							info->fServiceIdOption->fPortId = info->fServiceInfo.GetPortId();
						info->Complete(err);
						delete info;
						return err;
					}
				}
			}
			else
			{
				info->fServiceInfo.SetPortId(portId);
				info->fServiceInfo.SetServiceId(info->fServiceIdOption->fServiceId);
				started = true;
				info->fServiceIdOption->SetProcessed();
				info->Complete(noErr);
				delete info;
			}
		}
	} while (!foundService);

	if (foundService)
	{
		if (started)
			return noErr;
		err = kCMErr_ServiceNotFound;
	}
	else
		err = kCMErr_NoServiceSpecified;
	delete info;
	return err;
}


// ROM 0x0006c020 GetLastDevice__15TCMEventHandlerFP8TCMEvent
NewtonErr
TCMEventHandler::GetLastDevice(TCMEvent* event)
{
	NewtonErr err = noErr;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMDeviceEvent));
	TCMWorld* world = (TCMWorld*) GetGlobals();
	if (world->fLastDevice.fDeviceType == 0)
		err = kCMErr_NoLastDevice;
	else
		((TCMDeviceEvent*) event)->fDevice = world->fLastDevice;
	return err;
}


// ROM 0x0006c074 SetLastDevice__15TCMEventHandlerFP8TCMEvent
NewtonErr
TCMEventHandler::SetLastDevice(TCMEvent* event)
{
	((TCMWorld*) GetGlobals())->SetDevice(&((TCMDeviceEvent*) event)->fDevice);
	event->fEventError = noErr;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMEvent));
	return noErr;
}


// ROM 0x0006c0ac GetLastPackage__15TCMEventHandlerFP8TCMEvent
NewtonErr
TCMEventHandler::GetLastPackage(TCMEvent* event)
{
	NewtonErr err = noErr;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMPackageEvent));
	TCMWorld* world = (TCMWorld*) GetGlobals();
	if (world->fLastPackageA == 0)
		err = kCMErr_NoLastPackage;
	else
	{
		((TCMPackageEvent*) event)->fPackageA = world->fLastPackageA;
		((TCMPackageEvent*) event)->fPackageB = world->fLastPackageB;
	}
	return err;
}


// ROM 0x0006c104 SetLastPackage__15TCMEventHandlerFP8TCMEvent
NewtonErr
TCMEventHandler::SetLastPackage(TCMEvent* event)
{
	TCMPackageEvent* package = (TCMPackageEvent*) event;
	((TCMWorld*) GetGlobals())->SetLastPackage(package->fPackageA, package->fPackageB);
	event->fEventError = noErr;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMEvent));
	return noErr;
}


// ---------------------------------------------------------------------------
//	TCMSystemEventHandler
// ---------------------------------------------------------------------------

// ROM 0x0006ca9c PowerOn__21TCMSystemEventHandlerFP7TAEvent
void
TCMSystemEventHandler::PowerOn(TAEvent* event)
{
	SetReply(sizeof(TAESystemEvent), event);		// DEVIATION: the ROM's 0xC (three words), which is short on an LP64 host
	ReplyImmed();
	TCMWorld* world = (TCMWorld*) GetGlobals();
	if (!gSCPDevicePackageBusy)
		world->SCPLoad(0x1C1F38, 2, '****', nil, 0x30);
	else
		gSCPDevicePackageBusy = false;
}


// ROM 0x0006cad0 PowerOff__21TCMSystemEventHandlerFP7TAEvent
// (TAppWorld::AEReplyImmed in line.)
void
TCMSystemEventHandler::PowerOff(TAEvent* event)
{
	SetReply(sizeof(TAESystemEvent), event);		// DEVIATION: the ROM's 0xC (three words), which is short on an LP64 host
	((TAppWorld*) GetGlobals())->AEReplyImmed();
}


// ROM 0x0006caf8 AppAlive__21TCMSystemEventHandlerFP7TAEvent
void
TCMSystemEventHandler::AppAlive(TAEvent* event)
{
	SetReply(sizeof(TAESystemEvent), event);		// DEVIATION: the ROM's 0xC (three words), which is short on an LP64 host
	ReplyImmed();
	TCMWorld* world = (TCMWorld*) GetGlobals();
	if (!gSCPDevicePackageBusy)
		world->SCPLoad(0x1C1F38, 2, '****', nil, 0x31);
	else
		gSCPDevicePackageBusy = false;
}


// ---------------------------------------------------------------------------
//	TCMWorld
// ---------------------------------------------------------------------------

// ROM 0x0006c358 __ct__8TCMWorldFv
TCMWorld::TCMWorld()
{
	fSCPMessage = nil;
	memset(&fLastDevice, 0, sizeof(fLastDevice));
	fLastPackageB = 0;
	fLastPackageA = 0;
}


// ROM 0x0006c3e8 GetSizeOf__8TCMWorldFv
ULong
TCMWorld::GetSizeOf()
{
	return sizeof(TCMWorld);
}


// ROM 0x0006c3f0 MainConstructor__8TCMWorldFv
// The 'comg events, and the power-on, power-off and app-alive system
// events.  (The ROM registers for app-alive by hand - TSystemEventHandler::
// Init in line.)
long
TCMWorld::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	fEventHandler.Init(kCMEventId, kNewtEventClass);
	TCMSystemEventHandler* handler = new TCMSystemEventHandler;
	handler->Init(kSysEvent_PowerOn, 0);
	handler->Init(kSysEvent_PowerOff, 0);
	return handler->Init(kSysEvent_AppAlive, 0);
}


// ROM 0x0006c488 MainDestructor__8TCMWorldFv
void
TCMWorld::MainDestructor()
{
}


// ROM 0x0006c48c SCPCheck__8TCMWorldFUl
NewtonErr
TCMWorld::SCPCheck(ULong reason)
{
	if (!gSCPDevicePackageBusy)
		return SCPLoad(0x1C1F38, 2, '****', nil, reason);
	gSCPDevicePackageBusy = false;
	return noErr;
}


// ROM 0x0006c4e0 SCPLoad__8TCMWorldFUlN21P10TUMsgTokenT1
// Start the docking loader ('scpl, comms/SCPLoader.h) and ask it,
// asynchronously, to load what a connected device wants; its answer comes
// back to AECompletionProc, which answers the token's request with it.
// One load at a time: kCMErr_SCPLoadBusy while one is in flight.  ROM BUG:
// a request that fails after the message is made leaves it in fSCPMessage,
// so every later load answers busy.
// DEVIATION: the loader is started through the hook comms_dock sets
// (gStartSCPLoader, SCPLoader.h's RegisterSCPLoader); with none, the
// answer is kCMErr_SCPLoadBusy, as while a load is in flight.
NewtonErr
TCMWorld::SCPLoad(ULong waitPeriod, ULong tries, ULong filter, TUMsgToken* token, ULong reason)
{
	if (fSCPMessage != nil || gStartSCPLoader == nil)
		return kCMErr_SCPLoadBusy;
	NewtonErr err = gStartSCPLoader();
	if (err == noErr)
	{
		TUPort loaderPort;
		err = GetSCPLoaderPort(&loaderPort);
		if (err == noErr)
		{
			fSCPMessage = new TCMSCPAsyncMessage;
			if (fSCPMessage == nil)
				err = kError_No_Memory;
			else if ((err = fSCPMessage->Init(*GetMyPort(), &fEventHandler)) == noErr)
			{
				if (token != nil)
					fSCPMessage->SetToken(token);
				fSCPMessage->fRequest.fAEventClass = 'newt';
				fSCPMessage->fRequest.fAEventID = 'scpl';
				fSCPMessage->fRequest.fEvent = kCMSCPLoad;
				fSCPMessage->fRequest.fTries = tries;
				fSCPMessage->fRequest.fWaitPeriod = waitPeriod;
				fSCPMessage->fRequest.fFilter = filter;
				fSCPMessage->fRequest.fReason = reason;
				err = fSCPMessage->SendRPC(&loaderPort);
			}
		}
	}
	return err;
}


// ---------------------------------------------------------------------------
//	TCMSCPAsyncMessage
// ---------------------------------------------------------------------------

// ROM 0x0006c144 __ct__18TCMSCPAsyncMessageFv
TCMSCPAsyncMessage::TCMSCPAsyncMessage()
{
	fHasToken = false;
}


// ROM 0x0006c1a0 Init__18TCMSCPAsyncMessageFUlP14TAEventHandler
// The reply comes to the port, with the handler as its refCon.
NewtonErr
TCMSCPAsyncMessage::Init(TObjectId port, TAEventHandler* handler)
{
	NewtonErr err = TUAsyncMessage::Init(true);
	if (err != noErr)
		return err;
	err = SetCollectorPort(port);
	if (err != noErr)
		return err;
	return SetUserRefCon((ULong) handler);
}


// ROM 0x0006c28c SendRPC__18TCMSCPAsyncMessageFP6TUPort
NewtonErr
TCMSCPAsyncMessage::SendRPC(TUPort* port)
{
	return port->SendRPC(this, &fRequest, sizeof(fRequest), &fReply, sizeof(fReply));
}


// ROM 0x0006c2f4 SetToken__18TCMSCPAsyncMessageFP10TUMsgToken
// The request to answer when the loader has.
void
TCMSCPAsyncMessage::SetToken(TUMsgToken* token)
{
	if (token == nil)
		return;
	fToken = *token;
	fHasToken = true;
}


// ROM 0x0006c31c ReplyRPC__18TCMSCPAsyncMessageFv
NewtonErr
TCMSCPAsyncMessage::ReplyRPC()
{
	NewtonErr err = noErr;
	if (fHasToken)
		err = fToken.ReplyRPC(&fReply, sizeof(fReply), noErr);
	return err;
}


// ROM 0x0006c664 MatchPendingServiceMessage__8TCMWorldFP10TUMsgToken
TAsyncServiceMessage*
TCMWorld::MatchPendingServiceMessage(TUMsgToken* token)
{
	CListIterator iter(&fServiceMessages);
	for (TAsyncServiceMessage* message = (TAsyncServiceMessage*) iter.FirstItem(); iter.More(); message = (TAsyncServiceMessage*) iter.NextItem())
		if (message->Match(token))
			return message;
	return nil;
}


// ROM 0x0006c834 MatchPendingStartInfo__8TCMWorldFP10TCMService
TStartInfo*
TCMWorld::MatchPendingStartInfo(TCMService* service)
{
	CListIterator iter(&fStartInfos);
	for (TStartInfo* info = (TStartInfo*) iter.FirstItem(); iter.More(); info = (TStartInfo*) iter.NextItem())
		if (info->fService == service)
			return info;
	return nil;
}


// ROM 0x0006c8c0 SetDevice__8TCMWorldFP16TConnectedDevice
// The device, stamped with the time now.  ROM BUG: the time is written into
// a TTime made *over* the device's own connect time - so it is the time the
// device was recorded, whatever it said.
void
TCMWorld::SetDevice(TConnectedDevice* device)
{
	fLastDevice = *device;
	fLastDevice.fLastConnectTime = GetGlobalTime();
}


// ROM 0x0006c8e8 SetLastPackage__8TCMWorldFUlT1
void
TCMWorld::SetLastPackage(ULong a, ULong b)
{
	fLastPackageB = b;
	fLastPackageA = a;
}
