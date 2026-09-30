/*
	File:		comms/EzEndpointPipe.cpp

	Contains:	TEzEndpointPipe and the easy options (EzEndpointPipe.h).

	Reconstructed from the MP2x00 US ROM (0x000b0444-0x000b1a5c); each
	function cites its origin.
*/

#include "EzEndpointPipe.h"
#include "Endpoint.h"
#include "CommManager.h"
#include "ModemNavigator.h"
#include "Translators.h"
#include "SerialOptions.h"
#include "MNPOptions.h"
#include "CommToolOptions.h"
#include "Interpreter.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "CommAddresses.h"
#include "ModemOptions.h"
#include "Unicode.h"

#include <string.h>

extern const ExceptionName exPipeException;
extern const ExceptionName exTranslatorException;


// ROM 0x000b0dd0 __ct__15TEzEndpointPipeFv
// A ten-second timeout.
TEzEndpointPipe::TEzEndpointPipe()
{
	fError = noErr;
	fEndpoint = nil;
	fEzTimeout = 10 * kSeconds;
	fName = nil;
	fAppleTalkOpen = false;
}


// ROM 0x000b1048 __dt__15TEzEndpointPipeFv
TEzEndpointPipe::~TEzEndpointPipe()
{
	if (fEndpoint != nil)
		TearDown();
}


// ROM 0x000b0444 Abort__15TEzEndpointPipeFv
// (Nothing: the endpoint pipe's own abort is hidden.)
void
TEzEndpointPipe::Abort()
{ }


// ROM 0x000b10a4 CommonInit__15TEzEndpointPipeFUl
// The timeout, and the bytes-available option.
void
TEzEndpointPipe::CommonInit(ULong timeout)
{
	fEzTimeout = timeout;
	fError = fOptions.Init();
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
	TCMOSerialBytesAvailable available;
	fError = fOptions.InsertOptionAt(fOptions.GetArrayCount(), &available);
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
	fBytesAvailOpt = fOptions.OptionAt(0);
}


// ROM 0x000b15d4 Init__15TEzEndpointPipeFRC6RefVarUl
// From an options frame: its option arrays, made, used and deleted.
void
TEzEndpointPipe::Init(RefArg options, ULong timeout)
{
	if (ISNIL(options))
		Throw(exPipeException, (void*) -1, nil);
	if (!FrameHasSlot(options, RSSYMopenoptions))
		Throw(exPipeException, (void*) -1, nil);
	TOptionArray* open = nil;
	TOptionArray* bind = nil;
	TOptionArray* connect = nil;
	Boolean useEOP = EzConvertOptions(options, &open, &bind, &connect);
	Boolean failed = false;
	newton_try
	{
		Init(open, bind, connect, useEOP, timeout);
	}
	newton_catch_all
	{
		failed = true;
		if (connect != nil)
			delete connect;
		if (bind != nil)
			delete bind;
		if (open != nil)
			delete open;
		rethrow;
	}
	end_try;
	if (connect != nil)
		delete connect;
	if (bind != nil)
		delete bind;
	if (open != nil)
		delete open;
}


// ROM 0x000b170c Init__15TEzEndpointPipeFP12TOptionArrayN21UcUl
// The endpoint made (after the modem navigator has had its look), opened,
// bound and connected, and the pipe put over it with 2K buffers; a failure
// takes the endpoint down again and throws.
void
TEzEndpointPipe::Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
					  Boolean useEOP, ULong timeout)
{
	CommonInit(timeout);
	fError = RunModemNavigator(openOptions);
	if (fError != noErr)
		goto failed;
	fError = CMGetEndpoint(openOptions, &fEndpoint, false);
	if (fError != noErr)
		goto failed;
	fError = fEndpoint->Open(0);
	if (fError != noErr)
		goto deleteIt;
	fError = fEndpoint->nBind(bindOptions, timeout, true);
	if (fError != noErr)
		goto closeIt;
	fError = fEndpoint->nConnect(connectOptions, nil, nil, timeout, true);
	if (fError != noErr)
		fEndpoint->nUnBind(0, true);
	else
		TEndpointPipe::Init(fEndpoint, 0x800, 0x800, fEzTimeout, useEOP, nil);
	if (fError == noErr)
		return;
closeIt:
	fEndpoint->Close();
deleteIt:
	fEndpoint->Delete();
	fEndpoint = nil;
failed:
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b1858 Init__15TEzEndpointPipeF14ConnectionTypePPcUl
// By connection type; the name is a handle the pipe then owns (the phone
// number, for an MNP modem).  A type the ROM does not know (2, or above 6)
// makes no endpoint and goes on to the pipe's Init with none, as the ROM's.
// NOT YET: AppleTalk (ADSP) - OpenAppleTalk (0x00073c3c) and
// GetADSPEndpoint (0x000b05f8) wait on the AppleTalk tools; it throws
// kCommErrMethodNotImplemented.
void
TEzEndpointPipe::Init(ConnectionType type, char** name, ULong timeout)
{
	newton_try
	{
		CommonInit(timeout);
		fName = name;
		switch (type)
		{
		case kSerialConnection:
			GetSerialEndpoint();
			break;
		case kADSPConnection:
			Throw(exPipeException, (void*) (Long) kCommErrMethodNotImplemented, nil);
			break;
		case kConnectionType2:
			break;
		case kMNPSerialConnection:
			GetMNPSerialEndpoint();
			break;
		case kSharpIRConnection:
			GetSharpIREndpoint();
			break;
		case kMNPModemConnection:
			GetMNPModemEndpoint();
			break;
		case kIrDAConnection:
			GetIrDAEndpoint();
			break;
		}
		TEndpointPipe::Init(fEndpoint, 0x800, 0x800, fEzTimeout, type == kSharpIRConnection, nil);
	}
	newton_catch(exPipeException)
	{
		TearDown();
		fError = (NewtonErr) (Long) CurrentException()->data;
		rethrow;
	}
	newton_catch_all
	{
		NewtonErr err = TearDown();
		if (err == noErr)
			err = -1;
		if (fError != noErr)
			err = fError;
		fError = err;
		rethrow;
	}
	end_try;
}


// ROM 0x000b0534 GetSerialEndpoint__15TEzEndpointPipeFv
void
TEzEndpointPipe::GetSerialEndpoint()
{
	TOptionArray options;
	fError = options.Init();
	if (fError == noErr
	&&  (fError = EzSerialOptions(&options, fName, 0x800, 0x800)) == noErr
	&&  (fError = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
	{
		fEndpoint->UseForks(true);
		fError = fEndpoint->EasyConnect(0, nil, fEzTimeout);
	}
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b044c GetMNPSerialEndpoint__15TEzEndpointPipeFv
void
TEzEndpointPipe::GetMNPSerialEndpoint()
{
	TOptionArray options;
	fError = options.Init();
	if (fError == noErr
	&&  (fError = EzMNPSerialOptions(&options, fName)) == noErr
	&&  (fError = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
	{
		fEndpoint->UseForks(true);
		if ((fError = options.RemoveAllOptions()) == noErr
		&&  (fError = EzMNPConnectOptions(&options, fName)) == noErr)
			fError = fEndpoint->EasyConnect(0, &options, fEzTimeout);
	}
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b0764 GetMNPModemEndpoint__15TEzEndpointPipeFv
// The modem service with MNP, through the modem navigator when it is in
// use, dialling the name - the phone number, UniChars - with the default
// idle timer.  ROM BUG: the number is converted into 256 bytes with no
// limit, and whether its option went in is not asked.
void
TEzEndpointPipe::GetMNPModemEndpoint()
{
	TOptionArray options;
	fError = options.Init();
	if (fError == noErr
	&&  (fError = EzMNPModemOptions(&options, fName)) == noErr
	&&  (!UseModemNavigator() || (fError = RunModemNavigator(&options)) == noErr)
	&&  (fError = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
	{
		fEndpoint->UseForks(true);
		if ((fError = options.RemoveAllOptions()) == noErr)
		{
			char number[256];
			HLock((Handle) fName);
			ConvertFromUnicode((const UniChar*) *fName, number, kMacRomanEncoding, 0x7FFFFFFF);
			HUnlock((Handle) fName);
			ULong numberLen = strlen(number);
			TCMAPhoneNumber phone(numberLen);
			options.InsertVarOptionAt(options.GetArrayCount(), &phone, number, numberLen);
			TCMOIdleTimer idle;
			if ((fError = options.InsertOptionAt(options.GetArrayCount(), &idle)) == noErr)
				fError = fEndpoint->EasyConnect(0, &options, fEzTimeout);
		}
	}
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b0904 GetSharpIREndpoint__15TEzEndpointPipeFv
void
TEzEndpointPipe::GetSharpIREndpoint()
{
	TOptionArray options;
	fError = options.Init();
	if (fError == noErr
	&&  (fError = EzSharpIROptions(&options, fName)) == noErr
	&&  (fError = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
	{
		fEndpoint->UseForks(true);
		fError = fEndpoint->EasyConnect(0, nil, fEzTimeout);
	}
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b09c0 GetIrDAEndpoint__15TEzEndpointPipeFv
void
TEzEndpointPipe::GetIrDAEndpoint()
{
	TOptionArray options;
	fError = options.Init();
	if (fError == noErr
	&&  (fError = EzIrDAOptions(&options, fName)) == noErr
	&&  (fError = CMGetEndpoint(&options, &fEndpoint, false)) == noErr)
	{
		fEndpoint->UseForks(true);
		fError = fEndpoint->EasyConnect(0, nil, fEzTimeout);
	}
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
}


// ROM 0x000b19e0 TearDown__15TEzEndpointPipeFv
// The name given back, the endpoint closed and deleted.  (NOT YET: closing
// AppleTalk, CloseAppleTalk 0x00073dd8, for an ADSP pipe.)
NewtonErr
TEzEndpointPipe::TearDown()
{
	if (fName != nil)
	{
		DisposHandle((Handle) fName);
		fName = nil;
	}
	if (fEndpoint != nil)
	{
		fError = fEndpoint->EasyClose();
		if (fError != noErr)
			return fError;
		fEndpoint->Delete();
		fEndpoint = nil;
	}
	return fError;
}


// ROM 0x000b0a7c BytesAvailable__15TEzEndpointPipeFv
// How many bytes have come and not been read ('sbav, asked of the tool).
ULong
TEzEndpointPipe::BytesAvailable()
{
	if (fEndpoint == nil || fBytesAvailOpt == nil)
		return 0;
	fBytesAvailOpt->Reset();
	fBytesAvailOpt->SetOpCode(opGetCurrent);
	fError = fEndpoint->nOptMgmt(opProcess, &fOptions, fEzTimeout, true);
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
	fError = (signed char) fBytesAvailOpt->GetOpCodeResults();
	if (fError != noErr)
		Throw(exPipeException, (void*) (Long) fError, nil);
	return ((TCMOSerialBytesAvailable*) fBytesAvailOpt)->fBytesAvailable;
}


/*------------------------------------------------------------------------------
	The easy options
------------------------------------------------------------------------------*/

// ROM 0x000b1124 EzConvertOptions__FRC6RefVarPP12TOptionArrayN22
// The frame's openOptions (a method, called with no arguments; nil throws),
// and bindOptions and connectOptions if it has them, each turned into an
// option array by POptionDataOut.  ==> the frame's useEOP.  A failure
// deletes what was made and throws (exPipeException).
Boolean
EzConvertOptions(RefArg options, TOptionArray** open, TOptionArray** bind, TOptionArray** connect)
{
	NewtonErr err;
	Boolean useEOP = false;
	PFrameSink* optionsOut = nil;
	PFrameSink* scriptOut = nil;
	*open = new TOptionArray;
	err = MemError();
	if (*open == nil)
		goto failed;
	if ((err = (*open)->Init()) != noErr)
		goto failed;
	optionsOut = (PFrameSink*) NewByName("PFrameSink", "POptionDataOut");
	if (optionsOut == nil)
		err = MemError() != noErr ? MemError() : -1;
	if (err != noErr)
		goto failed;
	scriptOut = (PFrameSink*) NewByName("PFrameSink", "PScriptDataOut");
	if (scriptOut == nil)
		err = MemError() != noErr ? MemError() : -1;
	if (err != noErr)
		goto failed;
	newton_try
	{
		RefVar result;
		RefVar args(MakeArray(0));
		OptionDataParms parms;
		parms.fOptions = *open;
		result = DoMessage(options, RSSYMopenoptions, args);
		if (ISNIL(result))
			Throw(exPipeException, (void*) -1, nil);
		parms.fFrame = result;
		parms.fXlator = scriptOut;
		optionsOut->Translate(&parms, nil);
		if (FrameHasSlot(options, RSSYMbindoptions))
		{
			*bind = new TOptionArray;
			err = MemError();
			if (*bind == nil || (err = (*bind)->Init()) != noErr)
				Throw(exPipeException, (void*) (Long) err, nil);
			result = DoMessage(options, RSSYMbindoptions, args);
			if (NOTNIL(result))
			{
				parms.fOptions = *bind;
				parms.fFrame = result;
				parms.fXlator = scriptOut;
				optionsOut->Translate(&parms, nil);
			}
		}
		if (FrameHasSlot(options, RSSYMconnectoptions))
		{
			*connect = new TOptionArray;
			err = MemError();
			if (*connect == nil || (err = (*connect)->Init()) != noErr)
				Throw(exPipeException, (void*) (Long) err, nil);
			result = DoMessage(options, RSSYMconnectoptions, args);
			if (NOTNIL(result))
			{
				parms.fOptions = *connect;
				parms.fFrame = result;
				parms.fXlator = scriptOut;
				optionsOut->Translate(&parms, nil);
			}
		}
		useEOP = NOTNIL(GetFrameSlot(options, RSSYMuseeop));
	}
	newton_catch_all
	{
		optionsOut->Delete();
		scriptOut->Delete();
		if (*open != nil)
			delete *open;
		if (*bind != nil)
			delete *bind;
		if (*connect != nil)
			delete *connect;
		*open = *bind = *connect = nil;
		rethrow;
	}
	end_try;
	optionsOut->Delete();
	scriptOut->Delete();
	return useEOP;

failed:
	if (optionsOut != nil)
		optionsOut->Delete();
	if (scriptOut != nil)
		scriptOut->Delete();
	if (*open != nil)
		delete *open;
	if (*bind != nil)
		delete *bind;
	if (*connect != nil)
		delete *connect;
	*open = *bind = *connect = nil;
	if (err != noErr)
		Throw(exPipeException, (void*) (Long) err, nil);
	return false;
}


// ROM 0x000b0b28 EzSerialOptions__FP12TOptionArrayPPclT3
// The async serial service at 38400 bps with hardware flow control both
// ways, and (both sizes given) the buffers.
NewtonErr
EzSerialOptions(TOptionArray* options, char** name, long sendSize, long recvSize)
{
	TOption service;
	service.SetAsService('aser');
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &service);
	if (err != noErr)
		return err;
	TCMOSerialIOParms ioParms;
	ioParms.fSpeed = 38400;
	if ((err = options->InsertOptionAt(options->GetArrayCount(), &ioParms)) != noErr)
		return err;
	TCMOInputFlowControlParms inFlow;
	inFlow.useHardFlowControl = true;
	if ((err = options->InsertOptionAt(options->GetArrayCount(), &inFlow)) != noErr)
		return err;
	TCMOOutputFlowControlParms outFlow;
	outFlow.useHardFlowControl = true;
	if ((err = options->InsertOptionAt(options->GetArrayCount(), &outFlow)) != noErr)
		return err;
	if (sendSize > 0 && recvSize > 0)
	{
		TCMOSerialBuffers buffers;
		buffers.fRecvSize = recvSize;
		buffers.fSendSize = sendSize;
		err = options->InsertOptionAt(options->GetArrayCount(), &buffers);
	}
	return err;
}


// ROM 0x000b0e38 EzMNPSerialOptions__FP12TOptionArrayPPc
// The MNP service at 38400 bps.
NewtonErr
EzMNPSerialOptions(TOptionArray* options, char** name)
{
	TOption service;
	service.SetAsService('mnps');
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &service);
	if (err != noErr)
		return err;
	TCMOSerialIOParms ioParms;
	ioParms.fSpeed = 38400;
	return options->InsertOptionAt(options->GetArrayCount(), &ioParms);
}


// ROM 0x000b0eac EzMNPConnectOptions__FP12TOptionArrayPPc
// MNP's data rate 38400, and an idle timer of 30 seconds.
NewtonErr
EzMNPConnectOptions(TOptionArray* options, char** name)
{
	TCMOMNPDataRate rate;
	rate.fDataRate = 38400;
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &rate);
	if (err == noErr)
	{
		TCMOIdleTimer idle;
		idle.fValue = 30;
		err = options->InsertOptionAt(options->GetArrayCount(), &idle);
	}
	return err;
}


// ROM 0x000b0f0c EzSharpIROptions__FP12TOptionArrayPPc
// The Sharp IR service.
NewtonErr
EzSharpIROptions(TOptionArray* options, char** name)
{
	TOption service;
	service.SetAsService('slir');
	return options->InsertOptionAt(options->GetArrayCount(), &service);
}


// ROM 0x000b0f50 EzMNPModemOptions__FP12TOptionArrayPPc
// The modem service, MNP required ('mecp' 2, set as required), and the
// dialling the user's preferences give.
NewtonErr
EzMNPModemOptions(TOptionArray* options, char** name)
{
	TOption service;
	service.SetAsService('mods');
	NewtonErr err = options->InsertOptionAt(options->GetArrayCount(), &service);
	if (err != noErr)
		return err;
	TCMOModemECType ecType;
	ecType.fType = 2;
	ecType.SetOpCode(opSetRequired);
	if ((err = options->InsertOptionAt(options->GetArrayCount(), &ecType)) != noErr)
		return err;
	TCMOModemDialing dialing;
	SetDialingOptionsFromPrefs(&dialing);
	return options->InsertOptionAt(options->GetArrayCount(), &dialing);
}


// ROM 0x000b1004 EzIrDAOptions__FP12TOptionArrayPPc
// The IrDA service.
NewtonErr
EzIrDAOptions(TOptionArray* options, char** name)
{
	TOption service;
	service.SetAsService('irda');
	return options->InsertOptionAt(options->GetArrayCount(), &service);
}
