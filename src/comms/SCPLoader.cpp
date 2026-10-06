/*
	File:		comms/SCPLoader.cpp

	Contains:	The docking loader (SCPLoader.h).

	Reconstructed from the MP2x00 US ROM (0x001b9d08-0x001baaf8); each
	function cites its origin.
*/

#include "SCPLoader.h"
#include "CommManager.h"
#include "Endpoint.h"
#include "EndpointPipe.h"
#include "SerialOptions.h"
#include "HALOptions.h"
#include "SystemEvents.h"
#include "UserPorts.h"
#include "Protocols.h"
#include "Soups.h"
#include "StorePackages.h"
#include "LargeObjects.h"
#include "NewtWorld.h"
#include "SCPEvents.h"
#include "OSErrors.h"
#include "NewtonTime.h"
#include "host/RomBugs.h"

extern TClassInfoRegistry*	gProtocolRegistry;

// the comm manager's hook (CommManager.cpp)
extern NewtonErr	(*gStartSCPLoader)(void);

// the comm manager's errors the loader answers or looks for
#define kCMErr_ServiceVersionNotFound	(-26002)
#define kCMErr_DeviceAlreadyServed		(-26003)
#define kCMErr_NoDeviceId				(-26009)
#define kCMErr_NoPackageService			(-26010)
#define kCMErr_NotAPackage				(-26011)
#define kCMErr_BadSpeed					(-26014)
#define kCMErr_NoLastPackage			(-26015)

// what a load that is not worth trying again answers (the loader's retry
// loop stops on it as on success)
#define kSCPLoadGiveUp					(-18000)

// the device's answers a read gives up on
#define kSCPReadTimedOut				(-10021)
#define kSCPReadAborted					(-18003)

// the speeds offered: 19200, 38400, 57600, 115200 and 230400
#define kSCPSpeedsOffered				0x7c


/* -----------------------------------------------------------------------------
	TSCPLoaderEventHandler
----------------------------------------------------------------------------- */

// ROM 0x001b9d08 AEHandlerProc__22TSCPLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The comm manager's request (TCMSCPLoadEvent): reason 0x20 has the platform
// look for a keyboard (bit 0: the machine was powered on); reason 0x10 loads
// from the external port, and then from the modem's when nobody answered
// there, each up to the tries asked for (at most five).  The answer is the
// request with its result; the task then ends.
void
TSCPLoaderEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TCMSCPLoadEvent* request = (TCMSCPLoadEvent*) event;
	NewtonErr err = noErr;
	ULong tries = request->fTries;
	if (tries > 4)
		tries = 5;
	ULong reason = request->fReason;
	if (reason & 0x20)
		PowerOnDeviceCheck((reason & 1) != 0);
	if (reason & 0x10)
	{
		ULong i = 0;
		if (tries != 0)
		{
			do
			{
				err = ((TSCPLoader*) GetGlobals())->SCPLoad(request->fWaitPeriod, request->fFilter, kHWLocExternalSerial);
				if (err == noErr || err == kSCPLoadGiveUp)
					break;
			} while (++i < tries);
		}
		if (((TSCPLoader*) GetGlobals())->GetLastDevice() == 0 && (i = 0, tries != 0))
		{
			do
			{
				err = ((TSCPLoader*) GetGlobals())->SCPLoad(request->fWaitPeriod, request->fFilter, 'mdem');
				if (err == noErr || err == kSCPLoadGiveUp)
					break;
			} while (++i < tries);
		}
	}
	request->fEventError = err;
	((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCMSCPLoadEvent), event);
	((TAppWorld*) GetGlobals())->AEReplyImmed();
	((TAppWorld*) GetGlobals())->AETerminateLoop();
}


// ROM 0x001b9e24 AECompletionProc__22TSCPLoaderEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The 'dnot' system event delivered.
void
TSCPLoaderEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	((TSCPLoader*) GetGlobals())->DeviceNotifyCompletion();
}


/* -----------------------------------------------------------------------------
	TSCPLoader
----------------------------------------------------------------------------- */

// ROM 0x001ba454 __ct__10TSCPLoaderFv
TSCPLoader::TSCPLoader()
{
	fPipe = nil;
	fEndpoint = nil;
	fEventHandler = nil;
	fNotifyMessage = nil;
	fNotifyEvent = nil;
	fNotifySender = nil;
	fDeviceType = 0;
	fSpeed = 9600;
}


// ROM 0x001ba4c4 GetSizeOf__10TSCPLoaderFv
// DEVIATION (pointer size): the host's size (the ROM's 0x9c).
ULong
TSCPLoader::GetSizeOf()
{
	return sizeof(TSCPLoader);
}


// ROM 0x001ba4cc GetLastDevice__10TSCPLoaderFv
ULong
TSCPLoader::GetLastDevice()
{
	return fDeviceType;
}


// ROM 0x001ba4d4 MainConstructor__10TSCPLoaderFv
// No forking; the handler takes 'newt' 'scpl' events.
long
TSCPLoader::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	EnableForking(false);
	fEventHandler = new TSCPLoaderEventHandler;
	if (fEventHandler == nil)
		return MemError();
	fEventHandler->Init(kSCPLoaderName, 'newt');
	((TAppWorld*) GetGlobals())->AEInstallHandler(fEventHandler);
	return noErr;
}


// ROM 0x001ba548 MainDestructor__10TSCPLoaderFv
// The pipe and the endpoint given back, and the 'dnot' event waited for
// and given back, before the world's own.
void
TSCPLoader::MainDestructor()
{
	if (fPipe != nil)
		delete fPipe;
	if (fEndpoint != nil)
		fEndpoint->Delete();
	if (fNotifyMessage != nil)
	{
		fNotifyMessage->BlockTillDone(nil, nil, nil, nil);
		if (fNotifyMessage != nil)
			delete fNotifyMessage;
	}
	if (fNotifyEvent != nil)
		delete fNotifyEvent;
	if (fNotifySender != nil)
		delete fNotifySender;
	TAppWorld::MainDestructor();
}


// ROM 0x001ba5f4 SCPLoad__10TSCPLoaderFUlN21
// Open the port, see who answers and fetch its package when it is wanted
// (a device that is served already is no error); the port closed again;
// then a service of the device's type that starts by itself ('auto) is
// opened on the port and left running.
NewtonErr
TSCPLoader::SCPLoad(ULong waitPeriod, ULong filter, ULong hwLocation)
{
	fDeviceType = 0;
	NewtonErr err = SCPInit(hwLocation);
	if (err == noErr)
	{
		err = Look(waitPeriod, filter);
		if (err == kSCPReadTimedOut || err == kSCPReadAborted || err == kCMErr_DeviceAlreadyServed || err != noErr)
		{
			if (err == kSCPReadTimedOut)
				err = noErr;
		}
		else
			err = GetPackage();
	}
	if (fEndpoint != nil)
	{
		fEndpoint->EasyClose();
		if (fPipe != nil)
		{
			delete fPipe;
			fPipe = nil;
		}
		if (fEndpoint != nil)
		{
			fEndpoint->Delete();
			fEndpoint = nil;
		}
	}
	if (err == kCMErr_DeviceAlreadyServed)
		err = noErr;
	const TClassInfo* info;
	if (fDeviceType != 0 && err == noErr
	&&  (info = gProtocolRegistry->Satisfy(kServiceInterfaceName, nil, 'serv', (long) fDeviceType)) != nil
	&&  info->GetCapability('auto') != nil)
	{
		TOptionArray options;
		options.Init();
		TOption service;
		service.SetAsService(fDeviceType);
		options.InsertOptionAt(options.GetArrayCount(), &service);
		TCMOSerialHWChipLoc location;
		location.fHWLoc = hwLocation;
		options.InsertOptionAt(options.GetArrayCount(), &location);
		TEndpoint* endpoint = nil;
		CMGetEndpoint(&options, &endpoint, false);
		endpoint->EasyOpen(0);
		endpoint->DeleteLeavingTool();
		err = noErr;
	}
	return err;
}


// ROM 0x001ba7d4 SCPInit__10TSCPLoaderFUl
// The framed serial service on the port at 9600 bps, 8N1, a 0x400-byte
// receive buffer, hardware flow control in, the transmitter off until the
// first send; opened, with a framed pipe of 0x100 bytes each way over it
// (its timeout half a second) and the message buffer as big.  ROM BUG
// (fixed): the message buffer is allocated afresh for every load and the
// last one never freed.  The fix frees the last one first.
NewtonErr
TSCPLoader::SCPInit(ULong hwLocation)
{
	TOptionArray options;
	TOption service;
	TCMOSerialHWChipLoc location;
	TCMOSerialIOParms ioParms;
	TCMOSerialMiscConfig misc;
	TCMOInputFlowControlParms inFlow;
	TCMOSerialBuffers buffers;
	NewtonErr err = options.Init();
	if (err == noErr)
	{
		service.SetAsService('fser');
		if ((err = options.InsertOptionAt(options.GetArrayCount(), &service)) == noErr
		&&  (location.fHWLoc = hwLocation, (err = options.InsertOptionAt(options.GetArrayCount(), &location)) == noErr))
		{
			ioParms.fStopBits = 0;
			ioParms.fParity = 0;
			ioParms.fDataBits = 8;
			ioParms.fSpeed = 9600;
			if ((err = options.InsertOptionAt(options.GetArrayCount(), &ioParms)) == noErr)
			{
				buffers.fRecvSize = 0x400;
				if ((err = options.InsertOptionAt(options.GetArrayCount(), &buffers)) == noErr)
				{
					inFlow.useHardFlowControl = true;
					if ((err = options.InsertOptionAt(options.GetArrayCount(), &inFlow)) == noErr)
					{
						misc.txdOffUntilSend = true;
						misc.txdOnIfGPiOn = true;
						if ((err = options.InsertOptionAt(options.GetArrayCount(), &misc)) == noErr
						&&  (err = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
						{
							fPipe = new TEndpointPipe;
							if (RomBugFixed() && fMessage.fBuffer != nil)
							{
								DisposPtr((Ptr) fMessage.fBuffer);
								fMessage.fBuffer = nil;
							}
							if (fPipe == nil)
								err = kError_No_Memory;
							else if ((err = fMessage.Init(fPipe, 0x100)) == noErr)

							{
								fPipe->Init(fEndpoint, 0x100, 0x100, 500 * kMilliseconds, true, nil);	// 0x1c1f38
								err = fEndpoint->EasyOpen(0);
							}
						}
					}
				}
			}
		}
	}
	return err;
}


// ROM 0x001b9eb0 Look__10TSCPLoaderFUlT1
// Wait for the device's message and its 'd_id': the device recorded and
// announced.  Filter nought wants nothing more; otherwise the device must
// be of the type asked for (any, for '****') with no service of its type
// registered for its package to be fetched - kCMErr_DeviceAlreadyServed
// when there is one.  ROM QUIRK: a device of another type answers noErr,
// so its package is fetched all the same.
NewtonErr
TSCPLoader::Look(ULong waitPeriod, ULong filter)
{
	fPipe->SetTimeout(waitPeriod);
	NewtonErr err = fMessage.ReceiveMessage();
	if (err != kSCPReadTimedOut && err != kSCPReadAborted && err == noErr)
	{
		UByte* tuple = fMessage.Find(kCPDeviceIdTag, true);
		if (tuple == nil)
			err = kCMErr_NoDeviceId;
		else
		{
			TConnectedDevice device;
			device.fConnectedTo = 1;
			device.fDeviceType = CPTupleWord(tuple, 8);
			device.fManufacturer = CPTupleWord(tuple, 0xc);
			device.fVersion = CPTupleWord(tuple, 0x10);
			fDeviceType = device.fDeviceType;
			err = CMSetLastDevice(&device);
			ULong type;
			if (err == noErr && (type = DeviceNotify(&device), filter != 0))
			{
				if (filter != '****')
					type = CPTupleWord(tuple, 8);
				if (filter == '****' || type == filter)
				{
					ULong version = 0xFFFFFFFF;
					if (CMGetServiceVersion(CPTupleWord(tuple, 8), &version) == kCMErr_ServiceVersionNotFound)
						err = noErr;
					else
						err = kCMErr_DeviceAlreadyServed;
				}
			}
		}
	}
	return err;
}


// ROM 0x001b9fbc GetPackage__10TSCPLoaderFv
// Ask the device for its package (see SCPLoader.h) and store it.
NewtonErr
TSCPLoader::GetPackage()
{
	NewtonErr err;
	TCPWriteMessage request(fPipe);
	TCPNewtonIdTuple newtonId;
	if ((err = newtonId.Init()) != noErr)
		return err;
	request.AddTuple(&newtonId);
	TCPServiceInfoRequestTuple infoRequest('pack', 0);
	request.AddTuple(&infoRequest);
	TCPChangeSpeedRequestTuple speedRequest(kSCPSpeedsOffered);
	request.AddTuple(&speedRequest);
	if ((err = request.SendMessage()) != noErr)
		return err;
	if ((err = fMessage.ReceiveMessage()) != noErr)
		return err;

	UByte* tuple = fMessage.Find(kCPChangeSpeedResponseTag, false);
	if (tuple != nil)
	{
		ULong speed;
		switch (CPTupleWord(tuple, 8))
		{
		case 0x04:	speed = 19200; break;
		case 0x08:	speed = 38400; break;
		case 0x10:	speed = 57600; break;
		case 0x20:	speed = 115200; break;
		case 0x40:	speed = 230400; break;
		default:	return kCMErr_BadSpeed;
		}
		fSpeed = speed;
		TOptionArray options;
		TCMOSerialBitRate bitRate;
		bitRate.fBitsPerSecond = fSpeed;
		if ((err = options.Init()) != noErr
		||  (err = options.InsertOptionAt(options.GetArrayCount(), &bitRate)) != noErr
		||  (err = fEndpoint->OptMgmt(0x500, &options, 0)) != noErr)
			return err;
		Sleep(50 * kMilliseconds);		// 0x2cfec
	}

	tuple = fMessage.Find(kCPServiceInfoResponseTag, false);
	if (tuple == nil)
		return kCMErr_NoPackageService;
	if (CPTupleWord(tuple, 8) != 'pack')
		return kCMErr_NotAPackage;
	TCPWriteMessage serviceRequest(fPipe);
	TCPRequestServiceTuple requestService(CPTupleWord(tuple, 8), CPTupleWord(tuple, 0xc));
	serviceRequest.AddTuple(&requestService);
	if ((err = serviceRequest.SendMessage()) != noErr)
		return err;
	// twice the time the package takes at the speed (ten bits a byte, in
	// milliseconds), and at least half a second
	ULong milliseconds = (CPTupleWord(tuple, 0x10) * 10000 / fSpeed) << 1;
	if (milliseconds == 0)
		milliseconds = 500;
	fPipe->SetTimeout(milliseconds * kMilliseconds);
	fMessage.Reset();
	TCPTuple header;
	header.fTag = 0;
	if ((err = fMessage.ReadTuple(&header, true)) != noErr)
		return err;
	err = kCMErr_NotAPackage;
	if (header.fTag != 'pack')
		return err;
	TStore* store = GetInternalStore();
	ULong id;
	if ((err = StorePackage(fPipe, store, nil, &id)) != noErr
	||  (err = fMessage.ReadTuple(&header, true)) != noErr)
		return err;
	TCPWriteMessage done(fPipe);
	TCPAbortTuple abort;
	abort.fError = 1;
	done.AddTuple(&abort);
	if ((err = done.SendMessage()) != noErr)
		return err;

	TSCPEvent event;
	ULong lastId, lastType;
	NewtonErr lastErr = CMSCPGetLastLoadedPackage(&lastId, &lastType);
	if (lastErr == kCMErr_NoLastPackage)
	{
		lastType = 0;
		lastId = 0;
	}
	else if (lastErr != noErr)
		return lastErr;
	if (fDeviceType == lastType)
		// the same device again: the copy it sent is not wanted
		err = DeleteLargeObject(store, id);
	else
	{
		// DEVIATION (pointer size): the event's host size (the ROM's 0x14)
		ULong replySize;
		event.fId = 0;
		if (lastType != 0)
			event.fId = lastId;
		if (lastType != 0 && event.fId != 0)
		{
			event.fWhat = kSCPPackageRemoved;
			gNewtPort->SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
		}
		event.fWhat = kSCPPackageLoaded;
		event.fId = id;
		err = gNewtPort->SendRPC(&replySize, &event, sizeof(event), &event, sizeof(event));
		if (err == noErr)
			err = CMSCPSetLastLoadedPackage(id, fDeviceType);
	}
	return err;
}


// ROM 0x001ba9ac DeviceNotify__10TSCPLoaderFP16TConnectedDevice
// The device announced as a 'dnot' system event, asynchronously: the
// delivery comes back to the loader (DeviceNotifyCompletion).
NewtonErr
TSCPLoader::DeviceNotify(TConnectedDevice* device)
{
	NewtonErr err;
	fNotifyMessage = new TUAsyncMessage;
	if (fNotifyMessage == nil)
		return kError_No_Memory;
	if ((err = fNotifyMessage->Init(true)) != noErr)
		return err;
	if ((err = fNotifyMessage->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort())) != noErr)
		return err;
	if ((err = fNotifyMessage->SetUserRefCon((ULong) fEventHandler)) != noErr)
		return err;
	fNotifyEvent = new TDeviceNotifyEvent;
	if (fNotifyEvent == nil)
		return kError_No_Memory;
	fNotifySender = new TSendSystemEvent;
	if (fNotifySender == nil)
		return kError_No_Memory;
	if ((err = fNotifySender->Init()) != noErr)
		return err;
	fNotifySender->SetEvent('dnot');
	TDeviceNotifyEvent* event = (TDeviceNotifyEvent*) fNotifyEvent;
	event->fAEventID = 'sysm';
	event->fSysEventType = 'dnot';
	event->fDevice = *device;
	// DEVIATION (pointer size): the event's host size (the ROM's 0x24)
	return fNotifySender->SendSystemEvent(fNotifyMessage, fNotifyEvent, sizeof(TDeviceNotifyEvent), nil, 0);
}


// ROM 0x001b9e3c DeviceNotifyCompletion__10TSCPLoaderFv
void
TSCPLoader::DeviceNotifyCompletion()
{
	if (fNotifyMessage != nil)
	{
		delete fNotifyMessage;
		fNotifyMessage = nil;
	}
	if (fNotifyEvent != nil)
	{
		delete fNotifyEvent;
		fNotifyEvent = nil;
	}
	if (fNotifySender != nil)
	{
		delete fNotifySender;
		fNotifySender = nil;
	}
}


// ROM 0x00192924 PowerOnDeviceCheck__FUc
// The platform driver's check for a keyboard on the port (the MP2x00's
// TVoyagerPlatform starts the keyboard driver when it finds one).
// DEVIATION: the host has no platform driver (GetPlatformDriver answers
// nil), so there is nothing to check.
void
PowerOnDeviceCheck(Boolean poweredOn)
{
}


// host: what the comm manager's SCPLoad makes and starts (the ROM's does it
// in line: a TSCPLoader on its stack, Init('scpl', true, 0x1770))
static NewtonErr
StartSCPLoader(void)
{
	TSCPLoader loader;
	return loader.Init(kSCPLoaderName, true, 0x1770);
}

void
RegisterSCPLoader(void)
{
	gStartSCPLoader = StartSCPLoader;
}
