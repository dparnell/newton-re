/*
	File:		comms/StreamingEndpoint.cpp

	Contains:	TStreamingEndpointClient, TStreamingCallBack and the CIS...
				natives (StreamingEndpoint.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "StreamingEndpoint.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "NewtonTime.h"
#include "ConfigServer.h"

extern const ExceptionName exTranslatorException;		// "evt.ex.translator"

#define kExComm		"evt.ex.comm"


/* -------------------------------------------------------------------------------
	TStreamingCallBack
------------------------------------------------------------------------------- */

// ROM 0x00139c44 __ct__18TStreamingCallBackFv
TStreamingCallBack::TStreamingCallBack()
{
	fSpec = (Ref) 0;
	fIsIn = false;
}


// ROM 0x00139ca4 __dt__18TStreamingCallBackFv
TStreamingCallBack::~TStreamingCallBack()
{
	fSpec = (Ref) 0;
}


// ROM 0x00139cfc Status__18TStreamingCallBackFlT1
// spec:progressScript([bytes so far, bytes in all]) - each nil when not
// known - for the direction; its answer nil stops the transfer.  (The ROM
// tests the spec's Ref against nought, not nil.)
Boolean
TStreamingCallBack::Status(long bytesRead, long bytesWritten)
{
	if ((Ref) fSpec == 0)
		return true;
	RefVar args(MakeArray(2));
	long done, total;
	if (!fIsIn)
	{
		done = bytesWritten;
		total = fWriteTotal;
	}
	else
	{
		done = bytesRead;
		total = fReadTotal;
	}
	SetArraySlotRef(args, 0, (done == -1) ? RefVar() : RefVar(MAKEINT(done)));
	SetArraySlotRef(args, 1, (total == -1) ? RefVar() : RefVar(MAKEINT(total)));
	RefVar result(DoMessage(fSpec, RSSYMprogressscript, args));
	return NOTNIL(result);
}


/* -------------------------------------------------------------------------------
	TStreamingEndpointClient
------------------------------------------------------------------------------- */

// ROM 0x00139614 __ct__24TStreamingEndpointClientFv
TStreamingEndpointClient::TStreamingEndpointClient()
{
	fInCallback = nil;
	fOutCallback = nil;
}


// ROM 0x00139660 __dt__24TStreamingEndpointClientFv
TStreamingEndpointClient::~TStreamingEndpointClient()
{ }


// ROM 0x001396a0 ReadStreamParms__24TStreamingEndpointClientFP14StreamRefParmsUcRC6RefVarl
// The spec read into the stream parameters (any input spec cleared first);
// a progressScript makes a callback, told the length of what is written.
NewtonErr
TStreamingEndpointClient::ReadStreamParms(StreamRefParms* parms, Boolean isIn, RefArg spec, long length)
{
	ClearInputSpec();
	parms->fValue = NILREF;
	parms->fEndpoint = fEndpoint;
	parms->fTimeout = 0;
	parms->fFraming = false;
	NewtonErr err = noErr;
	if (NOTNIL(spec) && IsFrame(spec))
	{
		RefVar value(GetVariable(spec, RSSYMform, nil, 0));
		if (NOTNIL(value) && !EQRef(value, RSSYMframe))
			err = kCommScriptErrBadForm;
		if (isIn)
		{
			value = GetVariable(spec, RSSYMtarget, nil, 0);
			if (NOTNIL(value) && IsFrame(value))
				parms->fValue = GetVariable(value, RSSYMstore, nil, 0);
		}
		if (err == noErr)
		{
			value = GetVariable(spec, RSSYMreqtimeout, nil, 0);
			if (ISINT(value))
				parms->fTimeout = RINT(value) * kMilliseconds;
			value = GetVariable(spec, isIn ? RSSYMrcvflags : RSSYMsendflags, nil, 0);
			if (ISINT(value) && (RINT(value) & 2) != 0)
				parms->fFraming = true;
			value = GetVariable(spec, RSSYMprogressscript, nil, 0);
			if (NOTNIL(value))
			{
				TStreamingCallBack* callback = new TStreamingCallBack;
				callback->fSpec = spec;
				callback->fIsIn = isIn;
				if (!isIn)
				{
					callback->fWriteTotal = length;
					fOutCallback = callback;
				}
				else
					fInCallback = callback;
			}
		}
	}
	return err;
}


// ROM 0x001398d0 DoStreamIn__24TStreamingEndpointClientFRC6RefVarRl
// An object read from the connection (not while an input spec's receive
// is outstanding).
Ref
TStreamingEndpointClient::DoStreamIn(RefArg spec, long* error)
{
	*error = noErr;
	RefVar result;
	StreamRefParms parms;
	if (ISNIL(spec))
		*error = kCommScriptErrStreamSpec;
	else if (fRcvPending)
		*error = kCommScriptErrRcvPending;
	else if ((*error = ReadStreamParms(&parms, true, spec, -1)) == noErr)
	{
		PFrameSource* translator = (PFrameSource*) NewByName("PFrameSource", "PStreamInRef");
		if (translator == nil)
		{
			*error = MemError();
			if (*error == noErr)
				*error = -10007;		// (host: the registry sets no error)
		}
		else
		{
			newton_try
			{
				newton_try
				{
					result = translator->Translate(&parms, fInCallback);
				}
				newton_catch_all
				{
					translator->Delete();
					rethrow;
				}
				end_try;
				translator->Delete();
			}
			newton_catch(exTranslatorException)
			{
				*error = (long) (Long) CurrentException()->data;
			}
			end_try;
		}
	}
	if (fInCallback != nil)
	{
		delete fInCallback;
		fInCallback = nil;
	}
	return result;
}


// ROM 0x00139a9c DoStreamOut__24TStreamingEndpointClientFRC6RefVarT1
// An object written to the connection.
NewtonErr
TStreamingEndpointClient::DoStreamOut(RefArg data, RefArg spec)
{
	NewtonErr err;
	StreamRefParms parms;
	if (ISNIL(data))
		err = kCommScriptErrNoData;
	else if ((err = ReadStreamParms(&parms, false, spec, Length(data))) == noErr)
	{
		parms.fValue = data;
		PFrameSink* translator = (PFrameSink*) NewByName("PFrameSink", "PStreamOutRef");
		if (translator == nil)
		{
			err = MemError();
			if (err == noErr)
				err = -10007;		// (host: the registry sets no error)
		}
		else
		{
			newton_try
			{
				newton_try
				{
					translator->Translate(&parms, fOutCallback);
				}
				newton_catch_all
				{
					translator->Delete();
					rethrow;
				}
				end_try;
				translator->Delete();
			}
			newton_catch(exTranslatorException)
			{
				err = (NewtonErr) (Long) CurrentException()->data;
			}
			end_try;
		}
	}
	if (fOutCallback != nil)
	{
		delete fOutCallback;
		fOutCallback = nil;
	}
	return err;
}


/* -------------------------------------------------------------------------------
	The natives: protoStreamingEndpoint's own methods
------------------------------------------------------------------------------- */

static void
ThrowComm(NewtonErr err)
{
	Throw(kExComm, (void*) (Long) err, nil);
}


// ROM 0x00133d44 CISNewInstantiate
// (Unlike protoBasicEndpoint's, the options are not cloned, and nothing is
// answered.)
static Ref
CISNewInstantiate(RefArg rcvr, RefArg endpoint, RefArg options)
{
	TStreamingEndpointClient* client = new TStreamingEndpointClient;
	NewtonErr err;
	if (client == nil)
		err = -10007;
	else if ((err = client->InitScriptEndpointClient(endpoint, options, nil)) == noErr)
		return NILREF;
	ThrowComm(err);
	return NILREF;
}


// ROM 0x00133da8 CISNewInstantiateFromEndpoint
static Ref
CISNewInstantiateFromEndpoint(RefArg rcvr, RefArg endpoint, RefArg options, RefArg tEndpoint)
{
	TStreamingEndpointClient* client = new TStreamingEndpointClient;
	NewtonErr err;
	if (client == nil)
		err = -10007;
	else if ((err = client->InitScriptEndpointClient(endpoint, options, (TEndpoint*) RefToAddress(tEndpoint))) == noErr)
		return NILREF;
	ThrowComm(err);
	return NILREF;
}


// ROM 0x00133e20 CISStreamIn
static Ref
CISStreamIn(RefArg rcvr, RefArg spec)
{
	TStreamingEndpointClient* client = (TStreamingEndpointClient*) (void*) GetClient(rcvr);
	long err;
	RefVar result;
	if (client == nil)
		err = kCommScriptErrNotInstantiated;
	else
	{
		result = client->DoStreamIn(spec, &err);
		if (err == noErr)
			return result;
	}
	ThrowComm(err);
	return NILREF;
}


// ROM 0x00133ea4 CISStreamOut
static Ref
CISStreamOut(RefArg rcvr, RefArg data, RefArg spec)
{
	TStreamingEndpointClient* client = (TStreamingEndpointClient*) (void*) GetClient(rcvr);
	NewtonErr err;
	if (client == nil)
		err = kCommScriptErrNotInstantiated;
	else if ((err = client->DoStreamOut(data, spec)) == noErr)
		return NILREF;
	ThrowComm(err);
	return NILREF;
}


void
RegisterStreamingEndpointNatives(void)
{
	RegisterNativeFunction("CISNewInstantiate", (void*) CISNewInstantiate, 2);
	RegisterNativeFunction("CISNewInstantiateFromEndpoint", (void*) CISNewInstantiateFromEndpoint, 3);
	RegisterNativeFunction("CISStreamIn", (void*) CISStreamIn, 1);
	RegisterNativeFunction("CISStreamOut", (void*) CISStreamOut, 2);
}
