/*
	File:		hal/HALSerialChip.h

	Contains:	TSerialChip, the protocol a serial tool drives a serial chip
				through, and PSerialChipRegistry, where chips are registered
				by their hardware location and found by the tools - the
				seam between the serial comm tools (TSerTool, TAsyncSerTool,
				TMNP...) and the machine's serial hardware.  The ROM's chips
				are TSerialChipVoyager (the MessagePad's own ports) and
				TSerialChip16450 (a PCMCIA card's); a host provides its own
				(hal/host/HostSerialChip.h, whose wire is a TCP socket).

				The DDK's SerialChipV2.h and SerialChipRegistry.h, re-expressed
				for the portable build: the types and constants are the
				DDK's, and the protocols' methods are virtual in the ROM's
				dispatch order (tools/newton-rom/analysis/classinfo.py --name
				TSerialChip16450), as protocols/Protocols.h wants.  Defining
				the DDK headers' guards here keeps them out.
*/

#ifndef __HAL_HALSERIALCHIP_H
#define __HAL_HALSERIALCHIP_H

#define __SERIALCHIPV2_H			// (the DDK's, replaced by this one)
#define __SERIALCHIPREGISTRY_H

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

class TCardHandler;
class TCardSocket;
class TCircleBuf;
class TOption;
class TCMOSerialIOParms;
class TCMOSerialChipSpec;

#define kSerialChip16450ID		 	's450'
#define	kCapability_Version_2	'v2.0'

typedef ULong RxErrorStatus;
#define kSerialRxParityErr		(0x00000010)
#define kSerialRxOverrun		(0x00000020)
#define kSerialRxFramingErr		(0x00000040)
#define kSerialRxEOF			(0x00000080)
#define kSerialRxSoftOverrun	(0x00000100)
#define kSerialRxTimeout		(0x00000200)
#define kSerialRxUnderrun		(0x00000400)

typedef ULong SerialOutputControl;
#define kSerialOutputDTR		(0x00000001)
#define kSerialOutputRTS		(0x00000002)

typedef ULong SerialStatus;
#define kSerialRxCharAvailable	(0x00000001)
#define kSerialDSRAsserted		(0x00000002)
#define kSerialTxBufferEmpty	(0x00000004)
#define kSerialDCDAsserted		(0x00000008)
#define kSerialRIAsserted		(0x00000010)
#define kSerialCTSAsserted		(0x00000020)
#define kSerialTxUnderrun		(0x00000040)
#define kSerialBreak			(0x00000080)
#define kSerialAbort			(0x00000080)
#define kSerialChipGone			(0x00000100)

typedef ULong SerialFeatures;
#define kSerFeatureNone			(0x00000000)
#define kSerFeatureDefaults		(0x00000001)
#define kSerFeatureVersion2		(0x00000002)
#define kSerFeatureTriStateTxD	(0x00000004)
#define kSerFeatureHiSpeedClk	(0x00000008)
#define kSerFeatureCTSClock		(0x00000010)
#define kSerFeatureAllSent		(0x00000020)
#define kSerFeatureTxConfigNeeded (0x00000040)
#define kSerFeatureTVRemote		(0x00000080)
#define kSerFeatureGetErrByte	(0x00000100)
#define kSerFeatureSDLCMode		(0x00000200)
#define kSerFeatureLocalTalk 	(0x00000400)
#define kSerFeatureAsyncRxDMA 	(0x00001000)
#define kSerFeatureAsyncTxDMA 	(0x00002000)
#define kSerFeatureSyncRxDMA 	(0x00004000)
#define kSerFeatureSyncTxDMA 	(0x00008000)
#define kSerFeatureWireAsyncRxDMABuf (0x00010000)
#define kSerFeatureWireAsyncTxDMABuf (0x00020000)
#define kSerFeatureWaitForAllSent (0x00100000)

typedef ULong SerialIntSource;
#define kSerIntSrcAbort			(0x00000001)
#define kSerIntSrcHunt			(0x00000002)
#define kSerIntSrcUnderRun		(0x00000004)
#define kSerIntSrcCTS			(0x00000008)
#define kSerIntSrcDCD			(0x00000010)
#define kSerIntSrcRxSpecial 	(0x00000020)
#define kSerIntSrcRxOnAllChars  (0x00000040)
#define kSerIntSrcTxBufEmpty  	(0x00000080)
#define kSerIntSrcRxOnFirstChar (0x00000100)

typedef ULong SerialMode;
#define kSerModeAsync			(0x00000000)
#define kSerModeSync			(0x00000001)
#define kSerModeLocalTalk		(0x00000002)
#define kSerModeMask 			(0x00000003)
#define kSerModeHalfDuplex		(0x00000004)
#define kSerModePolled			(0x00000008)

typedef UByte DMAControl;
#define kDMANoOp				(0x00)
#define kDMAStart				(0x01)
#define kDMAStop				(0x02)
#define kDMASuspend				(0x03)
#define kDMASync				(0x04)
#define kDMAFlush				(0x05)
#define kDMACommandMask			(0x0f)
#define kDMANotifyOnNext		(0x10)

typedef ULong BitRate;
typedef ULong InterfaceSpeed;

typedef void(*SCCIntHandler)(void*);
typedef void(*RxDMAIntHandler)(void*, RxErrorStatus);
typedef void(*TxDMAIntHandler)(void*);

// the four interrupts of a channel: the tool's handlers, called with the
// tool (InstallChipHandler's serialTool)
struct SCCChannelInts
{
	SCCIntHandler		TxBEmptyIntHandler;
	SCCIntHandler		ExtStsIntHandler;
	SCCIntHandler		RxCAvailIntHandler;
	SCCIntHandler		RxCSpecialIntHandler;
};


PROTOCOL TSerialChip : public TProtocol
{
public:
	static TSerialChip*	New(char* implementation);
	void				Delete();

	VIRTUAL NewtonErr 			InstallChipHandler(void* serialTool, SCCChannelInts* intHandlers) ENDVIRTUAL;
	VIRTUAL NewtonErr 			RemoveChipHandler(void* serialTool) ENDVIRTUAL;
	VIRTUAL void				PutByte(UByte nextChar) ENDVIRTUAL;
	VIRTUAL void				ResetTxBEmpty() ENDVIRTUAL;
	VIRTUAL UByte				GetByte() ENDVIRTUAL;
	VIRTUAL Boolean				TxBufEmpty() ENDVIRTUAL;
	VIRTUAL Boolean				RxBufFull() ENDVIRTUAL;
	VIRTUAL RxErrorStatus		GetRxErrorStatus() ENDVIRTUAL;
	VIRTUAL SerialStatus		GetSerialStatus() ENDVIRTUAL;
	VIRTUAL void				ResetSerialStatus() ENDVIRTUAL;
	VIRTUAL void				SetSerialOutputs(SerialOutputControl) ENDVIRTUAL;
	VIRTUAL void				ClearSerialOutputs(SerialOutputControl) ENDVIRTUAL;
	VIRTUAL SerialOutputControl	GetSerialOutputs() ENDVIRTUAL;
	VIRTUAL void				PowerOff() ENDVIRTUAL;
	VIRTUAL void				PowerOn() ENDVIRTUAL;
	VIRTUAL Boolean				PowerIsOn() ENDVIRTUAL;
	VIRTUAL void				SetInterruptEnable(Boolean enable) ENDVIRTUAL;
	VIRTUAL void				Reset() ENDVIRTUAL;
	VIRTUAL void				SetBreak(Boolean assert) ENDVIRTUAL;
	VIRTUAL InterfaceSpeed		SetSpeed(BitRate bitsPerSec) ENDVIRTUAL;
	VIRTUAL void				SetIOParms(TCMOSerialIOParms* opt) ENDVIRTUAL;
	VIRTUAL void				Reconfigure() ENDVIRTUAL;
	VIRTUAL NewtonErr			Init(TCardSocket* theCardSocket, TCardHandler* theCardHandler, UByte* baseRegAddr) ENDVIRTUAL;
	VIRTUAL void				CardRemoved() ENDVIRTUAL;
	VIRTUAL SerialFeatures		GetFeatures() ENDVIRTUAL;
	VIRTUAL NewtonErr			InitByOption(TOption* initOpt) ENDVIRTUAL;
	VIRTUAL NewtonErr 			ProcessOption(TOption* opt) ENDVIRTUAL;
	VIRTUAL NewtonErr			SetSerialMode(SerialMode mode) ENDVIRTUAL;
	VIRTUAL void				SysEventNotify(ULong event) ENDVIRTUAL;
	VIRTUAL void				SetTxDTransceiverEnable(Boolean enable) ENDVIRTUAL;
	VIRTUAL RxErrorStatus		GetByteAndStatus(UByte* nextCharPtr) ENDVIRTUAL;
	VIRTUAL NewtonErr			SetIntSourceEnable(SerialIntSource src, Boolean enable) ENDVIRTUAL;
	VIRTUAL Boolean				AllSent() ENDVIRTUAL;
	VIRTUAL void 				ConfigureForOutput(Boolean start) ENDVIRTUAL;
	VIRTUAL NewtonErr			InitTxDMA(TCircleBuf* buf, SCCIntHandler txDMAIntHandler) ENDVIRTUAL;
	VIRTUAL NewtonErr			InitRxDMA(TCircleBuf* buf, ULong notifyLevel, RxDMAIntHandler intHandler) ENDVIRTUAL;
	VIRTUAL NewtonErr			TxDMAControl(DMAControl ctl) ENDVIRTUAL;
	VIRTUAL NewtonErr			RxDMAControl(DMAControl ctl) ENDVIRTUAL;
	VIRTUAL void 				SetSDLCAddress(UByte nodeID) ENDVIRTUAL;
	VIRTUAL void				ReEnableReceiver(Boolean reset) ENDVIRTUAL;
	VIRTUAL Boolean 			LinkIsFree(Boolean resetClks) ENDVIRTUAL;
	VIRTUAL Boolean 			SendControlPacket(UByte pType, UByte dest, Boolean syncPulse) ENDVIRTUAL;
	VIRTUAL void				WaitForPacket(ULong delay) ENDVIRTUAL;
	VIRTUAL NewtonErr			WaitForAllSent() ENDVIRTUAL;
};


#define kRegistryFullError		(-10073)		// kError_RAMTable_Full
#define kInvalidSerChipID		(-10015)		// kError_Bad_ObjectId
#define kNilSerChipID			(0)

typedef ULong SerialChipID;

PROTOCOL PSerialChipRegistry : public TProtocol
{
public:
	static PSerialChipRegistry*	New(char* implementation);
	void			Delete();

	VIRTUAL NewtonErr		Init() ENDVIRTUAL;
	VIRTUAL NewtonErr		Register(TSerialChip* theChip, ULong hwLoc) ENDVIRTUAL;
	VIRTUAL NewtonErr		UnRegister(TSerialChip* theChip) ENDVIRTUAL;
	VIRTUAL NewtonErr		SetDefaultChip(ULong serviceType, ULong* hwLocation, Boolean onlyIfUnset) ENDVIRTUAL;
	VIRTUAL TSerialChip*	GetChipPtr(SerialChipID) ENDVIRTUAL;
	VIRTUAL ULong			GetChipLocation(SerialChipID) ENDVIRTUAL;
	VIRTUAL SerialChipID	FindByChip(TSerialChip* theChip) ENDVIRTUAL;
	VIRTUAL SerialChipID	FindByOption(TCMOSerialChipSpec* opt) ENDVIRTUAL;
	VIRTUAL SerialChipID	FindByLocation(ULong hwLocation) ENDVIRTUAL;
	VIRTUAL NewtonErr		ClaimSerialChip(SerialChipID, Boolean passive, TObjectId ownerPortId) ENDVIRTUAL;
	VIRTUAL NewtonErr		GetDefaultChip(ULong serviceType, ULong* hwLocation) ENDVIRTUAL;
};

extern PSerialChipRegistry*	gSerialChipRegistry;
PSerialChipRegistry*		GetSerialChipRegistry(void);

// the registry made and initialised (the ROM: InitializeCommHardware)
NewtonErr	InitSerialChipRegistry(void);

#endif
