/*
	File:		comms/fax/FaxToolInterface.cpp

	Contains:	TFaxToolInterface: the fax tool driven directly in comm tool
				requests (comms/fax/FaxToolInterface.h).

	Reconstructed from the MP2x00 US ROM (0x000b9794-0x000bb274); each
	function cites its origin.
*/

#include "FaxToolInterface.h"
#include "CommManager.h"
#include "ModemNavigator.h"
#include "ModemOptions.h"
#include "CommAddresses.h"
#include "CommOptions.h"
#include "utility/AppWorld.h"
#include "UserTasks.h"

const NewtonErr	kFaxErrOptionPending = kCommErrCommandInProgress;	// -16001: an option request is already pending
const NewtonErr	kFaxErrNoOptions = kCommErrBadParameter;			// -16006


// ROM 0x000b9794 __ct__17TFaxToolInterfaceFUlT1
TFaxToolInterface::TFaxToolInterface(ULong serviceId, ULong toolId)
{
	fToolId = toolId;
	fServiceId = serviceId;
	fReserved54 = 0;
	fGetting = false;
	fBinding = false;
	fConnecting = false;
	fListening = false;
	fOptionBusy = false;
}


// ROM 0x000b98b4 __dt__17TFaxToolInterfaceFv
// The tool closed (a synchronous control request of op code 2, close)
// before the messages and buffers go.
TFaxToolInterface::~TFaxToolInterface()
{
	TCommToolReply reply;
	TCommToolControlRequest request;
	request.fOpCode = kCommToolClose;
	ULong returnSize;
	fToolPort.SendRPC(&returnSize, &request, sizeof(request), &reply, sizeof(reply), 0, kCommToolRequestTypeControl);
}


// ROM 0x000b9f3c AETestEvent__17TFaxToolInterfaceFP7TAEvent
Boolean
TFaxToolInterface::AETestEvent(TAEvent* event)
{
	return event->fAEventClass == kNewtEventClass && event->fAEventID == 'faxs';
}


// the session's result, if the tool says the session option came back
// processed and without error (and a pointer to it, copied)
static TOption*
FindSessionInfo(TOptionArray* options)
{
	TOptionIterator iter(options);
	TOption* option = iter.FindOption(kCMOFaxSessionInfo);
	if (option != nil && option->IsProcessed() && option->GetOpCodeResults() == 0)
		return option;
	return nil;
}


// ROM 0x000b9f68 AECompletionProc__17TFaxToolInterfaceFP10TUMsgTokenPUlP7TAEvent
// A request sent asynchronously has come back: which one is told by the
// message, and what is pending by the flags.
void
TFaxToolInterface::AECompletionProc(TUMsgToken* token, ULong* /*size*/, TAEvent* /*event*/)
{
	ULong msgId = token->GetMsgId();
	NewtonErr err = noErr;
	if (msgId == fConnectMsgId)
	{
		if (fBinding)
		{
			fBinding = false;
			err = fConnectReply.fResult;
			if (err == noErr)
			{
				PostConnect(true);
				return;
			}
		}
		else if (fConnecting)
		{
			fConnecting = false;
			err = fConnectReply.fResult;
			if (err == noErr)
			{
				if (fListening)
				{
					if (fConnectRequest.fOpCode != kCommToolListen)
					{
						AcceptSessionComplete(noErr, fSessionInfo.fHorizontalRes, fSessionInfo.fVerticalRes, fSessionInfo.fBitRate);
						return;
					}
					// listened: the session found, now accepted
					TOption* info = FindSessionInfo(fSessionOptions);
					if (info != nil)
					{
						memmove(&fSessionInfo, info, sizeof(TCMOFaxSessionInfo));
						fConnectRequest.fOpCode = kCommToolAccept;
						fConnectRequest.fOptions = nil;
						err = fToolPort.SendRPC(&fConnectMsg, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, nil, kCommToolRequestTypeControl);
						if (err == noErr)
							return;
					}
				}
				else
				{
					CleanUpAfterConnect();
					TOption* info = FindSessionInfo(fSessionOptions);
					if (info != nil)
					{
						memmove(&fSessionInfo, info, sizeof(TCMOFaxSessionInfo));
						OpenSessionComplete(fConnectReply.fResult, 1, 0, fSessionInfo.fHorizontalRes, fSessionInfo.fVerticalRes);
						return;
					}
				}
			}
		}
		else
		{
			CloseSessionComplete(fConnectReply.fResult);
			return;
		}
		if (fListening)
			AcceptSessionComplete(err, 0, 0, 0);
		else
			OpenSessionComplete(err, 0, 0, 0, 0);
	}
	else if (msgId == fOptionMsgId)
	{
		TOptionExtended* option = (TOptionExtended*) fOptions.OptionAt(0);
		fOptionBusy = false;
		switch (fOptionOp)
		{
		case kFaxOptionBeginPage:
			err = fOptionReply.fResult;
			if (err == noErr)
				err = option->GetExtendedResult();
			BeginPageComplete(err);
			break;
		case kFaxOptionEndPage:
			err = fOptionReply.fResult;
			if (err == noErr)
				err = option->GetExtendedResult();
			EndPageComplete(err);
			break;
		case kFaxOptionConfirmPage:
			err = fOptionReply.fResult;
			if (err == noErr)
				err = option->GetExtendedResult();
			ConfirmReceivedPageComplete(err, ((TCMOFaxEndMessage*) option)->fLastPage);
			break;
		case kFaxOptionPrintBand:
			err = fOptionReply.fResult;
			if (err == noErr)
				err = (NewtonErr) (Long) option->GetOpCodeResults();
			PrintBandContinue(err, true);
			break;
		default:
			break;
		}
	}
	else if (msgId == fKillMsgId)
		ContinueClose();
	else if (msgId == fDataMsgId)
	{
		if (fGetting)
		{
			fGetting = false;
			GetBandComplete(fGetReply.fResult, fGetReply.fGetBytesCount, fGetReply.fEndOfFrame);
		}
		else
			PrintBandComplete(fPutReply.fResult);
	}
}


// ROM 0x000ba324 IdleProc__17TFaxToolInterfaceFP10TUMsgTokenPUlP7TAEvent
void
TFaxToolInterface::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x000ba328 Init__17TFaxToolInterfaceFP12TOptionArrayUlT2
// The handler registered for the events asked (the fax driver: 'faxs'
// in 'newt'), and the fax service started with the options.
NewtonErr
TFaxToolInterface::Init(TOptionArray* options, ULong eventId, ULong eventClass)
{
	NewtonErr err = TAEventHandler::Init(eventId, eventClass);
	if (err != noErr)
		return err;
	return DoInit(options);
}


// ROM 0x000ba364 ContinueClose__17TFaxToolInterfaceFv
// The kill done: the disconnect, answered through the connect message.
void
TFaxToolInterface::ContinueClose()
{
	fConnectRequest.fOpCode = kCommToolDisconnect;
	fConnecting = false;
	NewtonErr err = fToolPort.SendRPC(&fConnectMsg, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, nil, kCommToolRequestTypeControl);
	if (err != noErr)
		CloseSessionComplete(err);
}


// ROM 0x000ba3f8 PostBind__17TFaxToolInterfaceFUc
// The tool bound to its address: the connect (or listen) follows.
void
TFaxToolInterface::PostBind(Boolean async)
{
	fBindRequest.fOptions = nil;
	fBindRequest.fOutside = false;
	NewtonErr err;
	if (async)
	{
		fBinding = true;
		err = fToolPort.SendRPC(&fConnectMsg, &fBindRequest, sizeof(fBindRequest), &fConnectReply, sizeof(fConnectReply), 0, nil, kCommToolRequestTypeControl);
		if (err == noErr)
			return;
	}
	else
	{
		ULong returnSize;
		err = fToolPort.SendRPC(&returnSize, &fBindRequest, sizeof(fBindRequest), &fConnectReply, sizeof(fConnectReply), 0, kCommToolRequestTypeControl);
		if (err == noErr)
			err = fConnectReply.fResult;
		if (err == noErr)
		{
			PostConnect(false);
			return;
		}
	}
	CleanUpAfterConnect();
	if (fListening == 1)
		AcceptSessionComplete(err, 0, 0, 0);
	else
		OpenSessionComplete(err, 0, 0, 0, 0);
}


// ROM 0x000ba56c PostConnect__17TFaxToolInterfaceFUc
// The connect (listen, when answering) with the session's options.  Done
// synchronously, a connect is followed by an accept too, and the
// session's result read straight back.
void
TFaxToolInterface::PostConnect(Boolean async)
{
	fConnectRequest.fOpCode = (fListening == 1) ? kCommToolListen : kCommToolConnect;
	fSessionOptions->Reset();
	fConnectRequest.fOptions = fSessionOptions;
	fConnectRequest.fOptionCount = fSessionOptions->GetArrayCount();
	fConnectRequest.fData = nil;
	fConnectRequest.fOutside = false;
	NewtonErr err;
	if (async)
	{
		fConnecting = true;
		err = fToolPort.SendRPC(&fConnectMsg, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, nil, kCommToolRequestTypeControl);
		if (err == noErr)
			return;
	}
	else
	{
		ULong returnSize;
		err = fToolPort.SendRPC(&returnSize, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, kCommToolRequestTypeControl);
		if (err == noErr)
			err = fConnectReply.fResult;
		if (err == noErr)
		{
			CleanUpAfterConnect();
			fConnectRequest.fOpCode = kCommToolAccept;
			fConnectRequest.fOptions = nil;
			err = fToolPort.SendRPC(&returnSize, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, kCommToolRequestTypeControl);
			if (err == noErr)
				err = fConnectReply.fResult;
			if (err == noErr)
			{
				TOption* info = FindSessionInfo(fSessionOptions);
				if (info != nil)
				{
					memmove(&fSessionInfo, info, sizeof(TCMOFaxSessionInfo));
					if (fListening == 1)
						AcceptSessionComplete(fConnectReply.fResult, fSessionInfo.fHorizontalRes, fSessionInfo.fVerticalRes, fSessionInfo.fBitRate);
					else
						OpenSessionComplete(fConnectReply.fResult, 1, 0, fSessionInfo.fHorizontalRes, fSessionInfo.fVerticalRes);
					return;
				}
				err = kFaxErrNoOptions;
			}
		}
	}
	CleanUpAfterConnect();
	if (fListening == 1)
		AcceptSessionComplete(err, 0, 0, 0);
	else
		OpenSessionComplete(err, 0, 0, 0, 0);
}


// ROM 0x000ba850 DoInit__17TFaxToolInterfaceFP12TOptionArray
// The modem found (the modem navigator), the fax service started by the
// comm manager and its port kept, and the four async messages and the
// requests set up.
NewtonErr
TFaxToolInterface::DoInit(TOptionArray* options)
{
	NewtonErr err = RunModemNavigator(options);
	if (err != noErr)
		return err;
	TServiceInfo info;
	err = CMStartService(options, &info);
	if (err != noErr)
		return err;
	fToolPort.CopyObject(info.GetPortId());
	fOptionBusy = false;
	fOptionRequest.fOptions = &fOptions;
	fOptionRequest.fOutside = false;
	fOptionRequest.fCopyBack = true;
	fOptionRequest.fRequestOpCode = opProcess;
	if ((err = InitAsyncMsg(&fConnectMsg, &fConnectMsgId)) != noErr
	 || (err = InitAsyncMsg(&fOptionMsg, &fOptionMsgId)) != noErr
	 || (err = InitAsyncMsg(&fKillMsg, &fKillMsgId)) != noErr
	 || (err = InitAsyncMsg(&fDataMsg, &fDataMsgId)) != noErr
	 || (err = fOptions.Init()) != noErr
	 || (err = fBandOptions.Init()) != noErr)
		return err;
	fSendBand.SetOpCode(opSetRequired);
	fPutRequest.fOptions = &fBandOptions;
	fPutRequest.fData = &fBandList;
	fPutRequest.fOutside = false;
	fPutRequest.fFrameData = false;
	fGetRequest.fOptions = nil;
	fGetRequest.fOptionCount = 0;
	return fBandList.Init(false);
}


// ROM 0x000ba97c InitAsyncMsg__17TFaxToolInterfaceFP14TUAsyncMessagePUl
// An async message whose replies come to the world's port, with this
// handler as its refcon (which is how the world finds the handler).
NewtonErr
TFaxToolInterface::InitAsyncMsg(TUAsyncMessage* msg, ULong* msgId)
{
	NewtonErr err = msg->Init(true);
	if (err != noErr)
		return err;
	err = msg->SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	if (err != noErr)
		return err;
	err = msg->SetUserRefCon((ULong) this);
	if (err == noErr)
		*msgId = msg->GetMsgId();
	return err;
}


// ROM 0x000ba9e0 InitConnect__17TFaxToolInterfaceFPUcUl
// The number to dial added to the session's options (when there is one),
// and the session option asked for back.  (The ROM inserts the session
// option by hand, with TOptionArray::InsertOptionAt's own code.)
NewtonErr
TFaxToolInterface::InitConnect(UChar* number, ULong numberLength)
{
	NewtonErr err;
	if (number != nil && numberLength != 0)
	{
		TCMAPhoneNumber phone(numberLength);
		err = fSessionOptions->InsertVarOptionAt(fSessionOptions->GetArrayCount(), &phone, number, numberLength);
		if (err != noErr)
			return err;
	}
	fSessionInfo.Reset();
	fSessionInfo.SetOpCode(opGetCurrent);
	return fSessionOptions->InsertOptionAt(fSessionOptions->GetArrayCount(), &fSessionInfo);
}


// ROM 0x000baa68 CleanUpAfterConnect__17TFaxToolInterfaceFv
void
TFaxToolInterface::CleanUpAfterConnect()
{ }


// ROM 0x000baa6c OpenSession__17TFaxToolInterfaceFP12TOptionArrayPUcUlUc
// A call made: the number dialled, a session sending.
void
TFaxToolInterface::OpenSession(TOptionArray* options, UChar* number, ULong numberLength, Boolean async)
{
	NewtonErr err = kFaxErrNoOptions;
	if (options != nil)
	{
		fSessionOptions = options;
		err = InitConnect(number, numberLength);
		if (err == noErr)
		{
			fListening = false;
			PostBind(async);
			return;
		}
	}
	OpenSessionComplete(err, 0, 0, 0, 0);
}


// ROM 0x000baaf8 AcceptSession__17TFaxToolInterfaceFP12TOptionArrayUc
// A call answered: a session receiving.  (No bind: straight to the
// listen.)
void
TFaxToolInterface::AcceptSession(TOptionArray* options, Boolean async)
{
	NewtonErr err = kFaxErrNoOptions;
	if (options != nil)
	{
		fSessionOptions = options;
		err = InitConnect(nil, 0);
		if (err == noErr)
		{
			fListening = true;
			PostConnect(async);
			return;
		}
	}
	AcceptSessionComplete(err, 0, 0, 0);
}


// ROM 0x000bab80 CloseSession__17TFaxToolInterfaceFUc
// The session ended: every control request killed, then the disconnect
// (asynchronously, ContinueClose sends it when the kill comes back).
void
TFaxToolInterface::CloseSession(Boolean async)
{
	fKillRequest.fRequestsToKill = kCommToolRequestTypeControl;
	NewtonErr err;
	if (async)
	{
		err = fToolPort.SendRPC(&fKillMsg, &fKillRequest, sizeof(fKillRequest), &fKillReply, sizeof(fKillReply), 0, nil, kCommToolRequestTypeKill);
		if (err == noErr)
			return;
	}
	else
	{
		ULong returnSize;
		err = fToolPort.SendRPC(&returnSize, &fKillRequest, sizeof(fKillRequest), &fKillReply, sizeof(fKillReply), 0, kCommToolRequestTypeKill);
		if (err == noErr)
		{
			fConnectRequest.fOpCode = kCommToolDisconnect;
			err = fToolPort.SendRPC(&returnSize, &fConnectRequest, sizeof(fConnectRequest), &fConnectReply, sizeof(fConnectReply), 0, kCommToolRequestTypeControl);
			if (err == noErr)
				err = fConnectReply.fResult;
		}
	}
	CloseSessionComplete(err);
}


// an option request: the option alone in fOptions, sent asynchronously
// (for op, the completion's) or synchronously (==> the RPC's error)
static NewtonErr
SendOption(TFaxToolInterface* fax, TOption* option, Boolean async, long op, Boolean* sent)
{
	*sent = false;
	NewtonErr err = fax->fOptions.RemoveAllOptions();
	if (err == noErr)
		err = fax->fOptions.InsertOptionAt(fax->fOptions.GetArrayCount(), option);
	if (err != noErr)
		return err;
	*sent = true;
	if (async)
	{
		fax->fOptionOp = op;
		err = fax->fToolPort.SendRPC(&fax->fOptionMsg, &fax->fOptionRequest, sizeof(fax->fOptionRequest), &fax->fOptionReply, sizeof(fax->fOptionReply), 0, nil, kCommToolRequestTypeControl);
		if (err == noErr)
			fax->fOptionBusy = true;
		return err;
	}
	ULong returnSize;
	return fax->fToolPort.SendRPC(&returnSize, &fax->fOptionRequest, sizeof(fax->fOptionRequest), &fax->fOptionReply, sizeof(fax->fOptionReply), 0, kCommToolRequestTypeControl);
}

// what a synchronous option request came to: the RPC's error, the
// reply's, or the option's own
static NewtonErr
OptionResult(TFaxToolInterface* fax, NewtonErr rpcErr)
{
	TOptionExtended* option = (TOptionExtended*) fax->fOptions.OptionAt(0);
	if (rpcErr != noErr)
		return rpcErr;
	if (fax->fOptionReply.fResult != noErr)
		return fax->fOptionReply.fResult;
	return option->GetExtendedResult();
}


// ROM 0x000bace4 BeginPage__17TFaxToolInterfaceFUc
// A page begun: 'fsgp', answered when the fax tool is ready for it.
void
TFaxToolInterface::BeginPage(Boolean async)
{
	NewtonErr err = kFaxErrOptionPending;
	if (!fOptionBusy)
	{
		TCMOFaxStartPage option;
		Boolean sent;
		err = SendOption(this, &option, async, kFaxOptionBeginPage, &sent);
		if (sent)
		{
			if (async)
			{
				if (err != noErr)
					BeginPageComplete(err);
			}
			else
				BeginPageComplete(OptionResult(this, err));
			return;
		}
	}
	BeginPageComplete(err);
}


// ROM 0x000bae7c EndPage__17TFaxToolInterfaceFUcT1
// A page ended: 'feom', saying whether it is the last.
void
TFaxToolInterface::EndPage(Boolean async, Boolean lastPage)
{
	NewtonErr err = kFaxErrOptionPending;
	if (!fOptionBusy)
	{
		TCMOFaxEndMessage option;
		option.fLastPage = lastPage;
		Boolean sent;
		err = SendOption(this, &option, async, kFaxOptionEndPage, &sent);
		if (sent)
		{
			if (async)
			{
				if (err != noErr)
					EndPageComplete(err);
			}
			else
				EndPageComplete(OptionResult(this, err));
			return;
		}
	}
	EndPageComplete(err);
}


// ROM 0x000b9b58 ConfirmReceivedPage__17TFaxToolInterfaceFUcT1
// A received page kept (or not): 'feom' the other way.
void
TFaxToolInterface::ConfirmReceivedPage(Boolean accepted, Boolean async)
{
	NewtonErr err = kFaxErrOptionPending;
	if (!fOptionBusy)
	{
		TCMOFaxEndMessage option;
		option.fPageAccepted = accepted;
		Boolean sent;
		err = SendOption(this, &option, async, kFaxOptionConfirmPage, &sent);
		if (sent)
		{
			if (async)
			{
				if (err != noErr)
					ConfirmReceivedPageComplete(err, false);
			}
			else
			{
				NewtonErr result = OptionResult(this, err);
				ConfirmReceivedPageComplete(result, ((TCMOFaxEndMessage*) fOptions.OptionAt(0))->fLastPage);
			}
			return;
		}
	}
	ConfirmReceivedPageComplete(err, false);
}


// ROM 0x000b9d04 SetMinScanLineTime__17TFaxToolInterfaceFUl
// The minimum scan line time set, synchronously.  (Nothing in the ROM
// calls it.)  DEVIATION: when neither the RPC nor the reply failed, the
// ROM answers the word after the option's time - past the end of the
// option, whatever follows it in the array; the host answers nought.
NewtonErr
TFaxToolInterface::SetMinScanLineTime(ULong time)
{
	NewtonErr err = kFaxErrOptionPending;
	if (!fOptionBusy)
	{
		TCMOFaxMinScanLineTime option;
		err = fOptions.RemoveAllOptions();
		if (err == noErr)
		{
			option.fTime = time;
			err = fOptions.InsertOptionAt(fOptions.GetArrayCount(), &option);
			if (err == noErr)
			{
				ULong returnSize;
				err = fToolPort.SendRPC(&returnSize, &fOptionRequest, sizeof(fOptionRequest), &fOptionReply, sizeof(fOptionReply), 0, kCommToolRequestTypeControl);
				fOptions.OptionAt(0);
			}
		}
		if (err == noErr)
			err = fOptionReply.fResult;
	}
	return err;
}


// ROM 0x000b9df4 SetDefaultConfig__17TFaxToolInterfaceFP12TOptionArrayUl
// The services to start: the fax tool over the modem tool (on the port
// given), a fax connection, sending.
void
TFaxToolInterface::SetDefaultConfig(TOptionArray* options, ULong portId)
{
	TCMOServiceIdentifier service;
	TCMOModemConnectType connectType;
	TCMOFaxDirection direction;
	options->RemoveAllOptions();
	service.fServiceId = 'faxs';
	service.SetAsService();
	if (options->InsertOptionAt(options->GetArrayCount(), &service) != noErr)
		return;
	service.fServiceId = 'mods';
	service.fPortId = portId;
	service.SetAsService();
	if (options->InsertOptionAt(options->GetArrayCount(), &service) != noErr)
		return;
	connectType.SetOpCode(opSetRequired);
	connectType.fData = false;
	connectType.fFax = true;
	if (options->InsertOptionAt(options->GetArrayCount(), &connectType) != noErr)
		return;
	direction.SetOpCode(opSetRequired);
	direction.fReceive = false;
	direction.fSend = true;
	options->InsertOptionAt(options->GetArrayCount(), &direction);
}


// ROM 0x000b9eec SetDefaultOptions__17TFaxToolInterfaceFP12TOptionArray
// (the same code as SetFaxOptions, which it branches to)
void
TFaxToolInterface::SetDefaultOptions(TOptionArray* options)
{
	SetFaxOptions(options, true);
}


// ROM 0x000b9ef4 SetFaxOptions__17TFaxToolInterfaceFP12TOptionArrayUc
// The page's set-up, as the tool's defaults have it (whichever way the
// fax goes).
void
TFaxToolInterface::SetFaxOptions(TOptionArray* options, Boolean /*send*/)
{
	TCMOFaxPageSetUp setUp;
	options->RemoveAllOptions();
	setUp.SetOpCode(opSetRequired);
	options->InsertOptionAt(options->GetArrayCount(), &setUp);
}


// ROM 0x000bb01c PrintBand__17TFaxToolInterfaceFPUcUlN22Uc
// A band of scan lines put: its bytes as the put's buffer, its layout as
// the put's 'fcsb'.  (No data - nil and no bytes a line - is that many
// white lines.)
void
TFaxToolInterface::PrintBand(UChar* data, ULong lines, ULong bytesPerLine, ULong leftOffset, Boolean async)
{
	NewtonErr err = fBandList.RemoveAll();
	if (err == noErr
	 && (err = fBandSegment.Init(data, bytesPerLine * lines, false, 0, -1)) == noErr
	 && (err = fBandList.InsertLast(&fBandSegment)) == noErr)
	{
		fSendBand.fBytesPerLine = bytesPerLine;
		fSendBand.fLeftOffset = leftOffset;
		fSendBand.fLines = lines;
		err = fBandOptions.RemoveAllOptions();
		if (err == noErr
		 && (err = fBandOptions.InsertOptionAt(fBandOptions.GetArrayCount(), &fSendBand)) == noErr)
		{
			if (!async)
			{
				ULong returnSize;
				err = fToolPort.SendRPC(&returnSize, &fPutRequest, sizeof(fPutRequest), &fPutReply, sizeof(fPutReply), 0, kCommToolRequestTypePut);
				if (err == noErr)
					err = fPutReply.fResult;
				PrintBandComplete(err);
				return;
			}
			err = fToolPort.SendRPC(&fDataMsg, &fPutRequest, sizeof(fPutRequest), &fPutReply, sizeof(fPutReply), 0, nil, kCommToolRequestTypePut);
			if (err == noErr)
				return;
		}
	}
	PrintBandComplete(err);
}


// ROM 0x000bb1c8 PrintBandContinue__17TFaxToolInterfaceFlUc
// The band put again from its start (after an option request came back).
void
TFaxToolInterface::PrintBandContinue(NewtonErr err, Boolean async)
{
	if (err == noErr)
	{
		fPutRequest.fData = &fBandList;
		fPutRequest.fOutside = false;
		fPutRequest.fFrameData = false;
		if (!async)
		{
			ULong returnSize;
			err = fToolPort.SendRPC(&returnSize, &fPutRequest, sizeof(fPutRequest), &fPutReply, sizeof(fPutReply), 0, kCommToolRequestTypePut);
			if (err == noErr)
				err = fPutReply.fResult;
			PrintBandComplete(err);
			return;
		}
		err = fToolPort.SendRPC(&fDataMsg, &fPutRequest, sizeof(fPutRequest), &fPutReply, sizeof(fPutReply), 0, nil, kCommToolRequestTypePut);
		if (err == noErr)
			return;
	}
	PrintBandComplete(err);
}


// ROM 0x000b99d0 GetBand__17TFaxToolInterfaceFPUcUlUc
// A band of a received page got into the caller's buffer (framed: a
// frame is a band).
void
TFaxToolInterface::GetBand(UChar* data, ULong size, Boolean async)
{
	NewtonErr err = fBandList.RemoveAll();
	if (err == noErr
	 && (err = fBandSegment.Init(data, size, false, 0, -1)) == noErr
	 && (err = fBandList.InsertLast(&fBandSegment)) == noErr)
	{
		fGetRequest.fData = &fBandList;
		fGetRequest.fOutside = false;
		fGetRequest.fFrameData = true;
		fGetRequest.fNonBlocking = false;
		if (!async)
		{
			ULong returnSize;
			err = fToolPort.SendRPC(&returnSize, &fGetRequest, sizeof(fGetRequest), &fGetReply, sizeof(fGetReply), 0, kCommToolRequestTypeGet);
			if (err == noErr)
				err = fGetReply.fResult;
			GetBandComplete(err, fGetReply.fGetBytesCount, fGetReply.fEndOfFrame);
			return;
		}
		err = fToolPort.SendRPC(&fDataMsg, &fGetRequest, sizeof(fGetRequest), &fGetReply, sizeof(fGetReply), 0, nil, kCommToolRequestTypeGet);
		if (err == noErr)
		{
			fGetting = true;
			return;
		}
	}
	GetBandComplete(err, 0, false);
}
