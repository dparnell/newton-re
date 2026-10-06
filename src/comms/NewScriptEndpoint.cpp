/*
	File:		comms/NewScriptEndpoint.cpp

	Contains:	The NewtonScript endpoint (NewScriptEndpoint.h):
				TNewScriptEndpointClient and the CINew... natives.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "NewScriptEndpoint.h"
#include "ModemNavigator.h"
#include "StreamingEndpoint.h"
#include "SerialEndpoint.h"
#include "CommManager.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "Marshalling.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "NewtonTime.h"
#include "Ports.h"
#include "RootView.h"
#include "NewtWorld.h"
#include "Notebook.h"
#include "ConfigServer.h"
#include "toolbox/ByteOrder.h"
#include "host/RomBugs.h"

#include <stdlib.h>
#include <string.h>

extern const ExceptionName exTranslatorException;		// "evt.ex.translator"
extern const ExceptionName exOutOfMemory;

#define kExComm		"evt.ex.comm"

static const NewtonErr kScriptEpNoMemory = -10007;		// the ROM's 0xffffd8e9

// (The ROM's malloc, free and ReallocPtr are all the pointer heap's; the
// host's malloc is the C library's, so the blocks here are NewPtr'd and
// DisposPtr'd to go with ReallocPtr and with what PScriptDataOut makes.)


/* -------------------------------------------------------------------------------
	Helpers
------------------------------------------------------------------------------- */

// ROM 0x00133c84 IsRaw__FRC6RefVar
// A binary that is not a string.
Boolean
IsRaw(RefArg obj)
{
	if (!IsBinary(obj) || IsInstance(obj, RSSYMstring))
		return false;
	return true;
}


// ROM 0x00133cc8 IsRawOrString__FRC6RefVar
// (IsBinary under another name)
Boolean
IsRawOrString(RefArg obj)
{
	return IsBinary(obj);
}


// ROM 0x00134944 CloneOptions__FRC6RefVar
// An options frame or array cloned, and each element of an array too, so
// the endpoint writes its results into copies.
Ref
CloneOptions(RefArg options)
{
	RefVar clone(Clone(options));
	if (IsArray(options))
	{
		for (ULong i = 0; i < (ULong) Length(options); i++)
		{
			RefVar element(GetArraySlotRef(options, i));
			SetArraySlotRef(clone, i, RefVar(Clone(element)));
		}
	}
	return clone;
}


// ROM 0x00134670 CharacterToUByte__FUsl
UByte
CharacterToUByte(UniChar c, long encoding)
{
	UniChar str[2];
	UByte bytes[4];
	str[0] = c;
	ConvertFromUnicode(str, bytes, encoding, 1);
	return bytes[0];
}


// The client an endpoint frame keeps in its ciPrivate slot: the ROM's
// GetClient (0x000ac358), which system/ConfigServer.cpp reconstructs typed
// for a config server's frame.
static TEndpointClient*
EndpointClient(RefArg endpoint)
{
	return (TEndpointClient*) (void*) GetClient(endpoint);
}


// ROM 0x000ac334 GetClientEndpoint__FRC6RefVar
TEndpoint*
GetClientEndpoint(RefArg endpoint)
{
	TEndpointClient* client = EndpointClient(endpoint);
	return client != nil ? client->fEndpoint : nil;
}


// An exception out of a script the endpoint sent, to its exceptionHandler
// ({name, data, debug: the script's symbol}), or reported (the ROM writes
// this out in each place a script is sent; the partialScript's also runs
// any deferred actions the handler left).
static void
EndpointScriptException(RefArg endpoint, Exception* exception, RefArg debug, Boolean deferred)
{
	if (ISNIL(endpoint) || GetVariable(endpoint, RSSYMexceptionhandler, nil, 0) == NILREF)
	{
		ExceptionNotify(exception);
		return;
	}
	RefVar ex(AllocateFrame());
	SetFrameSlot(ex, RSSYMname, RefVar(Intern(exception->name)));
	SetFrameSlot(ex, RSSYMdata, RefVar(MAKEINT((long) (Long) exception->data)));
	if (NOTNIL(debug))
		SetFrameSlot(ex, RSSYMdebug, debug);
	RefVar args(MakeArray(1));
	SetArraySlotRef(args, 0, ex);
	DoMessage(endpoint, RSSYMexceptionhandler, args);
	if (deferred)
		CheckForDeferredActions();
}


// A script sent, an evt.ex exception out of it handed to the endpoint.
static void
SendEndpointScript(RefArg endpoint, RefArg receiver, RefArg message, RefArg args, Boolean deferred = false)
{
	newton_try
	{
		DoMessage(receiver, message, args);
	}
	newton_catch("evt.ex")
	{
		EndpointScriptException(endpoint, CurrentException(), message, deferred);
	}
	end_try;
}


/* -------------------------------------------------------------------------------
	TNewScriptEndpointClient
------------------------------------------------------------------------------- */

// ROM 0x001352a8 __ct__24TNewScriptEndpointClientFv
TNewScriptEndpointClient::TNewScriptEndpointClient()
{
	fEndpointRef = NILREF;
	fInputSpec = NILREF;
	fInputBuffer = nil;
	fPartialFrequency = 0;
	fInputTimeout = 0;
	fEncoding = 1;
	fEndpoint = nil;
	fRcvFlags = 0;
	fUseEOP = false;
	fOptimize = false;
	fRcvPending = false;
	fInRcvComplete = false;
	fEndSequence = NILREF;
	fProxyBytes = NILREF;
	fProxies = NILREF;
	fOutputs = MakeArray(0);
	fRequests = MakeArray(0);
	fAborts = MakeArray(0);
	fOptionsOut = nil;
	fOptionsIn = nil;
	fBindOptions = nil;
	fDataOut = nil;
	fDataIn = nil;
	fFlattenOut = nil;
	fUnflattenIn = nil;
	// (host: what the ROM leaves as operator new left it)
	fByteCount = 0;
	fDiscardAfter = 0;
	fReadFrameLength = false;
	fSevenBit = false;
	fRcvOptionsState = 2;
	fRawTarget = false;
	fTargetOffset = 0;
	fInputBufferSize = 0;
	fReceived = 0;
	fPartialPending = false;
	fPartialOffset = 0;
	fInputForm = kFormNone;
}


// ROM 0x00136464 __dt__24TNewScriptEndpointClientFv
// The endpoint deleted with the client, and the endpoint frame's ciPrivate
// cleared.
TNewScriptEndpointClient::~TNewScriptEndpointClient()
{
	if (fEndpoint != nil)
		fEndpoint->Delete();
	if (fInputBuffer != nil)
		DisposPtr((Ptr) fInputBuffer);
	// ROM BUG (fixed): the ROM tests the Ref against nought, not nil: an
	// endpoint frame that was nil would have its slot set, and throw.  The
	// fix tests it against nil (and frees an asynchronous bind's options
	// never read back).
	if (RomBugFixed())
	{
		if (NOTNIL(fEndpointRef))
			SetFrameSlot(fEndpointRef, RSSYMciprivate, RefVar());
		if (fBindOptions != nil)
			delete fBindOptions;
		fBindOptions = nil;
	}
	else if ((Ref) fEndpointRef != 0)
		SetFrameSlot(fEndpointRef, RSSYMciprivate, RefVar());
	if (fOptionsOut != nil)
		fOptionsOut->Delete();
	if (fOptionsIn != nil)
		fOptionsIn->Delete();
	if (fDataOut != nil)
		fDataOut->Delete();
	if (fDataIn != nil)
		fDataIn->Delete();
	if (fFlattenOut != nil)
		fFlattenOut->Delete();
	if (fUnflattenIn != nil)
		fUnflattenIn->Delete();
}


// ROM 0x001377b8 InitScriptEndpointClient__24TNewScriptEndpointClientFRC6RefVarT1P9TEndpoint
// Over an endpoint already made (instantiateFromTEndpoint), or one the comm
// manager makes from the options - read back into them, and opened.
NewtonErr
TNewScriptEndpointClient::InitScriptEndpointClient(RefArg endpoint, RefArg options, TEndpoint* ep)
{
	fEndpointRef = endpoint;
	if (ISNIL(endpoint))
		return kCommScriptErrNotInstantiated;
	Ref sizeRef = GetVariable(endpoint, RSSYMinternalbuffersize, nil, 0);
	long size = (sizeRef == NILREF) ? 0x200 : RINT(sizeRef);
	NewtonErr err = fRcvBuffer.Init(size);
	if (err != noErr)
		return err;
	TEndpoint* theEndpoint = ep;
	if (ep == nil)
	{
		TOptionArray array;
		err = array.Init();
		if (err != noErr)
			return err;
		if (ISNIL(options))
			return kCommScriptErrNoOptions;
		err = ConvertToOptionArray(options, &array);
		if (err != noErr)
			return err;
		// a request for the modem service first put through the modem
		// navigator (what it answers is not looked at)
		if (UseModemNavigator() && ContainsModemService(&array))
			RunModemNavigator(&array);
		err = CMGetEndpoint(&array, &theEndpoint, false);
		if (err == noErr)
			err = ConvertFromOptionArray(options, &array);
		if (err != noErr)
			return err;
	}
	err = TEndpointClient::Init(theEndpoint, 'endp', 'newt');
	if (err != noErr)
		return err;
	SetFrameSlot(endpoint, RSSYMciprivate, RefVar(AddressToRef(this)));
	Ref encoding = GetVariable(endpoint, RSSYMencoding, nil, 0);
	if (encoding != NILREF)
		fEncoding = RINT(encoding);
	err = InitIdler(0, kMilliseconds, 0, false);
	if (err == noErr && ep == nil)
		err = fEndpoint->Open((ULong) this);
	return err;
}


// ROM 0x00133ccc GetScriptDataInXlator__24TNewScriptEndpointClientFv
PFrameSource*
TNewScriptEndpointClient::GetScriptDataInXlator(void)
{
	if (fDataIn == nil)
	{
		PFrameSource* xlator = (PFrameSource*) NewByName("PFrameSource", "PScriptDataIn");
		if (xlator == nil)
			Throw(exTranslatorException, (void*) (Long) MemError(), nil);
		else
			fDataIn = xlator;
	}
	return fDataIn;
}


// ROM 0x001391e8 GetScriptDataOutXlator__24TNewScriptEndpointClientFv
PFrameSink*
TNewScriptEndpointClient::GetScriptDataOutXlator(void)
{
	if (fDataOut == nil)
	{
		PFrameSink* xlator = (PFrameSink*) NewByName("PFrameSink", "PScriptDataOut");
		if (xlator == nil)
			Throw(exTranslatorException, (void*) (Long) MemError(), nil);
		else
			fDataOut = xlator;
	}
	return fDataOut;
}


// ROM 0x00133f00 ConvertToOptionArray__24TNewScriptEndpointClientFRC6RefVarP12TOptionArray
// The options frame(s) onto the array (a translator's error answered).
NewtonErr
TNewScriptEndpointClient::ConvertToOptionArray(RefArg options, TOptionArray* array)
{
	if (fOptionsOut == nil)
	{
		PFrameSink* xlator = (PFrameSink*) NewByName("PFrameSink", "POptionDataOut");
		if (xlator == nil)
			return MemError();
		fOptionsOut = xlator;
	}
	NewtonErr err = noErr;
	newton_try
	{
		OptionDataParms parms;
		parms.fOptions = array;
		parms.fFrame = options;
		parms.fXlator = GetScriptDataOutXlator();
		fOptionsOut->Translate(&parms, nil);
	}
	newton_catch(exTranslatorException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x00134018 ConvertFromOptionArray__24TNewScriptEndpointClientFRC6RefVarP12TOptionArray
// The array's results and data back into the options frame(s).
NewtonErr
TNewScriptEndpointClient::ConvertFromOptionArray(RefArg options, TOptionArray* array)
{
	if (fOptionsIn == nil)
	{
		PFrameSource* xlator = (PFrameSource*) NewByName("PFrameSource", "POptionDataIn");
		if (xlator == nil)
			return MemError();
		fOptionsIn = xlator;
	}
	NewtonErr err = noErr;
	newton_try
	{
		OptionDataParms parms;
		parms.fOptions = array;
		parms.fFrame = options;
		parms.fXlator = GetScriptDataInXlator();
		RefVar result(fOptionsIn->Translate(&parms, nil));
	}
	newton_catch(exTranslatorException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x001348f0 PrepOptions__24TNewScriptEndpointClientFRC6RefVarPP12TOptionArray
// A new option array (the caller's to delete) of the options frame(s).
NewtonErr
TNewScriptEndpointClient::PrepOptions(RefArg options, TOptionArray** array)
{
	*array = new TOptionArray;
	NewtonErr err = MemError();
	if (err != noErr)
		return err;
	err = (*array)->Init();
	if (err != noErr)
		return err;
	return ConvertToOptionArray(options, *array);
}


// ROM 0x00134848 GetParms__24TNewScriptEndpointClientSFRC6RefVarPUl
// A callback spec: synchronous unless it has an async slot, and its
// reqTimeout (milliseconds).
Boolean
TNewScriptEndpointClient::GetParms(RefArg callback, ULong* timeout)
{
	Boolean sync = true;
	if (NOTNIL(callback))
	{
		sync = (GetVariable(callback, RSSYMasync, nil, 0) == NILREF);
		Ref reqTimeout = GetVariable(callback, RSSYMreqtimeout, nil, 0);
		if (ISINT(reqTimeout))
			*timeout = RINT(reqTimeout) * kMilliseconds;
	}
	return sync;
}


// ROM 0x0013413c DoState__24TNewScriptEndpointClientFv
Ref
TNewScriptEndpointClient::DoState(void)
{
	return MAKEINT(fEndpoint->GetState());
}


// The shape of Bind, Connect, Listen and Accept: the options made an array,
// the options and callback queued for an asynchronous call, the endpoint
// asked, and a synchronous call's results read back into the options; the
// array is the completion's to delete when the call is asynchronous.
#define OPTIONS_REQUEST(call)																\
	TOptionArray* array = nil;																\
	ULong timeout = 0;																		\
	Boolean sync = GetParms(callback, &timeout);											\
	NewtonErr err = noErr;																	\
	if (ISNIL(options) || (err = PrepOptions(options, &array)) == noErr)					\
	{																						\
		if (!sync)																			\
			QueueOptions(options, callback);												\
		err = call;																			\
		if (err == noErr && sync && NOTNIL(options))										\
			err = ConvertFromOptionArray(options, array);									\
	}																						\
	if (array != nil)																		\
	{																						\
		if (err == noErr && !sync)															\
			return err;																		\
		delete array;																		\
	}																						\
	if (err != noErr && !sync)																\
		UnwindOptions();																	\
	return err;


// ROM 0x0013414c DoBind__24TNewScriptEndpointClientFRC6RefVarT1
NewtonErr
TNewScriptEndpointClient::DoBind(RefArg options, RefArg callback)
{
	if (RomBugFixed())
	{
		// OPTIONS_REQUEST, with the array an asynchronous bind leaves kept
		// for BindComplete (the ROM bug described there)
		TOptionArray* array = nil;
		ULong timeout = 0;
		Boolean sync = GetParms(callback, &timeout);
		NewtonErr err = noErr;
		if (ISNIL(options) || (err = PrepOptions(options, &array)) == noErr)
		{
			if (!sync)
				QueueOptions(options, callback);
			err = fEndpoint->nBind(array, timeout, sync);
			if (err == noErr && sync && NOTNIL(options))
				err = ConvertFromOptionArray(options, array);
		}
		if (array != nil)
		{
			if (err == noErr && !sync)
			{
				if (fBindOptions != nil)
					delete fBindOptions;
				fBindOptions = array;
				return err;
			}
			delete array;
		}
		if (err != noErr && !sync)
			UnwindOptions();
		return err;
	}
	OPTIONS_REQUEST(fEndpoint->nBind(array, timeout, sync))
}


// ROM 0x00134244 DoConnect__24TNewScriptEndpointClientFRC6RefVarT1
NewtonErr
TNewScriptEndpointClient::DoConnect(RefArg options, RefArg callback)
{
	OPTIONS_REQUEST(fEndpoint->nConnect(array, nil, nil, timeout, sync))
}


// ROM 0x0013434c DoListen__24TNewScriptEndpointClientFRC6RefVarT1
NewtonErr
TNewScriptEndpointClient::DoListen(RefArg options, RefArg callback)
{
	OPTIONS_REQUEST(fEndpoint->nListen(array, nil, nil, timeout, sync))
}


// ROM 0x00134560 DoAccept__24TNewScriptEndpointClientFRC6RefVarT1
// (the call is accepted on the endpoint itself)
NewtonErr
TNewScriptEndpointClient::DoAccept(RefArg options, RefArg callback)
{
	OPTIONS_REQUEST(fEndpoint->nAccept(fEndpoint, array, nil, 0, timeout, sync))
}


// ROM 0x00138d30 DoOption__24TNewScriptEndpointClientFRC6RefVarT1
// Options set (opcode 0x500, the options' own op codes), which must be given.
NewtonErr
TNewScriptEndpointClient::DoOption(RefArg options, RefArg callback)
{
	TOptionArray* array = nil;
	ULong timeout = 0;
	Boolean sync = GetParms(callback, &timeout);
	NewtonErr err;
	if (ISNIL(options))
		err = kCommScriptErrNotAnOption;
	else if ((err = PrepOptions(options, &array)) == noErr)
	{
		if (!sync)
			QueueOptions(options, callback);
		err = fEndpoint->nOptMgmt(0x500, array, timeout, sync);
		if (err == noErr && sync)
			err = ConvertFromOptionArray(options, array);
	}
	if (array != nil)
	{
		if (err == noErr && !sync)
			return err;
		delete array;
	}
	if (err != noErr && !sync)
		UnwindOptions();
	return err;
}


// ROM 0x00134454 DoDisconnect__24TNewScriptEndpointClientFRC6RefVarT1
// A release (orderly) when cancelPending is nil, else a disconnect.
NewtonErr
TNewScriptEndpointClient::DoDisconnect(RefArg cancelPending, RefArg callback)
{
	ULong timeout = 0;
	Boolean sync = GetParms(callback, &timeout);
	if (!sync)
		QueueCallback(callback);
	NewtonErr err;
	if (ISNIL(cancelPending))
		err = fEndpoint->nRelease(timeout, sync);
	else
		err = fEndpoint->nDisconnect(nil, 0, 0, timeout, sync);
	if (err != noErr && !sync)
		UnwindCallback();
	return err;
}


// ROM 0x001344f8 DoUnBind__24TNewScriptEndpointClientFRC6RefVar
NewtonErr
TNewScriptEndpointClient::DoUnBind(RefArg callback)
{
	ULong timeout = 0;
	Boolean sync = GetParms(callback, &timeout);
	if (!sync)
		QueueCallback(callback);
	NewtonErr err = fEndpoint->nUnBind(timeout, sync);
	if (err != noErr && !sync)
		UnwindCallback();
	return err;
}


// ROM 0x00134f1c DoAbort__24TNewScriptEndpointClientFRC6RefVar
NewtonErr
TNewScriptEndpointClient::DoAbort(RefArg callback)
{
	ULong timeout = 0;
	Boolean sync = GetParms(callback, &timeout);
	if (!sync)
		AddArraySlot(fAborts, callback);
	NewtonErr err = fEndpoint->nAbort(sync);
	if (err != noErr && !sync)
		SetLength(fAborts, Length(fAborts) - 1);
	return err;
}


// ROM 0x00134a08 QueueCallback__24TNewScriptEndpointClientFRC6RefVar
void
TNewScriptEndpointClient::QueueCallback(RefArg callback)
{
	AddArraySlot(fRequests, callback);
}


// ROM 0x00134a10 QueueOptions__24TNewScriptEndpointClientFRC6RefVarT1
void
TNewScriptEndpointClient::QueueOptions(RefArg options, RefArg callback)
{
	AddArraySlot(fRequests, options);
	AddArraySlot(fRequests, callback);
}


// ROM 0x00134a3c UnwindCallback__24TNewScriptEndpointClientFv
void
TNewScriptEndpointClient::UnwindCallback(void)
{
	SetLength(fRequests, Length(fRequests) - 1);
}


// ROM 0x00134a6c UnwindOptions__24TNewScriptEndpointClientFv
void
TNewScriptEndpointClient::UnwindOptions(void)
{
	SetLength(fRequests, Length(fRequests) - 2);
}


// ROM 0x00134a9c CommandComplete__24TNewScriptEndpointClientFlRC6RefVarT2
// The callback off the front of the queue, told.
void
TNewScriptEndpointClient::CommandComplete(long result, RefArg queue, RefArg options)
{
	RefVar callback(GetArraySlotRef(queue, 0));
	ArrayRemoveCount(queue, 0, 1);
	DoCompletion(result, callback, options);
}


// ROM 0x00134b04 OptionCommandComplete__24TNewScriptEndpointClientFlRC6RefVarP12TOptionArray
// The options off the front of the queue, the array's results read back
// into them (and the array deleted), then the callback told.
void
TNewScriptEndpointClient::OptionCommandComplete(long result, RefArg queue, TOptionArray* array)
{
	RefVar options(GetArraySlotRef(queue, 0));
	ArrayRemoveCount(queue, 0, 1);
	if (result == noErr)
		result = ConvertFromOptionArray(options, array);
	if (array != nil)
		delete array;
	CommandComplete(result, queue, options);
}


// ROM 0x00134b9c DoCompletion__24TNewScriptEndpointClientFlRC6RefVarT2
// callback:completionScript(endpoint, options, result - nil for success).
// With no completionScript an error goes to the endpoint's
// exceptionHandler, or is thrown.
void
TNewScriptEndpointClient::DoCompletion(long result, RefArg callback, RefArg options)
{
	if (ISNIL(callback) || GetVariable(callback, RSSYMcompletionscript, nil, 0) == NILREF)
	{
		if (result != noErr)
		{
			if (ISNIL(fEndpointRef) || GetVariable(fEndpointRef, RSSYMexceptionhandler, nil, 0) == NILREF)
				Throw(kExComm, (void*) (Long) result, nil);
			else
			{
				RefVar ex(AllocateFrame());
				SetFrameSlot(ex, RSSYMname, RefVar(Intern(kExComm)));
				SetFrameSlot(ex, RSSYMdata, RefVar(MAKEINT(result)));
				SetFrameSlot(ex, RSSYMdebug, RSSYMcompletionscript);
				RefVar args(MakeArray(1));
				SetArraySlotRef(args, 0, ex);
				DoMessage(fEndpointRef, RSSYMexceptionhandler, args);
			}
		}
	}
	else
	{
		RefVar args(MakeArray(3));
		SetArraySlotRef(args, 0, fEndpointRef);
		SetArraySlotRef(args, 1, options);
		SetArraySlotRef(args, 2, (result == noErr) ? RefVar() : RefVar(MAKEINT(result)));
		SendEndpointScript(fEndpointRef, callback, RSSYMcompletionscript, args);
	}
}


// ROM 0x001346a8 OptMgmtComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::OptMgmtComplete(TEndpointEvent* event)
{
	OptionCommandComplete(event->fError, fRequests, ((TOptMgmtCompleteEvent*) event)->fOptions);
}


// ROM 0x001346b8 BindComplete__24TNewScriptEndpointClientFP14TEndpointEvent
// ROM BUG (fixed): the options are read out of the event's first word as
// for an option request, but a bind's event has its queue length there
// (which TSerialEndpoint::nBind leaves nought), so an asynchronous bind's
// options are never read back and their array is never deleted.  The fix
// keeps the array DoBind made (fBindOptions) and reads them back from it.
void
TNewScriptEndpointClient::BindComplete(TEndpointEvent* event)
{
	if (RomBugFixed())
	{
		TOptionArray* array = fBindOptions;
		fBindOptions = nil;
		OptionCommandComplete(event->fError, fRequests, array);
		return;
	}
	OptionCommandComplete(event->fError, fRequests, ((TOptMgmtCompleteEvent*) event)->fOptions);
}


// ROM 0x001346c8 ConnectComplete__24TNewScriptEndpointClientFP14TEndpointEvent
// The address array deleted, the options read back.
void
TNewScriptEndpointClient::ConnectComplete(TEndpointEvent* event)
{
	TConnectCompleteEvent* ev = (TConnectCompleteEvent*) event;
	if (ev->fAddr != nil)
		delete ev->fAddr;
	OptionCommandComplete(ev->fError, fRequests, ev->fOptions);
}


// ROM 0x00134704 ListenComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::ListenComplete(TEndpointEvent* event)
{
	TConnectCompleteEvent* ev = (TConnectCompleteEvent*) event;
	if (ev->fAddr != nil)
		delete ev->fAddr;
	OptionCommandComplete(ev->fError, fRequests, ev->fOptions);
}


// ROM 0x00134740 AcceptComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::AcceptComplete(TEndpointEvent* event)
{
	TConnectCompleteEvent* ev = (TConnectCompleteEvent*) event;
	if (ev->fAddr != nil)
		delete ev->fAddr;
	OptionCommandComplete(ev->fError, fRequests, ev->fOptions);
}


// ROM 0x0013477c ReleaseComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::ReleaseComplete(TEndpointEvent* event)
{
	CommandComplete(event->fError, fRequests, RefVar());
}


// ROM 0x001347c0 DisconnectComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::DisconnectComplete(TEndpointEvent* event)
{
	CommandComplete(event->fError, fRequests, RefVar());
}


// ROM 0x00134804 UnBindComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::UnBindComplete(TEndpointEvent* event)
{
	CommandComplete(event->fError, fRequests, RefVar());
}


// ROM 0x00134f98 AbortComplete__24TNewScriptEndpointClientFP14TEndpointEvent
void
TNewScriptEndpointClient::AbortComplete(TEndpointEvent* event)
{
	CommandComplete(event->fError, fAborts, RefVar());
}


// ROM 0x00138688 AEHandlerProc__24TNewScriptEndpointClientFP10TUMsgTokenPUlP7TAEvent
// An endpoint event handled in the default port, the views brought up to
// date after it; an evt.ex exception to the endpoint's exceptionHandler.
void
TNewScriptEndpointClient::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	GrafPtr savedPort;
	GetPort(&savedPort);
	SetPort(&gGrafPort);
	newton_try
	{
		TEndpointClient::AEHandlerProc(token, size, event);
		gRootView->Update(nil);
	}
	newton_catch("evt.ex")
	{
		EndpointScriptException(fEndpointRef, CurrentException(), RefVar(), false);
	}
	end_try;
	SetPort(savedPort);
}


// ROM 0x0013930c Default__24TNewScriptEndpointClientFP14TEndpointEvent
// Any other event to endpoint:eventHandler({eventCode, data, serviceId,
// time}) - the service id the event's first four bytes as a string.
void
TNewScriptEndpointClient::Default(TEndpointEvent* event)
{
	if (ISNIL(fEndpointRef) || GetVariable(fEndpointRef, RSSYMeventhandler, nil, 0) == NILREF)
		return;
	RefVar args(MakeArray(1));
	RefVar info(AllocateFrame());
	char id[5];
	memmove(id, (char*) event + sizeof(TEndpointEvent), 4);
	id[4] = 0;
	RefVar serviceId(AllocateBinary(RSSYMstring, 10));
	ConvertToUnicode(id, (UniChar*) BinaryData(serviceId), 1, 0x7fffffff);
	SetFrameSlot(info, RSSYMeventcode, RefVar(MAKEINT(event->fEventCode)));
	SetFrameSlot(info, RSSYMdata, RefVar(MAKEINT(event->fReserved)));
	SetFrameSlot(info, RSSYMserviceid, serviceId);
	SetFrameSlot(info, RSSYMtime, RefVar(MAKEINT(TTimeToMilliseconds(event->fTime))));
	SetArraySlotRef(args, 0, info);
	SendEndpointScript(fEndpointRef, fEndpointRef, RSSYMeventhandler, args);
}


/* -------------------------------------------------------------------------------
	Output
------------------------------------------------------------------------------- */

// ROM 0x00134fdc DoOutput__24TNewScriptEndpointClientFRC6RefVarN21
// Data sent by the output spec's form ({form, sendFlags, target, async,
// reqTimeout, completionScript}): a 'frame flattened, a binary sent as it
// is (or the target's stretch of it), anything else converted.  An
// asynchronous output queues [the binary locked for it (or nil),
// options, callback].
NewtonErr
TNewScriptEndpointClient::DoOutput(RefArg data, RefArg options, RefArg outputSpec)
{
	ULong flags = 1;
	TOptionArray* array = nil;
	ULong timeout = 0;
	Boolean sync = GetParms(outputSpec, &timeout);
	NewtonErr err = noErr;
	FormType form;
	if (NOTNIL(options) && (err = PrepOptions(options, &array)) != noErr)
		goto done;
	if (ISNIL(outputSpec))
		form = GetDataForm(RefVar(), kFormUserAny);
	else
	{
		Ref sendFlags = GetVariable(outputSpec, RSSYMsendflags, nil, 0);
		if (ISINT(sendFlags))
			flags = RINT(sendFlags);
		form = GetDataForm(RefVar(GetVariable(outputSpec, RSSYMform, nil, 0)), kFormUserAny);
	}
	if (!sync)
	{
		AddArraySlot(fOutputs, RefVar());
		AddArraySlot(fOutputs, options);
		AddArraySlot(fOutputs, outputSpec);
	}
	if (form == kFormFrame)
	{
		if (IsFrame(data))
			err = OutputFrame(data, sync, flags, timeout, array);
		else
			err = OutputData(data, form, sync, flags, timeout, array);
	}
	else if ((form == kFormExport && IsRaw(data))
		  || (form == kFormBinary && IsRawOrString(data)))
		err = OutputRaw(data, outputSpec, sync, flags, timeout, array);
	else if (form == kFormNone)
	{
		err = kCommScriptErrBadForm;
		goto done;
	}
	else
		err = OutputData(data, form, sync, flags, timeout, array);
	if (err == noErr && sync && NOTNIL(options))
		err = ConvertFromOptionArray(options, array);

done:
	if (array != nil)
	{
		if (err == noErr && !sync)
			return err;
		delete array;
	}
	if (err != noErr && !sync)
		SetLength(fOutputs, Length(fOutputs) - 3);
	return err;
}


// ROM 0x00135440 OutputData__24TNewScriptEndpointClientFRC6RefVar8FormTypeUcUlT4P12TOptionArray
// The data converted by its form into a block, which is sent (and freed
// unless an asynchronous send has it).
NewtonErr
TNewScriptEndpointClient::OutputData(RefArg data, FormType form, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options)
{
	Ptr block = nil;
	NewtonErr err = noErr;
	newton_try
	{
		FrameSinkParms parms;
		parms.fValue = data;
		parms.fForm = form;
		parms.fEncoding = fEncoding;
		parms.fUseHandle = false;
		parms.fHeaderSize = 0;
		block = (Ptr) GetScriptDataOutXlator()->Translate(&parms, nil);
	}
	newton_catch(exTranslatorException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	if (err == noErr)
	{
		Size count = GetPtrSize(block);
		err = fEndpoint->nSnd((UByte*) block, &count, flags, timeout, sync, options);
	}
	if (block != nil && (err != noErr || sync))
		DisposPtr(block);
	return err;
}


// ROM 0x0013558c OutputRaw__24TNewScriptEndpointClientFRC6RefVarT1UcUlT4P12TOptionArray
// A binary sent where it is, locked meanwhile - the output spec's target
// {offset, length} saying which stretch.
NewtonErr
TNewScriptEndpointClient::OutputRaw(RefArg data, RefArg outputSpec, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options)
{
	NewtonErr err = noErr;
	LockRef(data);
	UByte* buf = (UByte*) BinaryData(data);
	Size count = Length(data);
	if (NOTNIL(outputSpec))
	{
		RefVar target(GetVariable(outputSpec, RSSYMtarget, nil, 0));
		if (NOTNIL(target) && IsFrame(target))
		{
			long offset = 0;
			Ref offsetRef = GetVariable(target, RSSYMoffset, nil, 0);
			if (ISINT(offsetRef))
			{
				offset = RINT(offsetRef);
				buf += offset;
			}
			Ref lengthRef = GetVariable(target, RSSYMlength, nil, 0);
			if (ISINT(lengthRef))
			{
				Long length = RINT(lengthRef);
				Boolean past = count < offset + length;
				count = length;
				if (past)
					err = kCommScriptErrBadRange;
			}
			else
			{
				count = count - offset;
				if (count < 1)
					err = kCommScriptErrBadRange;
			}
		}
		if (err != noErr)
			goto done;
	}
	if (!sync)
		SetArraySlotRef(fOutputs, Length(fOutputs) - 3, data);
	err = fEndpoint->nSnd(buf, &count, flags, timeout, sync, options);
done:
	if (err != noErr || sync)
		UnlockRef(data);
	return err;
}


// ROM 0x00135778 OutputFrame__24TNewScriptEndpointClientFRC6RefVarUcUlT3P12TOptionArray
// A frame flattened (PFlattenPtr), its length in the first word.
NewtonErr
TNewScriptEndpointClient::OutputFrame(RefArg data, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options)
{
	NewtonErr err = noErr;
	Ptr block = nil;
	if (fFlattenOut == nil)
	{
		PFrameSink* xlator = (PFrameSink*) NewByName("PFrameSink", "PFlattenPtr");
		if (xlator == nil)
		{
			err = MemError();
			if (err == noErr)
				err = kScriptEpNoMemory;		// (host: the registry does not set one)
		}
		else
			fFlattenOut = xlator;
		if (err != noErr)
			return err;
	}
	newton_try
	{
		FlattenPtrParms parms;
		parms.fValue = data;
		parms.fUseHandle = false;
		parms.fHeaderSize = 4;
		block = (Ptr) fFlattenOut->Translate(&parms, nil);
	}
	newton_catch(exTranslatorException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	if (err == noErr)
	{
		Size count = GetPtrSize(block);
		// DEVIATION: the length word in the device's (big-endian) order
		PutBigEndianWord(block, (unsigned int) (count - 4));
		err = fEndpoint->nSnd((UByte*) block, &count, flags, timeout, sync, options);
	}
	if ((err != noErr || sync) && block != nil)
		DisposPtr(block);
	return err;
}


// ROM 0x00135904 SndComplete__24TNewScriptEndpointClientFP14TEndpointEvent
// The binary sent from unlocked (or the converted block freed), then the
// options and callback told.
void
TNewScriptEndpointClient::SndComplete(TEndpointEvent* event)
{
	TSndCompleteEvent* ev = (TSndCompleteEvent*) event;
	RefVar data(GetArraySlotRef(fOutputs, 0));
	ArrayRemoveCount(fOutputs, 0, 1);
	if (ISNIL(data))
	{
		if (ev->fData != nil)
			delete ev->fData;
		else if (ev->fBuffer != nil)
			DisposPtr((Ptr) ev->fBuffer);
	}
	else
		UnlockRef(data);
	OptionCommandComplete(ev->fError, fOutputs, ev->fOptions);
}


/* -------------------------------------------------------------------------------
	Input
------------------------------------------------------------------------------- */

// ROM 0x001359a4 DoInputSpec__24TNewScriptEndpointClientFRC6RefVar
// A new input spec (nil: input stopped, the buffer given back) read, the
// buffers made ready and a receive posted; the partial idler started.
NewtonErr
TNewScriptEndpointClient::DoInputSpec(RefArg inputSpec)
{
	NewtonErr err = noErr;
	if (fRcvPending)
		err = kCommScriptErrRcvPending;
	if (err == noErr)
	{
		ClearInputSpec();
		if (ISNIL(inputSpec))
		{
			if (fInputBuffer != nil)
			{
				DisposPtr((Ptr) fInputBuffer);
				fInputBuffer = nil;
			}
			fInputBufferSize = 0;
			StopIdle();
			return err;
		}
		fInputSpec = inputSpec;
		err = ReadInputSlots();
		if (err == noErr && (err = InitInputBuffers()) == noErr)
		{
			err = PostReceive();
			if (fPartialFrequency != 0 && err == noErr)
			{
				ResetIdle(fPartialFrequency, kMilliseconds);
				return noErr;
			}
			StopIdle();
		}
	}
	if (err == noErr || err == kCommScriptErrRcvPending)
		return err;
	ClearInputSpec();
	StopIdle();
	return err;
}


// ROM 0x00135c8c ClearInputSpec__24TNewScriptEndpointClientFv
void
TNewScriptEndpointClient::ClearInputSpec(void)
{
	fInputSpec = NILREF;
	fEndSequence = NILREF;
	fProxyBytes = NILREF;
	fProxies = NILREF;
	fByteCount = 0;
	fPartialFrequency = 0;
	fSevenBit = false;
	fRcvOptions = NILREF;
	fRcvOptionsState = 2;
	fInputForm = kFormString;
}


// ROM 0x00135cd8 ReadInputSlots__24TNewScriptEndpointClientFv
// The input spec's slots: form, target, rcvFlags, termination (or the
// discardAfter a string or bytes keeps), optimize, filter, rcvOptions,
// partialFrequency, reqTimeout.
NewtonErr
TNewScriptEndpointClient::ReadInputSlots(void)
{
	NewtonErr err = noErr;
	RefVar value;
	fInputForm = GetDataForm(RefVar(GetVariable(fInputSpec, RSSYMform, nil, 0)), kFormUserData);
	if (fInputForm == kFormNone)
		err = kCommScriptErrBadForm;
	else
	{
		value = GetVariable(fInputSpec, RSSYMtarget, nil, 0);
		if (ISNIL(value) || !IsFrame(value))
		{
			fTarget = NILREF;
			if (fInputForm == kFormTemplate)
				err = kCommScriptErrBadTemplate;
			else if (fInputForm == kFormBinary)
				err = kCommScriptErrNoTarget;
		}
		else
			err = ReadTarget(value);
	}

	value = GetVariable(fInputSpec, RSSYMrcvflags, nil, 0);
	if (ISINT(value))
	{
		fRcvFlags = RINT(value);
		fUseEOP = (fRcvFlags & 2) != 0;
	}
	else
		fRcvFlags = 0;

	if (err == noErr)
	{
		value = GetVariable(fInputSpec, RSSYMtermination, nil, 0);
		if (ISNIL(value) || !IsFrame(value))
		{
			long discardAfter;
			if (fInputForm == kFormString || fInputForm == kFormBytes)
			{
				value = GetVariable(fInputSpec, RSSYMdiscardafter, nil, 0);
				discardAfter = ISINT(value) ? RINT(value) : 0x400;
			}
			else
				discardAfter = 0x7fffffff;
			fDiscardAfter = discardAfter;
			fByteCount = 0;
			fEndSequence = NILREF;
		}
		else
			err = ReadTermination(value);
	}

	value = GetVariable(fInputSpec, RSSYMoptimize, nil, 0);
	fOptimize = NOTNIL(value);

	if (err == noErr)
	{
		value = GetVariable(fInputSpec, RSSYMfilter, nil, 0);
		if (NOTNIL(value) && IsFrame(value))
			err = ReadFilter(value);
		else
		{
			fProxyBytes = NILREF;
			fProxies = NILREF;
			fSevenBit = false;
		}
	}
	if (err != noErr)
		return err;

	RefVar rcvOptions(GetVariable(fInputSpec, RSSYMrcvoptions, nil, 0));
	if (ISNIL(rcvOptions))
		fRcvOptions = NILREF;
	else
	{
		fRcvOptions = CloneOptions(rcvOptions);
		fRcvOptionsState = 0;
	}

	value = GetVariable(fInputSpec, RSSYMpartialfrequency, nil, 0);
	if (ISINT(value))
	{
		if (fInputForm == kFormString || fInputForm == kFormBytes)
			fPartialFrequency = RINT(value);
		else
			err = kCommScriptErrPartialForm;
	}
	else
		fPartialFrequency = 0;

	value = GetVariable(fInputSpec, RSSYMreqtimeout, nil, 0);
	fInputTimeout = ISINT(value) ? RINT(value) * kMilliseconds : 0;
	return err;
}


// ROM 0x0013608c ReadTermination__24TNewScriptEndpointClientFRC6RefVar
// {useEOP, byteCount, endSequence} for a string, bytes or binary input.
// A useEOP slot that disagrees with rcvFlags: a useEOP asked for without
// the flag is ignored whole; nil with the flag turns it off.
NewtonErr
TNewScriptEndpointClient::ReadTermination(RefArg termination)
{
	NewtonErr err = noErr;
	RefVar value;
	if (!(fInputForm == kFormString || fInputForm == kFormBytes || fInputForm == kFormBinary))
		return kCommScriptErrTerminationForm;
	long exists;
	value = GetVariable(termination, RSSYMuseeop, &exists, 0);
	if (exists != 0)
	{
		if ((fRcvFlags & 2) == 0)
		{
			if (NOTNIL(value))
				return kCommScriptErrTerminationForm;
		}
		else if (ISNIL(value))
			fUseEOP = false;
	}
	value = GetVariable(termination, RSSYMbytecount, nil, 0);
	if (ISINT(value))
	{
		fByteCount = RINT(value);
		fDiscardAfter = 0x7fffffff;
	}
	else
	{
		fByteCount = 0;
		value = GetVariable(fInputSpec, RSSYMdiscardafter, nil, 0);
		fDiscardAfter = ISINT(value) ? RINT(value) : 0x400;
	}
	value = GetVariable(termination, RSSYMendsequence, nil, 0);
	fEndSequence = NILREF;
	if (NOTNIL(value))
	{
		if (fInputForm == kFormBinary)
			return kCommScriptErrTerminationForm;
		fEndSequence = MakeArray(0);
		if (!IsArray(value))
			err = AddEndArrayElement(value);
		else
		{
			for (ULong i = 0; i < (ULong) Length(value); i++)
			{
				RefVar element(GetArraySlotRef(value, i));
				err = AddEndArrayElement(element);
				if (err != noErr)
					break;
			}
		}
		if (Length(fEndSequence) == 0 || err != noErr)
			fEndSequence = NILREF;
	}
	return err;
}


// ROM 0x001362f4 ReadTarget__24TNewScriptEndpointClientFRC6RefVar
// A 'binary input's {data, offset} - received into in place - or a
// 'template's {typelist, arglist}.
NewtonErr
TNewScriptEndpointClient::ReadTarget(RefArg target)
{
	NewtonErr err = noErr;
	if (fInputForm == kFormBinary)
	{
		RefVar data(GetVariable(target, RSSYMdata, nil, 0));
		if (ISNIL(data) || !IsRawOrString(data))
			err = kCommScriptErrNoTarget;
		else
		{
			fTarget = data;
			Ref offset = GetVariable(target, RSSYMoffset, nil, 0);
			fTargetOffset = ISINT(offset) ? RINT(offset) : 0;
		}
	}
	else
	{
		if (fInputForm != kFormTemplate)
		{
			fTarget = NILREF;
			return kCommScriptErrBadRange;
		}
		RefVar typelist(GetVariable(target, RSSYMtypelist, nil, 0));
		if (ISNIL(typelist) || !IsArray(typelist))
			err = kCommScriptErrBadTemplate;
		else
			fTarget = typelist;
	}
	return err;
}


// ROM 0x001365a8 ReadFilter__24TNewScriptEndpointClientFRC6RefVar
// {byteProxy: {byte, proxy} or an array of them, sevenBit}.
NewtonErr
TNewScriptEndpointClient::ReadFilter(RefArg filter)
{
	NewtonErr err = noErr;
	if (fInputForm == kFormBinary)
		return kCommScriptErrFilterForm;
	RefVar value(GetVariable(filter, RSSYMbyteproxy, nil, 0));
	fProxyBytes = NILREF;
	fProxies = NILREF;
	if (NOTNIL(value))
	{
		fProxyBytes = MakeArray(0);
		fProxies = MakeArray(0);
		if (!IsArray(value))
			err = AddProxyFrame(value);
		else
		{
			for (ULong i = 0; i < (ULong) Length(value); i++)
			{
				RefVar element(GetArraySlotRef(value, i));
				err = AddProxyFrame(element);
				if (err != noErr)
					break;
			}
		}
		if (Length(fProxyBytes) == 0 || err != noErr)
		{
			fProxyBytes = NILREF;
			fProxies = NILREF;
		}
	}
	fSevenBit = GetVariable(filter, RSSYMsevenbit, nil, 0) != NILREF;
	return err;
}


// ROM 0x00136760 AddProxyFrame__24TNewScriptEndpointClientFRC6RefVar
NewtonErr
TNewScriptEndpointClient::AddProxyFrame(RefArg proxy)
{
	RefVar byte(GetVariable(proxy, RSSYMbyte, nil, 0));
	RefVar replacement(GetVariable(proxy, RSSYMproxy, nil, 0));
	NewtonErr err = AddProxyArrayElement(byte, fProxyBytes);
	if (err == noErr)
		err = AddProxyArrayElement(replacement, fProxies);
	return err;
}


// ROM 0x001367fc AddProxyArrayElement__24TNewScriptEndpointClientFRC6RefVarT1
// A byte (an integer, or a character of one byte in the encoding), or nil,
// onto the array.
NewtonErr
TNewScriptEndpointClient::AddProxyArrayElement(RefArg element, RefArg array)
{
	NewtonErr err = noErr;
	newton_try
	{
		Ref r = element;
		if (ISCHAR(r))
		{
			UniChar c = RCHAR(r);
			if (Umbstrnlen(&c, fEncoding, 1) == 1)
				AddArraySlot(array, RefVar(MAKEINT(CharacterToUByte(c, fEncoding))));
			else
				err = kCommScriptErrBadCharacter;
		}
		else if (ISINT(r))
			AddArraySlot(array, element);
		else if (r == NILREF)
			AddArraySlot(array, RefVar());
		else
			err = kCommScriptErrBadProxy;
	}
	newton_catch(exOutOfMemory)
	{ }
	end_try;
	return err;
}


// ROM 0x0013696c AddEndArrayElement__24TNewScriptEndpointClientFRC6RefVar
// An end sequence: a byte (an integer or a character) or a binary as it
// is, or a string, an array of integers or an array of characters made a
// binary of their bytes.  (An array element of the wrong kind stops the
// copy and answers the error, but the binary made so far is added.)
NewtonErr
TNewScriptEndpointClient::AddEndArrayElement(RefArg element)
{
	NewtonErr err = noErr;
	newton_try
	{
		Ref r = element;
		if (ISCHAR(r))
			AddProxyArrayElement(element, fEndSequence);
		else if (ISINT(r) || IsRaw(element))
			AddArraySlot(fEndSequence, element);
		else if (IsArray(element) && ISINT(GetArraySlotRef(element, 0)))
		{
			LockRef(element);
			long length = Length(element);
			RefVar bytes(AllocateBinary(RSSYMbinary, length));
			LockRef(bytes);
			UByte* p = (UByte*) BinaryData(bytes);
			for (long i = 0; i < length; i++)
			{
				Ref b = GetArraySlotRef(element, i);
				if (!ISINT(b))
				{
					err = kCommScriptErrBadEndSequence;
					break;
				}
				p[i] = (UByte) RINT(b);
			}
			UnlockRef(bytes);
			UnlockRef(element);
			AddArraySlot(fEndSequence, bytes);
		}
		else if (IsArray(element) && ISCHAR(GetArraySlotRef(element, 0)))
		{
			LockRef(element);
			long length = Length(element);
			RefVar bytes(AllocateBinary(RSSYMbinary, length));
			LockRef(bytes);
			UByte* p = (UByte*) BinaryData(bytes);
			for (long i = 0; i < length; i++)
			{
				Ref c = GetArraySlotRef(element, i);
				if (!ISCHAR(c))
				{
					err = kCommScriptErrBadEndSequence;
					break;
				}
				p[i] = CharacterToUByte(RCHAR(c), fEncoding);
			}
			UnlockRef(bytes);
			UnlockRef(element);
			AddArraySlot(fEndSequence, bytes);
		}
		else if (IsString(element))
		{
			LockRef(element);
			const UniChar* s = GetCString(element);
			long length = Umbstrlen(s);
			RefVar bytes(AllocateBinary(RSSYMbinary, length));
			LockRef(bytes);
			UByte* p = (UByte*) BinaryData(bytes);
			for (long i = 0; i < length; i++)
				p[i] = CharacterToUByte(s[i], fEncoding);
			UnlockRef(bytes);
			AddArraySlot(fEndSequence, bytes);
			UnlockRef(element);
		}
		else if (NOTNIL(element))
			err = kCommScriptErrBadEndSequence;
	}
	newton_catch(exOutOfMemory)
	{ }
	end_try;
	return err;
}


// ROM 0x00136d94 InitInputBuffers__24TNewScriptEndpointClientFv
// The input buffer sized for the form: a binary target is received into
// in place (its length from the offset, or the byteCount if less); a
// 'frame reads a length word first; a number four bytes, a character one,
// a template what its arglist marshals to, anything else the byteCount,
// or discardAfter and 0x40 more.  Input kept from before is looked at
// again under the new spec.
NewtonErr
TNewScriptEndpointClient::InitInputBuffers(void)
{
	NewtonErr err = noErr;
	fRawTarget = false;
	if (fInputForm == kFormBinary)
	{
		long room = Length(fTarget) - fTargetOffset;
		if (fByteCount == 0 || room < fByteCount)
			fByteCount = room;
		fRawTarget = true;
		return noErr;
	}
	if (fInputForm == kFormFrame)
	{
		fReadFrameLength = true;
		fByteCount = 4;
		if (fInputBuffer != nil)
			goto keep;
		UByte* p = (UByte*) NewPtr(4);
		if (MemError() != noErr)
			err = kScriptEpNoMemory;
		fInputBuffer = p;
		fInputBufferSize = 4;
	}
	else
	{
		long size;
		if (fInputForm == kFormNumber)
			size = fByteCount = 4;
		else if (fInputForm == kFormChar)
			size = fByteCount = 1;
		else if (fInputForm == kFormTemplate)
		{
			RefVar target(GetVariable(fInputSpec, RSSYMtarget, nil, 0));
			RefVar arglist(GetVariable(target, RSSYMarglist, nil, 0));
			ULong marshalled;
			err = MarshalArgumentSize(arglist, fTarget, &marshalled, fEncoding);
			fByteCount = marshalled;
			size = fByteCount;
			if (err == noErr && size < 1)
				err = kCommScriptErrNoData;
			if (err != noErr)
				goto keep;
		}
		else
		{
			size = fByteCount;
			if (size < 1)
				size = fDiscardAfter + 0x40;
		}
		if (fInputBuffer != nil)
		{
			if (fInputBufferSize < size)
			{
				UByte* p = (UByte*) ReallocPtr((Ptr) fInputBuffer, size);
				if (MemError() != noErr)
					err = kScriptEpNoMemory;
				fInputBufferSize = size;
				fInputBuffer = p;
			}
			goto keep;
		}
		UByte* p = (UByte*) NewPtr(size);
		if (MemError() != noErr)
			err = kScriptEpNoMemory;
		fInputBufferSize = size;
		fInputBuffer = p;
	}
	fReceived = 0;

keep:
	fPartialOffset = 0;
	fPartialPending = false;
	if (err == noErr && fReceived > 0)
	{
		long received = fReceived;
		fPartialPending = true;
		for (long i = 0; i != fReceived; )
		{
			i++;
			err = CheckForInput(i, false);
			if (err != noErr)
				return err;
			if (fReceived != received)
			{
				i = 0;
				received = fReceived;
			}
		}
	}
	return err;
}


// ROM 0x00135a94 PostReceive__24TNewScriptEndpointClientFv
// A receive posted (unless one is, or a completion is being handled): into
// the target binary in place, or into the buffer segment - up to the
// byteCount still wanted when optimizing a count with nothing else to
// look for, else a byte at a time.  The receive options go with the first.
NewtonErr
TNewScriptEndpointClient::PostReceive(void)
{
	TOptionArray* array = nil;
	if (fRcvPending || fInRcvComplete)
		return noErr;
	ULong flags = fRcvFlags;
	NewtonErr err = noErr;
	if (fRcvOptionsState == 0 && NOTNIL(fRcvOptions)
	&&  (err = PrepOptions(fRcvOptions, &array)) != noErr)
		;
	else if (fRawTarget)
	{
		if (ISNIL(fTarget) || !IsRawOrString(fTarget))
		{
			err = kCommScriptErrNoTarget;
			goto failed;
		}
		LockRef(fTarget);
		Size count = fByteCount;
		fRcvPending = true;
		err = fEndpoint->nRcv((UByte*) BinaryData(fTarget) + fTargetOffset, &count, fByteCount, &flags, fInputTimeout, false, array);
	}
	else
	{
		fRcvBuffer.Reset();
		Size thresh = 1;
		fOptimize = fOptimize && fByteCount > 0 && ISNIL(fEndSequence) && fPartialFrequency == 0;
		if (fOptimize)
		{
			thresh = fByteCount - fReceived;
			if (thresh >= 0x200)
				thresh = 0x200;
		}
		fRcvPending = true;
		err = fEndpoint->nRcv(&fRcvBuffer, thresh, &flags, fInputTimeout, false, array);
	}
	if (err == noErr)
		return noErr;
failed:
	fRcvPending = false;
	if (array != nil)
		delete array;
	return err;
}


// ROM 0x0013707c RcvComplete__24TNewScriptEndpointClientFP14TEndpointEvent
// A receive done: the target unlocked, the receive options read back, the
// bytes looked at, and the next receive posted; an error ends the input
// spec and goes to its completion (nil callback: the endpoint's handler).
void
TNewScriptEndpointClient::RcvComplete(TEndpointEvent* event)
{
	TRcvCompleteEvent* ev = (TRcvCompleteEvent*) event;
	fRcvPending = false;
	NewtonErr err = ev->fError;
	if (fRawTarget)
	{
		if (ISNIL(fTarget) || !IsRawOrString(fTarget))
			err = kCommScriptErrNoTarget;
		else
			UnlockRef(fTarget);
	}
	if (ISNIL(fInputSpec))
		err = kCommScriptErrNoInputSpec;
	TOptionArray* array = ev->fOptions;
	if (array != nil && err == noErr)
	{
		// (the ROM tests the Ref against nought, not nil)
		if ((Ref) fRcvOptions != 0)
		{
			err = ConvertFromOptionArray(fRcvOptions, array);
			fRcvOptionsState = 1;
		}
		delete array;
	}
	if (err == noErr)
	{
		fInRcvComplete = true;
		if (!fRawTarget)
			err = FilterRcvComplete(ev->fFlags);
		else
			err = RawRcvComplete(ev);
		fInRcvComplete = false;
	}
	if (NOTNIL(fInputSpec) && err == noErr)
		err = PostReceive();
	if (err != noErr)
	{
		RefVar callback(fInputSpec);
		ClearInputSpec();
		DoCompletion(err, callback, RefVar(MAKEINT(0)));
	}
}


// ROM 0x0013720c RawRcvComplete__24TNewScriptEndpointClientFP17TRcvCompleteEvent
// A receive into the target binary: inputScript(endpoint, the binary,
// {condition: 'byteCount or 'useEOP, byteCount}, rcvOptions).
NewtonErr
TNewScriptEndpointClient::RawRcvComplete(TRcvCompleteEvent* event)
{
	// ROM BUG (fixed): the condition is 'useEOP only when fUseEOP is set
	// and the packet is complete; otherwise a register left from the caller
	// is compared, which on the device is never 2 - so 'byteCount.  The
	// host answers that on both paths, which is also the fix: 'useEOP for
	// a packet that ended, 'byteCount for anything else.

	Boolean eop = fUseEOP && (event->fFlags & 1) == 0;
	RefVar inputScript(GetVariable(fInputSpec, RSSYMinputscript, nil, 0));
	if (NOTNIL(inputScript))
	{
		RefVar terminator(AllocateFrame());
		SetFrameSlot(terminator, RSSYMcondition, eop ? RSSYMuseeop : RSSYMbytecount);
		SetFrameSlot(terminator, RSSYMbytecount, RefVar(MAKEINT(event->fCount)));
		RefVar options;
		if (fRcvOptionsState == 1)
		{
			options = fRcvOptions;
			fRcvOptions = NILREF;
			fRcvOptionsState = 2;
		}
		RefVar args(MakeArray(4));
		SetArraySlotRef(args, 0, fEndpointRef);
		SetArraySlotRef(args, 1, fTarget);
		SetArraySlotRef(args, 2, terminator);
		SetArraySlotRef(args, 3, options);
		SendEndpointScript(fEndpointRef, fInputSpec, RSSYMinputscript, args);
	}
	return noErr;
}


// ROM 0x00137524 FilterRcvComplete__24TNewScriptEndpointClientFUl
// The bytes received put through the filter (a proxy byte replaced, or
// dropped for a nil proxy; seven bits kept) into the input buffer, the
// input checked after each.
NewtonErr
TNewScriptEndpointClient::FilterRcvComplete(ULong flags)
{
	NewtonErr err = noErr;
	long proxies = ISNIL(fProxyBytes) ? 0 : Length(fProxyBytes);
	for ( ; ; )
	{
		if (fInputBuffer == nil)
			return err;
		int c = fRcvBuffer.Get();
		if (c == -1)
			return err;
		Boolean keep = true;
		for (long i = 0; i < proxies; i++)
		{
			if ((RINT(GetArraySlotRef(fProxyBytes, i)) & 0xff) == c)
			{
				Ref proxy = GetArraySlotRef(fProxies, i);
				if (proxy == NILREF)
					keep = false;
				else
					c = RINT(proxy) & 0xff;
				break;
			}
		}
		if (!keep)
			continue;
		UByte b = (UByte) c;
		if (fSevenBit)
			b &= 0x7f;
		fPartialPending = true;
		fInputBuffer[fReceived++] = b;
		err = CheckForInput(fReceived, (flags & 1) == 0);
		if (err != noErr)
			return err;
	}
}


// ROM 0x00137698 CheckForInput__24TNewScriptEndpointClientFlUc
// Whether the first count bytes of input are complete: the byteCount
// reached (a 'frame's length word first read), an end sequence matched
// (condition 3 + its index), or the end of a packet with nothing more in
// the segment (condition 2).  A full buffer loses its first 0x40 bytes.
NewtonErr
TNewScriptEndpointClient::CheckForInput(long count, Boolean checkEOP)
{
	NewtonErr err = noErr;
	long condition;
	if (fByteCount > 0 && fByteCount == count)
	{
		if (fInputForm != kFormFrame || !fReadFrameLength)
			return PostInput(count, 1);
		err = GetFrameLength();
	}
	else
	{
		long match;
		if (ISNIL(fEndSequence) || (match = CheckEndArray(fInputBuffer + count - 1)) < 1)
		{
			Boolean eop = checkEOP && fUseEOP;
			if (eop && fRcvBuffer.Peek() == -1)
			{
				condition = 2;
				return PostInput(count, condition);
			}
		}
		else
		{
			condition = match + 2;
			if (condition != 0)
				return PostInput(count, condition);
		}
	}
	if (fInputBufferSize == count)
	{
		memmove(fInputBuffer, fInputBuffer + 0x40, fInputBufferSize - 0x40);
		fReceived -= 0x40;
		if (fPartialOffset > 0)
		{
			fPartialOffset -= 0x40;
			if (fPartialOffset < 0)
				fPartialOffset = 0;
		}
	}
	return err;
}


// ROM 0x00136fec GetFrameLength__24TNewScriptEndpointClientFv
// A 'frame's length word read off the front of the buffer: the byteCount
// becomes it (the buffer grown to fit).
NewtonErr
TNewScriptEndpointClient::GetFrameLength(void)
{
	NewtonErr err = noErr;
	// DEVIATION: the length word is the device's (big-endian)
	long length = (long) (int) GetBigEndianWord(fInputBuffer);
	fReceived -= 4;
	if (length < 1)
		fReadFrameLength = true;
	else
	{
		memmove(fInputBuffer, fInputBuffer + 4, fReceived);
		fReadFrameLength = false;
		if (fInputBufferSize < length)
		{
			UByte* p = (UByte*) ReallocPtr((Ptr) fInputBuffer, length);
			if (MemError() != noErr)
				err = kScriptEpNoMemory;
			fInputBufferSize = length;
			fInputBuffer = p;
		}
		fByteCount = length;
	}
	return err;
}


// ROM 0x00137a40 CheckEndArray__24TNewScriptEndpointClientFPUc
// Which end sequence (1 on) the input ending at `last` ends with; 0 for none.
long
TNewScriptEndpointClient::CheckEndArray(UByte* last)
{
	long count = Length(fEndSequence);
	Boolean found = false;
	long i = 0;
	while (i < count && !found)
	{
		Ref element = GetArraySlotRef(fEndSequence, i);
		if (ISINT(element))
			found = RINT(element) == *last;
		else
		{
			long n = Length(element) - 1;
			if (n <= last - fInputBuffer)
			{
				found = true;
				const UByte* bytes = (const UByte*) BinaryData(element);
				for (long j = n; j >= 0 && found; j--)
					found = bytes[j] == last[j - n];
			}
		}
		i++;
	}
	return found ? i : 0;
}


// ROM 0x00137b64 DoInput__24TNewScriptEndpointClientFv
// The input so far handed over as if complete (condition none).
NewtonErr
TNewScriptEndpointClient::DoInput(void)
{
	if (ISNIL(fInputSpec))
		return kCommScriptErrNoInputSpec;
	return PostInput(fReceived, 0);
}


// ROM 0x00137b88 PostInput__24TNewScriptEndpointClientFlT1
// The first count bytes of input (the last discardAfter of them) converted
// by the form and sent to inputScript(endpoint, data, terminator,
// rcvOptions) - the terminator {byteCount, condition: 'byteCount,
// 'useEOP or 'endSequence with its index}, nil for condition 0 - and taken
// off the buffer.
NewtonErr
TNewScriptEndpointClient::PostInput(long count, long condition)
{
	long err = noErr;
	RefVar inputScript(GetVariable(fInputSpec, RSSYMinputscript, nil, 0));
	Boolean hasScript = NOTNIL(inputScript);
	RefVar data;
	long n = 0;
	if (count > 0)
	{
		n = count;
		if (fDiscardAfter < count)
			n = fDiscardAfter;
		if (hasScript)
			data = ParseInput(fInputForm, fEncoding, n, fInputBuffer + count - n, fTarget, &err);
		if (count < fReceived)
			memmove(fInputBuffer, fInputBuffer + count, fReceived - count);
		fReceived -= count;
		if (fInputForm == kFormFrame)
		{
			fReadFrameLength = true;
			fByteCount = 4;
		}
	}
	fPartialOffset = 0;
	fPartialPending = false;
	if (ISNIL(data) || !hasScript || err != noErr)
		return err;

	RefVar terminator;
	if (condition != 0)
	{
		terminator = AllocateFrame();
		SetFrameSlot(terminator, RSSYMbytecount, RefVar(MAKEINT(n)));
		if (condition == 1)
			SetFrameSlot(terminator, RSSYMcondition, RSSYMbytecount);
		else if (condition == 2)
			SetFrameSlot(terminator, RSSYMcondition, RSSYMuseeop);
		else
		{
			SetFrameSlot(terminator, RSSYMcondition, RSSYMendsequence);
			SetFrameSlot(terminator, RSSYMindex, RefVar(MAKEINT(condition - 3)));
		}
	}
	RefVar options;
	if (fRcvOptionsState == 1)
	{
		options = fRcvOptions;
		fRcvOptions = NILREF;
		fRcvOptionsState = 2;
	}
	RefVar args(MakeArray(4));
	SetArraySlotRef(args, 0, fEndpointRef);
	SetArraySlotRef(args, 1, data);
	SetArraySlotRef(args, 2, terminator);
	SetArraySlotRef(args, 3, options);
	RefVar inputSpec(fInputSpec);
	SendEndpointScript(fEndpointRef, inputSpec, RSSYMinputscript, args);
	return err;
}


// ROM 0x00137fd8 IdleProc__24TNewScriptEndpointClientFP10TUMsgTokenPUlP7TAEvent
// The partial idler: when input has come since the last time,
// partialScript(endpoint, what has come) - an error ending the input spec -
// then the idler set again.
void
TNewScriptEndpointClient::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	long err = noErr;
	if (fPartialPending && NOTNIL(fInputSpec))
	{
		RefVar partialScript(GetVariable(fInputSpec, RSSYMpartialscript, nil, 0));
		if (NOTNIL(partialScript))
		{
			RefVar data(GetPartialData(&err));
			if (NOTNIL(data) && err == noErr)
			{
				RefVar args(MakeArray(2));
				SetArraySlotRef(args, 0, fEndpointRef);
				SetArraySlotRef(args, 1, data);
				SendEndpointScript(fEndpointRef, fInputSpec, RSSYMpartialscript, args, true);
			}
		}
		fPartialPending = false;
		if (err != noErr)
		{
			RefVar callback(fInputSpec);
			ClearInputSpec();
			DoCompletion(err, callback, RefVar(MAKEINT(0)));
			return;
		}
	}
	if (fPartialFrequency != 0 && NOTNIL(fInputSpec))
		ResetIdle(fPartialFrequency, kMilliseconds);
}


// ROM 0x001382dc DoPartial__24TNewScriptEndpointClientFPl
// What has come so far (a 'string or 'bytes input only).
Ref
TNewScriptEndpointClient::DoPartial(long* error)
{
	*error = kCommScriptErrPartialForm;
	if (fInputForm == kFormString || fInputForm == kFormBytes)
		*error = noErr;
	if (ISNIL(fInputSpec))
		*error = kCommScriptErrNoInputSpec;
	else if (*error == noErr)
	{
		*error = noErr;
		RefVar data;
		if (fReceived > 0)
		{
			data = ParseInput(fInputForm, fEncoding, fReceived, fInputBuffer, fTarget, error);
			fPartialOffset = fReceived;
			fPartialPending = false;
		}
		return data;
	}
	return NILREF;
}


// ROM 0x0013832c GetPartialData__24TNewScriptEndpointClientFPl
Ref
TNewScriptEndpointClient::GetPartialData(long* error)
{
	*error = noErr;
	RefVar data;
	if (fReceived > 0)
	{
		data = ParseInput(fInputForm, fEncoding, fReceived, fInputBuffer, fTarget, error);
		fPartialOffset = fReceived;
		fPartialPending = false;
	}
	return data;
}


// ROM 0x001383ac ParseInput__24TNewScriptEndpointClientF8FormTypelT2PUcRC6RefVarPl
// Bytes as a value of the form, through PScriptDataIn (a 'frame through
// PUnFlattenPtr).
Ref
TNewScriptEndpointClient::ParseInput(FormType form, long encoding, long length, UByte* data, RefArg target, long* error)
{
	RefVar result;
	*error = noErr;
	if (form == kFormFrame)
	{
		if (fUnflattenIn == nil)
		{
			PFrameSource* xlator = (PFrameSource*) NewByName("PFrameSource", "PUnFlattenPtr");
			if (xlator == nil)
			{
				*error = MemError();
				if (*error == noErr)
					*error = kScriptEpNoMemory;		// (host: the registry does not set one)
			}
			else
				fUnflattenIn = xlator;
			if (*error != noErr)
				return result;
		}
		newton_try
		{
			UnflattenPtrParms parms;
			parms.fData = data;
			parms.fLength = length;
			parms.fStore = NILREF;
			result = fUnflattenIn->Translate(&parms, nil);
		}
		newton_catch(exTranslatorException)
		{
			*error = (long) (Long) CurrentException()->data;
		}
		end_try;
	}
	else
	{
		newton_try
		{
			FrameSourceParms parms;
			parms.fData = data;
			parms.fLength = length;
			parms.fForm = form;
			parms.fEncoding = encoding;
			parms.fTemplate = target;
			result = GetScriptDataInXlator()->Translate(&parms, nil);
		}
		newton_catch(exTranslatorException)
		{
			*error = (long) (Long) CurrentException()->data;
		}
		end_try;
	}
	return result;
}


// ROM 0x001385a0 DoFlushInput__24TNewScriptEndpointClientFv
void
TNewScriptEndpointClient::DoFlushInput(void)
{
	fReceived = 0;
	fPartialOffset = 0;
	fPartialPending = false;
}


// ROM 0x001385b4 DoFlushPartial__24TNewScriptEndpointClientFv
// What a Partial has seen taken off the buffer.
void
TNewScriptEndpointClient::DoFlushPartial(void)
{
	long remaining = fReceived - fPartialOffset;
	if (remaining > 0)
		memmove(fInputBuffer, fInputBuffer + fPartialOffset, remaining);
	fPartialOffset = 0;
	fReceived = remaining;
	fPartialPending = false;
}


/* -------------------------------------------------------------------------------
	The natives: protoBasicEndpoint's methods
------------------------------------------------------------------------------- */

static TNewScriptEndpointClient*
ScriptClient(RefArg rcvr)
{
	return (TNewScriptEndpointClient*) EndpointClient(rcvr);
}

static void
ThrowComm(NewtonErr err)
{
	Throw(kExComm, (void*) (Long) err, nil);
}


// ROM 0x001385fc CINewInstantiate
// endpoint:Instantiate(endpoint, options) ==> the options, read back
static Ref
CINewInstantiate(RefArg rcvr, RefArg endpoint, RefArg options)
{
	TNewScriptEndpointClient* client = new TNewScriptEndpointClient;
	RefVar clone(CloneOptions(options));
	NewtonErr err;
	if (client == nil)
		err = kScriptEpNoMemory;
	else if ((err = client->InitScriptEndpointClient(endpoint, clone, nil)) == noErr)
		return clone;
	ThrowComm(err);
	return clone;
}


// ROM 0x00138844 CINewInstantiateFromEndpoint
static Ref
CINewInstantiateFromEndpoint(RefArg rcvr, RefArg endpoint, RefArg options, RefArg tEndpoint)
{
	TNewScriptEndpointClient* client = new TNewScriptEndpointClient;
	RefVar clone(CloneOptions(options));
	NewtonErr err;
	if (client == nil)
		err = kScriptEpNoMemory;
	else if ((err = client->InitScriptEndpointClient(endpoint, clone, (TEndpoint*) RefToAddress(tEndpoint))) == noErr)
		return clone;
	ThrowComm(err);
	return clone;
}


// ROM 0x001388e0 CINewDispose
static Ref
CINewDispose(RefArg rcvr)
{
	TEndpointClient* client = EndpointClient(rcvr);
	if (client != nil)
		delete client;
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar());
	return NILREF;
}


// ROM 0x00138940 CINewDisposeLeavingTool
static Ref
CINewDisposeLeavingTool(RefArg rcvr)
{
	TEndpointClient* client = EndpointClient(rcvr);
	if (client != nil)
	{
		NewtonErr err = ((TSerialEndpoint*) client->fEndpoint)->DeleteLeavingTool();
		if (err != noErr)
		{
			ThrowComm(err);
			return NILREF;
		}
		client->fEndpoint = nil;
		delete client;
	}
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar());
	return NILREF;
}


// ROM 0x001389dc CINewDisposeLeavingTEndpoint
// ==> the endpoint (as an address), aborted and let go of by the client
static Ref
CINewDisposeLeavingTEndpoint(RefArg rcvr)
{
	RefVar result;
	TEndpointClient* client = EndpointClient(rcvr);
	if (client != nil)
	{
		TEndpoint* ep = client->fEndpoint;
		result = AddressToRef(ep);
		ep->SetClientHandler(0);
		ep->Abort();
		client->fEndpoint = nil;
		delete client;
	}
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar());
	return result;
}


// The requests that take options and a callback, and answer the options.
#define OPTIONS_NATIVE(name, method)								\
static Ref															\
name(RefArg rcvr, RefArg options, RefArg callback)					\
{																	\
	TNewScriptEndpointClient* client = ScriptClient(rcvr);			\
	RefVar clone(CloneOptions(options));							\
	NewtonErr err;													\
	if (client == nil)												\
		err = kCommScriptErrNotInstantiated;						\
	else if ((err = client->method(clone, callback)) == noErr)		\
		return clone;												\
	ThrowComm(err);													\
	return clone;													\
}

// ROM 0x00138a88 CINewOption
OPTIONS_NATIVE(CINewOption, DoOption)
// ROM 0x00138ba4 CINewBind
OPTIONS_NATIVE(CINewBind, DoBind)
// ROM 0x00138c28 CINewConnect
OPTIONS_NATIVE(CINewConnect, DoConnect)
// ROM 0x00138cac CINewListen
OPTIONS_NATIVE(CINewListen, DoListen)
// ROM 0x00138e30 CINewAccept
OPTIONS_NATIVE(CINewAccept, DoAccept)


// ROM 0x00138b0c CINewState
static Ref
CINewState(RefArg rcvr)
{
	TEndpointClient* client = EndpointClient(rcvr);
	if (client != nil)
		return MAKEINT(client->fEndpoint->GetState());
	return NILREF;
}


// ROM 0x00138b30 CINewSetState
static Ref
CINewSetState(RefArg rcvr, RefArg state)
{
	TEndpointClient* client = EndpointClient(rcvr);
	if (client != nil)
	{
		NewtonErr err = ((TSerialEndpoint*) client->fEndpoint)->SetState(RINT(state));
		if (err != noErr)
			ThrowComm(err);
	}
	return NILREF;
}


// ROM 0x00138eb4 CINewDisconnect
static Ref
CINewDisconnect(RefArg rcvr, RefArg cancelPending, RefArg callback)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	NewtonErr err = (client == nil) ? kCommScriptErrNotInstantiated : client->DoDisconnect(cancelPending, callback);
	if (err != noErr)
		ThrowComm(err);
	return NILREF;
}


// ROM 0x00138f10 CINewUnBind
static Ref
CINewUnBind(RefArg rcvr, RefArg callback)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	NewtonErr err = (client == nil) ? kCommScriptErrNotInstantiated : client->DoUnBind(callback);
	if (err != noErr)
		ThrowComm(err);
	return NILREF;
}


// ROM 0x00138f64 CINewAbort
static Ref
CINewAbort(RefArg rcvr, RefArg callback)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	NewtonErr err = (client == nil) ? kCommScriptErrNotInstantiated : client->DoAbort(callback);
	if (err != noErr)
		ThrowComm(err);
	return NILREF;
}


// ROM 0x00138fb8 CINewOutput
// endpoint:Output(data, options, outputSpec) ==> the options, read back
static Ref
CINewOutput(RefArg rcvr, RefArg data, RefArg options, RefArg outputSpec)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	RefVar clone(CloneOptions(options));
	NewtonErr err;
	if (client == nil)
		err = kCommScriptErrNotInstantiated;
	else if ((err = client->DoOutput(data, clone, outputSpec)) == noErr)
		return clone;
	ThrowComm(err);
	return clone;
}


// ROM 0x00139044 CINewSetInputSpec
static Ref
CINewSetInputSpec(RefArg rcvr, RefArg inputSpec)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	NewtonErr err = (client == nil) ? kCommScriptErrNotInstantiated : client->DoInputSpec(inputSpec);
	if (err != noErr)
		ThrowComm(err);
	return NILREF;
}


// ROM 0x00139098 CINewInput
static Ref
CINewInput(RefArg rcvr)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	NewtonErr err = (client == nil) ? kCommScriptErrNotInstantiated : client->DoInput();
	if (err != noErr)
		ThrowComm(err);
	return NILREF;
}


// ROM 0x001390e4 CINewPartial
static Ref
CINewPartial(RefArg rcvr)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	long err;
	RefVar result;
	if (client == nil)
		err = kCommScriptErrNotInstantiated;
	else
	{
		result = client->DoPartial(&err);
		if (err == noErr)
			return result;
	}
	ThrowComm(err);
	return NILREF;
}


// ROM 0x00139160 CINewFlushPartial
static Ref
CINewFlushPartial(RefArg rcvr)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		ThrowComm(kCommScriptErrNotInstantiated);
	else
		client->DoFlushPartial();
	return NILREF;
}


// ROM 0x001391a4 CINewFlushInput
static Ref
CINewFlushInput(RefArg rcvr)
{
	TNewScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		ThrowComm(kCommScriptErrNotInstantiated);
	else
		client->DoFlushInput();
	return NILREF;
}


// ROM 0x0013925c CIRequestsPending
// endpoint:RequestsPending('sync, 'async or anything else for either)
static Ref
CIRequestsPending(RefArg rcvr, RefArg which)
{
	TEndpointClient* client = EndpointClient(rcvr);
	if (client == nil)
	{
		ThrowComm(kCommScriptErrNotInstantiated);
		return NILREF;
	}
	ULong kind;
	if (EQRef(which, RSSYMsync))
		kind = kSyncCall;
	else if (EQRef(which, RSSYMasync))
		kind = kAsyncCall;
	else
		kind = kEitherCall;
	return client->fEndpoint->IsPending(kind) ? TRUEREF : NILREF;
}


// The natives bound under the names they have in the ROM's native table.
void
RegisterCommsNatives(void)
{
	RegisterStreamingEndpointNatives();
	RegisterNativeFunction("CINewInstantiate", (void*) CINewInstantiate, 2);
	RegisterNativeFunction("CINewInstantiateFromEndpoint", (void*) CINewInstantiateFromEndpoint, 3);
	RegisterNativeFunction("CINewDispose", (void*) CINewDispose, 0);
	RegisterNativeFunction("CINewDisposeLeavingTool", (void*) CINewDisposeLeavingTool, 0);
	RegisterNativeFunction("CINewDisposeLeavingTEndpoint", (void*) CINewDisposeLeavingTEndpoint, 0);
	RegisterNativeFunction("CINewOption", (void*) CINewOption, 2);
	RegisterNativeFunction("CINewState", (void*) CINewState, 0);
	RegisterNativeFunction("CINewSetState", (void*) CINewSetState, 1);
	RegisterNativeFunction("CINewBind", (void*) CINewBind, 2);
	RegisterNativeFunction("CINewConnect", (void*) CINewConnect, 2);
	RegisterNativeFunction("CINewListen", (void*) CINewListen, 2);
	RegisterNativeFunction("CINewAccept", (void*) CINewAccept, 2);
	RegisterNativeFunction("CINewDisconnect", (void*) CINewDisconnect, 2);
	RegisterNativeFunction("CINewUnBind", (void*) CINewUnBind, 1);
	RegisterNativeFunction("CINewAbort", (void*) CINewAbort, 1);
	RegisterNativeFunction("CINewOutput", (void*) CINewOutput, 3);
	RegisterNativeFunction("CINewSetInputSpec", (void*) CINewSetInputSpec, 1);
	RegisterNativeFunction("CINewInput", (void*) CINewInput, 0);
	RegisterNativeFunction("CINewPartial", (void*) CINewPartial, 0);
	RegisterNativeFunction("CINewFlushPartial", (void*) CINewFlushPartial, 0);
	RegisterNativeFunction("CINewFlushInput", (void*) CINewFlushInput, 0);
	RegisterNativeFunction("CIRequestsPending", (void*) CIRequestsPending, 1);
}
