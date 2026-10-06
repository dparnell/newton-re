/*
	File:		comms/irda/IrGlue.cpp

	Contains:	TIrGlue (IrGlue.h).

	Reconstructed from the MP2x00 US ROM (0x000ef538-0x000f0cc8); each
	function cites its origin.
*/

#include "IrGlue.h"
#include "IrDATool.h"
#include "IrLAP.h"
#include "IrLMP.h"
#include "IrIAS.h"
#include "IrIASService.h"
#include "BufferSegment.h"
#include "CommErrors.h"
#include "utility/Random.h"
#include "host/RomBugs.h"


// ROM 0x000ef538 __ct__7TIrGlueFv
TIrGlue::TIrGlue()
{
	fState = kIrGlueDisconnected;
	fDisconnectState = 0;
	fDiscoveredDevices = nil;
	fCurrentStream = nil;
	fPendingStreams = nil;
	fFreeEventBlocks = nil;
	fLAP = nil;
	fLMP = nil;
	fNameService = nil;
	fNameServer = nil;
	fNameClient = nil;
	fLSAPConn = nil;
	fRecvBuffer = nil;
	fPutsPending = 0;
	fLSAPIdsInUse = 1;
}


// ROM 0x000ef5d4 __dt__7TIrGlueFv
TIrGlue::~TIrGlue()
{
	DeInit(true);
}


// ROM 0x000efb54 Init__7TIrGlueFP9TIrDATool
// The link, the multiplexer, the lists and a first event block.
NewtonErr
TIrGlue::Init(TIrDATool* tool)
{
	fTool = tool;
	NewtonErr err = TIrStream::Init(this);
	if (err == noErr)
	{
		err = -7000;
		if ((fLAP = new TIrLAP) != nil
		 && (fLMP = new TIrLMP) != nil
		 && (fDiscoveredDevices = new CList) != nil
		 && (fPendingStreams = new CList) != nil)
		{
			err = InitEventBlockList();
			if (err == noErr)
				err = fLAP->Init(this, fLMP);
			if (err == noErr)
				err = fLMP->Init(this, fLAP);
			if (err == noErr)
				return noErr;
		}
	}
	DeInit(true);
	return err;
}


// ROM 0x000eff6c DeInit__7TIrGlueFUc
// A connection's layers thrown away; all of it too, or else the link and
// the multiplexer reset for the next.
void
TIrGlue::DeInit(Boolean all)
{
	if (fLSAPConn != nil)
	{
		delete fLSAPConn;
		fLSAPConn = nil;
	}
	if (fNameService != nil)
	{
		delete fNameService;
		fNameService = nil;
	}
	if (fNameServer != nil)
	{
		delete fNameServer;
		fNameServer = nil;
	}
	if (fNameClient != nil)
	{
		delete fNameClient;
		fNameClient = nil;
	}
	if (fRecvBuffer != nil)
	{
		delete fRecvBuffer;
		fRecvBuffer = nil;
	}
	DeleteDiscoveredDevicesList(all);
	DeleteEventBlockList(all);
	if (all)
	{
		if (fLMP != nil)
		{
			delete fLMP;
			fLMP = nil;
		}
		if (fLAP != nil)
		{
			delete fLAP;
			fLAP = nil;
		}
		if (fPendingStreams != nil)
		{
			delete fPendingStreams;
			fPendingStreams = nil;
		}
	}
	else
	{
		if (fLAP != nil)
			fLAP->Reset();
		if (fLMP != nil)
			fLMP->Reset();
		fPendingStreams->RemoveElementsAt(0, fPendingStreams->Count());
	}
}


// ROM 0x000f06a4 NextState__7TIrGlueFUl
void
TIrGlue::NextState(ULong event)
{
	switch (fState)
	{
	case kIrGlueDisconnected:		HandleDisconnectedStateEvent(event); break;
	case kIrGlueDiscovering:		HandleDiscoveringStateEvent(event); break;
	case kIrGlueNameServerLookup:	HandleNameServerLookupStateEvent(event); break;
	case kIrGlueConnecting:			HandleConnectingStateEvent(event); break;
	case kIrGlueListening:			HandleListeningStateEvent(event); break;
	case kIrGlueAccepting:			HandleAcceptingStateEvent(event); break;
	case kIrGlueConnected:			HandleConnectedStateEvent(event); break;
	}
}


// ROM 0x000f06e8 HandleDisconnectedStateEvent__7TIrGlueFUl
void
TIrGlue::HandleDisconnectedStateEvent(ULong event)
{
	if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f06f4 HandleDiscoveringStateEvent__7TIrGlueFUl
void
TIrGlue::HandleDiscoveringStateEvent(ULong event)
{
	if (event == kIrDiscoverReply)
		HandleDiscoverComplete();
	else if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f0708 HandleNameServerLookupStateEvent__7TIrGlueFUl
void
TIrGlue::HandleNameServerLookupStateEvent(ULong event)
{
	if (event == kIrConnectReply)
		HandleNameServerConnectComplete();
	else if (event == kIrLookupReply)
		HandleNameServerLookupComplete();
	else if (event == kIrReleaseReply)
		HandleNameServerReleaseComplete();
	else if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f07d0 HandleConnectingStateEvent__7TIrGlueFUl
void
TIrGlue::HandleConnectingStateEvent(ULong event)
{
	if (event == kIrConnectReply)
		HandleConnectComplete();
	else if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f07e4 HandleListeningStateEvent__7TIrGlueFUl
void
TIrGlue::HandleListeningStateEvent(ULong event)
{
	if (event == kIrListenReply)
		HandleListenComplete();
	else if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f07f8 HandleAcceptingStateEvent__7TIrGlueFUl
void
TIrGlue::HandleAcceptingStateEvent(ULong event)
{
	if (event == kIrAcceptReply)
		HandleAcceptComplete();
	else if (event == kIrDisconnectReply)
		HandleDisconnectComplete();
}


// ROM 0x000f080c HandleConnectedStateEvent__7TIrGlueFUl
void
TIrGlue::HandleConnectedStateEvent(ULong event)
{
	switch (event)
	{
	case kIrGetDataReply:		HandleGetComplete(); break;
	case kIrPutDataReply:		HandlePutComplete(); break;
	case kIrCancelGetReply:		HandleCancelGetComplete(); break;
	case kIrCancelPutReply:		HandleCancelPutComplete(); break;
	case kIrDisconnectReply:	HandleDisconnectComplete(); break;
	}
}


/*------------------------------------------------------------------------------
	Discovery and the IAS lookup
------------------------------------------------------------------------------*/

// ROM 0x000f0c58 DiscoverStart__7TIrGlueFUlUc
void
TIrGlue::DiscoverStart(ULong numSlots, Boolean mediaBusyCheck)
{
	TIrEvent* event = GrabEventBlock(kIrDiscoverRequest, 0x18);
	if (event == nil)
	{
		DiscoverComplete(-7000, nil);
		return;
	}
	event->fNumSlots = numSlots;
	event->fDiscoveredList = fDiscoveredDevices;
	event->fMediaBusyCheck = mediaBusyCheck;
	fLMP->EnqueueEvent(event);
	fState = kIrGlueDiscovering;
}


// ROM 0x000ef644 HandleDiscoverComplete__7TIrGlueFv
void
TIrGlue::HandleDiscoverComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	ReleaseEventBlock(event);
	fState = kIrGlueDisconnected;
	DiscoverComplete(result, fDiscoveredDevices);
}


// ROM 0x000f01b4 DiscoverComplete__7TIrGlueFlP5CList
void
TIrGlue::DiscoverComplete(NewtonErr result, CList* devices)
{
	fTool->DoDiscoverComplete(result, devices);
}


// ROM 0x000f026c DeleteDiscoveredDevicesList__7TIrGlueFUc
void
TIrGlue::DeleteDiscoveredDevicesList(Boolean all)
{
	if (fDiscoveredDevices == nil)
		return;
	TIrDscInfo* info;
	while ((info = (TIrDscInfo*) fDiscoveredDevices->At(fDiscoveredDevices->Count() - 1)) != nil)
	{
		delete info;
		fDiscoveredDevices->RemoveElementsAt(fDiscoveredDevices->Count() - 1, 1);
	}
	if (all)
	{
		if (fDiscoveredDevices != nil)
			delete fDiscoveredDevices;
		fDiscoveredDevices = nil;
	}
}


// ROM 0x000ef684 LSAPLookupStart__7TIrGlueFUlPUcT2
// An IAS client connected to the other side's LSAP 0 to ask it, and a
// server listening on ours for it to ask us.
void
TIrGlue::LSAPLookupStart(ULong devAddr, UByte* className, UByte* attrName)
{
	TIrEvent* event = nil;
	NewtonErr err;
	fLookupClassName = className;
	fLookupAttrName = attrName;
	fNameClient = new TIASClient;
	if (fNameClient == nil)
		err = -7000;
	else if ((err = fNameClient->Init(this, fLMP, this)) != noErr)
	{
		LSAPLookupComplete(err, 0);
		return;
	}
	else if ((event = GrabEventBlock(kIrConnectRequest, 0x20)) == nil)
		err = -7000;
	else
	{
		event->fMyQOS = &fMyQOS;
		event->fPeerQOS = &fPeerQOS;
		event->fConnectData = nil;
		event->fDevAddr = devAddr;
		fNameClient->EnqueueEvent(event);
		// ROM BUG (fixed): should the server fail below, the event just
		// queued for the client is released as well, while the client still
		// has it.  The fix leaves it to the client, which has it now.
		if (RomBugFixed())
			event = nil;
		fNameServer = new TIASServer;
		if (fNameServer == nil)
			err = -7000;
		else if ((err = fNameServer->Init(this, fLMP, this)) == noErr)
		{
			fNameServer->SetNameService(fNameService);
			fState = kIrGlueNameServerLookup;
			return;
		}
	}
	if (event != nil)
		ReleaseEventBlock(event);
	LSAPLookupComplete(err, 0);
}


// ROM 0x000ef784 HandleNameServerConnectComplete__7TIrGlueFv
// Connected to the other side's IAS: the lookup, in the same block.
void
TIrGlue::HandleNameServerConnectComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	if (result != noErr)
	{
		ReleaseEventBlock(event);
		fState = kIrGlueDisconnected;
		LSAPLookupComplete(result, 0);
		return;
	}
	event->fEvent = kIrLookupRequest;
	event->fClassName = (const char*) fLookupClassName;
	event->fAttrName = (const char*) fLookupAttrName;
	fNameClient->EnqueueEvent(event);
}


// ROM 0x000ef7f4 HandleNameServerLookupComplete__7TIrGlueFv
// The answer's first element, an integer, is the LSAP; then the IAS
// connection released.
void
TIrGlue::HandleNameServerLookupComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	fPeerLSAPId = 0;
	fLookupResult = event->fResult;
	if (event->fResult == noErr && event->fAttribute != nil)
	{
		TIASElement* element = (TIASElement*) event->fAttribute->At(0);
		if (element != nil && element->GetInteger(&fPeerLSAPId) != noErr)
			fPeerLSAPId = 0;
		if (event->fAttribute != nil)
			delete event->fAttribute;
	}
	event->fEvent = kIrReleaseRequest;
	event->fResult = noErr;
	fNameClient->EnqueueEvent(event);
}


// ROM 0x000ef87c HandleNameServerReleaseComplete__7TIrGlueFv
void
TIrGlue::HandleNameServerReleaseComplete(void)
{
	ReleaseEventBlock(fCurrentEvent);
	if (fLookupResult == noErr && fPeerLSAPId == 0)
		fLookupResult = kIrDAErrNoSuchClass;
	fState = kIrGlueDisconnected;
	LSAPLookupComplete(fLookupResult, fPeerLSAPId);
}


// ROM 0x000f01bc LSAPLookupComplete__7TIrGlueFlUl
void
TIrGlue::LSAPLookupComplete(NewtonErr result, ULong lsapId)
{
	fTool->DoLSAPLookupComplete(result, lsapId);
}


// ROM 0x000f072c InitNameService__7TIrGlueFv
// This station's IAS database: the device's name and its IrLMP support
// (version 1, no special modes).
NewtonErr
TIrGlue::InitNameService(void)
{
	fNameService = new TIASService;
	if (fNameService == nil)
		return -7000;
	NewtonErr err = fNameService->AddStringEntry(kIASDeviceClassStr, kIASDeviceNameAttrStr, kIASDeviceNameNewton);
	if (err == noErr)
	{
		err = fNameService->AddNBytesEntry(kIASDeviceClassStr, kIASLMPSupportAttrStr, 0x01000000, 3);
		if (err == noErr)
			return noErr;
	}
	if (fNameService != nil)
		delete fNameService;
	fNameService = nil;
	return err;
}


// ROM 0x000f0a58 RegisterMyNameAndLSAPId__7TIrGlueFPUcT1Ul
// An LSAP for this side (the one asked for, or any free), registered under
// the class and attribute given.
NewtonErr
TIrGlue::RegisterMyNameAndLSAPId(UByte* className, UByte* attrName, ULong lsapId)
{
	NewtonErr err;
	if (fNameService == nil && (err = InitNameService()) != noErr)
		return err;
	if ((err = ObtainLSAPId(lsapId)) != noErr)
		return err;
	if ((err = fNameService->AddIntegerEntry((const char*) className, (const char*) attrName, lsapId)) != noErr)
	{
		ReleaseLSAPId(lsapId);
		return err;
	}
	fMyLSAPId = lsapId;
	return noErr;
}


// ROM 0x000f0b34 ObtainLSAPId__7TIrGlueFRUl
// The one asked for (1-0x1f) if it is free, or with 0 one picked at
// random.
NewtonErr
TIrGlue::ObtainLSAPId(ULong& lsapId)
{
	if (fLSAPIdsInUse == 0xffffffff)			// (a 32-bit mask)
		return kCommErrResourceNotAvailable;
	ULong id;
	if (lsapId == 0)
		id = NewtonRand() & 0x1f;
	else if (lsapId > 0x1f)
		return kCommErrBadParameter;
	else
		id = lsapId & 0xff;
	while (id == 0 || (fLSAPIdsInUse & ((ULong) 1 << id)) != 0)
		id = (id + 1) & 0x1f;
	if (lsapId != 0 && id != lsapId)
		return kCommErrResourceNotAvailable;
	lsapId = id;
	fLSAPIdsInUse |= (ULong) 1 << id;
	return noErr;
}


// ROM 0x000f0c34 ReleaseLSAPId__7TIrGlueFUc
void
TIrGlue::ReleaseLSAPId(UByte lsapId)
{
	fLSAPIdsInUse &= ~((ULong) 1 << lsapId);
}


/*------------------------------------------------------------------------------
	Connecting, listening, accepting
------------------------------------------------------------------------------*/

// ROM 0x000ef8cc ConnectStart__7TIrGlueFUlT1P7CBuffer
void
TIrGlue::ConnectStart(ULong devAddr, ULong lsapId, CBuffer* data)
{
	NewtonErr err = -7000;
	fLSAPConn = new TLSAPConn;
	if (fLSAPConn != nil)
	{
		if ((err = fLSAPConn->Init(this, fLMP, this)) != noErr)
		{
			ConnectComplete(err);
			return;
		}
		fLSAPConn->AssignId(fMyLSAPId);
		TIrEvent* event = GrabEventBlock(kIrConnectRequest, 0x20);
		if (event != nil)
		{
			event->fDevAddr = devAddr;
			event->fLSAPId = lsapId;
			event->fMyQOS = &fMyQOS;
			event->fPeerQOS = &fPeerQOS;
			event->fConnectData = data;
			fLSAPConn->EnqueueEvent(event);
			fState = kIrGlueConnecting;
			return;
		}
		err = -7000;
	}
	ConnectComplete(err);
}


// ROM 0x000ef98c HandleConnectComplete__7TIrGlueFv
void
TIrGlue::HandleConnectComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	if (result == noErr)
		result = InitBuffers();
	fState = (result != noErr) ? kIrGlueDisconnected : kIrGlueConnected;
	ReleaseEventBlock(event);
	ConnectComplete(result);
}


// ROM 0x000f01c4 ConnectComplete__7TIrGlueFl
void
TIrGlue::ConnectComplete(NewtonErr result)
{
	fTool->DoConnectComplete(result);
}


// ROM 0x000ef9e4 ListenStart__7TIrGlueFP7CBuffer
// Our LSAP listening, and the IAS server for the other side to look it up.
void
TIrGlue::ListenStart(CBuffer* data)
{
	NewtonErr err = -7000;
	fLSAPConn = new TLSAPConn;
	if (fLSAPConn != nil)
	{
		if ((err = fLSAPConn->Init(this, fLMP, this)) != noErr)
		{
			ListenComplete(err);
			return;
		}
		fLSAPConn->AssignId(fMyLSAPId);
		fNameServer = new TIASServer;
		if (fNameServer != nil)
		{
			if ((err = fNameServer->Init(this, fLMP, this)) != noErr)
			{
				ListenComplete(err);
				return;
			}
			fNameServer->SetNameService(fNameService);
			TIrEvent* event = GrabEventBlock(kIrListenRequest, 0x20);
			if (event != nil)
			{
				event->fMyQOS = &fMyQOS;
				event->fPeerQOS = &fPeerQOS;
				event->fConnectData = data;
				fLSAPConn->EnqueueEvent(event);
				fState = kIrGlueListening;
				return;
			}
		}
		err = -7000;
	}
	ListenComplete(err);
}


// ROM 0x000efac4 HandleListenComplete__7TIrGlueFv
void
TIrGlue::HandleListenComplete(void)
{
	NewtonErr result = fCurrentEvent->fResult;
	fState = (result != noErr) ? kIrGlueDisconnected : kIrGlueAccepting;
	ReleaseEventBlock(fCurrentEvent);
	ListenComplete(result);
}


// ROM 0x000f01cc ListenComplete__7TIrGlueFl
void
TIrGlue::ListenComplete(NewtonErr result)
{
	fTool->DoListenComplete(result);
}


// ROM 0x000efb04 AcceptStart__7TIrGlueFP7CBuffer
void
TIrGlue::AcceptStart(CBuffer* data)
{
	TIrEvent* event = GrabEventBlock(kIrAcceptRequest, 0x20);
	if (event == nil)
	{
		AcceptComplete(-7000);
		return;
	}
	event->fConnectData = data;
	fLSAPConn->EnqueueEvent(event);
}


// ROM 0x000efc2c HandleAcceptComplete__7TIrGlueFv
void
TIrGlue::HandleAcceptComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	if (result == noErr)
		result = InitBuffers();
	fState = (result != noErr) ? kIrGlueDisconnected : kIrGlueConnected;
	ReleaseEventBlock(event);
	AcceptComplete(result);
}


// ROM 0x000f01d4 AcceptComplete__7TIrGlueFl
void
TIrGlue::AcceptComplete(NewtonErr result)
{
	fTool->DoAcceptComplete(result);
}


// ROM 0x000f02f4 InitBuffers__7TIrGlueFv
// Connected: a put's frames are the other side's data size less the
// LMPDU header, a window of them at a time; the receive buffer is ours.
NewtonErr
TIrGlue::InitBuffers(void)
{
	fPeerWindowSize = fPeerQOS.GetWindowSize();
	fMaxPutSize = fPeerQOS.GetDataSize() - 2;
	fRecvBufferSize = fMyQOS.GetDataSize() - 2;
	fRecvBuffer = new CBufferSegment;
	if (fRecvBuffer == nil)
		return -7000;
	NewtonErr err = fRecvBuffer->Init(fRecvBufferSize);
	if (err != noErr)
		return err;
	ResetRecvBufferState();
	return noErr;
}


/*------------------------------------------------------------------------------
	Gets
------------------------------------------------------------------------------*/

// ROM 0x000efc84 GetStart__7TIrGlueFP7CBufferUl
void
TIrGlue::GetStart(CBuffer* buffer, ULong threshold)
{
	fGetThreshold = threshold;
	fGetBuffer = buffer;
	if (CheckGetDone(0, false))
	{
		GetComplete(noErr);
		return;
	}
	TIrEvent* event = GrabEventBlock(kIrGetDataRequest, 0x1c);
	if (event != nil)
		InitGetRequest(event);
	else
		GetComplete(-7000);
}


// ROM 0x000f03b8 InitGetRequest__7TIrGlueFP16TIrDataXferEvent
// A frame's worth of room in the client's buffer: straight into it;
// otherwise into the receive buffer.
void
TIrGlue::InitGetRequest(TIrDataXferEvent* event)
{
	ULong room = fGetBuffer->GetSize() - fGetBuffer->Position();
	fGetDirect = !(fRecvBufferSize > room);
	event->fEvent = kIrGetDataRequest;
	event->fResult = noErr;
	if (fGetDirect)
	{
		event->fBuffer = fGetBuffer;
		event->fOffset = fGetBytes;
		event->fLength = room;
	}
	else
	{
		fRecvBuffer->Reset();
		event->fBuffer = fRecvBuffer;
		event->fOffset = 0;
		event->fLength = fRecvBufferSize;
	}
	fLSAPConn->EnqueueEvent(event);
}


// ROM 0x000f0474 CheckGetDone__7TIrGlueFUlUc
// The bytes just got counted (from the receive buffer, as much as the
// client's buffer takes); done when the threshold is reached.
Boolean
TIrGlue::CheckGetDone(ULong bytes, Boolean fromRecvBuffer)
{
	if (fGetDirect)
		fGetBytes = fGetBytes + bytes;
	else
	{
		if (fromRecvBuffer)
		{
			fRecvBuffer->Hide(fRecvBufferSize - bytes, kSeekFromEnd);
			fRecvBuffer->Seek(0, kSeekFromBeginning);
		}
		ULong room = fGetBuffer->GetSize() - fGetBuffer->Position();
		ULong position = fRecvBuffer->Position();
		ULong left = fRecvBuffer->GetSize() - position;
		if (left > 0)
		{
			if ((Long) left >= (Long) room)
				left = room;
			Size copied = fGetBuffer->Putn(fRecvBuffer->fBufStart + position, left);
			fRecvBuffer->Seek(copied, kSeekFromHere);
			fGetBytes = fGetBytes + copied;
		}
	}
	return fGetBytes >= fGetThreshold;
}


// ROM 0x000efcec HandleGetComplete__7TIrGlueFv
void
TIrGlue::HandleGetComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	if (result == noErr && !CheckGetDone(event->fLength, true))
	{
		InitGetRequest(event);
		return;
	}
	ReleaseEventBlock(event);
	GetComplete(result);
}


// ROM 0x000f01dc GetComplete__7TIrGlueFl
// What is left in the receive buffer stays for the next get.
void
TIrGlue::GetComplete(NewtonErr result)
{
	fTool->DoGetDataComplete(result, fGetBytes);
	fGetBytes = 0;
	if (result == noErr && fRecvBuffer != nil && fRecvBuffer->GetSize() - fRecvBuffer->Position() != 0)
		return;
	ResetRecvBufferState();
}


// ROM 0x000f0370 ResetRecvBufferState__7TIrGlueFv
void
TIrGlue::ResetRecvBufferState(void)
{
	if (fRecvBuffer != nil)
		fRecvBuffer->Hide(fRecvBufferSize, kSeekFromEnd);
	fGetBytes = 0;
	fGetDirect = true;
}


// ROM 0x000efe84 CancelGetStart__7TIrGlueFv
void
TIrGlue::CancelGetStart(void)
{
	TIrEvent* event = GrabEventBlock(kIrCancelGetRequest, 0xc);
	if (event != nil)
		fLSAPConn->EnqueueEvent(event);
	else
		CancelGetComplete(-7000);
}


// ROM 0x000efec4 HandleCancelGetComplete__7TIrGlueFv
void
TIrGlue::HandleCancelGetComplete(void)
{
	NewtonErr result = fCurrentEvent->fResult;
	ReleaseEventBlock(fCurrentEvent);
	CancelGetComplete(result);
}


// ROM 0x000f025c CancelGetComplete__7TIrGlueFl
void
TIrGlue::CancelGetComplete(NewtonErr result)
{
	fTool->DoCancelGetComplete(result);
}


/*------------------------------------------------------------------------------
	Puts
------------------------------------------------------------------------------*/

// ROM 0x000efd58 PutStart__7TIrGlueFP7CBuffer
void
TIrGlue::PutStart(CBuffer* buffer)
{
	NewtonErr err = InitPutRequests(buffer, 0, buffer->GetSize());
	if (err == noErr)
	{
		fPutResult = noErr;
		fPutBytesSent = 0;
	}
	else
		PutComplete(err);
}


// ROM 0x000f05ac InitPutRequests__7TIrGlueFP7CBufferUlT2
// A window's worth of frames queued.
NewtonErr
TIrGlue::InitPutRequests(CBuffer* buffer, ULong offset, ULong size)
{
	// DEVIATION: cleared, where the ROM's stack array is not (see below)
	TIrEvent* requests[8] = { nil };
	UByte window = fPeerWindowSize;
	while (window > 0 && size > 0)
	{
		TIrEvent* event = GrabEventBlock(kIrPutDataRequest, 0x1c);
		if (event == nil)
		{
			// ROM BUG (fixed): the blocks are released from
			// requests[fPutsPending] down to requests[1] - one past the last
			// and never the first - so a stale stack word goes into the free
			// list and a block is lost.  The fix releases requests[0] up to
			// the last.
			while (fPutsPending > 0)
			{
				UByte i = RomBugFixed() ? --fPutsPending : fPutsPending--;
				ReleaseEventBlock(requests[i]);
			}

			return -7000;
		}
		event->fOffset = offset;
		event->fBuffer = buffer;
		event->fLength = ((Long) size >= (Long) fMaxPutSize) ? fMaxPutSize : size;
		requests[fPutsPending++] = event;
		window--;
		offset += event->fLength;
		size -= event->fLength;
	}
	for (ULong i = 0; i < fPutsPending; i++)
		fLSAPConn->EnqueueEvent(requests[i]);
	return noErr;
}


// ROM 0x000efdb0 HandlePutComplete__7TIrGlueFv
// When the window's last frame is sent, the next window, or the put done.
void
TIrGlue::HandlePutComplete(void)
{
	TIrEvent* event = fCurrentEvent;
	NewtonErr result = event->fResult;
	CBuffer* buffer = event->fBuffer;
	ULong length = event->fLength;
	Boolean last = (--fPutsPending == 0);
	if (result != noErr)
	{
		length = 0;
		fPutResult = result;
	}
	ReleaseEventBlock(event);
	if (fPutResult == noErr)
	{
		fPutBytesSent += length;
		if (!last)
			return;
		if (buffer->GetSize() > fPutBytesSent)
		{
			fPutResult = InitPutRequests(buffer, fPutBytesSent, buffer->GetSize() - fPutBytesSent);
			if (fPutResult == noErr)
				return;
		}
	}
	if (last)
		PutComplete(fPutResult);
}


// ROM 0x000f0250 PutComplete__7TIrGlueFl
void
TIrGlue::PutComplete(NewtonErr result)
{
	fTool->DoPutDataComplete(result, fPutBytesSent);
}


// ROM 0x000efef0 CancelPutStart__7TIrGlueFv
void
TIrGlue::CancelPutStart(void)
{
	TIrEvent* event = GrabEventBlock(kIrCancelPutRequest, 0xc);
	if (event != nil)
		fLSAPConn->EnqueueEvent(event);
	else
		CancelPutComplete(-7000);
}


// ROM 0x000eff30 HandleCancelPutComplete__7TIrGlueFv
void
TIrGlue::HandleCancelPutComplete(void)
{
	NewtonErr result = fCurrentEvent->fResult;
	ReleaseEventBlock(fCurrentEvent);
	CancelPutComplete(result);
}


// ROM 0x000f0264 CancelPutComplete__7TIrGlueFl
void
TIrGlue::CancelPutComplete(NewtonErr result)
{
	fTool->DoCancelPutComplete(result);
}


/*------------------------------------------------------------------------------
	Disconnecting
------------------------------------------------------------------------------*/

// ROM 0x000eff5c DisconnectStart__7TIrGlueFl
void
TIrGlue::DisconnectStart(NewtonErr reason)
{
	fDisconnectReason = reason;
	fDisconnectState = 1;
	HandleDisconnectComplete();
}


// ROM 0x000f00ac HandleDisconnectComplete__7TIrGlueFv
// One disconnect block goes to each layer there is in turn - our LSAP
// connection, the IAS client, the IAS server, then the multiplexer (for
// the link) - each answer bringing it back here for the next; the last
// answer tears the connection's layers down.
void
TIrGlue::HandleDisconnectComplete(void)
{
	TIrEvent* event;
	if (fDisconnectState != 1)
	{
		event = fCurrentEvent;
		event->fEvent = kIrDisconnectRequest;
		event->fResult = fDisconnectReason;
	}
	else if ((event = GrabEventBlock(kIrDisconnectRequest, 0xc)) == nil)
		fDisconnectState = 6;
	else
	{
		fDisconnectState = 2;
		event->fResult = fDisconnectReason;
	}
	switch (fDisconnectState)
	{
	case 2:
		fDisconnectState = 3;
		if (fLSAPConn != nil)
		{
			fLSAPConn->EnqueueEvent(event);
			break;
		}
		// fall through
	case 3:
		fDisconnectState = 4;
		if (fNameClient != nil)
		{
			fNameClient->EnqueueEvent(event);
			break;
		}
		// fall through
	case 4:
		fDisconnectState = 5;
		if (fNameServer != nil)
		{
			fNameServer->EnqueueEvent(event);
			break;
		}
		// fall through
	case 5:
		fDisconnectState = 6;
		event->fLSAPConn = nil;
		fLMP->EnqueueEvent(event);
		break;
	case 6:
		fDisconnectState = 0;
		ReleaseEventBlock(event);
		fState = kIrGlueDisconnected;
		fMyQOS.Reset();
		fPeerQOS.Reset();
		DeInit(false);
		DisconnectComplete();
		break;
	}
}


// ROM 0x000f02e8 DisconnectComplete__7TIrGlueFv
void
TIrGlue::DisconnectComplete(void)
{
	fTool->TerminateComplete();
}


/*------------------------------------------------------------------------------
	The run queue and the event blocks
------------------------------------------------------------------------------*/

// ROM 0x000f0840 NextStateMachine__7TIrGlueFP9TIrStream
void
TIrGlue::NextStateMachine(TIrStream* stream)
{
	if (fCurrentStream == nil)
		fCurrentStream = stream;
	else if (fCurrentStream != stream)
		fPendingStreams->InsertAt(0, stream);
}


// ROM 0x000f0868 HandleInternalEvent__7TIrGlueFv
// Every stream with something queued run until none is left.
void
TIrGlue::HandleInternalEvent(void)
{
	for (;;)
	{
		while (fCurrentStream != nil)
		{
			TIrStream* stream = fCurrentStream;
			fCurrentStream = nil;
			stream->ProcessNextEvent();
		}
		if (fPendingStreams == nil)
			return;
		fCurrentStream = (TIrStream*) fPendingStreams->At(fPendingStreams->Count() - 1);
		if (fCurrentStream == nil)
			return;
		fPendingStreams->RemoveElementsAt(fPendingStreams->Count() - 1, 1);
	}
}


// ROM 0x000f08dc InitEventBlockList__7TIrGlueFv
NewtonErr
TIrGlue::InitEventBlockList(void)
{
	NewtonErr err = -7000;
	fFreeEventBlocks = new CList;
	if (fFreeEventBlocks != nil)
	{
		TIrEvent* block = GrabEventBlock(0, 0);
		if (block != nil)
			err = fFreeEventBlocks->InsertAt(fFreeEventBlocks->Count(), block);
	}
	return err;
}


// ROM 0x000f0938 DeleteEventBlockList__7TIrGlueFUc
void
TIrGlue::DeleteEventBlockList(Boolean all)
{
	if (fFreeEventBlocks == nil)
		return;
	for (ArrayIndex i = fFreeEventBlocks->Count() - 1; i >= 0; i--)
	{
		TIrEvent* block = (TIrEvent*) fFreeEventBlocks->At(i);
		fFreeEventBlocks->RemoveElementsAt(i, 1);
		::operator delete(block);
	}
	if (all)
	{
		if (fFreeEventBlocks != nil)
			delete fFreeEventBlocks;
		fFreeEventBlocks = nil;
	}
}


// ROM 0x000f09c0 GrabEventBlock__7TIrGlueFUlT1
// A free block, or a new one.  (The size is not looked at: every block is
// the biggest event's.)
TIrEvent*
TIrGlue::GrabEventBlock(ULong event, ULong size)
{
	TIrEvent* block;
	if (fFreeEventBlocks->Count() > 0)
	{
		block = (TIrEvent*) fFreeEventBlocks->At(fFreeEventBlocks->Count() - 1);
		fFreeEventBlocks->RemoveElementsAt(fFreeEventBlocks->Count() - 1, 1);
	}
	else
	{
		// DEVIATION (pointer size): the host's size (the ROM's 0x24)
		block = (TIrEvent*) ::operator new(sizeof(TIrEvent));
		if (block == nil)
			return nil;
	}
	block->fEvent = event;
	block->fResult = noErr;
	return block;
}


// ROM 0x000f0a2c ReleaseEventBlock__7TIrGlueFP8TIrEvent
void
TIrGlue::ReleaseEventBlock(TIrEvent* block)
{
	if (block == nil)
		return;
	if (fFreeEventBlocks == nil)
		::operator delete(block);
	else
		fFreeEventBlocks->InsertAt(fFreeEventBlocks->Count(), block);
}


/*------------------------------------------------------------------------------
	Passed to the tool, and from it to the link
------------------------------------------------------------------------------*/

// ROM 0x000f0adc PostAsyncEvent__7TIrGlueFUl
void TIrGlue::PostAsyncEvent(ULong data)	{ fTool->PostAsyncEvent(data); }

// ROM 0x000f0ae4 StartTerminate__7TIrGlueFl
void TIrGlue::StartTerminate(NewtonErr reason)	{ fTool->StartTerminate(reason); }

// ROM 0x000f0aec StartTimer1__7TIrGlueFUli
void TIrGlue::StartTimer1(ULong delay, int kind)	{ fTool->StartTimer1(delay, kind); }

// ROM 0x000f0af4 StopTimer1__7TIrGlueFv
void TIrGlue::StopTimer1(void)	{ fTool->StopTimer1(); }

// ROM 0x000f0afc StartTimer2__7TIrGlueFUli
void TIrGlue::StartTimer2(ULong delay, int kind)	{ fTool->StartTimer2(delay, kind); }

// ROM 0x000f0b04 StopTimer2__7TIrGlueFv
void TIrGlue::StopTimer2(void)	{ fTool->StopTimer2(); }

// ROM 0x000f0b0c StartTransmit__7TIrGlueFP15TIrLAPPutBufferUl
void TIrGlue::StartTransmit(TIrLAPPutBuffer* frame, ULong extraBOFs)	{ fTool->StartTransmit(frame, extraBOFs); }

// ROM 0x000f0b14 StopTransmit__7TIrGlueFv
void TIrGlue::StopTransmit(void)	{ fTool->StopTransmit(); }

// ROM 0x000f0b1c StartReceive__7TIrGlueFP14CBufferSegmentUcT2
void TIrGlue::StartReceive(CBufferSegment* buffer, UByte address, UByte keepLong)	{ fTool->StartReceive(buffer, address, keepLong); }

// ROM 0x000f0b2c StopReceive__7TIrGlueFv
void TIrGlue::StopReceive(void)	{ fTool->StopReceive(); }

// ROM 0x000f0bdc MediaBusy__7TIrGlueFv
Boolean TIrGlue::MediaBusy(void)	{ return fTool->MediaBusy(); }

// ROM 0x000f0be4 ReceivingInput__7TIrGlueFv
Boolean TIrGlue::ReceivingInput(void)	{ return fTool->ReceivingInput(); }

// ROM 0x000f0bec SetMediaBusy__7TIrGlueFUc
void TIrGlue::SetMediaBusy(UByte busy)	{ fTool->SetMediaBusy(busy); }

// ROM 0x000f0bf8 ChangeSpeed__7TIrGlueFUl
void TIrGlue::ChangeSpeed(ULong bitsPerSec)	{ fTool->ChangeSpeed(bitsPerSec); }

// ROM 0x000f0c00 TimerComplete__7TIrGlueFUl
void TIrGlue::TimerComplete(ULong kind)	{ fLAP->TimerComplete(kind); }

// ROM 0x000f0c08 OutputComplete__7TIrGlueFv
void TIrGlue::OutputComplete(void)	{ fLAP->OutputComplete(); }

// ROM 0x000f0c10 InputComplete__7TIrGlueFUcT1
void TIrGlue::InputComplete(UByte address, UByte control)	{ fLAP->InputComplete(address, control); }

// ROM 0x000f0c20 ConnectedAsPrimary__7TIrGlueFv
Boolean TIrGlue::ConnectedAsPrimary(void)	{ return fLAP->fPrimary; }

// ROM 0x000f0c2c CopyStatsTo__7TIrGlueFP15TCMOSlowIRStats
void TIrGlue::CopyStatsTo(TCMOSlowIRStats* stats)	{ fLAP->CopyStatsTo(stats); }

// ROM 0x000f0c50 ResetStats__7TIrGlueFv
void TIrGlue::ResetStats(void)	{ fLAP->ResetStats(); }
