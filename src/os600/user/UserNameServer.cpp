/*
	File:		user/UserNameServer.cpp

	Contains:	TUNameServer (NameServer.h): the client of the name server.
				A request is a TNameRequest sent by RPC to the name server
				port, with the name and type strings passed in two shared
				memory objects the client keeps for the purpose.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "NameServer.h"
#include "SystemEvents.h"
#include "NewtonGestalt.h"
#include "UserGlobals.h"
#include "OSErrors.h"

#include <string.h>

// The type ports are registered under (NameServer.h declares it; the ROM has
// no symbol for it - the string is at 0x002252b0).
const char kTUPort[] = "TUPort";


// ROM 0x00132408 __ct__18TNameServerRequestFv
TNameServerRequest::TNameServerRequest()
{
	fCommand = kRegisterName;
}


// ROM 0x00132a3c __ct__12TNameRequestFv
TNameRequest::TNameRequest()
{
	fCommand = kLookup;
	fThing = 0;
	fSpec = 0;
	fObjectName = 0;
	fObjectType = 0;
}


// ROM 0x00133184 __ct__16TSysEventRequestFv
TSysEventRequest::TSysEventRequest()
{
	fCommand = kRegisterForSystemEvent;
	fTheEvent = kSysEvent_PowerOn;
	fSysEventObjId = 0;
	fSysEventTimeOut = 0;
	fSysEventSendFilter = 0;
}


// ROM 0x00133894 __ct__15TGestaltRequestFv
TGestaltRequest::TGestaltRequest()
{
	fCommand = kGestalt;
	fSelector = kGestalt_Version;
}


// ROM 0x001338dc __ct__22TResArbitrationRequestFv
TResArbitrationRequest::TResArbitrationRequest()
{
	fCommand = kResourceArbitration;
	fRequestType = kResArbitrationClaim;
	fOwnerPortId = 0;
	fOwnerName = 0;
}


// ROM 0x0013392c __ct__16TNameServerReplyFv
TNameServerReply::TNameServerReply()
{
	fResult = noErr;
	fThing = 0;
	fSpec = 0;
}


/* -------------------------------------------------------------------------------
	TUNameServer
------------------------------------------------------------------------------- */

// ROM 0x00131f1c __ct__12TUNameServerFv
// The name server port, and a shared memory object each for the name and
// the type strings of a request.
TUNameServer::TUNameServer()
{
	fNameServerPort.CopyObject(GetPortSWI(kGetNameServerPort));
	fMsgName = new TUSharedMem;
	if (fMsgName != nil)
		fMsgName->Init();
	fMsgType = new TUSharedMem;
	if (fMsgType != nil)
		fMsgType->Init();
}


// ROM 0x00131fa0 __dt__12TUNameServerFv
TUNameServer::~TUNameServer()
{
	if (fMsgName != nil)
		delete fMsgName;
	if (fMsgType != nil)
		delete fMsgType;
}


// ROM 0x00132014 RegisterName__12TUNameServerFPcT1UlT3
NewtonErr
TUNameServer::RegisterName(char* name, char* type, ULong thing, ULong spec)
{
	TNameRequest request;
	TNameServerReply reply;
	if (fMsgName == nil || fMsgType == nil)
		return kError_No_Memory;
	fMsgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	fMsgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fCommand = kRegisterName;
	request.fSpec = spec;
	request.fObjectName = *fMsgName;
	request.fObjectType = *fMsgType;
	request.fThing = thing;
	ULong returnSize;
	return fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
}


// ROM 0x00132114 UnRegisterName__12TUNameServerFPcT1
NewtonErr
TUNameServer::UnRegisterName(char* name, char* type)
{
	TNameRequest request;
	TNameServerReply reply;
	if (fMsgName == nil || fMsgType == nil)
		return kError_No_Memory;
	fMsgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	fMsgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fCommand = kUnregisterName;
	request.fObjectName = *fMsgName;
	request.fObjectType = *fMsgType;
	ULong returnSize;
	return fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
}


// ROM 0x00132204 WaitForRegister__12TUNameServerFPcT1PUlT3
// Blocks until the name is registered (the name server holds the reply).
NewtonErr
TUNameServer::WaitForRegister(char* name, char* type, ULong* thing, ULong* spec)
{
	TNameRequest request;
	TNameServerReply reply;
	if (fMsgName == nil || fMsgType == nil)
		return kError_No_Memory;
	fMsgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	fMsgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fCommand = kWaitForRegister;
	request.fObjectName = *fMsgName;
	request.fObjectType = *fMsgType;
	ULong returnSize;
	NewtonErr err = fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
	if (err == noErr)
	{
		*thing = reply.fThing;
		*spec = reply.fSpec;
	}
	return err;
}


// ROM 0x00132318 WaitForUnregister__12TUNameServerFPcT1
NewtonErr
TUNameServer::WaitForUnregister(char* name, char* type)
{
	TNameRequest request;
	TNameServerReply reply;
	if (fMsgName == nil || fMsgType == nil)
		return kError_No_Memory;
	fMsgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	fMsgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fCommand = kWaitForUnregister;
	request.fObjectName = *fMsgName;
	request.fObjectType = *fMsgType;
	ULong returnSize;
	return fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
}


// ROM 0x00132438 Lookup__12TUNameServerFPcT1PUlT3
NewtonErr
TUNameServer::Lookup(char* name, char* type, ULong* thing, ULong* spec)
{
	TNameRequest request;
	TNameServerReply reply;
	if (fMsgName == nil || fMsgType == nil)
		return kError_No_Memory;
	fMsgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	fMsgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fCommand = kLookup;
	request.fObjectName = *fMsgName;
	request.fObjectType = *fMsgType;
	ULong returnSize;
	NewtonErr err = fNameServerPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
	if (err == noErr)
	{
		*thing = reply.fThing;
		*spec = reply.fSpec;
	}
	return err;
}


// The resource arbitration calls are TResArbitrationRequests by RPC; the
// reply's fResult (kError_Call_Not_Implemented while the server side is
// NOT YET RECONSTRUCTED) is the answer.
static NewtonErr
ResourceArbitrationRequest(TUPort& port, TUSharedMem* msgName, TUSharedMem* msgType, char* name, char* type, ULong requestType, TObjectId ownerPortId, TObjectId ownerName)
{
	TResArbitrationRequest request;
	TNameServerReply reply;
	if (msgName == nil || msgType == nil)
		return kError_No_Memory;
	msgName->SetBuffer(name, strlen(name) + 1, kSMemReadOnly);
	msgType->SetBuffer(type, strlen(type) + 1, kSMemReadOnly);
	request.fRequestType = requestType;
	request.fOwnerName = ownerName;
	request.fObjectName = *msgName;
	request.fObjectType = *msgType;
	request.fOwnerPortId = ownerPortId;
	ULong returnSize;
	NewtonErr err = port.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply));
	if (err == noErr)
		err = reply.fResult;
	return err;
}


// ROM 0x0013254c ResourceClaim__12TUNameServerFPcT1UlT3
NewtonErr
TUNameServer::ResourceClaim(char* name, char* type, TObjectId ownerPortId, TObjectId applicationNameId)
{
	return ResourceArbitrationRequest(fNameServerPort, fMsgName, fMsgType, name, type, kResArbitrationClaim, ownerPortId, applicationNameId);
}


// ROM 0x00132658 ResourcePassiveClaim__12TUNameServerFPcT1UlT3
NewtonErr
TUNameServer::ResourcePassiveClaim(char* name, char* type, TObjectId ownerPortId, TObjectId applicationNameId)
{
	return ResourceArbitrationRequest(fNameServerPort, fMsgName, fMsgType, name, type, kResArbitrationPassiveClaim, ownerPortId, applicationNameId);
}


// ROM 0x00132764 ResourceUnclaim__12TUNameServerFPcT1
NewtonErr
TUNameServer::ResourceUnclaim(char* name, char* type)
{
	return ResourceArbitrationRequest(fNameServerPort, fMsgName, fMsgType, name, type, kResArbitrationUncliam, 0, 0);
}


// ROM 0x00132860 ResourcePassiveUnclaim__12TUNameServerFPcT1
NewtonErr
TUNameServer::ResourcePassiveUnclaim(char* name, char* type)
{
	return ResourceArbitrationRequest(fNameServerPort, fMsgName, fMsgType, name, type, kResArbitrationPassiveUnclaim, 0, 0);
}
