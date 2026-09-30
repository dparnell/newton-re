/*
	File:		comms/fax/FaxToolInterface.h

	Contains:	TFaxToolInterface, a C++ client's way of driving the fax
				tool (comms/fax/FaxTool.h) without an endpoint: the fax
				service started by the comm manager over the modem tool,
				and the tool's port talked to directly in comm tool
				requests - a session opened (bind, then connect, or listen
				and accept when answering), pages begun and ended through
				the 'fsgp'/'feom' options, bands of scan lines put
				(PrintBand, laid out by 'fcsb') or got (GetBand), and the
				session closed (a kill, then a disconnect).

				Every call can be made synchronously, the result handed to
				the matching ...Complete virtual before it returns, or
				asynchronously: the request goes with one of four async
				messages whose collector is the world's port, and
				AECompletionProc - the reply coming back through the
				world's event loop - hands the result on.  The ...Complete
				virtuals are for the subclass: the fax print driver's
				TFaxDriverData (print/FaxDriver.h) is the ROM's one.

	Reconstructed from the MP2x00 US ROM (0x000b9794-0x000bb274); each
	function cites its origin.  The class is not in the DDK; the fields are
	named from their uses (the ROM offsets in the comments) and the
	virtuals are in the ROM's vtable order (analysis/vtable.py build/MP2x00US
	0x1e798).  DEVIATION: the requests' and replies' sizes are the host's
	(sizeof), their ROM sizes (0x28, 0x1c, ...) being those of 32-bit
	pointers.
*/

#ifndef __COMMS_FAX_FAXTOOLINTERFACE_H
#define __COMMS_FAX_FAXTOOLINTERFACE_H

#include "AEventHandler.h"
#include "UserPorts.h"
#include "OptionArray.h"
#include "CommTool.h"
#include "BufferList.h"
#include "BufferSegment.h"
#include "FaxOptions.h"

// what the pending option request is for (fOptionOp)
enum
{
	kFaxOptionNone = 0,
	kFaxOptionBeginPage,
	kFaxOptionEndPage,
	kFaxOptionConfirmPage,
	kFaxOptionMinScanLine,
	kFaxOptionPrintBand
};


class TFaxToolInterface : public TAEventHandler
{
public:
					TFaxToolInterface(ULong serviceId, ULong toolId);		// ROM 0x000b9794 __ct__17TFaxToolInterfaceFUlT1
	virtual			~TFaxToolInterface();									// ROM 0x000b98b4 __dt__17TFaxToolInterfaceFv

	virtual Boolean	AETestEvent(TAEvent* event);							// ROM 0x000b9f3c AETestEvent__17TFaxToolInterfaceFP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x000b9f68 AECompletionProc__17TFaxToolInterfaceFP10TUMsgTokenPUlP7TAEvent
	virtual void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);			// ROM 0x000ba324 IdleProc__17TFaxToolInterfaceFP10TUMsgTokenPUlP7TAEvent

	virtual void	OpenSession(TOptionArray* options, UChar* number, ULong numberLength, Boolean async);	// ROM 0x000baa6c OpenSession__17TFaxToolInterfaceFP12TOptionArrayPUcUlUc
	virtual void	OpenSessionComplete(NewtonErr err, ULong connected, ULong reserved, ULong hRes, ULong vRes) = 0;
	virtual void	AcceptSession(TOptionArray* options, Boolean async);	// ROM 0x000baaf8 AcceptSession__17TFaxToolInterfaceFP12TOptionArrayUc
	virtual void	AcceptSessionComplete(NewtonErr err, ULong hRes, ULong vRes, ULong bitRate) = 0;
	virtual void	CloseSession(Boolean async);							// ROM 0x000bab80 CloseSession__17TFaxToolInterfaceFUc
	virtual void	CloseSessionComplete(NewtonErr err) = 0;
	virtual void	BeginPage(Boolean async);								// ROM 0x000bace4 BeginPage__17TFaxToolInterfaceFUc
	virtual void	BeginPageComplete(NewtonErr err) = 0;
	virtual void	EndPage(Boolean async, Boolean lastPage);				// ROM 0x000bae7c EndPage__17TFaxToolInterfaceFUcT1
	virtual void	EndPageComplete(NewtonErr err) = 0;
	virtual void	PrintBand(UChar* data, ULong lines, ULong bytesPerLine, ULong leftOffset, Boolean async);	// ROM 0x000bb01c PrintBand__17TFaxToolInterfaceFPUcUlN22Uc
	virtual void	PrintBandContinue(NewtonErr err, Boolean async);		// ROM 0x000bb1c8 PrintBandContinue__17TFaxToolInterfaceFlUc
	virtual void	PrintBandComplete(NewtonErr err) = 0;
	virtual void	GetBand(UChar* data, ULong size, Boolean async);		// ROM 0x000b99d0 GetBand__17TFaxToolInterfaceFPUcUlUc
	virtual void	GetBandComplete(NewtonErr err, ULong count, Boolean endOfFrame) = 0;
	virtual void	ConfirmReceivedPage(Boolean accepted, Boolean async);	// ROM 0x000b9b58 ConfirmReceivedPage__17TFaxToolInterfaceFUcT1
	virtual void	ConfirmReceivedPageComplete(NewtonErr err, Boolean anotherPage) = 0;
	virtual void	ContinueClose();										// ROM 0x000ba364 ContinueClose__17TFaxToolInterfaceFv
	virtual void	PostBind(Boolean async);								// ROM 0x000ba3f8 PostBind__17TFaxToolInterfaceFUc
	virtual void	PostConnect(Boolean async);								// ROM 0x000ba56c PostConnect__17TFaxToolInterfaceFUc
	virtual NewtonErr	DoInit(TOptionArray* options);						// ROM 0x000ba850 DoInit__17TFaxToolInterfaceFP12TOptionArray
	virtual NewtonErr	InitConnect(UChar* number, ULong numberLength);		// ROM 0x000ba9e0 InitConnect__17TFaxToolInterfaceFPUcUl
	virtual void	CleanUpAfterConnect();									// ROM 0x000baa68 CleanUpAfterConnect__17TFaxToolInterfaceFv

	NewtonErr		Init(TOptionArray* options, ULong eventId, ULong eventClass);	// ROM 0x000ba328 Init__17TFaxToolInterfaceFP12TOptionArrayUlT2
	NewtonErr		InitAsyncMsg(TUAsyncMessage* msg, ULong* msgId);		// ROM 0x000ba97c InitAsyncMsg__17TFaxToolInterfaceFP14TUAsyncMessagePUl
	NewtonErr		SetMinScanLineTime(ULong time);							// ROM 0x000b9d04 SetMinScanLineTime__17TFaxToolInterfaceFUl
	void			SetDefaultConfig(TOptionArray* options, ULong portId);	// ROM 0x000b9df4 SetDefaultConfig__17TFaxToolInterfaceFP12TOptionArrayUl
	void			SetDefaultOptions(TOptionArray* options);				// ROM 0x000b9eec SetDefaultOptions__17TFaxToolInterfaceFP12TOptionArray
	void			SetFaxOptions(TOptionArray* options, Boolean send);		// ROM 0x000b9ef4 SetFaxOptions__17TFaxToolInterfaceFP12TOptionArrayUc

	TUPort			fToolPort;			// +0x14  the fax tool
	TOptionArray*	fSessionOptions;	// +0x1c  the caller's options for the session
	TOptionArray	fOptions;			// +0x20  an option request's
	TOptionArray	fBandOptions;		// +0x38  a band's ('fcsb')
	long			fOptionOp;			// +0x50  what the pending option request is for
	long			fReserved54;		// +0x54
	TCMOFaxSessionInfo	fSessionInfo;	// +0x58  the session agreed
	TCMOFaxConfigSendBand	fSendBand;	// +0x78  the band being put
	CBufferList		fBandList;			// +0x90  the band's bytes, for a put or get
	CBufferSegment	fBandSegment;		// +0xb0
	Boolean			fConnecting;		// +0xd8  a connect (or accept) is pending
	Boolean			fListening;			// +0xd9  answering rather than calling
	Boolean			fGetting;			// +0xda  the data request pending is a get
	Boolean			fBinding;			// +0xdb  a bind is pending
	TCommToolConnectRequest	fConnectRequest;	// +0xdc  connect, listen, accept, disconnect
	TCommToolBindRequest	fBindRequest;		// +0x104
	TCommToolReply	fConnectReply;		// +0x124
	TUAsyncMessage	fConnectMsg;		// +0x134
	ULong			fConnectMsgId;		// +0x144
	TCommToolPutRequest	fPutRequest;	// +0x148
	TCommToolPutReply	fPutReply;		// +0x164
	TCommToolGetRequest	fGetRequest;	// +0x178
	TCommToolGetReply	fGetReply;		// +0x194
	TUAsyncMessage	fDataMsg;			// +0x1ac
	ULong			fDataMsgId;			// +0x1bc
	Boolean			fOptionBusy;		// +0x1c0  an option request is pending
	TCommToolOptionMgmtRequest	fOptionRequest;	// +0x1c4
	TCommToolReply	fOptionReply;		// +0x1e0
	TUAsyncMessage	fOptionMsg;			// +0x1f0
	ULong			fOptionMsgId;		// +0x200
	TCommToolKillRequest	fKillRequest;	// +0x204
	TCommToolReply	fKillReply;			// +0x210
	TUAsyncMessage	fKillMsg;			// +0x220
	ULong			fKillMsgId;			// +0x230
	ULong			fToolId;			// +0x234  the service under the fax tool ('mods')
	ULong			fServiceId;			// +0x238  the fax tool's ('faxs')
};

#endif	/* __COMMS_FAX_FAXTOOLINTERFACE_H */
