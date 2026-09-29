/*
	File:		comms/irda/IrIAS.h

	Contains:	The IAS client and server: how one station asks another's
				Information Access Service which LSAP a service is on,
				over an LSAP connection to the other side's LSAP 0.

				TIASClient connects to the other side's LSAP 0, sends a
				GetValueByClass request (0x84: a length and the class name,
				a length and the attribute name) and reads the answer - a
				return code (0 found, 1 no such class, 2 no such attribute)
				and the attribute's elements - acknowledging a reply that
				comes in more than one frame (0x44), then gives the lookup
				back to the glue; the glue releases the connection.

				TIASServer listens on LSAP 0 for as long as the stack is
				up, answering each GetValueByClass out of the station's
				database (IrIASService.h) and anything else with 0xff
				(unsupported).

				The field names are ours, their order the ROM's (offsets
				noted, for the ROM's 0x58 and 0x50 bytes).

	Reconstructed from the MP2x00 US ROM (0x000f0cc8-0x000f1924); each
	function cites its origin.
*/

#ifndef __COMMS_IRIAS_H
#define __COMMS_IRIAS_H

#include "IrStream.h"

class TIrLMP;
class TIASService;
class TIASAttribute;
class CBufferSegment;


class TIASClient : public TIrStream
{
public:
						TIASClient();
	virtual				~TIASClient();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client);
	void				DeInit(void);
	void				HandleDisconnectedStateEvent(ULong event);
	void				HandleConnectedStateEvent(ULong event);
	void				GetStart(void);
	void				PutStart(void);
	NewtonErr			SendRequest(void);
	void				ParseInput(void);
	NewtonErr			ParseReply(void);
	void				LookupComplete(NewtonErr result);

	UByte				fState;					// +0x14  0 disconnected, 1 connected
	UByte				fReceiveState;			// +0x15  1: a reply in more than one frame
	TIrGlue*			fIrGlue;				// +0x18
	TIrLMP*				fLMP;					// +0x1c
	TIrStream*			fClient;				// +0x20
	TLSAPConn*			fLSAPConn;				// +0x24
	TIrDataXferEvent	fEvent;					// +0x28  the gets and puts
	CBufferSegment*		fBuffer;				// +0x4c  0x80 bytes
	TIrEvent*			fLookupRequest;			// +0x50
	TIASAttribute*		fAttribute;				// +0x54  the answer
};


class TIASServer : public TIrStream
{
public:
						TIASServer();
	virtual				~TIASServer();
	virtual void		NextState(ULong event);

	NewtonErr			Init(TIrGlue* glue, TIrLMP* lmp, TIrStream* client);
	void				DeInit(void);
	void				SetNameService(TIASService* service);
	void				ListenStart(void);
	void				GetStart(void);
	void				PutStart(void);
	void				ParseInput(void);
	TIASAttribute*		ParseRequest(UByte& returnCode);
	Boolean				GotAValidString(UByte* string);
	void				SendResponse(UByte returnCode, TIASAttribute* attribute);

	UByte				fOpCode;				// +0x14  the request's
	UByte				fReceiveState;			// +0x15  1: a request in more than one frame
	TIrGlue*			fIrGlue;				// +0x18
	TIrStream*			fClient;				// +0x1c
	TIASService*		fNameService;			// +0x20
	TLSAPConn*			fLSAPConn;				// +0x24
	TIrLSAPConnEvent	fEvent;					// +0x28
	CBufferSegment*		fBuffer;				// +0x4c  0x80 bytes
};

#endif	/* __COMMS_IRIAS_H */
