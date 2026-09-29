/*
	File:		comms/StreamingEndpoint.h

	Contains:	protoStreamingEndpoint's C++ side: TStreamingEndpointClient,
				a TNewScriptEndpointClient that can also stream a whole
				NewtonScript object across the connection as NSOF
				(StreamOut) and read one back (StreamIn), synchronously,
				through PStreamOutRef/PStreamInRef and a TEndpointPipe; and
				TStreamingCallBack, which tells the spec's progressScript
				how far a transfer has got.  The CIS... natives are its
				methods (the rest are protoBasicEndpoint's).

				The spec frame: form (nil or 'frame), reqTimeout, sendFlags
				or rcvFlags (2: framed), progressScript(spec, [bytes so far,
				bytes in all]) - nil stops the transfer - and, for a
				StreamIn, target.store for any large binary read.

	Reconstructed from the MP2x00 US ROM (0x00133d44-0x00133ea4,
	0x00139614-0x00139d80); each function cites its origin.
*/

#ifndef __COMMS_STREAMINGENDPOINT_H
#define __COMMS_STREAMINGENDPOINT_H

#ifndef __COMMS_NEWSCRIPTENDPOINT_H
#include "NewScriptEndpoint.h"
#endif
#ifndef __PIPES_H
#include "Pipes.h"
#endif

class TStreamingCallBack : public PipeCallBack
{
public:
						TStreamingCallBack();
	virtual				~TStreamingCallBack();

	virtual Boolean		Status(long bytesRead, long bytesWritten);

	RefStruct			fSpec;			// +0x0c  the spec frame whose progressScript is sent
	Boolean				fIsIn;			// +0x10
};

class TStreamingEndpointClient : public TNewScriptEndpointClient
{
public:
						TStreamingEndpointClient();
	virtual				~TStreamingEndpointClient();

	Ref					DoStreamIn(RefArg spec, long* error);
	NewtonErr			DoStreamOut(RefArg data, RefArg spec);

protected:
	NewtonErr			ReadStreamParms(StreamRefParms* parms, Boolean isIn, RefArg spec, long length);

public:
	TStreamingCallBack*	fInCallback;	// +0xd4
	TStreamingCallBack*	fOutCallback;	// +0xd8
};

void		RegisterStreamingEndpointNatives(void);

#endif
