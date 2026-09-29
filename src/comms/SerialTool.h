/*
	File:		comms/SerialTool.h

	Contains:	The serial comm tools: TSerTool, the base of every tool that
				drives a serial chip (hal/HALSerialChip.h) - claiming a chip
				from the registry by its hardware location, binding to it
				(its interrupt handlers installed), turning it on and off,
				and the serial options ('scc ', 'schp', 'sers', 'siop',
				'sbps', 'sbrk', 'sctl', '1way', 'sbav') - and TAsyncSerTool,
				the asynchronous byte stream over it ("Async Serial Tool",
				serv 'aser'): the output and input kept in two TCircleBufs,
				filled and emptied a byte at a time by the chip's interrupts
				(or by DMA, where the chip has it), with software (XON/XOFF)
				and hardware (CTS/RTS) flow control, break framing, the
				serial events a client can ask to be told of, and the
				statistics.  TAsyncService is the comm manager service that
				starts one; the MNP tool (the serial dock's) is built on it.

				Interrupt level and task level meet through two words: an
				interrupt handler sets bits in fIntFlags (0x20000000 output
				done, 0x40000000 input to read, 0x10000000 the chip gone, the
				low bits serial events) and asks IHRequest to send the tool's
				own port a message from interrupt level; the tool task's
				HandleRequest receives it and IHReqHandler takes the bits
				(Swap) and does the work.

				The ROM's classes; their declarations are not in the DDK, so
				the names of the fields are ours, their order the ROM's
				(offsets noted, for the ROM's layout of 0x380 and 0x4b0
				bytes).  The virtuals are in the ROM's vtable order
				(TSerTool's vtable 0x0002043c).

				DEVIATION (pointer size): the replies carry their host size
				(TSerToolReply), and GetSizeOf answers the host's; the
				interrupt handlers the chip is given are static members
				(the ROM hands it member functions, whose first argument is
				the tool anyway).

	Reconstructed from the MP2x00 US ROM (TSerTool 0x001b8538-0x001b9d00,
	TAsyncSerTool 0x0003913c-0x0003b0c4, TAsyncService 0x0003b0c4-0x0003b14c);
	each function cites its origin.  docs/comms/README.md, "The desktop
	connection (Dock)".
*/

#ifndef __COMMS_SERIALTOOL_H
#define __COMMS_SERIALTOOL_H

#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif
#ifndef __HAL_HALSERIALCHIP_H
#include "HALSerialChip.h"
#endif
#ifndef __SERIALOPTIONS_H
#include "SerialOptions.h"
#endif
#ifndef __HALOPTIONS_H
#include "HALOptions.h"
#endif
#ifndef __DELAYTIMER_H
#include "DelayTimer.h"
#endif
#ifndef __CMSERVICE_H
#include "CMService.h"
#endif
#include "UserTasks.h"
#include "AEvents.h"
#include "CircleBuf.h"

class TFIQTimer;
struct FIQTimer;

// TSerTool's own control request: the chip turned on or off
#define kSerToolTurnOnOff		0x100

struct TSerToolTurnOnOffRequest : public TCommToolControlRequest
{
	Boolean				fTurnOn;				// +0x0c
};

// fIntFlags / fIntMask bits
#define kSerIntOutputDone		0x20000000
#define kSerIntInputReady		0x40000000
#define kSerIntChipGone			0x10000000
#define kSerIntRequested		0x80000000		// the interrupt-level message is on its way
#define kSerIntEventMask		0x00000F7F		// the serial events a client may be told of

// the DMA channels' states (fRxDMAState, fTxDMAState)
#define kSerDMANone				0				// a byte at a time
#define kSerDMAIdle				1
#define kSerDMARunning			2
#define kSerDMASuspended		3

// EmptyInputBuffer's end of a frame (a marker with the value 1000)
#define kSerEndOfFrameMarker	1000


class TSerToolReply : public TCommToolReply
{
public:
						TSerToolReply();

	ULong				fData[8];				// +0x10  (the ROM's reply is 0x30 bytes)
};


class TSerTool : public TCommTool
{
public:
						TSerTool(ULong serviceId);
	virtual				~TSerTool();

protected:
	// TUTaskWorld / TCommTool
	virtual NewtonErr	TaskConstructor();
	virtual void		TaskDestructor();
	virtual void		HandleRequest(TUMsgToken& msgToken, ULong msgType);
	virtual NewtonErr	DoControl(ULong opCode, ULong msgType);
	virtual NewtonErr	DoKillControl(ULong msgType);
	virtual void		GetCommEvent();
	virtual void		ConnectStart();
	virtual void		ListenStart();
	virtual void		BindStart();
	virtual void		UnbindStart();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		PutComplete(NewtonErr result, ULong putBytesCount);
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		GetBytesImmediate(CBufferList* clientBuffer, Size threshold);
	virtual void		GetComplete(NewtonErr result, Boolean endOfFrame = false, ULong getBytesCount = 0);
	virtual void		ResArbReleaseStart(UChar* resName, UChar* resType);
	virtual void		ResArbClaimNotification(UChar* resName, UChar* resType);
	virtual void		TerminateComplete();

	// TSerTool's own, in the ROM's vtable order (from +0x128)
	virtual void		PowerOnEvent(ULong reason);
	virtual void		PowerOffEvent(ULong reason);
	virtual NewtonErr	ClaimSerialChip();
	virtual NewtonErr	AllocateBuffers() = 0;
	virtual NewtonErr	BindToSerChip();
	virtual NewtonErr	TurnOnSerChip() = 0;
	virtual void		TurnOffSerChip() = 0;
	virtual void		UnbindToSerChip();
	virtual void		DeallocateBuffers() = 0;
	virtual void		UnclaimSerialChip();
	virtual NewtonErr	TurnOn();
	virtual void		TurnOff();
	virtual void		SetIOParms(TCMOSerialIOParms* opt);
	virtual NewtonErr	SetSerialChipSelect(TCMOSerialHardware* opt);
	virtual NewtonErr	SetSerialChipLocation(TCMOSerialHWChipLoc* opt);
	virtual NewtonErr	SetSerialChipSpec(TCMOSerialChipSpec* opt);
	virtual void		BytesAvailable(ULong& count) = 0;
	virtual void		StartOutput(CBufferList* clientBuffer);
	virtual void		DoOutput() = 0;
	virtual void		StartInput(CBufferList* clientBuffer);
	virtual void		DoInput() = 0;
	virtual void		GetChannelIntHandlers(SCCChannelInts* handlers) = 0;
	virtual void		IHRequest(ULong delay);
	virtual void		IHReqHandler() = 0;
	virtual void		WakeUpHandler();

	// the non-virtuals
	NewtonErr			LookUpSerialChip(ULong hwLocation);
	InterfaceSpeed		ChangeSpeed(ULong bitsPerSec);
	NewtonErr			SendWakeUp(ULong delay);
	NewtonErr			KillWakeUp();
	void				SetBreak(Boolean assert);
	void				SetTxDTransceiverEnable(Boolean enable);
	void				SetHSKo(Boolean assert);
	void				SetSerialOutputs(ULong toSet, ULong toClear);
	SerialOutputControl	GetSerialOutputs();
	void				CleanUp();
	void				ControlComplete(TCommToolReply& reply);
	NewtonErr			PostSerialEvent();

	ULong				fSerFlags;				// +0x26c  1: told of power off, 2: of power on
	CBufferList*		fPutBuffer;				// +0x270  the put in hand
	ULong				fPutSize;				// +0x274  what is left of it
	Boolean				fPutEOF;				// +0x278  a framed put's end of frame
	Boolean				fPutFramed;				// +0x279
	Boolean				fOutputStarting;		// +0x27a  the put just started (DoOutput frames it)
	CBufferList*		fGetBuffer;				// +0x27c  the get in hand
	ULong				fGetSize;				// +0x280  the room left in it
	ULong				fGetThresholdLeft;		// +0x284  an immediate get's threshold
	Boolean				fGetFramed;				// +0x288
	Boolean				fGetImmediate;			// +0x289
	UByte				fDataMask;				// +0x28a  0xff >> (8 - data bits)
	Boolean				fChipClaimed;			// +0x28b
	Boolean				fBuffersAllocated;		// +0x28c
	Boolean				fChipBound;				// +0x28d  the interrupt handlers installed
	Boolean				fChipOn;				// +0x28e
	Boolean				fChipSpecSet;			// +0x28f  'sers or 'schp named the chip
	Boolean				fPoweredOff;			// +0x290  turned off by a power-off event
	Boolean				fHalfDuplex;			// +0x291
	Boolean				fConfigureForOutput;	// +0x292  half duplex on a chip that wants telling
	TSerToolReply		fReply;					// +0x294  kSerToolTurnOnOff's
	TTime				fEventTime;				// +0x2c4  when the pending events happened
	ULong				fEventData;				// +0x2cc  the serial events not yet posted
	TULockStack			fLockStack;				// +0x2d0
	TUAsyncMessage		fWakeMsg;				// +0x2d8  ends a break (SendWakeUp)
	TObjectId			fWakeMsgId;				// +0x2e8
	TAEvent				fWakeEvent;				// +0x2ec
	SCCSide				fSCCSide;				// +0x2f8  'scc '
	SCCChip				fSCCChip;				// +0x2fc
	SCCService			fSCCService;			// +0x300
	TSerialChip*		fChip;					// +0x304
	SerialChipID		fChipId;				// +0x308
	SerialFeatures		fFeatures;				// +0x30c
	TCMOSerialChipSpec	fChipSpec;				// +0x310  (its fHWLoc, +0x31c, the chip's location)
	TFIQTimer*			fFIQTimer;				// +0x330
	PSerialChipRegistry*	fRegistry;			// +0x334
	TDelayTimer			fDelayTimer;			// +0x338
	TCMOSerialIOParms	fIOParms;				// +0x344
	TUAsyncMessage		fIHMsg;					// +0x360  the interrupt handlers' message (IHRequest)
	TObjectId			fIHMsgId;				// +0x370
	TAEvent				fIHEvent;				// +0x374
	UByte				fSerialStatus;			// +0x37c  the chip's SerialStatus when last looked at
};


class TAsyncSerTool : public TSerTool
{
public:
						TAsyncSerTool(ULong serviceId);
	virtual				~TAsyncSerTool();

	virtual ULong		GetSizeOf();

protected:
	virtual NewtonErr	TaskConstructor();
	virtual UChar*		GetToolName();
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);
	virtual NewtonErr	AddDefaultOptions(TOptionArray* options);
	virtual NewtonErr	AddCurrentOptions(TOptionArray* options);
	virtual void		KillPut();
	virtual void		KillGet();

	virtual NewtonErr	AllocateBuffers();
	virtual NewtonErr	TurnOnSerChip();
	virtual void		TurnOffSerChip();
	virtual void		DeallocateBuffers();
	virtual void		BytesAvailable(ULong& count);
	virtual void		DoOutput();
	virtual void		DoInput();
	virtual void		GetChannelIntHandlers(SCCChannelInts* handlers);
	virtual void		IHRequest(ULong delay);
	virtual void		IHReqHandler();

	// TAsyncSerTool's own, in the ROM's vtable order (from +0x18c)
	virtual void		DoPutComplete(NewtonErr result);
	virtual ULong		FillOutputBuffer();
	virtual void		DoGetComplete(NewtonErr result, Boolean endOfFrame);
	virtual ULong		EmptyInputBuffer(ULong* markerValue);
	virtual void		TxDataSent();
	virtual void		RxDataAvailable();
	virtual void		SerialEvents(ULong events);
	virtual Boolean		DataInObserver(UByte byte);
	virtual void		SetInputSendForIntDelay(ULong delay);
	virtual void		RestoreInputSendForIntDelay();

	// the non-virtuals
	void				ResetStats();
	void				GetStats(TCMOSerialIOStats* opt);
	void				UpdateStats(ULong status);
	void				SyncInputBuffer();
	void				FlushInputBytes();
	ULong				FlushOutputBytes();
	Boolean				GPiOn();
	Boolean				HSKiOn();
	NewtonErr			SetOutputFlowControl(TCMOOutputFlowControlParms* opt);
	NewtonErr			SetInputFlowControl(TCMOInputFlowControlParms* opt);
	void				SetEventEnables(TCMOSerialEventEnables* opt);
	void				ContinueOutputST(Boolean start);
	void				StartOutputST();
	Boolean				OutputStopped();
	void				SuspendTxDMA();
	void				ContinueOutputIH(Boolean start);
	void				DoBreakFraming();
	Boolean				GetNextOutChar(UByte* byte);
	Boolean				GetMoreOutChars(UByte* byte);
	void				HandleCharIn(UByte byte, ULong status);
	void				DoInputFlowControl();
	void				ConfigureModemInterrupts();
	void				EmptyInFIFO();

	// the interrupt handlers (the chip's, the DMA channels', the carrier timer's)
	static void			TxBEmptyInt(void* tool);
	static void			ExtStatusInt(void* tool);
	static void			RxCAvailInt(void* tool);
	static void			RxCSpecialInt(void* tool);
	static void			TxDMAInterrupt(void* tool);
	static void			RxMultiByteInterrupt(void* tool, RxErrorStatus status);
	static void			CarrierTimerInterrupt(void* tool, ULong arg);

	Boolean				fInDoInput;				// +0x380
	UByte				fFlowChar;				// +0x381  an XON/XOFF to send ahead of the data
	TCircleBuf			fOutBuf;				// +0x384
	TCircleBuf			fInBuf;					// +0x3ac
	TCMOSerialBuffers	fBuffers;				// +0x3d4
	ULong				fRxDMAState;			// +0x3ec  kSerDMA...
	ULong				fTxDMAState;			// +0x3f0
	TCMOOutputFlowControlParms	fOutFlow;		// +0x3f4
	TCMOInputFlowControlParms	fInFlow;		// +0x408
	TCMOSerialIOStats	fStats;					// +0x41c
	TCMOBreakFraming	fBreakFraming;			// +0x43c
	TCMOSerialEventEnables	fEventEnables;		// +0x458
	FIQTimer*			fCarrierTimer;			// +0x46c
	TCMOSerialMiscConfig	fMiscConfig;		// +0x470
	ULong				fSendForIntDelay;		// +0x488
	ULong				fInputHighWater;		// +0x48c  input flow stopped above it
	ULong				fInputLowWater;			// +0x490  and started again below it
	ULong				fIntFlags;				// +0x494  what the interrupt handlers found
	ULong				fIntMask;				// +0x498  what the task wants to hear of
	ULong				fCTSChanges;			// +0x49c  CTS changes counted
	ULong				fCTSChangeLimit;		// +0x4a0  so many (100) in fCTSInterval: CTS is a clock, stop listening to it
	TTimeout			fCTSInterval;			// +0x4a4
	TTime				fCTSIntervalEnd;		// +0x4a8
};


PROTOCOL TAsyncService : public TCMService
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TAsyncService);
	TAsyncService*		New();
	void				Delete();
	NewtonErr			Start(TOptionArray* options, ULong serviceId, TServiceInfo* serviceInfo);
	NewtonErr			DoneStarting(TAEvent* event, ULong size, TServiceInfo* serviceInfo);
};

// the serial service put in the protocol registry (the kernel services
// task's job, the registry being a monitor)
void	RegisterSerialCommServices(void);

#endif	/* __COMMS_SERIALTOOL_H */
