/*
	File:		comms/NewScriptEndpoint.h

	Contains:	The NewtonScript endpoint: TNewScriptEndpointClient, the C++
				side of protoBasicEndpoint, and the CINew... natives that
				are protoBasicEndpoint's methods (Instantiate, Bind,
				Connect, Output, SetInputSpec, ...).

				A script's endpoint frame keeps the client in its ciPrivate
				slot.  Every request takes an options frame (or array of
				them), turned into a TOptionArray by the POptionDataOut
				translator and read back into the frames by POptionDataIn
				when the request is done, and a callback spec frame
				({async: ..., reqTimeout: ..., completionScript: ...}).
				Without `async` the call is synchronous (the endpoint's
				fork waits) and answers the options; with it the options
				and the callback are queued (fRequests, a Newton array of
				[options, callback] pairs, or just [callback] for an
				unbind/disconnect) and the endpoint's completion event
				sends the callback its completionScript(endpoint,
				options, result).  An error with no completionScript goes
				to the endpoint's exceptionHandler, or is thrown.

				Output converts the data by its form (PScriptDataOut, or
				the flattener for a 'frame) and sends it;
				SetInputSpec describes what a script wants to receive -
				the form, a termination (byteCount, endSequence, useEOP),
				a filter (byte substitutions, seven-bit), a target (a
				binary received into in place, or a template's typelist),
				receive options, a partialScript and its frequency - and a
				receive is kept outstanding into the client's buffer
				segment; bytes that come are moved into the input buffer
				(filtered) and CheckForInput decides whether the input is
				complete, whereupon the inputScript(endpoint, data,
				terminator, options) is sent.

				The offsets in the comments are the ROM's (the object is
				0xd4 bytes there); the host's are wider for the pointers.

	Reconstructed from the MP2x00 US ROM (0x00133c84-0x0013930c); each
	function cites its origin.  The streaming endpoint (protoStreamingEndpoint)
	is StreamingEndpoint.h's; a configuration asking for the modem service
	goes through the modem navigator first (ModemNavigator.h), as the ROM's
	does.  docs/comms/README.md.
*/

#ifndef __COMMS_NEWSCRIPTENDPOINT_H
#define __COMMS_NEWSCRIPTENDPOINT_H

#ifndef __COMMS_ENDPOINT_H
#include "Endpoint.h"
#endif
#ifndef __COMMS_TRANSLATORS_H
#include "Translators.h"
#endif
#ifndef __BUFFERSEGMENT_H
#include "BufferSegment.h"
#endif

// the script endpoint's errors (the translators' are in Translators.h)
#define kCommScriptErrNoInputSpec		(-54000)	// input asked for with no input spec
#define kCommScriptErrStreamSpec		(-54003)	// StreamIn with no spec
#define kCommScriptErrBadEndSequence	(-54005)	// an end sequence element that is not a byte, character, string or binary
#define kCommScriptErrPartialForm		(-54006)	// a partial for a form that is not 'string or 'bytes
#define kCommScriptErrTerminationForm	(-54007)	// a termination for a form that takes none
#define kCommScriptErrBadRange			(-54008)	// an output target past the data; a target for a form that takes none
#define kCommScriptErrFilterForm		(-54009)	// a filter on a 'binary input
#define kCommScriptErrNoTarget			(-54010)	// a 'binary input with no target data
#define kCommScriptErrRcvPending		(-54012)	// an input spec changed while a receive completes
#define kCommScriptErrBadProxy			(-54013)
#define kCommScriptErrNotInstantiated	(-54014)	// no client; no endpoint frame
#define kCommScriptErrBadCharacter		(-54016)	// a proxy character that is more than a byte
#define kCommScriptErrNoOptions			(-26004)	// Instantiate with no options

class TNewScriptEndpointClient : public TEndpointClient
{
public:
						TNewScriptEndpointClient();
	virtual				~TNewScriptEndpointClient();

	NewtonErr			InitScriptEndpointClient(RefArg endpoint, RefArg options, TEndpoint* ep);

	// the events
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		Default(TEndpointEvent* event);
	virtual void		SndComplete(TEndpointEvent* event);
	virtual void		RcvComplete(TEndpointEvent* event);
	virtual void		OptMgmtComplete(TEndpointEvent* event);
	virtual void		ListenComplete(TEndpointEvent* event);
	virtual void		ConnectComplete(TEndpointEvent* event);
	virtual void		AcceptComplete(TEndpointEvent* event);
	virtual void		ReleaseComplete(TEndpointEvent* event);
	virtual void		DisconnectComplete(TEndpointEvent* event);
	virtual void		BindComplete(TEndpointEvent* event);
	virtual void		UnBindComplete(TEndpointEvent* event);
	virtual void		AbortComplete(TEndpointEvent* event);

	// the requests
	Ref					DoState(void);
	NewtonErr			DoBind(RefArg options, RefArg callback);
	NewtonErr			DoConnect(RefArg options, RefArg callback);
	NewtonErr			DoListen(RefArg options, RefArg callback);
	NewtonErr			DoAccept(RefArg options, RefArg callback);
	NewtonErr			DoDisconnect(RefArg cancelPending, RefArg callback);
	NewtonErr			DoUnBind(RefArg callback);
	NewtonErr			DoOption(RefArg options, RefArg callback);
	NewtonErr			DoAbort(RefArg callback);
	NewtonErr			DoOutput(RefArg data, RefArg options, RefArg outputSpec);
	NewtonErr			DoInputSpec(RefArg inputSpec);
	NewtonErr			DoInput(void);
	Ref					DoPartial(long* error);
	void				DoFlushInput(void);
	void				DoFlushPartial(void);

	static Boolean		GetParms(RefArg callback, ULong* timeout);		// ==> synchronous

protected:
	NewtonErr			ConvertToOptionArray(RefArg options, TOptionArray* array);
	NewtonErr			ConvertFromOptionArray(RefArg options, TOptionArray* array);
	NewtonErr			PrepOptions(RefArg options, TOptionArray** array);
	PFrameSource*		GetScriptDataInXlator(void);
	PFrameSink*			GetScriptDataOutXlator(void);

	void				QueueCallback(RefArg callback);
	void				QueueOptions(RefArg options, RefArg callback);
	void				UnwindCallback(void);
	void				UnwindOptions(void);
	void				CommandComplete(long result, RefArg queue, RefArg options);
	void				OptionCommandComplete(long result, RefArg queue, TOptionArray* array);
	void				DoCompletion(long result, RefArg callback, RefArg options);

	NewtonErr			OutputData(RefArg data, FormType form, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options);
	NewtonErr			OutputRaw(RefArg data, RefArg outputSpec, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options);
	NewtonErr			OutputFrame(RefArg data, Boolean sync, ULong flags, TTimeout timeout, TOptionArray* options);

	NewtonErr			PostReceive(void);
	void				ClearInputSpec(void);
	NewtonErr			ReadInputSlots(void);
	NewtonErr			ReadTermination(RefArg termination);
	NewtonErr			ReadTarget(RefArg target);
	NewtonErr			ReadFilter(RefArg filter);
	NewtonErr			AddProxyFrame(RefArg proxy);
	NewtonErr			AddProxyArrayElement(RefArg element, RefArg array);
	NewtonErr			AddEndArrayElement(RefArg element);
	NewtonErr			InitInputBuffers(void);
	NewtonErr			GetFrameLength(void);
	NewtonErr			RawRcvComplete(TRcvCompleteEvent* event);
	NewtonErr			FilterRcvComplete(ULong flags);
	NewtonErr			CheckForInput(long count, Boolean checkEOP);
	long				CheckEndArray(UByte* last);
	NewtonErr			PostInput(long count, long condition);
	Ref					GetPartialData(long* error);
	Ref					ParseInput(FormType form, long encoding, long length, UByte* data, RefArg target, long* error);

public:
	RefStruct			fEndpointRef;		// +0x18  the script's endpoint frame
	long				fEncoding;			// +0x1c  the text encoding (1, Mac Roman, unless the endpoint says)
	ULong				fRcvFlags;			// +0x20  the input spec's rcvFlags
	Boolean				fUseEOP;			// +0x24  a receive ends at the end of a packet
	RefStruct			fOutputs;			// +0x28  [binary or nil, options, callback] per asynchronous output
	RefStruct			fRequests;			// +0x2c  [options,] callback per asynchronous request
	RefStruct			fAborts;			// +0x30  the callbacks of asynchronous aborts
	RefStruct			fInputSpec;			// +0x34
	long				fByteCount;			// +0x38  the termination's byteCount (0: none)
	long				fDiscardAfter;		// +0x3c  how much of the input is kept
	RefStruct			fEndSequence;		// +0x40  an array of end sequences: characters and binaries
	Boolean				fReadFrameLength;	// +0x44  a 'frame's length word is what comes next
	TTimeout			fInputTimeout;		// +0x48  the input spec's reqTimeout
	ULong				fPartialFrequency;	// +0x4c  milliseconds between partialScripts (0: none)
	RefStruct			fProxyBytes;		// +0x50  the filter's bytes...
	RefStruct			fProxies;			// +0x54  ...and what each becomes (nil: dropped)
	Boolean				fSevenBit;			// +0x58
	RefStruct			fRcvOptions;		// +0x5c  the input spec's rcvOptions (a clone)
	long				fRcvOptionsState;	// +0x60  0 sent with the next receive, 1 read back, 2 none
	Boolean				fRawTarget;			// +0x64  receiving into the target binary in place
	RefStruct			fTarget;			// +0x68  the target binary, or a template's typelist
	long				fTargetOffset;		// +0x6c
	Boolean				fOptimize;			// +0x70
	CBufferSegment		fRcvBuffer;			// +0x74  what a receive fills
	UByte*				fInputBuffer;		// +0x9c  what has come, filtered
	long				fInputBufferSize;	// +0xa0
	long				fReceived;			// +0xa4  how much of it
	Boolean				fPartialPending;	// +0xa8  new input since the last partialScript
	long				fPartialOffset;		// +0xac  how much a Partial has seen
	Boolean				fRcvPending;		// +0xb0  a receive is outstanding
	Boolean				fInRcvComplete;		// +0xb1
	FormType			fInputForm;			// +0xb4
	PFrameSink*			fOptionsOut;		// +0xbc  POptionDataOut
	PFrameSource*		fOptionsIn;			// +0xc0  POptionDataIn
	PFrameSink*			fDataOut;			// +0xc4  PScriptDataOut
	PFrameSource*		fDataIn;			// +0xc8  PScriptDataIn
	PFrameSink*			fFlattenOut;		// +0xcc  PFlattenPtr
	PFrameSource*		fUnflattenIn;		// +0xd0  PUnFlattenPtr
	// host (DEVIATION): an asynchronous bind's options, for BindComplete
	// to read back (the fix of the ROM bug described there)
	TOptionArray*		fBindOptions;
};


Boolean		IsRaw(RefArg obj);
Boolean		IsRawOrString(RefArg obj);
Ref			CloneOptions(RefArg options);
UByte		CharacterToUByte(UniChar c, long encoding);
TEndpoint*	GetClientEndpoint(RefArg endpoint);

void		RegisterCommsNatives(void);

#endif
