/*
	File:		comms/EzEndpointPipe.h

	Contains:	TEzEndpointPipe, the "easy" endpoint pipe: a TEndpointPipe
				that makes its own endpoint - from a NewtonScript options
				frame (openOptions, bindOptions, connectOptions methods and
				useEOP, as the Connection application builds for the
				docker), from three option arrays, or (NOT YET but for
				serial and MNP serial) from a connection type - opened,
				bound and connected, with 2K buffers each way; and asks it
				how many bytes are waiting ('sbav).  Failures throw
				exPipeException with the error as the data.

				The ROM's class; its declaration is not in the DDK, so the
				names of the fields are ours, their order the ROM's (0x58
				bytes).

	Reconstructed from the MP2x00 US ROM (0x000b0444-0x000b1a5c); each
	function cites its origin.
*/

#ifndef __COMMS_EZENDPOINTPIPE_H
#define __COMMS_EZENDPOINTPIPE_H

#ifndef __COMMS_ENDPOINTPIPE_H
#include "EndpointPipe.h"
#endif
#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif
#include "Objects.h"

enum ConnectionType
{
	kSerialConnection = 0,
	kADSPConnection = 1,
	kConnectionType2 = 2,
	kMNPSerialConnection = 3,
	kSharpIRConnection = 4,
	kMNPModemConnection = 5,
	kIrDAConnection = 6
};

class TEzEndpointPipe : public TEndpointPipe
{
public:
					TEzEndpointPipe();
	virtual			~TEzEndpointPipe();

	void			Init(RefArg options, ULong timeout);
	void			Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
						 Boolean useEOP, ULong timeout);
	void			Init(ConnectionType type, char** name, ULong timeout);
	NewtonErr		TearDown();
	ULong			BytesAvailable();
	void			Abort();

private:
	void			CommonInit(ULong timeout);
	void			GetSerialEndpoint();
	void			GetMNPSerialEndpoint();

public:
	NewtonErr		fError;				// +0x2c
	char**			fName;				// +0x30  (a Handle)
	ULong			fEzTimeout;			// +0x34
	TOptionArray	fOptions;			// +0x38  the 'sbav option BytesAvailable asks for
	TOption*		fBytesAvailOpt;		// +0x50
	Boolean			fAppleTalkOpen;		// +0x54
};

// the options frame's option arrays (made here; the caller deletes them)
// ==> whether the frame asks for useEOP
Boolean		EzConvertOptions(RefArg options, TOptionArray** open, TOptionArray** bind, TOptionArray** connect);
NewtonErr	EzSerialOptions(TOptionArray* options, char** name, long sendSize, long recvSize);
NewtonErr	EzMNPSerialOptions(TOptionArray* options, char** name);
NewtonErr	EzMNPConnectOptions(TOptionArray* options, char** name);

#endif	/* __COMMS_EZENDPOINTPIPE_H */
