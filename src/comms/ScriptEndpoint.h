/*
	File:		comms/ScriptEndpoint.h

	Contains:	The Newton 1.x NewtonScript endpoint: TScriptEndpointClient,
				the C++ side of protoEndpoint (the ROM's @ protoEndpoint,
				which older packages use), and the CI... natives that are
				its methods (Instantiate, xconnect, Listen, output,
				SetInputSpec, input, Dispose, the just... calls, ...).

				A script's endpoint frame keeps the client in its ciPrivate
				slot.  The options are the 1.x option frames - {label,
				type ('service, 'option, 'config, 'address), opCode,
				data} - with the data an integer (a word), a string, an
				array of bytes or a binary; five labels are made into their
				classes from named slots instead ('rout the address
				{addressType, addressData}, 'siop {bps, parity, dataBits,
				stopBits}, 'iflc/'oflc {xonChar, xoffChar,
				useSoftFlowControl, useHardFlowControl}, 'mdo  the
				dialing, comms/ModemOptions.h), and a 'service option
				other than 'sid  names its service by its label.

				Connect and Listen are synchronous (the endpoint made
				synchronous for the Bind and the Connect or the Listen and
				Accept); afterwards the endpoint is asynchronous, the
				client keeping a receive outstanding into a 0x200-byte
				buffer segment while there is an input spec, and moving
				what comes into its input buffer.  An input spec says the
				form ('string, 'frame - NSOF after a length word - 'raw,
				or an array of bytes), what ends the input (byteCount,
				endCharacter, the end of a packet with recvFlags 2), how
				much to keep (discardAfter), a partialScript and its
				frequency, a nullProxy for the zero byte and sevenBit; the
				inputScript is sent endpoint:inputScript(endpoint, data)
				and then the endpoint's nextInputSpec is set.  output
				sends asynchronously with no more than 1024 bytes
				outstanding, waiting (Yield, over a TPseudoSyncState) for
				room; an error from a completion goes to the endpoint's
				exceptionHandler as evt.ex.comm, or is thrown.

				The offsets in the comments are the ROM's (the object is
				0x94 bytes there); the host's are wider for the pointers.

	Reconstructed from the MP2x00 US ROM (0x00067440-0x0006b5c0); each
	function cites its origin.  NOT YET: the CCL modem scripts that
	startCCL/stopCCL wait for.
*/

#ifndef __COMMS_SCRIPTENDPOINT_H
#define __COMMS_SCRIPTENDPOINT_H

#ifndef __COMMS_ENDPOINT_H
#include "Endpoint.h"
#endif
#ifndef __BUFFERSEGMENT_H
#include "BufferSegment.h"
#endif
#include "PseudoSyncState.h"
#include "objects.h"

class TCMOModemDialing;

class TScriptEndpointClient : public TEndpointClient
{
public:
						TScriptEndpointClient();
	virtual				~TScriptEndpointClient();

	NewtonErr			InitScriptEndpointClient(RefArg endpoint, RefArg options, TEndpoint* ep);

	// the events
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		Default(TEndpointEvent* event);
	virtual void		SndComplete(TEndpointEvent* event);
	virtual void		RcvComplete(TEndpointEvent* event);
	virtual void		OptMgmtComplete(TEndpointEvent* event);

	// the options
	NewtonErr			DoSetOptions(RefArg options);
	Ref					DoGetOptions(RefArg options);
	Ref					DoGetOption(RefArg option);
	Ref					ConvertFromOptionArray(TOptionArray* array);
	NewtonErr			ConvertToOptionArray(RefArg options, TOptionArray* array);
	TOption*			ConvertToAddressParms(RefArg option);
	TOption*			ConvertToSerialIOParms(RefArg option);
	TOption*			ConvertToFlowControlParms(RefArg option, Boolean input);
	TCMOModemDialing*	ConvertToModemDialingOption(RefArg option);
	TOption*			ConvertToOption(RefArg option);
	Ref					ConvertFromOption(TOption* option);

	// the requests
	Ref					DoState(void);
	NewtonErr			DoConnect(RefArg address, RefArg options);
	NewtonErr			DoListen(RefArg options);
	Ref					DoCaller(void);
	NewtonErr			DoAccept(void);
	NewtonErr			DoReject(void);
	NewtonErr			DoRelease(void);
	NewtonErr			DoDisconnect(void);
	NewtonErr			DoAbort(void);

	// output
	NewtonErr			DoOutput(RefArg data, RefArg flags);
	long				DoOutputOne(RefArg data, UByte* buffer);
	NewtonErr			DoFlushOutput(void);
	NewtonErr			DoOutputFrame(RefArg data, RefArg flags);
	Ref					DoReadyForOutput(RefArg data);
	Ref					DoOutputDone(void);

	// input
	void				DoInputSpec(RefArg inputSpec);
	void				SetInputSpec(RefArg inputSpec);
	void				DoFlushPartial(void);
	void				DoFlushInput(void);
	void				CheckForInput(void);
	void				PostInput(void);
	Ref					DoInput(void);
	Ref					ConvertBlock(UByte* data, long length);
	Ref					DoInputAvailable(void);
	Ref					DoPartial(void);
	Ref					DoBytesAvailable(void);

	void				Yield(void);
	void				StopYielding(void);
	long				TranslateError(long error);
	void				DoException(long error);

	Boolean				fYielding;				// +0x18
	TPseudoSyncState	fYieldState;			// +0x1c  what Yield waits on
	RefStruct			fEndpointRef;			// +0x24  the script's endpoint frame
	long				fOutputRoom;			// +0x28  of the 1024 bytes that may be outstanding
	long				fEncoding;				// +0x2c  the text encoding (1, Mac Roman, unless the endpoint says)
	ULong				fRecvFlags;				// +0x30  the input spec's recvFlags
	RefStruct			fInputSpec;				// +0x34
	long				fByteCount;				// +0x38  the input ends after this many bytes (0: not by count)
	long				fDiscardAfter;			// +0x3c  the most input kept
	Boolean				fUseEndChar;			// +0x40
	UByte				fEndChar;				// +0x41
	Boolean				fFormString;			// +0x42
	Boolean				fFormFrame;				// +0x43
	Boolean				fFormRaw;				// +0x44
	Boolean				fFrameNeedsLength;		// +0x45  a 'frame input still waits for its length word
	CBufferSegment		fRcvBuffer;				// +0x48  what the outstanding receive fills
	UByte*				fInput;					// +0x70  the input buffer
	long				fInputSize;				// +0x74
	long				fInputCount;			// +0x78
	long				fInputEnd;				// +0x7c  the length of a complete input (0: none yet)
	ULong				fPartialFrequency;		// +0x80  milliseconds
	long				fPartialConsumed;		// +0x84  what the last partial took
	Boolean				fPartialPending;		// +0x88  there is new input for the partialScript
	Boolean				fRcvPending;			// +0x89
	Boolean				fInRcvComplete;			// +0x8a
	long				fNullProxy;				// +0x8c  what a zero byte becomes (0: itself)
	Boolean				fSevenBit;				// +0x90
};

// ROM 0x00067dec ContainsModemService__FP12TOptionArray - a 'mods service
// named and no 'mpro option: the modem navigator is to be run
Boolean		ContainsModemService(TOptionArray* options);
// ROM 0x0006a374 ConvertToServiceOption__FUl - a 'sid ' option naming it
TOption*	ConvertToServiceOption(ULong serviceId);

void		RegisterScriptEndpointNatives(void);

#endif	/* __COMMS_SCRIPTENDPOINT_H */
