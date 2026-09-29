/*
	File:		comms/irda/IrIAS.cpp

	Contains:	TIASClient and TIASServer (IrIAS.h).

	Reconstructed from the MP2x00 US ROM (0x000f0cc8-0x000f1924); each
	function cites its origin.
*/

#include "IrIAS.h"
#include "IrGlue.h"
#include "IrLMP.h"
#include "IrIASService.h"
#include "BufferSegment.h"
#include "CommErrors.h"

#include <string.h>

// an IAS frame's control byte
#define kIASLast			0x80			// the last frame of the request or reply
#define kIASAck				0x40			// an acknowledgement
#define kIASGetValueByClass	4


/*------------------------------------------------------------------------------
	TIASClient
------------------------------------------------------------------------------*/

// ROM 0x000f0cc8 __ct__10TIASClientFv
TIASClient::TIASClient()
{
	fState = 0;
	fReceiveState = 0;
	fLSAPConn = nil;
	fBuffer = nil;
	fLookupRequest = nil;
	fAttribute = nil;
}


// ROM 0x000f0d24 __dt__10TIASClientFv
TIASClient::~TIASClient()
{
	DeInit();
}


// ROM 0x000f0e68 Init__10TIASClientFP7TIrGlueP6TIrLMPP9TIrStream
// A 0x80-byte buffer and an LSAP connection with an LSAP of its own.
NewtonErr
TIASClient::Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client)
{
	fIrGlue = glue;
	fLMP = lmp;
	fClient = client;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
	{
		err = -7000;
		fBuffer = new CBufferSegment;
		if (fBuffer != nil && (err = fBuffer->Init(0x80)) == noErr)
		{
			err = -7000;
			fLSAPConn = new TLSAPConn;
			if (fLSAPConn != nil && (err = fLSAPConn->Init(fIrGlue, fLMP, this)) == noErr)
			{
				ULong lsapId = 0;
				if ((err = fIrGlue->ObtainLSAPId(lsapId)) == noErr)
				{
					fLSAPConn->AssignId(lsapId);
					return noErr;
				}
			}
		}
	}
	DeInit();
	return err;
}


// ROM 0x000f0f34 DeInit__10TIASClientFv
void
TIASClient::DeInit(void)
{
	if (fLSAPConn != nil)
	{
		delete fLSAPConn;
		fLSAPConn = nil;
	}
	if (fBuffer != nil)
	{
		delete fBuffer;
		fBuffer = nil;
	}
	if (fAttribute != nil)
	{
		delete fAttribute;
		fAttribute = nil;
	}
}


// ROM 0x000f0f9c NextState__10TIASClientFUl
void
TIASClient::NextState(ULong event)
{
	if (fState == 0)
		HandleDisconnectedStateEvent(event);
	else if (fState == 1)
		HandleConnectedStateEvent(event);
}


// ROM 0x000f0fb4 HandleDisconnectedStateEvent__10TIASClientFUl
// Connecting to the other side's LSAP 0, with no user data.
void
TIASClient::HandleDisconnectedStateEvent(ULong event)
{
	switch (event)
	{
	case kIrConnectRequest:
		fCurrentEvent->fLSAPId = 0;
		fCurrentEvent->fConnectData = nil;
		fLSAPConn->EnqueueEvent(fCurrentEvent);
		break;
	case kIrConnectReply:
		if (fCurrentEvent->fResult == noErr)
			fState = 1;
		fClient->EnqueueEvent(fCurrentEvent);
		break;
	case kIrDisconnectRequest:
		fLSAPConn->EnqueueEvent(fCurrentEvent);
		break;
	case kIrDisconnectReply:
		fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f1014 HandleConnectedStateEvent__10TIASClientFUl
void
TIASClient::HandleConnectedStateEvent(ULong event)
{
	NewtonErr err;
	switch (event)
	{
	case kIrGetDataReply:
		if ((err = fCurrentEvent->fResult) == noErr)
		{
			ParseInput();
			return;
		}
		LookupComplete(err);
		break;
	case kIrPutDataReply:
		if ((err = fCurrentEvent->fResult) == noErr)
		{
			GetStart();
			return;
		}
		LookupComplete(err);
		break;
	case kIrLookupRequest:
		if ((err = SendRequest()) != noErr)
			LookupComplete(err);
		break;
	case kIrReleaseRequest:
	case kIrDisconnectRequest:
		fLSAPConn->EnqueueEvent(fCurrentEvent);
		break;
	case kIrReleaseReply:
	case kIrDisconnectReply:
		fState = 0;
		fClient->EnqueueEvent(fCurrentEvent);
		break;
	}
}


// ROM 0x000f0d6c GetStart__10TIASClientFv
void
TIASClient::GetStart(void)
{
	fEvent.fEvent = kIrGetDataRequest;
	fEvent.fBuffer = fBuffer;
	fEvent.fOffset = 0;
	fEvent.fLength = fBuffer->GetSize();
	fLSAPConn->EnqueueEvent(&fEvent);
}


// ROM 0x000f0dbc PutStart__10TIASClientFv
void
TIASClient::PutStart(void)
{
	fEvent.fEvent = kIrPutDataRequest;
	fEvent.fBuffer = fBuffer;
	fEvent.fOffset = 0;
	fEvent.fLength = fBuffer->Position();
	fLSAPConn->EnqueueEvent(&fEvent);
}


// ROM 0x000f10e0 SendRequest__10TIASClientFv
// GetValueByClass: the class and attribute names, each after its length.
NewtonErr
TIASClient::SendRequest(void)
{
	TIrEvent* request = fCurrentEvent;
	fLookupRequest = request;
	int classLen = strlen(request->fClassName);
	int attrLen = strlen(request->fAttrName);
	if (classLen + attrLen + 3 > 0x80)
		return kCommErrBadParameter;
	fBuffer->Seek(0, kSeekFromBeginning);
	fBuffer->Put(kIASLast | kIASGetValueByClass);
	fBuffer->Put(classLen & 0xff);
	fBuffer->Putn((const UByte*) request->fClassName, classLen);
	fBuffer->Put(attrLen & 0xff);
	fBuffer->Putn((const UByte*) request->fAttrName, attrLen);
	PutStart();
	return noErr;
}


// ROM 0x000f11cc ParseInput__10TIASClientFv
// A frame of the reply: acknowledged if more are to come, parsed when it is
// the last.
void
TIASClient::ParseInput(void)
{
	fBuffer->Seek(0, kSeekFromBeginning);
	UByte control = fBuffer->Get();
	UByte last = control & kIASLast;
	UByte ack = control & kIASAck;
	NewtonErr err = kIrDAErrNoSuchClass;
	if (fReceiveState == 0)
	{
		if (ack)
			GetStart();
		else if (control == (kIASLast | kIASGetValueByClass))
			LookupComplete(ParseReply());
		else if (!last)
		{
			fReceiveState = 1;
			goto acknowledge;
		}
		else
			LookupComplete(err);
	}
	else if (fReceiveState == 1)
	{
		// the last frame of a reply in several: taken as no such class
		if (last)
		{
			fReceiveState = 0;
			LookupComplete(err);
		}
		else
			goto acknowledge;
	}
	if (fReceiveState != 1)
		return;
acknowledge:
	fBuffer->Seek(0, kSeekFromBeginning);
	fBuffer->Put(kIASAck | kIASGetValueByClass);
	PutStart();
}


// ROM 0x000f12c8 ParseReply__10TIASClientFv
NewtonErr
TIASClient::ParseReply(void)
{
	UByte returnCode = fBuffer->Get();
	if (returnCode == 0)
	{
		fAttribute = new TIASAttribute;
		if (fAttribute == nil)
			return -7000;
		return fAttribute->ExtractInfoFromBuffer(fBuffer);
	}
	if (returnCode == 1)
		return kIrDAErrNoSuchClass;
	if (returnCode == 2)
		return kIrDAErrNoSuchAttribute;
	return kCommErrNotSupported;
}


// ROM 0x000f0e0c LookupComplete__10TIASClientFl
// The lookup's block back to the glue with the attribute (the client's no
// longer).
void
TIASClient::LookupComplete(NewtonErr result)
{
	if (result != noErr && fAttribute != nil)
	{
		delete fAttribute;
		fAttribute = nil;
	}
	TIrEvent* request = fLookupRequest;
	request->fEvent = kIrLookupReply;
	request->fResult = result;
	request->fAttribute = fAttribute;
	fLookupRequest = nil;
	fAttribute = nil;
	fClient->EnqueueEvent(request);
}


/*------------------------------------------------------------------------------
	TIASServer
------------------------------------------------------------------------------*/

// ROM 0x000f1340 __ct__10TIASServerFv
TIASServer::TIASServer()
{
	fLSAPConn = nil;
	fOpCode = 0;
	fReceiveState = 0;
	fBuffer = nil;
}


// ROM 0x000f1394 __dt__10TIASServerFv
TIASServer::~TIASServer()
{
	DeInit();
}


// ROM 0x000f14bc Init__10TIASServerFP7TIrGlueP6TIrLMPP9TIrStream
// A 0x80-byte buffer and an LSAP connection on LSAP 0, listening.
NewtonErr
TIASServer::Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client)
{
	fIrGlue = glue;
	fClient = client;
	NewtonErr err = TIrStream::Init(glue);
	if (err == noErr)
	{
		err = -7000;
		fBuffer = new CBufferSegment;
		if (fBuffer != nil && (err = fBuffer->Init(0x80)) == noErr)
		{
			err = -7000;
			fLSAPConn = new TLSAPConn;
			if (fLSAPConn != nil && (err = fLSAPConn->Init(glue, lmp, this)) == noErr)
			{
				fLSAPConn->AssignId(0);
				ListenStart();
				return noErr;
			}
		}
	}
	DeInit();
	return err;
}


// ROM 0x000f1578 DeInit__10TIASServerFv
void
TIASServer::DeInit(void)
{
	if (fLSAPConn != nil)
	{
		delete fLSAPConn;
		fLSAPConn = nil;
	}
	if (fBuffer != nil)
	{
		delete fBuffer;
		fBuffer = nil;
	}
}


// ROM 0x000f15c8 SetNameService__10TIASServerFP11TIASService
void
TIASServer::SetNameService(TIASService* service)
{
	fNameService = service;
}


// ROM 0x000f13dc ListenStart__10TIASServerFv
void
TIASServer::ListenStart(void)
{
	fEvent.fEvent = kIrListenRequest;
	fEvent.fResult = noErr;
	fEvent.fDevAddr = 0;
	fEvent.fLSAPId = 0;
	fEvent.fMyQOS = &fIrGlue->fMyQOS;
	fEvent.fConnectData = nil;
	fEvent.fPeerQOS = &fIrGlue->fPeerQOS;
	fLSAPConn->EnqueueEvent(&fEvent);
}


// ROM 0x000f141c GetStart__10TIASServerFv
void
TIASServer::GetStart(void)
{
	fEvent.fEvent = kIrGetDataRequest;
	fEvent.fBuffer = fBuffer;
	fEvent.fOffset = 0;
	fEvent.fLength = fBuffer->GetSize();
	fLSAPConn->EnqueueEvent(&fEvent);
}


// ROM 0x000f146c PutStart__10TIASServerFv
void
TIASServer::PutStart(void)
{
	fEvent.fEvent = kIrPutDataRequest;
	fEvent.fBuffer = fBuffer;
	fEvent.fOffset = 0;
	fEvent.fLength = fBuffer->Position();
	fLSAPConn->EnqueueEvent(&fEvent);
}


// ROM 0x000f15d0 NextState__10TIASServerFUl
// Listening again after anything that fails; a connection is accepted and
// requests answered while the stack is not disconnecting.
void
TIASServer::NextState(ULong event)
{
	TIrEvent* current = fCurrentEvent;
	if (current->fEvent == kIrDisconnectRequest)
	{
		fLSAPConn->EnqueueEvent(current);
		return;
	}
	if (current->fEvent == kIrDisconnectReply)
	{
		fClient->EnqueueEvent(current);
		return;
	}
	if (current->fResult != noErr)
		event = kIrListenRequest;
	if (fIrGlue->fDisconnectState != 0)
		return;
	switch (event)
	{
	case kIrListenRequest:
		ListenStart();
		break;
	case kIrListenReply:
		current->fEvent = kIrAcceptRequest;
		fLSAPConn->EnqueueEvent(current);
		break;
	case kIrAcceptReply:
	case kIrPutDataReply:
		GetStart();
		break;
	case kIrGetDataReply:
		ParseInput();
		break;
	}
}


// ROM 0x000f1660 ParseInput__10TIASServerFv
// A request frame: acknowledged while more are to come, answered when the
// last is in (only GetValueByClass is known).
void
TIASServer::ParseInput(void)
{
	UByte returnCode;							// (not set on every path, as in the ROM)
	TIASAttribute* attribute = nil;				// (likewise)
	fBuffer->Seek(0, kSeekFromBeginning);
	UByte control = fBuffer->Get();
	UByte last = control & kIASLast;
	UByte ack = control & kIASAck;
	if (fReceiveState == 0)
	{
		if (ack == 0)
		{
			fOpCode = control & 0x3f;
			if (!last)
			{
				fReceiveState = 1;
				goto acknowledge;
			}
			if (fOpCode == kIASGetValueByClass)
				attribute = ParseRequest(returnCode);
			else
			{
				attribute = nil;
				returnCode = 0xff;
			}
		}
	}
	else if (fReceiveState == 1)
	{
		if (!last)
			goto acknowledge;
		// the last frame of a request in several: answered no such class
		ack = 0;
		attribute = nil;
		returnCode = (fOpCode == kIASGetValueByClass) ? 1 : 0xff;
	}
	if (last && !ack)
	{
		SendResponse(returnCode, attribute);
		fOpCode = 0;
		fReceiveState = 0;
		return;
	}
	if (fReceiveState != 1)
	{
		GetStart();
		return;
	}
acknowledge:
	fBuffer->Seek(0, kSeekFromBeginning);
	fBuffer->Put(fOpCode | kIASAck);
	PutStart();
}


// ROM 0x000f17b4 ParseRequest__10TIASServerFRUc
// The class and the attribute looked up: 1 no such class, 2 no such
// attribute, 0 found.
TIASAttribute*
TIASServer::ParseRequest(UByte& returnCode)
{
	char name[0x40];
	returnCode = 1;
	if (GotAValidString((UByte*) name))
	{
		TIASClass* theClass = fNameService->FindClass(name);
		if (theClass != nil)
		{
			returnCode = 2;
			if (GotAValidString((UByte*) name))
			{
				TIASAttribute* attribute = theClass->FindAttribute(name);
				if (attribute != nil)
				{
					returnCode = 0;
					return attribute;
				}
			}
		}
	}
	return nil;
}


// ROM 0x000f183c GotAValidString__10TIASServerFPUc
// A length (at most 60) and so many characters.
Boolean
TIASServer::GotAValidString(UByte* string)
{
	int length = fBuffer->Get() & 0xff;
	if (length > 0x3c)
		return false;
	if (fBuffer->Getn(string, length) != length)
		return false;
	string[length] = 0;
	return true;
}


// ROM 0x000f18a4 SendResponse__10TIASServerFUcP13TIASAttribute
void
TIASServer::SendResponse(UByte returnCode, TIASAttribute* attribute)
{
	fBuffer->Seek(0, kSeekFromBeginning);
	fBuffer->Put(fOpCode | kIASLast);
	fBuffer->Put(returnCode);
	if (returnCode == 0)
		attribute->AddInfoToBuffer(fBuffer);
	PutStart();
}
